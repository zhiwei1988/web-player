const WebSocket = require('ws');
const fs = require('fs');
const path = require('path');

// Configuration
const PORT = 8080;
const SEND_INTERVAL = 1000; // Send data every 1 second

// Codec type from environment variable (default: h264)
const CODEC_TYPE = process.env.CODEC_TYPE || 'h264';
const IS_H265 = CODEC_TYPE === 'h265' || CODEC_TYPE === 'hevc';

// Test video path based on codec type
const TEST_VIDEO_PATH = IS_H265
    ? './tests/fixtures/TSU_640x360.h265'
    : './tests/fixtures/test_video.h264';
const NAL_SEND_INTERVAL = 40;  // ~25fps (milliseconds)

console.log(`Codec type: ${CODEC_TYPE} (${IS_H265 ? 'H.265/HEVC' : 'H.264/AVC'})`);
console.log(`Video file: ${TEST_VIDEO_PATH}`);

// Parse H.264 NAL units from buffer
function parseNALUnits(buffer) {
    const nalUnits = [];
    let start = 0;
    let firstNalFound = false;

    // Find NAL start codes: 0x00 0x00 0x00 0x01 or 0x00 0x00 0x01
    for (let i = 0; i < buffer.length - 3; i++) {
        // Check for 4-byte start code first
        const is4ByteStart = buffer[i] === 0 && buffer[i+1] === 0 &&
                             buffer[i+2] === 0 && buffer[i+3] === 1;
        // Check for 3-byte start code, but make sure it's not part of a 4-byte start code
        const is3ByteStart = !is4ByteStart && i > 0 &&
                             buffer[i] === 0 && buffer[i+1] === 0 &&
                             buffer[i+2] === 1;

        if (is4ByteStart || is3ByteStart) {
            if (firstNalFound) {
                // Save the previous NAL unit
                nalUnits.push(buffer.slice(start, i));
            }
            start = i;
            firstNalFound = true;
            // Skip the start code bytes to avoid re-detecting them
            i += is4ByteStart ? 3 : 2;
        }
    }

    // Add the last NAL unit
    if (firstNalFound && start < buffer.length) {
        nalUnits.push(buffer.slice(start));
    }

    return nalUnits;
}


// Load test video file
let testVideoData = null;
let nalUnits = [];

try {
    testVideoData = fs.readFileSync(TEST_VIDEO_PATH);
    nalUnits = parseNALUnits(testVideoData);  // NAL parsing is same for H.264/H.265
    console.log(`Loaded ${IS_H265 ? 'H.265' : 'H.264'} test video: ${TEST_VIDEO_PATH}`);
    console.log(`NAL units count: ${nalUnits.length}`);
    console.log(`File size: ${(testVideoData.length / 1024).toFixed(2)} KB`);

    // Log first few NAL sizes
    console.log('\nFirst 5 NAL units:');
    for (let i = 0; i < Math.min(5, nalUnits.length); i++) {
        console.log(`  NAL ${i}: ${nalUnits[i].length} bytes`);
    }
} catch (error) {
    console.error(`Failed to load test video file: ${error.message}`);
}

// Create WebSocket server
const wss = new WebSocket.Server({ port: PORT });

// Track connections
let connectionCount = 0;
const connections = new Map();

// Handle new connections
wss.on('connection', (ws, req) => {
    const clientId = ++connectionCount;
    const clientIp = req.socket.remoteAddress;

    console.log(`[Connection #${clientId}] New client connected`);
    console.log(`   IP Address: ${clientIp}`);
    console.log(`   Current connections: ${wss.clients.size}`);
    console.log('');

    // Store connection info
    const connectionInfo = {
        id: clientId,
        ip: clientIp,
        connectedAt: new Date(),
        messagesSent: 0,
        bytesSent: 0
    };
    connections.set(ws, connectionInfo);

    // Start sending data (NAL units)
    // Create a timer to send data every NAL_SEND_INTERVAL milliseconds
    const dataInterval = setInterval(() => {
        if (ws.readyState === WebSocket.OPEN) {
            if (nalUnits.length > 0) {
                // Send NAL units (loop playback)
                const nalIndex = connectionInfo.messagesSent % nalUnits.length;
                const dataToSend = nalUnits[nalIndex];
                const dataSize = dataToSend.length;

                // Log every 25 NAL units
                if (nalIndex % 25 === 0) {
                    console.log(`[Connection #${clientId}] Sending NAL unit ${nalIndex}/${nalUnits.length} (${dataSize} bytes)`);
                }

                try {
                    ws.send(dataToSend);
                    connectionInfo.messagesSent++;
                    connectionInfo.bytesSent += dataSize;
                } catch (error) {
                    console.error(`[Connection #${clientId}] Failed to send data:`, error.message);
                }
            }
        }
    }, NAL_SEND_INTERVAL);

    // Handle incoming messages
    ws.on('message', (message) => {
        try {
            const data = message.toString();
            console.log(`[Connection #${clientId}] Received message: ${data}`);
        } catch (error) {
            console.error(`[Connection #${clientId}] Failed to process message:`, error.message);
        }
    });

    // Handle errors
    ws.on('error', (error) => {
        console.error(`[Connection #${clientId}] WebSocket error:`, error.message);
    });

    // Handle disconnection
    ws.on('close', (code, reason) => {
        // Clear the data interval timer
        clearInterval(dataInterval);

        const info = connections.get(ws);
        if (info) {
            const duration = Math.floor((Date.now() - info.connectedAt.getTime()) / 1000);
            const mbSent = (info.bytesSent / 1024 / 1024).toFixed(2);

            console.log('');
            console.log(`[Connection #${clientId}] Client disconnected`);
            console.log(`   Close code: ${code}`);
            console.log(`   Close reason: ${reason || 'none'}`);
            console.log(`   Connection duration: ${duration} seconds`);
            console.log(`   Messages sent: ${info.messagesSent}`);
            console.log(`   Data sent: ${mbSent} MB`);
            console.log(`   Remaining connections: ${wss.clients.size}`);
            console.log('');

            connections.delete(ws);
        }
    });
});

// Handle server errors
wss.on('error', (error) => {
    console.error('WebSocket server error:', error);
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('');
    console.log('Shutting down server...');

    wss.clients.forEach((ws) => {
        ws.close(1000, 'Server is shutting down');
    });

    wss.close(() => {
        console.log('Server closed');
        process.exit(0);
    });
});

// Log server stats every 60 seconds
setInterval(() => {
    if (wss.clients.size > 0) {
        console.log('');
        console.log('Server status:');
        console.log(`   Active connections: ${wss.clients.size}`);
        console.log(`   Total connections: ${connectionCount}`);

        let totalBytesSent = 0;
        let totalMessagesSent = 0;
        connections.forEach((info) => {
            totalBytesSent += info.bytesSent;
            totalMessagesSent += info.messagesSent;
        });

        if (totalBytesSent > 0) {
            const mbSent = (totalBytesSent / 1024 / 1024).toFixed(2);
            console.log(`   Total sent: ${totalMessagesSent} messages, ${mbSent} MB`);
        }
        console.log('');
    }
}, 60000);
