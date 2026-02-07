import { DataBufferQueue } from '../buffer/DataBufferQueue.js';
import { WorkerBridge } from '../decoder/WorkerBridge.js';
import type { CodecType, VideoFrame, DecoderStats } from '../decoder/types.js';

export interface StreamConfig {
  id: string;
  wsUrl: string;
  codecType: CodecType;
  canvas: HTMLCanvasElement;
  wasmPath?: string;
  bufferConfig?: {
    maxSize?: number;
    maxBytes?: number;
  };
  consumeInterval?: number;
}

export interface StreamStats {
  bytesReceived: number;
  messagesReceived: number;
  connectionStartTime: number | null;
  dataRate: number;
  decoderStats: DecoderStats | null;
  bufferStats: ReturnType<DataBufferQueue['getStats']> | null;
}

export type StreamStatus = 'disconnected' | 'connecting' | 'connected' | 'error';

export class StreamInstance {
  readonly id: string;
  private wsUrl: string;
  private codecType: CodecType;
  private canvas: HTMLCanvasElement;
  private wasmPath: string;

  private ws: WebSocket | null = null;
  private queue: DataBufferQueue;
  private consumerInterval: number | null = null;
  private decoder: WorkerBridge | null = null;

  private status: StreamStatus = 'disconnected';
  private stats: StreamStats = {
    bytesReceived: 0,
    messagesReceived: 0,
    connectionStartTime: null,
    dataRate: 0,
    decoderStats: null,
    bufferStats: null
  };

  private lastUpdateTime: number = 0;
  private lastBytesReceived: number = 0;
  private consumeIntervalMs: number;

  private onStatusChange?: (status: StreamStatus) => void;
  private onStatsUpdate?: (stats: StreamStats) => void;
  private onError?: (error: string) => void;

  constructor(config: StreamConfig) {
    this.id = config.id;
    this.wsUrl = config.wsUrl;
    this.codecType = config.codecType;
    this.canvas = config.canvas;
    this.wasmPath = config.wasmPath || '/dist/decoder.js';
    this.consumeIntervalMs = config.consumeInterval || 33;

    this.queue = new DataBufferQueue(config.bufferConfig || {
      maxSize: 100,
      maxBytes: 10 * 1024 * 1024
    });
  }

  async connect(): Promise<void> {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      throw new Error(`Stream ${this.id} already connected`);
    }

    this.updateStatus('connecting');

