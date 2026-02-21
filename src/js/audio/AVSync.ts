import type { AudioPlayer } from './AudioPlayer.js';
import type { VideoFrame } from '../decoder/types.js';

export enum SyncAction {
    RENDER = 'render',
    DELAY = 'delay',
    SKIP = 'skip',
}

const SYNC_AHEAD_THRESHOLD_MS = 30;
const SYNC_BEHIND_THRESHOLD_MS = 60;

export class AVSync {
    private audioPlayer: AudioPlayer;
    private pendingFrame: VideoFrame | null = null;
    private delayTimer: number | null = null;
    private renderCallback: ((frame: VideoFrame) => void) | null = null;

    constructor(audioPlayer: AudioPlayer) {
        this.audioPlayer = audioPlayer;
    }

    setRenderCallback(callback: (frame: VideoFrame) => void): void {
        this.renderCallback = callback;
    }

    /**
     * Determine sync action for a video frame based on audio clock.
     * If audio is not active, always render immediately.
     */
    decideSyncAction(videoPtsMs: number): SyncAction {
        if (!this.audioPlayer.isActive()) {
            return SyncAction.RENDER;
        }

        const audioClockMs = this.audioPlayer.getClockTimeMs();
        if (audioClockMs < 0) {
            return SyncAction.RENDER;
        }

        const diff = videoPtsMs - audioClockMs;

        if (diff > SYNC_AHEAD_THRESHOLD_MS) {
            return SyncAction.DELAY;
        }

        if (diff < -SYNC_BEHIND_THRESHOLD_MS) {
            return SyncAction.SKIP;
        }

        return SyncAction.RENDER;
    }

    /**
     * Process a video frame: render, delay, or skip based on sync.
     */
    processVideoFrame(frame: VideoFrame): void {
        const action = this.decideSyncAction(frame.pts);

        switch (action) {
            case SyncAction.RENDER:
                this.doRender(frame);
                break;

            case SyncAction.DELAY:
                this.scheduleDelayedRender(frame);
                break;

            case SyncAction.SKIP:
                // Drop the frame
                break;
        }
    }

    private doRender(frame: VideoFrame): void {
        if (this.renderCallback) {
            this.renderCallback(frame);
        }
    }

    private scheduleDelayedRender(frame: VideoFrame): void {
        // Cancel any previously pending delay
        if (this.delayTimer !== null) {
            clearTimeout(this.delayTimer);
        }

        this.pendingFrame = frame;

        const audioClockMs = this.audioPlayer.getClockTimeMs();
        const delayMs = Math.max(1, frame.pts - audioClockMs);

        this.delayTimer = window.setTimeout(() => {
            this.delayTimer = null;
            if (this.pendingFrame) {
                this.doRender(this.pendingFrame);
                this.pendingFrame = null;
            }
        }, delayMs);
    }

    destroy(): void {
        if (this.delayTimer !== null) {
            clearTimeout(this.delayTimer);
            this.delayTimer = null;
        }
        this.pendingFrame = null;
        this.renderCallback = null;
    }
}
