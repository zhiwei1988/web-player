import { StreamManager } from '../../dist/js/js/stream/StreamManager.js';
import { GridLayout } from '../../dist/js/js/ui/GridLayout.js';
import { StreamCard } from '../../dist/js/js/ui/StreamCard.js';

const streamManager = new StreamManager();
const gridLayout = new GridLayout('streamGrid');

let streamIdCounter = 1;

function updateStreamCount() {
    const count = streamManager.getStreamCount();
    document.getElementById('streamCount').textContent = count;

    const emptyState = document.getElementById('emptyState');
    if (count === 0) {
        emptyState.classList.remove('hidden');
    } else {
        emptyState.classList.add('hidden');
    }
}

function addStream() {
    const streamId = `stream-${streamIdCounter++}`;

    const card = new StreamCard({
        id: streamId,
        onConnect: async (wsUrl) => {
            try {
                const canvas = card.getCanvas();
                const audioContext = new AudioContext();

                const stream = streamManager.createStream({
                    id: streamId,
                    wsUrl: wsUrl,
                    canvas: canvas,
                    wasmPath: '/dist/decoder.js',
                    audioContext: audioContext,
                });

                stream.setOnStatusChange((status) => {
                    card.updateStatus(status);
                });

                stream.setOnStatsUpdate((stats) => {
                    card.updateStats(stats);
                });

                stream.setOnError((error) => {
                    console.error(`Stream ${streamId} error:`, error);
                });

                await stream.connect();
                console.log(`Stream ${streamId} connected`);
            } catch (error) {
                console.error(`Failed to connect stream ${streamId}:`, error);
                alert(`Failed to connect: ${error.message}`);
            }
        },
        onDisconnect: () => {
            const stream = streamManager.getStream(streamId);
            if (stream) {
                stream.disconnect();
                console.log(`Stream ${streamId} disconnected`);
            }
        },
        onRemove: () => {
            const stream = streamManager.getStream(streamId);
            if (stream) {
                stream.disconnect();
            }
            streamManager.destroyStream(streamId);
            gridLayout.removeCard(streamId);
            updateStreamCount();
            console.log(`Stream ${streamId} removed`);
        },
    });

    gridLayout.addCard(card);
    updateStreamCount();
    console.log(`Stream ${streamId} added`);
}

function removeAllStreams() {
    if (streamManager.getStreamCount() === 0) {
        return;
    }

    if (confirm('Remove all streams?')) {
        streamManager.destroyAll();
        gridLayout.clear();
        updateStreamCount();
        console.log('All streams removed');
    }
}

document.getElementById('addStreamBtn').addEventListener('click', addStream);
document.getElementById('removeAllBtn').addEventListener('click', removeAllStreams);

document.getElementById('gridModeSelect').addEventListener('change', (event) => {
    const mode = event.target.value;
    gridLayout.setMode(mode);
    console.log(`Grid mode changed to ${mode}`);
});

updateStreamCount();
console.log('Multi-stream player initialized');
