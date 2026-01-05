const WebSocket = require('ws');
const fs = require('fs');
const path = require('path');

// Configuration
const PORT = 8080;
const SEND_INTERVAL = 1000; // Send data every 1 second
const CHUNK_SIZE = 1024 * 10; // 10KB per chunk

// H.264 test video configuration
const USE_TEST_VIDEO = true;  // Enable H.264 test video
const TEST_VIDEO_PATH = './tests/fixtures/test_video.h264';
const NAL_SEND_INTERVAL = 33;  // ~30fps (milliseconds)

// Parse H.264 NAL units from buffer
function parseH264NALUnits(buffer) {
    const nalUnits = [];
    let start = 0;

    // Find NAL start codes: 0x00 0x00 0x00 0x01 or 0x00 0x00 0x01
    for (let i = 0; i < buffer.length - 3; i++) {
        const is4ByteStart = buffer[i] === 0 && buffer[i+1] === 0 &&
                             buffer[i+2] === 0 && buffer[i+3] === 1;
        const is3ByteStart = buffer[i] === 0 && buffer[i+1] === 0 &&
                             buffer[i+2] === 1;

        if (is4ByteStart || is3ByteStart) {
            if (start > 0) {
                nalUnits.push(buffer.slice(start, i));
            }
            start = i;
        }
    }

    // Add the last NAL unit
    if (start > 0 && start < buffer.length) {
        nalUnits.push(buffer.slice(start));
    }

    return nalUnits;
}

// Load H.264 test video file
let testVideoData = null;
let nalUnits = [];

if (USE_TEST_VIDEO) {
    try {
        testVideoData = fs.readFileSync(TEST_VIDEO_PATH);
        nalUnits = parseH264NALUnits(testVideoData);
        console.log(`✅ 已加载测试视频: ${TEST_VIDEO_PATH}`);
        console.log(`📦 NAL 单元数量: ${nalUnits.length}`);
        console.log(`📊 文件大小: ${(testVideoData.length / 1024).toFixed(2)} KB`);
    } catch (error) {
        console.error(`❌ 无法加载测试视频文件: ${error.message}`);
        console.log('⚠️  将回退到发送随机数据');
    }
}

// Create WebSocket server
const wss = new WebSocket.Server({ port: PORT });

console.log('='.repeat(60));
console.log('🚀 WebSocket 测试服务器启动成功');
console.log('='.repeat(60));
console.log(`📡 监听端口: ${PORT}`);
console.log(`🔗 连接地址: ws://localhost:${PORT}`);
console.log(`📊 数据发送间隔: ${SEND_INTERVAL}ms`);
console.log(`📦 每次数据块大小: ${CHUNK_SIZE} bytes`);
console.log('='.repeat(60));
console.log('');

// Track connections
let connectionCount = 0;
const connections = new Map();

// Generate mock binary data
function generateMockData(size) {
    const buffer = new ArrayBuffer(size);
    const view = new Uint8Array(buffer);

    // Fill with random data to simulate encoded video/audio data
    for (let i = 0; i < size; i++) {
        view[i] = Math.floor(Math.random() * 256);
    }

    return buffer;
}

// Generate mock video frame header (simplified)
function generateVideoFrameHeader() {
    return {
        type: 'video',
        codec: 'h264',
        timestamp: Date.now(),
        frameNumber: Math.floor(Math.random() * 1000),
        size: CHUNK_SIZE
    };
}

