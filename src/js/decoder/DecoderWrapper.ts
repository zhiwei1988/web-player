import {
  DecodeStatus,
  FrameParseStatus
} from './types.js';

import type {
  DecoderConfig,
  VideoFrame,
  DecoderStats,
  ParsedFrameInfo,
  EmscriptenModule
} from './types.js';

export class DecoderWrapper {
  private module: EmscriptenModule | null = null;
  private initialized: boolean = false;
  private protocolInitialized: boolean = false;
  private parsedFramePtr: number = 0;

  private stats: DecoderStats = {
    totalFrames: 0,
    droppedFrames: 0,
    avgDecodeTime: 0,
    currentFPS: 0
  };

  private decodeTimeSamples: number[] = [];
  private lastFrameTime: number = 0;
  private frameTimeWindow: number[] = [];

  async init(config: DecoderConfig): Promise<void> {
    if (this.initialized) {
      throw new Error('Decoder already initialized');
    }

    // Check BigInt support
    if (typeof BigInt === 'undefined') {
      throw new Error('浏览器不支持 BigInt，请升级到最新版本');
    }

    const wasmPath = config.wasmPath || '/dist/decoder.js';

    try {
      const createModule = await this.loadWASMModule(wasmPath);
      this.module = await createModule({
        locateFile: (path: string) => {
          if (path.endsWith('.wasm')) {
            if (config.wasmPath) {
              return config.wasmPath.replace('.js', '.wasm');
            }
            return '/dist/decoder.wasm';
          }
          return path;
        }
      });

      if (!this.module) {
        throw new Error('Failed to initialize WASM module');
      }

      const codecTypeValue = config.codecType === 'h264' ? 0 : 1;
      const result = this.module.ccall(
        'decoder_init_video',
        'number',
        ['number'],
        [codecTypeValue]
      );

      if (result !== 0) {
        throw new Error(`Failed to initialize decoder, error code: ${result}`);
      }

      this.initialized = true;
      this.lastFrameTime = performance.now();
    } catch (error) {
      throw new Error(`Failed to load WASM module: ${error}`);
    }
  }

  private async loadWASMModule(path: string): Promise<any> {
    const module = await import(path);
    return module.default || module.createDecoderModule;
  }

  async decode(data: Uint8Array, pts: number = 0): Promise<VideoFrame | null> {
    if (!this.initialized || !this.module) {
      throw new Error('Decoder not initialized');
    }

    // Validate PTS value
    if (!Number.isSafeInteger(pts)) {
      throw new Error(`PTS value ${pts} is not a safe integer`);
    }
    if (pts < 0) {
      throw new Error(`PTS value ${pts} must be non-negative`);
    }

    const startTime = performance.now();

    const dataPtr = this.module.ccall('decoder_malloc', 'number', ['number'], [data.length]);
    if (!dataPtr) {
      throw new Error('Failed to allocate memory for input data');
    }

    try {
      this.module.HEAPU8.set(data, dataPtr);

      const sendResult = this.module.ccall(
        'decoder_send_video_packet',
        'number',
        ['number', 'number', 'bigint'],
        [dataPtr, data.length, BigInt(pts)]
      );

      if (sendResult < 0) {
        throw new Error(`Failed to send packet to decoder, error: ${sendResult}`);
      }

      const frameInfoPtr = this.module.ccall('decoder_malloc', 'number', ['number'], [64]);
      if (!frameInfoPtr) {
        throw new Error('Failed to allocate memory for frame info');
      }

      try {
        const receiveResult = this.module.ccall(
          'decoder_receive_video_frame',
          'number',
          ['number'],
          [frameInfoPtr]
        );

        if (receiveResult === DecodeStatus.NEED_MORE_DATA) {
          return null;
        }

        if (receiveResult < 0) {
          throw new Error(`Failed to receive frame, error: ${receiveResult}`);
        }

        const frame = this.extractVideoFrame(frameInfoPtr);

        const decodeTime = performance.now() - startTime;
        this.updateStats(decodeTime);

        return frame;
      } finally {
        this.module.ccall('decoder_free', null, ['number'], [frameInfoPtr]);
      }
    } finally {
      this.module.ccall('decoder_free', null, ['number'], [dataPtr]);
    }
  }

