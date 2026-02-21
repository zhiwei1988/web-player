import type { AudioFrame } from '../decoder/types.js';

export class AudioPlayer {
    private audioCtx: AudioContext | null = null;
    private externalAudioCtx: boolean = false;
    private nextPlayTime: number = 0;
    private baseTime: number = 0;
    private basePts: number = -1;

    constructor(audioCtx?: AudioContext | null) {
        if (audioCtx) {
            this.audioCtx = audioCtx;
            this.externalAudioCtx = true;
            this.nextPlayTime = audioCtx.currentTime;
        }
    }

    /**
     * Get the audio clock time in milliseconds, relative to the first audio PTS.
     * Returns -1 if audio has not started yet.
     */
    getClockTimeMs(): number {
        if (!this.audioCtx || this.basePts < 0) {
            return -1;
        }
        return (this.audioCtx.currentTime - this.baseTime) * 1000 + this.basePts;
    }

    isActive(): boolean {
        return this.audioCtx !== null && this.audioCtx.state === 'running' && this.basePts >= 0;
    }

    playFrame(frame: AudioFrame): void {
        if (!this.audioCtx) {
            this.audioCtx = new AudioContext({ sampleRate: frame.sampleRate });
            this.nextPlayTime = this.audioCtx.currentTime;
        }

        // Initialize base time on first frame
        if (this.basePts < 0) {
            this.basePts = frame.pts;
            this.baseTime = this.audioCtx.currentTime;
            this.nextPlayTime = this.audioCtx.currentTime;
        }

        const audioBuffer = this.audioCtx.createBuffer(frame.channels, frame.nbSamples, frame.sampleRate);

        // Deinterleave PCM data into per-channel buffers
        for (let ch = 0; ch < frame.channels; ch++) {
            const channelData = audioBuffer.getChannelData(ch);
            for (let i = 0; i < frame.nbSamples; i++) {
                channelData[i] = frame.data[i * frame.channels + ch];
            }
        }

        const source = this.audioCtx.createBufferSource();
        source.buffer = audioBuffer;
        source.connect(this.audioCtx.destination);

        // Schedule playback; ensure we don't schedule in the past
        if (this.nextPlayTime < this.audioCtx.currentTime) {
            this.nextPlayTime = this.audioCtx.currentTime;
        }

        source.start(this.nextPlayTime);
        this.nextPlayTime += frame.nbSamples / frame.sampleRate;
    }

    destroy(): void {
        if (this.audioCtx && !this.externalAudioCtx) {
            this.audioCtx.close();
        }
        this.audioCtx = null;
        this.basePts = -1;
        this.baseTime = 0;
        this.nextPlayTime = 0;
    }
}
