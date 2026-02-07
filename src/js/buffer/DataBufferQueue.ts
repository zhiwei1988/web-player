interface DataPacket {
  data: ArrayBuffer | Blob;
  timestamp: number;
  size: number;
}

interface QueueConfig {
  maxSize?: number;
  maxBytes?: number;
}

interface QueueStats {
  totalEnqueued: number;
  totalDequeued: number;
  totalOverflows: number;
  totalBytesEnqueued: number;
  totalBytesDequeued: number;
  totalBytesDropped: number;
}

interface QueueUsage {
  sizeUsage: string;
  bytesUsage: string;
}

export class DataBufferQueue {
  private maxSize: number;
  private maxBytes: number;
  private queue: DataPacket[] = [];
  private currentBytes: number = 0;
  private stats: QueueStats;

  constructor(config: QueueConfig = {}) {
    this.maxSize = config.maxSize || 100;
    this.maxBytes = config.maxBytes || 10 * 1024 * 1024;

    this.stats = {
      totalEnqueued: 0,
      totalDequeued: 0,
      totalOverflows: 0,
      totalBytesEnqueued: 0,
      totalBytesDequeued: 0,
      totalBytesDropped: 0
    };
  }

  enqueue(data: ArrayBuffer | Blob): boolean {
    const dataSize = data instanceof ArrayBuffer ? data.byteLength : data.size;

    if (this.queue.length >= this.maxSize ||
        this.currentBytes + dataSize > this.maxBytes) {
      const dropped = this.queue.shift();
      if (dropped) {
        const droppedSize = dropped.data instanceof ArrayBuffer ?
                          dropped.data.byteLength : dropped.data.size;
        this.currentBytes -= droppedSize;
        this.stats.totalOverflows++;
        this.stats.totalBytesDropped += droppedSize;
      }
    }

    const packet: DataPacket = {
      data,
      timestamp: Date.now(),
      size: dataSize
    };

    this.queue.push(packet);
    this.currentBytes += dataSize;
    this.stats.totalEnqueued++;
    this.stats.totalBytesEnqueued += dataSize;

    return true;
  }

  dequeue(): DataPacket | null {
    if (this.queue.length === 0) {
      return null;
    }

    const packet = this.queue.shift()!;
    this.currentBytes -= packet.size;
    this.stats.totalDequeued++;
    this.stats.totalBytesDequeued += packet.size;

    return packet;
  }

  peek(): DataPacket | null {
    return this.queue.length > 0 ? this.queue[0] : null;
  }

  clear(): void {
    this.queue = [];
    this.currentBytes = 0;
  }

  size(): number {
    return this.queue.length;
  }

  isEmpty(): boolean {
    return this.queue.length === 0;
  }

  isFull(): boolean {
    return this.queue.length >= this.maxSize ||
           this.currentBytes >= this.maxBytes;
  }

  getUsage(): QueueUsage {
    return {
      sizeUsage: (this.queue.length / this.maxSize * 100).toFixed(1),
      bytesUsage: (this.currentBytes / this.maxBytes * 100).toFixed(1)
    };
  }

  getStats() {
    return {
      currentSize: this.queue.length,
      currentBytes: this.currentBytes,
      maxSize: this.maxSize,
      maxBytes: this.maxBytes,
      usage: this.getUsage(),
      ...this.stats
    };
  }
}