// Handle new connections
wss.on('connection', (ws, req) => {
    const clientId = ++connectionCount;
    const clientIp = req.socket.remoteAddress;

    console.log(`✅ [连接 #${clientId}] 新客户端已连接`);
    console.log(`   IP地址: ${clientIp}`);
    console.log(`   当前连接数: ${wss.clients.size}`);
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

    // Send welcome message
    ws.send(JSON.stringify({
        type: 'welcome',
        message: '欢迎连接到WebSocket流媒体测试服务器',
        serverId: 'test-server-001',
        timestamp: Date.now()
    }));

    // Start sending data (H.264 NAL units or mock data)
    const dataInterval = setInterval(() => {
        if (ws.readyState === WebSocket.OPEN) {
            let dataToSend;
            let dataSize;

            if (USE_TEST_VIDEO && nalUnits.length > 0) {
                // Send H.264 NAL units (loop playback)
                const nalIndex = connectionInfo.messagesSent % nalUnits.length;
                dataToSend = nalUnits[nalIndex];
                dataSize = dataToSend.length;

                // Log every 30 frames (1 second at 30fps)
                if (nalIndex % 30 === 0) {
                    console.log(`📤 [连接 #${clientId}] 发送 NAL 单元 ${nalIndex}/${nalUnits.length} (${dataSize} bytes)`);
                }
            } else {
                // Fallback to random data
                dataToSend = generateMockData(CHUNK_SIZE);
                dataSize = CHUNK_SIZE;

                if (connectionInfo.messagesSent % 5 === 0) {
                    const mbSent = (connectionInfo.bytesSent / 1024 / 1024).toFixed(2);
                    console.log(`📤 [连接 #${clientId}] 已发送 ${connectionInfo.messagesSent} 条消息，共 ${mbSent} MB`);
                }
            }

            try {
                ws.send(dataToSend);
                connectionInfo.messagesSent++;
                connectionInfo.bytesSent += dataSize;

                // Occasionally send metadata as text
                if (connectionInfo.messagesSent % 10 === 0) {
                    const frameHeader = USE_TEST_VIDEO ?
                        { type: 'video', codec: 'h264', timestamp: Date.now(), frameNumber: connectionInfo.messagesSent, size: dataSize } :
                        generateVideoFrameHeader();

                    ws.send(JSON.stringify({
                        type: 'metadata',
                        frameInfo: frameHeader,
                        stats: {
                            totalMessagesSent: connectionInfo.messagesSent,
                            totalBytesSent: connectionInfo.bytesSent,
                            uptime: Math.floor((Date.now() - connectionInfo.connectedAt.getTime()) / 1000)
                        }
                    }));
                }
            } catch (error) {
                console.error(`❌ [连接 #${clientId}] 发送数据失败:`, error.message);
            }
        }
    }, USE_TEST_VIDEO ? NAL_SEND_INTERVAL : SEND_INTERVAL);

    // Handle incoming messages
    ws.on('message', (message) => {
        try {
            const data = message.toString();
            console.log(`📨 [连接 #${clientId}] 收到消息: ${data}`);

            // Echo back with confirmation
            ws.send(JSON.stringify({
                type: 'echo',
                originalMessage: data,
                receivedAt: Date.now()
            }));
        } catch (error) {
            console.error(`❌ [连接 #${clientId}] 处理消息失败:`, error.message);
        }
    });

    // Handle errors
    ws.on('error', (error) => {
        console.error(`❌ [连接 #${clientId}] WebSocket错误:`, error.message);
    });

    // Handle disconnection
    ws.on('close', (code, reason) => {
        clearInterval(dataInterval);

        const info = connections.get(ws);
        if (info) {
            const duration = Math.floor((Date.now() - info.connectedAt.getTime()) / 1000);
            const mbSent = (info.bytesSent / 1024 / 1024).toFixed(2);

            console.log('');
            console.log(`👋 [连接 #${clientId}] 客户端已断开`);
            console.log(`   断开代码: ${code}`);
            console.log(`   断开原因: ${reason || '无'}`);
            console.log(`   连接时长: ${duration} 秒`);
            console.log(`   发送消息数: ${info.messagesSent} 条`);
            console.log(`   发送数据量: ${mbSent} MB`);
            console.log(`   剩余连接数: ${wss.clients.size}`);
            console.log('');

            connections.delete(ws);
        }
    });

    // Send heartbeat every 30 seconds
    const heartbeatInterval = setInterval(() => {
        if (ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({
                type: 'heartbeat',
                timestamp: Date.now()
            }));
        } else {
            clearInterval(heartbeatInterval);
        }
    }, 30000);
});

// Handle server errors
wss.on('error', (error) => {
    console.error('❌ WebSocket服务器错误:', error);
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('');
    console.log('📴 正在关闭服务器...');

    wss.clients.forEach((ws) => {
        ws.close(1000, '服务器正在关闭');
    });

    wss.close(() => {
        console.log('✅ 服务器已关闭');
        process.exit(0);
    });
});

// Log server stats every 60 seconds
setInterval(() => {
    if (wss.clients.size > 0) {
        console.log('');
        console.log('📊 服务器状态:');
        console.log(`   活动连接数: ${wss.clients.size}`);
        console.log(`   总连接次数: ${connectionCount}`);

        let totalBytesSent = 0;
        let totalMessagesSent = 0;
        connections.forEach((info) => {
            totalBytesSent += info.bytesSent;
            totalMessagesSent += info.messagesSent;
        });

        if (totalBytesSent > 0) {
            const mbSent = (totalBytesSent / 1024 / 1024).toFixed(2);
            console.log(`   累计发送: ${totalMessagesSent} 条消息，${mbSent} MB`);
        }
        console.log('');
    }
}, 60000);
