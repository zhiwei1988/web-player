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
function parseH264NALUnits(buffer) {
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

// Get NAL unit type (H.264)
function getH264NALType(nalUnit) {
    const startCodeLen = (nalUnit[0] === 0 && nalUnit[1] === 0 && nalUnit[2] === 0 && nalUnit[3] === 1) ? 4 : 3;
    return nalUnit[startCodeLen] & 0x1f;
}

// Get NAL unit type (H.265)
// H.265 NAL header is 2 bytes, type is in bits 1-6 of the first byte
function getH265NALType(nalUnit) {
    const startCodeLen = (nalUnit[0] === 0 && nalUnit[1] === 0 && nalUnit[2] === 0 && nalUnit[3] === 1) ? 4 : 3;
    return (nalUnit[startCodeLen] >> 1) & 0x3f;
}

// Check if NAL unit is a VCL slice (H.264)
function isH264VCLNAL(nalType) {
    // VCL NAL unit types: 1-5 are slice types
    return nalType >= 1 && nalType <= 5;
}

// Check if NAL unit is a VCL slice (H.265)
function isH265VCLNAL(nalType) {
    // H.265 VCL NAL types: 0-31 (excluding VPS=32, SPS=33, PPS=34, AUD=35, etc.)
    return nalType >= 0 && nalType <= 31;
}

// Check if H.265 NAL is an IDR/IRAP frame
function isH265IDR(nalType) {
    // IDR_W_RADL=19, IDR_N_LP=20, CRA_NUT=21
    return nalType === 19 || nalType === 20 || nalType === 21;
}

// Group NAL units into Access Units (frames) - H.264
function groupH264AccessUnits(nalUnits) {
    const accessUnits = [];
    let currentAU = [];
    let spsData = null;
    let ppsData = null;
    let hasVCLInCurrentAU = false;

    for (const nal of nalUnits) {
        const nalType = getH264NALType(nal);

        // Save SPS and PPS for later use
        if (nalType === 7) {  // SPS
            spsData = nal;
            continue;
        }
        if (nalType === 8) {  // PPS
            ppsData = nal;
            continue;
        }

        // AUD (type 9) marks the start of a new Access Unit
        if (nalType === 9) {
            if (currentAU.length > 0) {
                accessUnits.push(Buffer.concat(currentAU));
                currentAU = [];
                hasVCLInCurrentAU = false;
            }
            currentAU.push(nal);
            continue;
        }

        // Check if this VCL NAL starts a new Access Unit
        if (isH264VCLNAL(nalType)) {
            if (hasVCLInCurrentAU) {
                accessUnits.push(Buffer.concat(currentAU));
                currentAU = [];
                hasVCLInCurrentAU = false;
            }
            hasVCLInCurrentAU = true;

            // For the first IDR frame, prepend SPS and PPS
            if (nalType === 5 && spsData && ppsData && accessUnits.length === 0) {
                currentAU.push(spsData, ppsData);
            }
        }

        currentAU.push(nal);
    }

    if (currentAU.length > 0) {
        accessUnits.push(Buffer.concat(currentAU));
    }

    return accessUnits;
}

// Group NAL units into Access Units (frames) - H.265
function groupH265AccessUnits(nalUnits) {
    const accessUnits = [];
    let currentAU = [];
    let vpsData = null;
    let spsData = null;
    let ppsData = null;
    let hasVCLInCurrentAU = false;

    for (const nal of nalUnits) {
        const nalType = getH265NALType(nal);

        // Save VPS, SPS and PPS for later use
        if (nalType === 32) {  // VPS
            vpsData = nal;
            continue;
        }
        if (nalType === 33) {  // SPS
            spsData = nal;
            continue;
        }
        if (nalType === 34) {  // PPS
            ppsData = nal;
            continue;
        }

        // AUD (type 35) marks the start of a new Access Unit
        if (nalType === 35) {
            if (currentAU.length > 0) {
                accessUnits.push(Buffer.concat(currentAU));
                currentAU = [];
                hasVCLInCurrentAU = false;
            }
            currentAU.push(nal);
            continue;
        }

        // Check if this VCL NAL starts a new Access Unit
        if (isH265VCLNAL(nalType)) {
            if (hasVCLInCurrentAU) {
                accessUnits.push(Buffer.concat(currentAU));
                currentAU = [];
                hasVCLInCurrentAU = false;
            }
            hasVCLInCurrentAU = true;

            // For the first IDR/IRAP frame, prepend VPS, SPS and PPS
            if (isH265IDR(nalType) && vpsData && spsData && ppsData && accessUnits.length === 0) {
                currentAU.push(vpsData, spsData, ppsData);
            }
        }

        currentAU.push(nal);
    }

    if (currentAU.length > 0) {
        accessUnits.push(Buffer.concat(currentAU));
    }

    return accessUnits;
}

// Group NAL units into Access Units (dispatch based on codec type)
function groupIntoAccessUnits(nalUnits) {
    return IS_H265 ? groupH265AccessUnits(nalUnits) : groupH264AccessUnits(nalUnits);
}

// Load test video file
let testVideoData = null;
let nalUnits = [];

try {
    testVideoData = fs.readFileSync(TEST_VIDEO_PATH);
    nalUnits = parseH264NALUnits(testVideoData);  // NAL parsing is same for H.264/H.265
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
