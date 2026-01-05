export type CodecType = 'h264' | 'hevc';

export interface DecoderConfig {
  codecType: CodecType;
  wasmPath?: string;
}

export interface VideoFrame {
  width: number;
  height: number;
  pts: number;
  duration: number;
  yData: Uint8Array;
  uData: Uint8Array;
  vData: Uint8Array;
  yStride: number;
  uStride: number;
  vStride: number;
}

export enum DecodeStatus {
  SUCCESS = 0,
  NEED_MORE_DATA = 1,
  ERROR = -1,
  END_OF_STREAM = 2
}

export interface DecoderStats {
  totalFrames: number;
  droppedFrames: number;
  avgDecodeTime: number;
  currentFPS: number;
}

export type WorkerRequest =
  | { type: 'init'; config: DecoderConfig }
  | { type: 'decode'; data: Uint8Array; pts?: number }
  | { type: 'flush' }
  | { type: 'destroy' };

export type WorkerResponse =
  | { type: 'ready' }
  | { type: 'frame'; frame: VideoFrame }
  | { type: 'error'; error: string }
  | { type: 'destroyed' }
  | { type: 'stats'; stats: DecoderStats };

export interface EmscriptenModule {
  ccall: (
    name: string,
    returnType: string | null,
    argTypes: string[],
    args: (number | bigint)[]
  ) => any;
  cwrap: (
    name: string,
    returnType: string | null,
    argTypes: string[]
  ) => (...args: (number | bigint)[]) => any;
  getValue: (ptr: number, type: string) => any;
  setValue: (ptr: number, value: any, type: string) => void;
  HEAPU8: Uint8Array;
  HEAPF32: Float32Array;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
}
