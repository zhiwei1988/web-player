import { StreamInstance, StreamConfig } from './StreamInstance.js';

export class StreamManager {
  private streams: Map<string, StreamInstance> = new Map();

  createStream(config: StreamConfig): StreamInstance {
    if (this.streams.has(config.id)) {
      throw new Error(`Stream with id ${config.id} already exists`);
    }

    const stream = new StreamInstance(config);
    this.streams.set(config.id, stream);
    return stream;
  }

  getStream(id: string): StreamInstance | undefined {
    return this.streams.get(id);
  }

  getAllStreams(): StreamInstance[] {
    return Array.from(this.streams.values());
  }

  destroyStream(id: string): boolean {
    const stream = this.streams.get(id);
    if (!stream) {
      return false;
    }

    stream.disconnect();
    this.streams.delete(id);
    return true;
  }

  destroyAll(): void {
    for (const stream of this.streams.values()) {
      stream.disconnect();
    }
    this.streams.clear();
  }

  getStreamCount(): number {
    return this.streams.size;
  }

  getStreamById(id: string): StreamInstance | undefined {
    return this.streams.get(id);
  }

  hasStream(id: string): boolean {
    return this.streams.has(id);
  }

  getConnectedStreams(): StreamInstance[] {
    return this.getAllStreams().filter(
      stream => stream.getStatus() === 'connected'
    );
  }
}
