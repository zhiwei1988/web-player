import { DecoderWrapper } from '../js/decoder/DecoderWrapper.js';
import { FrameParseStatus } from '../js/decoder/types.js';
import type { WorkerRequest, WorkerResponse } from '../js/decoder/types.js';

let decoder: DecoderWrapper | null = null;
let statsInterval: number | null = null;

self.onmessage = async (event: MessageEvent<WorkerRequest>) => {
  const request = event.data;

  try {
    switch (request.type) {
      case 'init':
        await handleInit(request.config);
        break;

      case 'decode':
        await handleDecode(request.data, request.pts);
        break;

      case 'flush':
        await handleFlush();
        break;

      case 'destroy':
        await handleDestroy();
        break;

      default:
        postError(`Unknown request type: ${(request as any).type}`);
    }
  } catch (error) {
    postError(`Worker error: ${error instanceof Error ? error.message : String(error)}`);
  }
};

async function handleInit(config: any): Promise<void> {
  if (decoder) {
    decoder.destroy();
  }

  decoder = new DecoderWrapper();
  await decoder.init(config);
  decoder.initProtocol();

  const response: WorkerResponse = { type: 'ready' };
  self.postMessage(response);

  if (statsInterval) {
    clearInterval(statsInterval);
  }
  statsInterval = self.setInterval(() => {
    if (decoder) {
      const stats = decoder.getStats();
      const response: WorkerResponse = { type: 'stats', stats };
      self.postMessage(response);
    }
  }, 1000);
}

async function handleDecode(data: Uint8Array, _pts?: number): Promise<void> {
  if (!decoder) {
    throw new Error('Decoder not initialized');
  }

  const parsed = decoder.parseFrame(data);
  if (!parsed) {
    return;
  }

  if (parsed.status === FrameParseStatus.FRAGMENT_PENDING) {
    return;
  }

  if (parsed.status === FrameParseStatus.SKIP ||
      parsed.status === FrameParseStatus.ERROR) {
    return;
  }

  // FRAME_COMPLETE: only process VIDEO messages (msg_type = 0x01)
  if (parsed.msgType !== 0x01) {
    return;
  }

  const frame = await decoder.decode(parsed.payload, parsed.timestamp);

  if (frame) {
    const transferableBuffers = [
      frame.yData.buffer,
      frame.uData.buffer,
      frame.vData.buffer
    ];

    const response: WorkerResponse = {
      type: 'frame',
      frame
    };

    self.postMessage(response, transferableBuffers);
  }
}

async function handleFlush(): Promise<void> {
  if (!decoder) {
    throw new Error('Decoder not initialized');
  }

  const frames = await decoder.flush();

  for (const frame of frames) {
    const transferableBuffers = [
      frame.yData.buffer,
      frame.uData.buffer,
      frame.vData.buffer
    ];

    const response: WorkerResponse = {
      type: 'frame',
      frame
    };

    self.postMessage(response, transferableBuffers);
  }
}

async function handleDestroy(): Promise<void> {
  if (statsInterval) {
    clearInterval(statsInterval);
    statsInterval = null;
  }

  if (decoder) {
    decoder.destroy();
    decoder = null;
  }

  const response: WorkerResponse = { type: 'destroyed' };
  self.postMessage(response);
}

function postError(message: string): void {
  const response: WorkerResponse = {
    type: 'error',
    error: message
  };
  self.postMessage(response);
}

self.onerror = ((event: string | Event) => {
  if (typeof event === 'string') {
    postError(`Worker uncaught error: ${event}`);
  } else if (event instanceof ErrorEvent) {
    postError(`Worker uncaught error: ${event.message}`);
  } else {
    postError('Worker uncaught error: unknown error');
  }
}) as OnErrorEventHandler;

self.onunhandledrejection = (event: PromiseRejectionEvent) => {
  postError(`Worker unhandled rejection: ${event.reason}`);
};
