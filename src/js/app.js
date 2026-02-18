/**
 * 数据缓冲队列类 - WS-006 实现
 * 使用循环缓冲区管理接收到的数据包
 */
class DataBufferQueue {
    constructor(config = {}) {
        this.maxSize = config.maxSize || 100;           // 最大缓冲包数量
        this.maxBytes = config.maxBytes || 10 * 1024 * 1024;  // 最大缓冲字节数 (10MB)

        this.queue = [];
        this.currentBytes = 0;

        // 统计信息
        this.stats = {
            totalEnqueued: 0,      // 总入队数量
            totalDequeued: 0,      // 总出队数量
            totalOverflows: 0,     // 总溢出次数
            totalBytesEnqueued: 0, // 总入队字节数
            totalBytesDequeued: 0, // 总出队字节数
            totalBytesDropped: 0   // 总丢弃字节数
        };
    }

    /**
     * 入队操作
     * @param {ArrayBuffer|Object} data - 要缓冲的数据
     * @returns {boolean} - 成功返回 true，溢出返回 false
     */
    enqueue(data) {
        const dataSize = data instanceof ArrayBuffer ? data.byteLength :
                        (data.data instanceof ArrayBuffer ? data.data.byteLength : 0);

        // 检查是否会导致溢出
        if (this.queue.length >= this.maxSize ||
            this.currentBytes + dataSize > this.maxBytes) {

            // 缓冲区满，丢弃最旧的数据包
            const dropped = this.queue.shift();
            if (dropped) {
                const droppedSize = dropped.data instanceof ArrayBuffer ?
                                  dropped.data.byteLength : 0;
                this.currentBytes -= droppedSize;
                this.stats.totalOverflows++;
                this.stats.totalBytesDropped += droppedSize;
            }
        }

        // 包装数据包（添加时间戳）
        const packet = {
            data: data,
            timestamp: Date.now(),
            size: dataSize
        };

        this.queue.push(packet);
        this.currentBytes += dataSize;
        this.stats.totalEnqueued++;
        this.stats.totalBytesEnqueued += dataSize;

        return true;
    }

    /**
     * 出队操作
     * @returns {Object|null} - 数据包或 null（队列为空）
     */
    dequeue() {
        if (this.queue.length === 0) {
            return null;
        }

        const packet = this.queue.shift();
        this.currentBytes -= packet.size;
        this.stats.totalDequeued++;
        this.stats.totalBytesDequeued += packet.size;

        return packet;
    }

    /**
     * 查看队首元素（不出队）
     * @returns {Object|null}
     */
    peek() {
        return this.queue.length > 0 ? this.queue[0] : null;
    }

    /**
     * 清空队列
     */
    clear() {
        this.queue = [];
        this.currentBytes = 0;
    }

    /**
     * 获取队列长度
     * @returns {number}
     */
    size() {
        return this.queue.length;
    }

    /**
     * 检查队列是否为空
     * @returns {boolean}
     */
    isEmpty() {
        return this.queue.length === 0;
    }

    /**
     * 检查队列是否已满
     * @returns {boolean}
     */
    isFull() {
        return this.queue.length >= this.maxSize ||
               this.currentBytes >= this.maxBytes;
    }

    /**
     * 获取使用率
     * @returns {Object}
     */
    getUsage() {
        return {
            sizeUsage: (this.queue.length / this.maxSize * 100).toFixed(1),
            bytesUsage: (this.currentBytes / this.maxBytes * 100).toFixed(1)
        };
    }