  /**
   * 从 WASM 内存读取 int64_t 并转换为 JavaScript number
   * 假设小端字节序（WASM 默认）
   */
  private readInt64AsNumber(ptr: number): number {
    if (!this.module) {
      throw new Error('Module not initialized');
    }

    // 读取低 32 位（无符号）
    const low = this.module.getValue(ptr, 'i32') >>> 0;

    // 读取高 32 位（有符号转无符号）
    const high = this.module.getValue(ptr + 4, 'i32') >>> 0;

    // 组合：low + high * 2^32
    const value = low + (high * 4294967296);

    return value;
  }

  private extractVideoFrame(frameInfoPtr: number): VideoFrame {
    if (!this.module) {
      throw new Error('Module not initialized');
    }

    const width = this.module.getValue(frameInfoPtr, 'i32');
    const height = this.module.getValue(frameInfoPtr + 4, 'i32');
    const pts = this.readInt64AsNumber(frameInfoPtr + 8);
    const duration = this.readInt64AsNumber(frameInfoPtr + 16);

    const yDataPtr = this.module.getValue(frameInfoPtr + 24, 'i32');
    const uDataPtr = this.module.getValue(frameInfoPtr + 28, 'i32');
    const vDataPtr = this.module.getValue(frameInfoPtr + 32, 'i32');

    const yStride = this.module.getValue(frameInfoPtr + 36, 'i32');
    const uStride = this.module.getValue(frameInfoPtr + 40, 'i32');
    const vStride = this.module.getValue(frameInfoPtr + 44, 'i32');

    const ySize = yStride * height;
    const uSize = uStride * (height / 2);
    const vSize = vStride * (height / 2);

    const yData = new Uint8Array(ySize);
    const uData = new Uint8Array(uSize);
    const vData = new Uint8Array(vSize);

    yData.set(this.module.HEAPU8.subarray(yDataPtr, yDataPtr + ySize));
    uData.set(this.module.HEAPU8.subarray(uDataPtr, uDataPtr + uSize));
    vData.set(this.module.HEAPU8.subarray(vDataPtr, vDataPtr + vSize));

    return {
      width,
      height,
      pts,
      duration,
      yData,
      uData,
      vData,
      yStride,
      uStride,
      vStride
    };
  }

  private updateStats(decodeTime: number): void {
    this.stats.totalFrames++;

    this.decodeTimeSamples.push(decodeTime);
    if (this.decodeTimeSamples.length > 30) {
      this.decodeTimeSamples.shift();
    }

    const sum = this.decodeTimeSamples.reduce((a, b) => a + b, 0);
    this.stats.avgDecodeTime = sum / this.decodeTimeSamples.length;

    const now = performance.now();
    const frameInterval = now - this.lastFrameTime;
    this.lastFrameTime = now;

    this.frameTimeWindow.push(frameInterval);
    if (this.frameTimeWindow.length > 30) {
      this.frameTimeWindow.shift();
    }

    if (this.frameTimeWindow.length > 0) {
      const avgInterval = this.frameTimeWindow.reduce((a, b) => a + b, 0) / this.frameTimeWindow.length;
      this.stats.currentFPS = 1000 / avgInterval;
    }
  }

  async flush(): Promise<VideoFrame[]> {
    if (!this.initialized || !this.module) {
      throw new Error('Decoder not initialized');
    }

    this.module.ccall('decoder_flush_video', null, [], []);

    const frames: VideoFrame[] = [];
    const frameInfoPtr = this.module.ccall('decoder_malloc', 'number', ['number'], [64]);

    if (!frameInfoPtr) {
      throw new Error('Failed to allocate memory for frame info');
    }

    try {
      while (true) {
        const result = this.module.ccall(
          'decoder_receive_video_frame',
          'number',
          ['number'],
          [frameInfoPtr]
        );

        if (result === DecodeStatus.NEED_MORE_DATA || result === DecodeStatus.END_OF_STREAM) {
          break;
        }

        if (result < 0) {
          break;
        }

        const frame = this.extractVideoFrame(frameInfoPtr);
        frames.push(frame);
      }
    } finally {
      this.module.ccall('decoder_free', null, ['number'], [frameInfoPtr]);
    }

    return frames;
  }

