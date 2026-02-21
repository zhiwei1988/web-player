import type {
    AudioDecoderConfig,
    AudioFrame,
    DecoderConfig,
    VideoFrame,
    DecoderStats,
    WorkerRequest,
    WorkerResponse,
} from './types.js';

type FrameCallback = (frame: VideoFrame) => void;
type AudioFrameCallback = (frame: AudioFrame) => void;
type StatsCallback = (stats: DecoderStats) => void;
type ErrorCallback = (error: string) => void;

export class WorkerBridge {
    private worker: Worker | null = null;
    private initPromise: Promise<void> | null = null;
    private frameCallback: FrameCallback | null = null;
    private audioFrameCallback: AudioFrameCallback | null = null;
    private statsCallback: StatsCallback | null = null;
    private errorCallback: ErrorCallback | null = null;

    constructor(workerPath: string) {
        try {
            this.worker = new Worker(workerPath, { type: 'module' });
            this.worker.onmessage = this.handleWorkerMessage.bind(this);
            this.worker.onerror = this.handleWorkerError.bind(this);
        } catch (error) {
            throw new Error(`Failed to create worker: ${error}`);
        }
    }

    async init(config: DecoderConfig): Promise<void> {
        if (this.initPromise) {
            return this.initPromise;
        }

        this.initPromise = new Promise<void>((resolve, reject) => {
            if (!this.worker) {
                reject(new Error('Worker not initialized'));
                return;
            }

            const timeout = setTimeout(() => {
                reject(new Error('Worker initialization timeout'));
            }, 10000);

            const messageHandler = (event: MessageEvent<WorkerResponse>) => {
                const response = event.data;

                if (response.type === 'ready') {
                    clearTimeout(timeout);
                    this.worker?.removeEventListener('message', messageHandler);
                    resolve();
                } else if (response.type === 'error') {
                    clearTimeout(timeout);
                    this.worker?.removeEventListener('message', messageHandler);
                    reject(new Error(response.error));
                }
            };

            this.worker.addEventListener('message', messageHandler);

            const request: WorkerRequest = { type: 'init', config };
            this.worker.postMessage(request);
        });

        return this.initPromise;
    }

    async initAudio(config: AudioDecoderConfig): Promise<void> {
        if (!this.worker) {
            throw new Error('Worker not initialized');
        }

        if (!this.initPromise) {
            throw new Error('Video decoder not initialized. Call init() first.');
        }

        await this.initPromise;

        const request: WorkerRequest = { type: 'initAudio', config };
        this.worker.postMessage(request);
    }

    async decode(data: Uint8Array, pts?: number): Promise<void> {
        if (!this.worker) {
            throw new Error('Worker not initialized');
        }

        if (!this.initPromise) {
            throw new Error('Decoder not initialized. Call init() first.');
        }

        await this.initPromise;

        const request: WorkerRequest = {
            type: 'decode',
            data,
            pts,
        };

        this.worker.postMessage(request, [data.buffer]);
    }

    async flush(): Promise<void> {
        if (!this.worker) {
            throw new Error('Worker not initialized');
        }

        if (!this.initPromise) {
            throw new Error('Decoder not initialized. Call init() first.');
        }

        await this.initPromise;

        const request: WorkerRequest = { type: 'flush' };
        this.worker.postMessage(request);
    }

    destroy(): void {
        if (this.worker) {
            const request: WorkerRequest = { type: 'destroy' };
            this.worker.postMessage(request);

            setTimeout(() => {
                if (this.worker) {
                    this.worker.terminate();
                    this.worker = null;
                }
            }, 100);
        }

        this.initPromise = null;
        this.frameCallback = null;
        this.audioFrameCallback = null;
        this.statsCallback = null;
        this.errorCallback = null;
    }

    onFrame(callback: FrameCallback): void {
        this.frameCallback = callback;
    }

    onAudioFrame(callback: AudioFrameCallback): void {
        this.audioFrameCallback = callback;
    }

    onStats(callback: StatsCallback): void {
        this.statsCallback = callback;
    }

    onError(callback: ErrorCallback): void {
        this.errorCallback = callback;
    }

    private handleWorkerMessage(event: MessageEvent<WorkerResponse>): void {
        const response = event.data;

        switch (response.type) {
            case 'frame':
                if (this.frameCallback) {
                    this.frameCallback(response.frame);
                }
                break;

            case 'audioFrame':
                if (this.audioFrameCallback) {
                    this.audioFrameCallback(response.frame);
                }
                break;

            case 'stats':
                if (this.statsCallback) {
                    this.statsCallback(response.stats);
                }
                break;

            case 'error':
                if (this.errorCallback) {
                    this.errorCallback(response.error);
                } else {
                    console.error('Worker error:', response.error);
                }
                break;

            case 'destroyed':
                console.log('Worker destroyed');
                break;

            case 'ready':
                break;

            default:
                console.warn('Unknown worker response type:', (response as any).type);
        }
    }

    private handleWorkerError(event: ErrorEvent): void {
        const errorMessage = `Worker error: ${event.message} at ${event.filename}:${event.lineno}:${event.colno}`;
        console.error(errorMessage);

        if (this.errorCallback) {
            this.errorCallback(errorMessage);
        }
    }
}