    /**
     * 获取统计信息
     * @returns {Object}
     */
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

let ws = null;
let dataBuffer = null;  // 数据缓冲队列实例
let processing = false; // processQueue 并发锁
let stats = {
    bytesReceived: 0,
    messagesReceived: 0,
    connectionStartTime: null,
    lastUpdateTime: null,
    lastBytesReceived: 0
};
let statsInterval = null;

function updateStatus(state, message) {
    const statusEl = document.getElementById('status');
    statusEl.className = `status ${state}`;
    statusEl.textContent = `状态：${message}`;
}

/**
 * Data-driven consumer: drain queue as long as data is available.
 * The `processing` flag prevents concurrent invocations.
 */
async function processQueue() {
    if (processing) return;
    processing = true;
    try {
        while (dataBuffer && !dataBuffer.isEmpty()) {
            const packet = dataBuffer.dequeue();
            if (!packet) break;

            try {
                if (window.decoderBridge) {
                    let arrayBuffer = packet.data;

                    if (packet.data instanceof Blob) {
                        arrayBuffer = await packet.data.arrayBuffer();
                    }

                    if (arrayBuffer instanceof ArrayBuffer) {
                        const uint8Array = new Uint8Array(arrayBuffer);
                        await window.decoderBridge.decode(uint8Array, packet.timestamp);
                    }
                } else {
                    const dataSize = packet.data instanceof ArrayBuffer ?
                                   packet.data.byteLength :
                                   (packet.data instanceof Blob ? packet.data.size : 0);
                    const age = Date.now() - packet.timestamp;
                    console.log(`消费数据包: ${dataSize} 字节 (队列延迟: ${age}ms, 解码器未初始化)`);
                }
            } catch (error) {
                console.error(`解码错误: ${error.message}`);
            }
        }
    } finally {
        processing = false;
    }
}

function stopConsumer() {
    processing = false;
    if (dataBuffer) {
        dataBuffer.clear();
    }
    console.log('数据消费者已停止');
}

function updateStats() {
    document.getElementById('bytesReceived').textContent = stats.bytesReceived.toLocaleString();
    document.getElementById('messagesReceived').textContent = stats.messagesReceived.toLocaleString();

    if (stats.connectionStartTime) {
        const duration = Math.floor((Date.now() - stats.connectionStartTime) / 1000);
        document.getElementById('connectionTime').textContent = duration;

        // Calculate data rate
        const currentTime = Date.now();
        if (stats.lastUpdateTime) {
            const timeDiff = (currentTime - stats.lastUpdateTime) / 1000; // seconds
            const bytesDiff = stats.bytesReceived - stats.lastBytesReceived;
            const rate = (bytesDiff / timeDiff / 1024).toFixed(2); // KB/s
            document.getElementById('dataRate').textContent = rate;
        }
        stats.lastUpdateTime = currentTime;
        stats.lastBytesReceived = stats.bytesReceived;
    }

    // 更新缓冲队列统计信息
    if (dataBuffer) {
        const bufferStats = dataBuffer.getStats();

        document.getElementById('bufferSize').textContent = bufferStats.currentSize;
        document.getElementById('bufferMaxSize').textContent = bufferStats.maxSize;
        document.getElementById('bufferBytes').textContent = (bufferStats.currentBytes / 1024).toFixed(2);
        document.getElementById('bufferMaxBytes').textContent = (bufferStats.maxBytes / 1024).toFixed(0);

        // 使用更高的使用率（包数或字节数）
        const maxUsage = Math.max(
            parseFloat(bufferStats.usage.sizeUsage),
            parseFloat(bufferStats.usage.bytesUsage)
        );
        document.getElementById('bufferUsage').textContent = maxUsage.toFixed(1);

        document.getElementById('bufferEnqueued').textContent = bufferStats.totalEnqueued.toLocaleString();
        document.getElementById('bufferDequeued').textContent = bufferStats.totalDequeued.toLocaleString();
        document.getElementById('bufferOverflows').textContent = bufferStats.totalOverflows.toLocaleString();
        document.getElementById('bufferDropped').textContent = (bufferStats.totalBytesDropped / 1024).toFixed(2);

        // 如果有溢出，警告用户
        if (bufferStats.totalOverflows > 0 && bufferStats.totalOverflows % 10 === 0) {
            console.error(`缓冲区溢出警告: 已丢弃 ${bufferStats.totalOverflows} 个数据包`);
        }
    } else {
        // 重置缓冲区统计信息
        document.getElementById('bufferSize').textContent = '0';
        document.getElementById('bufferMaxSize').textContent = '0';
        document.getElementById('bufferBytes').textContent = '0';
        document.getElementById('bufferMaxBytes').textContent = '0';
        document.getElementById('bufferUsage').textContent = '0';
        document.getElementById('bufferEnqueued').textContent = '0';
        document.getElementById('bufferDequeued').textContent = '0';
        document.getElementById('bufferOverflows').textContent = '0';
        document.getElementById('bufferDropped').textContent = '0';
    }
}

async function handleSignal(msg) {
    let data;
    try {
        data = JSON.parse(msg);
    } catch (e) {
        return;
    }

    if (data.type === 'media-offer') {
        const streams = data.payload && data.payload.streams;
        const videoStream = streams ? streams.find(s => s.type === 'video') : null;
        const codecType = videoStream ? videoStream.codec : 'h264';
        try {
            await window.initDecoder(codecType);
            ws.send(JSON.stringify({ type: 'media-answer', payload: { accepted: true } }));
        } catch (e) {
            ws.send(JSON.stringify({ type: 'media-answer', payload: { accepted: false, reason: e.message } }));
        }
    } else if (data.type === 'metadata') {
        console.log(`元数据: 帧号 ${data.frameInfo?.frameNumber || 'N/A'}`);
    }
}

function connect() {
    const url = document.getElementById('wsUrl').value || 'wss://localhost:6061';

    if (ws && ws.readyState === WebSocket.OPEN) {
        console.error('已存在连接，请先断开');
        return;
    }

    updateStatus('connecting', '正在连接...');
    console.log(`正在连接到 ${url}`);

    try {
        ws = new WebSocket(url);
        ws.binaryType = 'arraybuffer';

        ws.onopen = () => {
            stats.connectionStartTime = Date.now();
            stats.lastUpdateTime = Date.now();
            stats.lastBytesReceived = 0;

            // 初始化数据缓冲队列
            dataBuffer = new DataBufferQueue({
                maxSize: 100,              // 最多缓冲100个数据包
                maxBytes: 10 * 1024 * 1024 // 最多缓冲10MB
            });
            console.log('数据缓冲队列已初始化 (最大: 100包/10MB)');

            updateStatus('connected', `已连接到 ${url}`);
            console.log('WebSocket 连接成功！');

            // Start stats update interval
            if (statsInterval) clearInterval(statsInterval);
            statsInterval = setInterval(() => {
                if (ws && ws.readyState === WebSocket.OPEN) {
                    updateStats();
                }
            }, 1000);
        };

        ws.onmessage = (event) => {
            stats.messagesReceived++;

            let dataSize = 0;
            let dataType = '';

            if (event.data instanceof ArrayBuffer) {
                dataSize = event.data.byteLength;
                dataType = 'ArrayBuffer';
                stats.bytesReceived += dataSize;

                // 将二进制数据放入缓冲队列
                if (dataBuffer) {
                    dataBuffer.enqueue(event.data);
                    processQueue();
                }

                console.log(`接收二进制数据: ${dataSize} 字节 (类型: ${dataType}) → 缓冲队列`);
            } else if (event.data instanceof Blob) {
                dataSize = event.data.size;
                dataType = 'Blob';
                stats.bytesReceived += dataSize;

                // 将 Blob 数据放入缓冲队列
                if (dataBuffer) {
                    dataBuffer.enqueue(event.data);
                    processQueue();
                }

                log(`接收二进制数据: ${dataSize} 字节 (类型: ${dataType}) → 缓冲队列`);
            } else {
                // 文本信令消息
                dataSize = new Blob([event.data]).size;
                dataType = 'Text';
                stats.bytesReceived += dataSize;

                const preview = event.data.length > 50 ? event.data.substring(0, 50) + '...' : event.data;
                console.log(`接收文本数据: ${preview}`);

                handleSignal(event.data);
            }

            updateStats();
        };

        ws.onerror = (error) => {
            console.error(`WebSocket 错误: ${error}`);
            updateStatus('disconnected', '连接错误');
        };

        ws.onclose = (event) => {
            console.error(`WebSocket 关闭: code=${event.code}, reason=${event.reason || '无'}`);
            updateStatus('disconnected', '连接已断开');
            stats.connectionStartTime = null;

            // 停止消费者
            stopConsumer();

            // 清理缓冲队列
            if (dataBuffer) {
                const bufferStats = dataBuffer.getStats();
                console.log(`清理缓冲队列: ${bufferStats.currentSize} 个数据包 (${(bufferStats.currentBytes / 1024).toFixed(2)} KB)`);
                dataBuffer.clear();
                dataBuffer = null;
            }

            if (statsInterval) {
                clearInterval(statsInterval);
                statsInterval = null;
            }
        };
    } catch (error) {
        console.error(`连接异常: ${error.message}`);
        updateStatus('disconnected', '连接失败');
    }
}

function disconnect() {
    if (ws) {
        ws.close();
        ws = null;
        console.log('主动断开连接');

        // 停止消费者
        stopConsumer();

        // 清理缓冲队列
        if (dataBuffer) {
            const bufferStats = dataBuffer.getStats();
            console.log(`清理缓冲队列: ${bufferStats.currentSize} 个数据包 (${(bufferStats.currentBytes / 1024).toFixed(2)} KB)`);
            dataBuffer.clear();
            dataBuffer = null;
        }

        if (statsInterval) {
            clearInterval(statsInterval);
            statsInterval = null;
        }
    }
}

// Initialize canvas
const canvas = document.getElementById('videoCanvas');
const ctx = canvas.getContext('2d');
ctx.fillStyle = '#000';
ctx.fillRect(0, 0, canvas.width, canvas.height);
ctx.fillStyle = '#fff';
ctx.font = '20px Arial';
ctx.textAlign = 'center';
ctx.fillText('等待连接...', canvas.width / 2, canvas.height / 2);

// Initial log
console.log('页面加载完成，准备就绪');

