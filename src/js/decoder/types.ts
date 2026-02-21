export type CodecType = 'h264' | 'hevc';
export type AudioCodecType = 'g711a' | 'g711u' | 'g726' | 'aac';

export interface DecoderConfig {
    codecType: CodecType;
    wasmPath?: string;
}

export interface AudioDecoderConfig {
    audioCodecType: AudioCodecType;
    sampleRate: number;
    channels: number;
}

export interface AudioFrame {
    sampleRate: number;
    channels: number;
    nbSamples: number;
    pts: number;
    data: Float32Array;
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
    END_OF_STREAM = 2,
}

export enum FrameParseStatus {
    COMPLETE = 0,
    FRAGMENT_PENDING = 1,
    ERROR = -1,
    SKIP = 2,
}

export interface ParsedFrameInfo {
    status: FrameParseStatus;
    msgType: number;
    codec: number;
    frameType: number;
    timestamp: number;
    absTime: number;
    payload: Uint8Array;
}

export interface DecoderStats {
    totalFrames: number;
    droppedFrames: number;
    avgDecodeTime: number;
    currentFPS: number;
}

export type WorkerRequest =
    | { type: 'init'; config: DecoderConfig }
    | { type: 'initAudio'; config: AudioDecoderConfig }
    | { type: 'decode'; data: Uint8Array; pts?: number }
    | { type: 'flush' }
    | { type: 'destroy' };

export type WorkerResponse =
    | { type: 'ready' }
    | { type: 'frame'; frame: VideoFrame }
    | { type: 'audioFrame'; frame: AudioFrame }
    | { type: 'error'; error: string }
    | { type: 'destroyed' }
    | { type: 'stats'; stats: DecoderStats };

export interface EmscriptenModule {
    ccall: (name: string, returnType: string | null, argTypes: string[], args: (number | bigint)[]) => any;
    cwrap: (name: string, returnType: string | null, argTypes: string[]) => (...args: (number | bigint)[]) => any;
    getValue: (ptr: number, type: string) => any;
    setValue: (ptr: number, value: any, type: string) => void;
    HEAPU8: Uint8Array;
    HEAPF32: Float32Array;
    _malloc: (size: number) => number;
    _free: (ptr: number) => void;
}