    try {
      await this.initDecoder();
      await this.connectWebSocket();
      this.startConsumer();
    } catch (error) {
      this.updateStatus('error');
      this.handleError(`Connection failed: ${error}`);
      throw error;
    }
  }

  disconnect(): void {
    this.stopConsumer();

    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }

    if (this.decoder) {
      this.decoder.destroy();
      this.decoder = null;
    }

    this.queue.clear();
    this.stats.connectionStartTime = null;
    this.updateStatus('disconnected');
  }

  getStatus(): StreamStatus {
    return this.status;
  }

  getStats(): StreamStats {
    this.stats.bufferStats = this.queue.getStats();
    return { ...this.stats };
  }

  setOnStatusChange(callback: (status: StreamStatus) => void): void {
    this.onStatusChange = callback;
  }

  setOnStatsUpdate(callback: (stats: StreamStats) => void): void {
    this.onStatsUpdate = callback;
  }

  setOnError(callback: (error: string) => void): void {
    this.onError = callback;
  }

  private async initDecoder(): Promise<void> {
    const workerPath = '/dist/js/worker/decode-worker.js';
    this.decoder = new WorkerBridge(workerPath);

    this.decoder.onFrame((frame: VideoFrame) => {
      this.renderFrame(frame);
    });

    this.decoder.onStats((stats: DecoderStats) => {
      this.stats.decoderStats = stats;
      this.notifyStatsUpdate();
    });

    this.decoder.onError((error: string) => {
      this.handleError(`Decoder error: ${error}`);
    });

    await this.decoder.init({
      codecType: this.codecType,
      wasmPath: this.wasmPath
    });
  }

  private async connectWebSocket(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.wsUrl);
      this.ws.binaryType = 'arraybuffer';

      this.ws.onopen = () => {
        this.stats.connectionStartTime = Date.now();
        this.stats.bytesReceived = 0;
        this.stats.messagesReceived = 0;
        this.lastUpdateTime = Date.now();
        this.lastBytesReceived = 0;
        this.updateStatus('connected');
        resolve();
      };

      this.ws.onmessage = (event) => {
        this.handleWebSocketMessage(event);
      };

      this.ws.onerror = () => {
        reject(new Error('WebSocket error'));
      };

      this.ws.onclose = () => {
        this.updateStatus('disconnected');
        this.stopConsumer();
      };
    });
  }

  private handleWebSocketMessage(event: MessageEvent): void {
    this.stats.messagesReceived++;

    if (event.data instanceof ArrayBuffer) {
      const dataSize = event.data.byteLength;
      this.stats.bytesReceived += dataSize;
      this.queue.enqueue(event.data);

      this.updateDataRate();
    } else if (event.data instanceof Blob) {
      const dataSize = event.data.size;
      this.stats.bytesReceived += dataSize;
      this.queue.enqueue(event.data);

      this.updateDataRate();
    } else {
      const dataSize = new Blob([event.data]).size;
      this.stats.bytesReceived += dataSize;
    }

    this.notifyStatsUpdate();
  }

  private updateDataRate(): void {
    const currentTime = Date.now();
    const timeDiff = (currentTime - this.lastUpdateTime) / 1000;

    if (timeDiff >= 1) {
      const bytesDiff = this.stats.bytesReceived - this.lastBytesReceived;
      this.stats.dataRate = bytesDiff / timeDiff / 1024;
      this.lastUpdateTime = currentTime;
      this.lastBytesReceived = this.stats.bytesReceived;
    }
  }

  private startConsumer(): void {
    if (this.consumerInterval !== null) {
      return;
    }

    this.consumerInterval = window.setInterval(async () => {
      if (!this.queue.isEmpty() && this.decoder) {
        const packet = this.queue.dequeue();
        if (packet) {
          try {
            let arrayBuffer = packet.data;

            if (packet.data instanceof Blob) {
              arrayBuffer = await packet.data.arrayBuffer();
            }

            if (arrayBuffer instanceof ArrayBuffer) {
              const uint8Array = new Uint8Array(arrayBuffer);
              await this.decoder.decode(uint8Array, packet.timestamp);
            }
          } catch (error) {
            this.handleError(`Decode error: ${error}`);
          }
        }
      }
    }, this.consumeIntervalMs);
  }

  private stopConsumer(): void {
    if (this.consumerInterval !== null) {
      clearInterval(this.consumerInterval);
      this.consumerInterval = null;
    }
  }

  private renderFrame(frame: VideoFrame): void {
    const ctx = this.canvas.getContext('2d');
    if (!ctx) return;

    if (this.canvas.width !== frame.width || this.canvas.height !== frame.height) {
      this.canvas.width = frame.width;
      this.canvas.height = frame.height;
    }

    const imageData = ctx.createImageData(frame.width, frame.height);
    const data = imageData.data;

    const yData = frame.yData;
    const uData = frame.uData;
    const vData = frame.vData;

    for (let y = 0; y < frame.height; y++) {
      for (let x = 0; x < frame.width; x++) {
        const yIndex = y * frame.yStride + x;
        const uvIndex = (y >> 1) * frame.uStride + (x >> 1);

        const Y = yData[yIndex];
        const U = uData[uvIndex] - 128;
        const V = vData[uvIndex] - 128;

        const R = Y + 1.402 * V;
        const G = Y - 0.344136 * U - 0.714136 * V;
        const B = Y + 1.772 * U;

        const pixelIndex = (y * frame.width + x) * 4;
        data[pixelIndex] = Math.max(0, Math.min(255, R));
        data[pixelIndex + 1] = Math.max(0, Math.min(255, G));
        data[pixelIndex + 2] = Math.max(0, Math.min(255, B));
        data[pixelIndex + 3] = 255;
      }
    }

    ctx.putImageData(imageData, 0, 0);
  }

  private updateStatus(status: StreamStatus): void {
    this.status = status;
    if (this.onStatusChange) {
      this.onStatusChange(status);
    }
  }

  private notifyStatsUpdate(): void {
    if (this.onStatsUpdate) {
      this.onStatsUpdate(this.getStats());
    }
  }

  private handleError(errorMsg: string): void {
    console.error(`Stream ${this.id}: ${errorMsg}`);
    if (this.onError) {
      this.onError(errorMsg);
    }
  }
}
