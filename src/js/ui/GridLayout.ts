import { StreamCard } from './StreamCard.js';

export type GridMode = '1x1' | '2x2' | '3x3' | '4x4';

export class GridLayout {
  private container: HTMLElement;
  private gridMode: GridMode = '2x2';
  private cards: Map<string, StreamCard> = new Map();

  constructor(containerId: string) {
    const element = document.getElementById(containerId);
    if (!element) {
      throw new Error(`Container with id ${containerId} not found`);
    }
    this.container = element;
    this.applyGridClass();
  }

  setMode(mode: GridMode): void {
    this.gridMode = mode;
    this.applyGridClass();
  }

  getMode(): GridMode {
    return this.gridMode;
  }

  addCard(card: StreamCard): void {
    if (this.cards.has(card.id)) {
      throw new Error(`Card with id ${card.id} already exists`);
    }

    this.cards.set(card.id, card);
    this.container.appendChild(card.getElement());
  }

  removeCard(id: string): boolean {
    const card = this.cards.get(id);
    if (!card) {
      return false;
    }

    card.destroy();
    this.cards.delete(id);
    return true;
  }

  getCard(id: string): StreamCard | undefined {
    return this.cards.get(id);
  }

  getAllCards(): StreamCard[] {
    return Array.from(this.cards.values());
  }

  getCardCount(): number {
    return this.cards.size;
  }

  clear(): void {
    for (const card of this.cards.values()) {
      card.destroy();
    }
    this.cards.clear();
  }

  private applyGridClass(): void {
    this.container.className = 'grid-container grid gap-4 p-4';

    switch (this.gridMode) {
      case '1x1':
        this.container.classList.add('grid-cols-1');
        break;
      case '2x2':
        this.container.classList.add('grid-cols-1', 'md:grid-cols-2');
        break;
      case '3x3':
        this.container.classList.add('grid-cols-1', 'md:grid-cols-2', 'lg:grid-cols-3');
        break;
      case '4x4':
        this.container.classList.add('grid-cols-1', 'md:grid-cols-2', 'lg:grid-cols-3', 'xl:grid-cols-4');
        break;
    }
  }
}