  initProtocol(): void {
    if (!this.module) {
      throw new Error('Module not initialized');
    }

    const result = this.module.ccall('frame_protocol_init', 'number', [], []);
    if (result < 0) {
      throw new Error(`Failed to initialize frame protocol, error: ${result}`);
    }

    this.parsedFramePtr = this.module.ccall(
      'frame_protocol_alloc_result', 'number', [], []);
    if (!this.parsedFramePtr) {
      throw new Error('Failed to allocate ParsedFrame struct');
    }

    this.protocolInitialized = true;
  }

  parseFrame(data: Uint8Array): ParsedFrameInfo | null {
    if (!this.module || !this.protocolInitialized || !this.parsedFramePtr) {
      throw new Error('Protocol not initialized');
    }

    const dataPtr = this.module.ccall(
      'decoder_malloc', 'number', ['number'], [data.length]);
    if (!dataPtr) {
      throw new Error('Failed to allocate memory for protocol frame');
    }

    try {
      this.module.HEAPU8.set(data, dataPtr);

      const status: FrameParseStatus = this.module.ccall(
        'frame_protocol_parse', 'number',
        ['number', 'number', 'number'],
        [dataPtr, data.length, this.parsedFramePtr]);

      if (status === FrameParseStatus.FRAGMENT_PENDING) {
        return { status, msgType: 0, codec: 0, frameType: 0,
                 timestamp: 0, absTime: 0, payload: new Uint8Array(0) };
      }

      if (status === FrameParseStatus.ERROR || status === FrameParseStatus.SKIP) {
        return { status, msgType: 0, codec: 0, frameType: 0,
                 timestamp: 0, absTime: 0, payload: new Uint8Array(0) };
      }

      // FRAME_COMPLETE: read ParsedFrame fields from WASM memory
      const ptr = this.parsedFramePtr;
      const msgType = this.module.HEAPU8[ptr];
      const codec = this.module.HEAPU8[ptr + 1];
      const frameType = this.module.HEAPU8[ptr + 2];
      const timestamp = this.readInt64AsNumber(ptr + 8);
      const absTime = this.readInt64AsNumber(ptr + 16);
      const payloadPtr = this.module.getValue(ptr + 24, 'i32');
      const payloadSize = this.module.getValue(ptr + 28, 'i32') >>> 0;

      let payload = new Uint8Array(0);
      if (payloadPtr && payloadSize > 0) {
        payload = new Uint8Array(payloadSize);
        payload.set(this.module.HEAPU8.subarray(payloadPtr, payloadPtr + payloadSize));
      }

      return { status, msgType, codec, frameType, timestamp, absTime, payload };
    } finally {
      this.module.ccall('decoder_free', null, ['number'], [dataPtr]);
    }
  }

  destroyProtocol(): void {
    if (this.module && this.protocolInitialized) {
      if (this.parsedFramePtr) {
        this.module.ccall('frame_protocol_free_result', null,
                          ['number'], [this.parsedFramePtr]);
        this.parsedFramePtr = 0;
      }
      this.module.ccall('frame_protocol_destroy', null, [], []);
      this.protocolInitialized = false;
    }
  }

  destroy(): void {
    if (this.initialized && this.module) {
      this.destroyProtocol();
      this.module.ccall('decoder_destroy', null, [], []);
      this.initialized = false;
      this.module = null;
    }
  }

  getVersion(): string {
    if (!this.module) {
      return 'Not initialized';
    }

    const versionPtr = this.module.ccall('decoder_get_version', 'number', [], []);
    return this.ptrToString(versionPtr);
  }

  getFFmpegVersion(): string {
    if (!this.module) {
      return 'Not initialized';
    }

    const versionPtr = this.module.ccall('decoder_get_ffmpeg_version', 'number', [], []);
    return this.ptrToString(versionPtr);
  }

  getStats(): DecoderStats {
    return { ...this.stats };
  }

  private ptrToString(ptr: number): string {
    if (!this.module) {
      return '';
    }

    const chars: number[] = [];
    let i = 0;
    while (true) {
      const char = this.module.HEAPU8[ptr + i];
      if (char === 0) break;
      chars.push(char);
      i++;
    }
    return String.fromCharCode(...chars);
  }

  isInitialized(): boolean {
    return this.initialized;
  }
}
