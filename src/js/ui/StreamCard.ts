import type { StreamStats, StreamStatus } from '../stream/StreamInstance.js';

export interface StreamCardConfig {
  id: string;
  onConnect?: (wsUrl: string, codec: 'h264' | 'hevc') => void;
  onDisconnect?: () => void;
  onRemove?: () => void;
}

export class StreamCard {
  readonly id: string;
  private container: HTMLElement;
  private canvas: HTMLCanvasElement;
  private statusIndicator: HTMLElement;
  private statsPanel: HTMLElement;
  private fpsOverlay: HTMLElement;
  private statsCollapsed: boolean = true;

  private config: StreamCardConfig;

  constructor(config: StreamCardConfig) {
    this.id = config.id;
    this.config = config;
    this.container = this.createContainer();
    this.canvas = this.container.querySelector('.stream-canvas') as HTMLCanvasElement;
    this.statusIndicator = this.container.querySelector('.status-indicator') as HTMLElement;
    this.statsPanel = this.container.querySelector('.stats-content') as HTMLElement;
    this.fpsOverlay = this.container.querySelector('.fps-overlay') as HTMLElement;
  }

  private createContainer(): HTMLElement {
    const card = document.createElement('div');
    card.className = 'stream-card bg-white rounded-lg shadow-md overflow-hidden';
    card.innerHTML = `
      <div class="stream-header p-3 bg-gray-100 border-b flex justify-between items-center">
        <div class="flex items-center gap-2">
          <span class="status-indicator w-3 h-3 rounded-full bg-gray-400"></span>
          <span class="stream-title font-semibold text-sm">Stream ${this.id}</span>
        </div>
        <button class="remove-btn text-red-500 hover:text-red-700 text-xl font-bold leading-none">&times;</button>
      </div>

      <div class="stream-content">
        <div class="canvas-container bg-black relative">
          <canvas class="stream-canvas w-full h-auto" width="640" height="360"></canvas>
          <div class="stream-overlay absolute inset-0 flex items-center justify-center text-white text-sm">
            Disconnected
          </div>
          <div class="fps-overlay absolute top-1 right-1 px-2 py-1 bg-black bg-opacity-60 text-white text-xs font-mono rounded hidden">
            0.0 FPS
          </div>
        </div>

        <div class="stream-controls p-3 bg-gray-50 border-t">
          <div class="flex gap-2 mb-2">
            <input type="text" class="ws-url flex-1 px-2 py-1 text-xs border rounded"
                   placeholder="wss://192.168.50.101:6061" value="wss://192.168.50.101:6061">
            <select class="codec-select px-2 py-1 text-xs border rounded">
              <option value="h264">H.264</option>
              <option value="hevc">H.265</option>
            </select>
          </div>
          <div class="flex gap-2">
            <button class="connect-btn flex-1 px-3 py-1 text-xs bg-green-500 text-white rounded hover:bg-green-600">
              Connect
            </button>
            <button class="disconnect-btn flex-1 px-3 py-1 text-xs bg-red-500 text-white rounded hover:bg-red-600">
              Disconnect
            </button>
          </div>
        </div>

        <div class="stream-stats border-t">
          <div class="stats-header p-2 bg-gray-100 cursor-pointer flex justify-between items-center">
            <span class="text-xs font-semibold">Statistics</span>
            <span class="toggle-icon text-xs">▼</span>
          </div>
          <div class="stats-content hidden p-3 text-xs space-y-1">
            <div class="stat-row flex justify-between">
              <span>Data Rate:</span>
              <span class="stat-data-rate font-mono">0 KB/s</span>
            </div>
            <div class="stat-row flex justify-between">
              <span>Messages:</span>
              <span class="stat-messages font-mono">0</span>
            </div>
            <div class="stat-row flex justify-between">
              <span>Bytes:</span>
              <span class="stat-bytes font-mono">0</span>
            </div>
            <div class="stat-row flex justify-between">
              <span>Buffer:</span>
              <span class="stat-buffer font-mono">0/0</span>
            </div>
            <div class="stat-row flex justify-between">
              <span>FPS:</span>
              <span class="stat-fps font-mono">0</span>
            </div>
            <div class="stat-row flex justify-between">
              <span>Resolution:</span>
              <span class="stat-resolution font-mono">N/A</span>
            </div>
          </div>
        </div>
      </div>
    `;

    this.attachEventListeners(card);
    return card;
  }

