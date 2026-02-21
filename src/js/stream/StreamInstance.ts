import { AudioPlayer } from '../audio/AudioPlayer.js';
import { AVSync } from '../audio/AVSync.js';
import { DataBufferQueue } from '../buffer/DataBufferQueue.js';
import { WorkerBridge } from '../decoder/WorkerBridge.js';
import type { AudioCodecType, AudioFrame, CodecType, VideoFrame, DecoderStats } from '../decoder/types.js';

export interface StreamConfig {
    id: string;
    wsUrl: string;
    canvas: HTMLCanvasElement;
    wasmPath?: string;
    audioContext?: AudioContext;
    bufferConfig?: {
        maxSize?: number;
        maxBytes?: number;
    };
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
    private canvas: HTMLCanvasElement;
    private wasmPath: string;

    private ws: WebSocket | null = null;
    private queue: DataBufferQueue;
    private processing: boolean = false;
    private decoder: WorkerBridge | null = null;
    private audioPlayer: AudioPlayer | null = null;
    private avSync: AVSync | null = null;

    private status: StreamStatus = 'disconnected';
    private stats: StreamStats = {
        bytesReceived: 0,
        messagesReceived: 0,
        connectionStartTime: null,
        dataRate: 0,
        decoderStats: null,
        bufferStats: null,
    };

    private lastUpdateTime: number = 0;
    private lastBytesReceived: number = 0;

    private audioContext: AudioContext | null = null;

    private onStatusChange?: (status: StreamStatus) => void;
    private onStatsUpdate?: (stats: StreamStats) => void;
    private onError?: (error: string) => void;

    constructor(config: StreamConfig) {
        this.id = config.id;
        this.wsUrl = config.wsUrl;
        this.canvas = config.canvas;
        this.wasmPath = config.wasmPath || '/dist/decoder.js';
        this.audioContext = config.audioContext || null;

        this.queue = new DataBufferQueue(
            config.bufferConfig || {
                maxSize: 100,
                maxBytes: 10 * 1024 * 1024,
            }
        );
    }

    async connect(): Promise<void> {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            throw new Error(`Stream ${this.id} already connected`);
        }

        this.updateStatus('connecting');

        try {
            await this.connectWebSocket();
        } catch (error) {
            this.updateStatus('error');
            this.handleError(`Connection failed: ${error}`);
            throw error;
        }
    }

    disconnect(): void {
        this.processing = false;

        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }

        if (this.avSync) {
            this.avSync.destroy();
            this.avSync = null;
        }

        if (this.audioPlayer) {
            this.audioPlayer.destroy();
            this.audioPlayer = null;
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

    private async initDecoder(codecType: CodecType): Promise<void> {
        const workerPath = '/dist/js/worker/decode-worker.js';
        this.decoder = new WorkerBridge(workerPath);

        // Set up audio player and AV sync
        this.audioPlayer = new AudioPlayer(this.audioContext);
        this.avSync = new AVSync(this.audioPlayer);
        this.avSync.setRenderCallback((frame: VideoFrame) => {
            this.renderFrame(frame);
        });

        this.decoder.onFrame((frame: VideoFrame) => {
            if (this.avSync) {
                this.avSync.processVideoFrame(frame);
            } else {
                this.renderFrame(frame);
            }
        });

        this.decoder.onAudioFrame((frame: AudioFrame) => {
            if (this.audioPlayer) {
                this.audioPlayer.playFrame(frame);
            }
        });

        this.decoder.onStats((stats: DecoderStats) => {
            this.stats.decoderStats = stats;
            this.notifyStatsUpdate();
        });

        this.decoder.onError((error: string) => {
            this.handleError(`Decoder error: ${error}`);
        });

        await this.decoder.init({
            codecType,
            wasmPath: this.wasmPath,
        });
    }

    private async connectWebSocket(): Promise<void> {
        return new Promise((resolve, reject) => {
            this.ws = new WebSocket(this.wsUrl);
            this.ws.binaryType = 'arraybuffer';

            let negotiated = false;

            this.ws.onopen = () => {
                // Wait for media-offer before reporting connected
            };

            this.ws.onmessage = async (event) => {
                if (!negotiated && typeof event.data === 'string') {
                    let msg: any;
                    try {
                        msg = JSON.parse(event.data);
                    } catch {
                        return;
                    }

                    if (msg.type === 'media-offer') {
                        const streams: Array<{ type: string; codec: string; sampleRate?: number; channels?: number }> =
                            msg.payload?.streams || [];
                        const videoStream = streams.find((s) => s.type === 'video');
                        const audioStream = streams.find((s) => s.type === 'audio');
                        const codecType = (videoStream?.codec as CodecType) || 'h264';

                        try {
                            await this.initDecoder(codecType);

                            // Initialize audio decoder if audio stream is present
                            if (audioStream && this.decoder) {
                                const audioCodecMap: Record<string, AudioCodecType> = {
                                    aac: 'aac',
                                    pcm_alaw: 'g711a',
                                    pcm_mulaw: 'g711u',
                                    g726: 'g726',
                                };
                                const audioCodecType = audioCodecMap[audioStream.codec];
                                if (audioCodecType) {
                                    await this.decoder.initAudio({
                                        audioCodecType,
                                        sampleRate: audioStream.sampleRate || 44100,
                                        channels: audioStream.channels || 2,
                                    });
                                }
                            }

                            this.ws!.send(JSON.stringify({ type: 'media-answer', payload: { accepted: true } }));
                            negotiated = true;
                            this.stats.connectionStartTime = Date.now();
                            this.stats.bytesReceived = 0;
                            this.stats.messagesReceived = 0;
                            this.lastUpdateTime = Date.now();
                            this.lastBytesReceived = 0;
                            this.updateStatus('connected');
                            resolve();
                        } catch (error) {
                            this.ws!.send(
                                JSON.stringify({
                                    type: 'media-answer',
                                    payload: { accepted: false, reason: String(error) },
                                })
                            );
                            reject(new Error(`Decoder init failed: ${error}`));
                        }
                    }
                } else {
                    this.handleWebSocketMessage(event);
                }
            };

            this.ws.onerror = () => {
                reject(new Error('WebSocket error'));
            };

            this.ws.onclose = () => {
                this.updateStatus('disconnected');
                this.processing = false;
            };
        });
    }

    private handleWebSocketMessage(event: MessageEvent): void {
        this.stats.messagesReceived++;

        if (event.data instanceof ArrayBuffer) {
            const dataSize = event.data.byteLength;
            this.stats.bytesReceived += dataSize;
            this.queue.enqueue(event.data);
            this.processQueue();

            this.updateDataRate();
        } else if (event.data instanceof Blob) {
            const dataSize = event.data.size;
            this.stats.bytesReceived += dataSize;
            this.queue.enqueue(event.data);
            this.processQueue();

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

    private async processQueue(): Promise<void> {
        if (this.processing) return;
        this.processing = true;
        try {
            while (!this.queue.isEmpty() && this.decoder) {
                const packet = this.queue.dequeue();
                if (!packet) break;

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
        } finally {
            this.processing = false;
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
