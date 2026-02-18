import { WorkerBridge } from '../../dist/js/js/decoder/WorkerBridge.js';

let decoderBridge = null;
let lastVideoResolution = null;

async function initDecoder(codecType) {
    try {
        decoderBridge = new WorkerBridge('../../dist/js/worker/decode-worker.js');

        decoderBridge.onFrame((frame) => {
            renderFrame(frame);

            if (!lastVideoResolution ||
                lastVideoResolution.width !== frame.width ||
                lastVideoResolution.height !== frame.height) {
                lastVideoResolution = { width: frame.width, height: frame.height };
                document.getElementById('videoResolution').textContent =
                    `${frame.width}x${frame.height}`;
            }
        });

        decoderBridge.onStats((stats) => {
            document.getElementById('decoderFPS').textContent = stats.currentFPS.toFixed(2);
            document.getElementById('decodeTime').textContent = stats.avgDecodeTime.toFixed(2);
            document.getElementById('totalFrames').textContent = stats.totalFrames;
            document.getElementById('droppedFrames').textContent = stats.droppedFrames;
        });

        decoderBridge.onError((error) => {
            console.error('Decoder error:', error);
            document.getElementById('decoderStatus').textContent = `错误: ${error}`;
        });

        await decoderBridge.init({
            codecType: codecType || 'h264',
            wasmPath: '/dist/decoder.js'
        });

        const codecName = codecType === 'h265' ? 'H.265/HEVC' : 'H.264/AVC';
        document.getElementById('decoderStatus').textContent = `已初始化 (${codecName})`;

        window.decoderBridge = decoderBridge;
    } catch (error) {
        console.error('Failed to initialize decoder:', error);
        document.getElementById('decoderStatus').textContent = `初始化失败: ${error.message}`;
        throw error;
    }
}

function renderFrame(frame) {
    const canvas = document.getElementById('videoCanvas');
    const ctx = canvas.getContext('2d');

    if (canvas.width !== frame.width || canvas.height !== frame.height) {
        canvas.width = frame.width;
        canvas.height = frame.height;
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

window.initDecoder = initDecoder;
window.renderFrame = renderFrame;