  private attachEventListeners(card: HTMLElement): void {
    const connectBtn = card.querySelector('.connect-btn') as HTMLButtonElement;
    const disconnectBtn = card.querySelector('.disconnect-btn') as HTMLButtonElement;
    const removeBtn = card.querySelector('.remove-btn') as HTMLButtonElement;
    const statsHeader = card.querySelector('.stats-header') as HTMLElement;

    connectBtn.addEventListener('click', () => {
      const wsUrl = (card.querySelector('.ws-url') as HTMLInputElement).value;
      const codec = (card.querySelector('.codec-select') as HTMLSelectElement).value as 'h264' | 'hevc';
      if (this.config.onConnect) {
        this.config.onConnect(wsUrl, codec);
      }
    });

    disconnectBtn.addEventListener('click', () => {
      if (this.config.onDisconnect) {
        this.config.onDisconnect();
      }
    });

    removeBtn.addEventListener('click', () => {
      if (this.config.onRemove) {
        this.config.onRemove();
      }
    });

    statsHeader.addEventListener('click', () => {
      this.toggleStats();
    });
  }

  private toggleStats(): void {
    this.statsCollapsed = !this.statsCollapsed;
    const toggleIcon = this.container.querySelector('.toggle-icon') as HTMLElement;

    if (this.statsCollapsed) {
      this.statsPanel.classList.add('hidden');
      toggleIcon.textContent = '▼';
    } else {
      this.statsPanel.classList.remove('hidden');
      toggleIcon.textContent = '▲';
    }
  }

  updateStatus(status: StreamStatus): void {
    const overlay = this.container.querySelector('.stream-overlay') as HTMLElement;

    switch (status) {
      case 'disconnected':
        this.statusIndicator.className = 'status-indicator w-3 h-3 rounded-full bg-gray-400';
        overlay.textContent = 'Disconnected';
        overlay.classList.remove('hidden');
        this.fpsOverlay.classList.add('hidden');
        break;
      case 'connecting':
        this.statusIndicator.className = 'status-indicator w-3 h-3 rounded-full bg-yellow-400';
        overlay.textContent = 'Connecting...';
        overlay.classList.remove('hidden');
        this.fpsOverlay.classList.add('hidden');
        break;
      case 'connected':
        this.statusIndicator.className = 'status-indicator w-3 h-3 rounded-full bg-green-500';
        overlay.classList.add('hidden');
        this.fpsOverlay.classList.remove('hidden');
        break;
      case 'error':
        this.statusIndicator.className = 'status-indicator w-3 h-3 rounded-full bg-red-500';
        overlay.textContent = 'Error';
        overlay.classList.remove('hidden');
        this.fpsOverlay.classList.add('hidden');
        break;
    }
  }

  updateStats(stats: StreamStats): void {
    const dataRateEl = this.container.querySelector('.stat-data-rate') as HTMLElement;
    const messagesEl = this.container.querySelector('.stat-messages') as HTMLElement;
    const bytesEl = this.container.querySelector('.stat-bytes') as HTMLElement;
    const bufferEl = this.container.querySelector('.stat-buffer') as HTMLElement;
    const fpsEl = this.container.querySelector('.stat-fps') as HTMLElement;

    dataRateEl.textContent = `${stats.dataRate.toFixed(2)} KB/s`;
    messagesEl.textContent = stats.messagesReceived.toString();
    bytesEl.textContent = this.formatBytes(stats.bytesReceived);

    if (stats.bufferStats) {
      bufferEl.textContent = `${stats.bufferStats.currentSize}/${stats.bufferStats.maxSize}`;
    }

    if (stats.decoderStats) {
      fpsEl.textContent = stats.decoderStats.currentFPS.toFixed(1);
      this.fpsOverlay.textContent = `${stats.decoderStats.currentFPS.toFixed(1)} FPS`;
    } else {
      this.fpsOverlay.textContent = '0.0 FPS';
    }
  }

  setResolution(width: number, height: number): void {
    const el = this.container.querySelector('.stat-resolution') as HTMLElement;
    el.textContent = `${width}x${height}`;
  }

  private formatBytes(bytes: number): string {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
  }

  getCanvas(): HTMLCanvasElement {
    return this.canvas;
  }

  getElement(): HTMLElement {
    return this.container;
  }

  destroy(): void {
    this.container.remove();
  }
}
