# 产品需求文档 (PRD)

## WebSocket 音视频流媒体播放器

**版本：** 1.0  
**日期：** 2025年12月29日  
**状态：** 草案

---

## 1. 概述

### 1.1 产品背景

随着实时音视频通信需求的增长，需要开发一款基于浏览器的流媒体播放器，能够接收来自媒体服务器的实时音视频数据，并在 Web 端实现低延迟、高性能的播放体验。

### 1.2 产品定义

本产品是一个纯 Web 端的音视频播放器应用，通过 WebSocket 协议从媒体服务器接收编码后的音视频数据，使用 FFmpeg（WebAssembly 版本）进行解码，并通过 WebGL 技术实现高性能渲染。

### 1.3 目标用户

- 需要在浏览器中播放实时音视频流的开发者
- 视频监控、在线直播、远程会议等场景的最终用户

---

## 2. 技术架构

### 2.1 系统架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         Web 应用程序                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │   WebSocket  │───▶│    FFmpeg    │───▶│    WebGL     │      │
│  │    客户端    │    │   (WASM)     │    │    渲染器    │      │
│  │              │    │    解码器    │    │              │      │
│  └──────────────┘    └──────────────┘    └──────────────┘      │
│         ▲                   │                   │               │
│         │                   ▼                   ▼               │
│         │            ┌──────────────┐    ┌──────────────┐      │
│         │            │  Web Audio   │    │    Canvas    │      │
│         │            │     API      │    │    元素      │      │
│         │            └──────────────┘    └──────────────┘      │
│         │                                                       │
└─────────│───────────────────────────────────────────────────────┘
          │
          │ WebSocket (ws/wss)
          ▼
┌──────────────────┐
│    媒体服务器    │
│  (编码音视频流)  │
└──────────────────┘
```

### 2.2 数据流程

```
媒体服务器 ──▶ WebSocket传输 ──▶ 接收缓冲 ──▶ FFmpeg解码 ──▶ YUV/RGB数据 ──▶ WebGL渲染 ──▶ 屏幕显示
                                                    │
                                                    ▼
                                              PCM音频数据 ──▶ Web Audio播放
```

---

## 3. 功能需求

### 3.1 WebSocket 数据接收模块

#### 3.1.1 功能描述

建立与媒体服务器的 WebSocket 连接，接收实时音视频编码数据。

#### 3.1.2 详细需求

| 需求编号 | 需求描述 | 优先级 |
|---------|---------|-------|
| WS-001 | 支持 ws:// 和 wss:// 协议连接 | P0 |
| WS-002 | 支持自定义服务器地址和端口配置 | P0 |
| WS-003 | 支持二进制数据（ArrayBuffer/Blob）接收 | P0 |
| WS-004 | 实现自动重连机制，断线后自动尝试重新连接 | P1 |
| WS-005 | 支持连接状态监控（连接中/已连接/断开/错误） | P1 |
| WS-006 | 实现数据接收缓冲队列，防止数据丢失 | P0 |
| WS-007 | 支持心跳检测机制，保持连接活跃 | P2 |
| WS-008 | 提供连接统计信息（接收字节数、丢包率等） | P2 |

#### 3.1.3 接口设计

```typescript
interface WebSocketConfig {
  url: string;                    // WebSocket 服务器地址
  protocols?: string[];           // 子协议列表
  reconnectInterval?: number;     // 重连间隔（毫秒）
  maxReconnectAttempts?: number;  // 最大重连次数
  heartbeatInterval?: number;     // 心跳间隔（毫秒）
}

interface WebSocketClient {
  connect(): Promise<void>;
  disconnect(): void;
  onData(callback: (data: ArrayBuffer) => void): void;
  onStateChange(callback: (state: ConnectionState) => void): void;
  getStats(): ConnectionStats;
}

type ConnectionState = 'connecting' | 'connected' | 'disconnected' | 'error';

interface ConnectionStats {
  bytesReceived: number;
  packetsReceived: number;
  connectionTime: number;
  latency: number;
}
```

---

### 3.2 FFmpeg 解码模块

#### 3.2.1 功能描述

使用 FFmpeg WebAssembly 版本对接收到的编码数据进行解码，输出原始音视频帧数据。

#### 3.2.2 详细需求

| 需求编号 | 需求描述 | 优先级 |
|---------|---------|-------|
| DEC-001 | 支持 H.264/AVC 视频编码格式解码 | P0 |
| DEC-002 | 支持 H.265/HEVC 视频编码格式解码 | P1 |
| DEC-003 | 支持 AAC 音频编码格式解码 | P0 |
| DEC-004 | 支持 Opus 音频编码格式解码 | P1 |
| DEC-005 | 解码后视频输出 YUV420P 格式 | P0 |
| DEC-006 | 解码后音频输出 PCM 格式（Float32） | P0 |
| DEC-007 | 支持在 Web Worker 中运行，避免阻塞主线程 | P0 |
| DEC-008 | 实现解码缓冲管理，平衡延迟与流畅度 | P1 |
| DEC-009 | 支持硬件加速解码（WebCodecs API 可用时） | P2 |
| DEC-010 | 提供解码性能统计（帧率、解码耗时等） | P2 |

#### 3.2.3 接口设计

```typescript
interface DecoderConfig {
  videoCodec: 'h264' | 'h265';
  audioCodec: 'aac' | 'opus';
  useWorker?: boolean;
  bufferSize?: number;
  enableHardwareAcceleration?: boolean;
}

interface VideoFrame {
  width: number;
  height: number;
  timestamp: number;
  data: {
    y: Uint8Array;    // Y 平面
    u: Uint8Array;    // U 平面
    v: Uint8Array;    // V 平面
  };
  stride: {
    y: number;
    u: number;
    v: number;
  };
}

interface AudioFrame {
  sampleRate: number;
  channels: number;
  timestamp: number;
  data: Float32Array[];  // 每个通道的 PCM 数据
}

interface Decoder {
  init(config: DecoderConfig): Promise<void>;
  decode(data: ArrayBuffer): Promise<void>;
  onVideoFrame(callback: (frame: VideoFrame) => void): void;
  onAudioFrame(callback: (frame: AudioFrame) => void): void;
  flush(): Promise<void>;
  destroy(): void;
  getStats(): DecoderStats;
}

interface DecoderStats {
  decodedVideoFrames: number;
  decodedAudioFrames: number;
  averageDecodeTime: number;
  currentFps: number;
}
```

---

### 3.3 WebGL 渲染模块

#### 3.3.1 功能描述

使用 WebGL 技术将解码后的 YUV 视频帧数据高效渲染到 Canvas 画布上。

#### 3.3.2 详细需求

| 需求编号 | 需求描述 | 优先级 |
|---------|---------|-------|
| GL-001 | 支持 YUV420P 到 RGB 的 GPU 加速色彩空间转换 | P0 |
| GL-002 | 支持自适应分辨率渲染 | P0 |
| GL-003 | 支持全屏显示模式 | P1 |
| GL-004 | 支持画面比例保持（16:9, 4:3 等） | P1 |
| GL-005 | 支持画面缩放（适应/填充/拉伸） | P1 |
| GL-006 | 实现双缓冲机制，避免画面撕裂 | P0 |
| GL-007 | 支持基础图像处理（亮度/对比度/饱和度调节） | P2 |
| GL-008 | 优雅降级到 Canvas 2D 渲染（WebGL 不可用时） | P2 |
| GL-009 | 提供渲染性能统计（帧率、GPU 使用率等） | P2 |
| GL-010 | 支持截图功能 | P2 |

#### 3.3.3 接口设计

```typescript
interface RendererConfig {
  canvas: HTMLCanvasElement;
  preserveAspectRatio?: boolean;
  scaleMode?: 'fit' | 'fill' | 'stretch';
  enableImageProcessing?: boolean;
}

interface ImageProcessingOptions {
  brightness?: number;   // -1.0 ~ 1.0
  contrast?: number;     // 0.0 ~ 2.0
  saturation?: number;   // 0.0 ~ 2.0
}

interface Renderer {
  init(config: RendererConfig): Promise<void>;
  render(frame: VideoFrame): void;
  setScaleMode(mode: 'fit' | 'fill' | 'stretch'): void;
  setImageProcessing(options: ImageProcessingOptions): void;
  enterFullscreen(): Promise<void>;
  exitFullscreen(): Promise<void>;
  captureFrame(): Promise<Blob>;
  destroy(): void;
  getStats(): RendererStats;
}

interface RendererStats {
  renderedFrames: number;
  droppedFrames: number;
  currentFps: number;
  renderTime: number;
}
```

#### 3.3.4 Shader 设计

**顶点着色器：**
```glsl
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;

void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
  v_texCoord = a_texCoord;
}
```

**片段着色器（YUV to RGB）：**
```glsl
precision mediump float;
varying vec2 v_texCoord;
uniform sampler2D u_textureY;
uniform sampler2D u_textureU;
uniform sampler2D u_textureV;

void main() {
  float y = texture2D(u_textureY, v_texCoord).r;
  float u = texture2D(u_textureU, v_texCoord).r - 0.5;
  float v = texture2D(u_textureV, v_texCoord).r - 0.5;
  
  float r = y + 1.402 * v;
  float g = y - 0.344 * u - 0.714 * v;
  float b = y + 1.772 * u;
  
  gl_FragColor = vec4(r, g, b, 1.0);
}
```

---

### 3.4 音频播放模块

#### 3.4.1 功能描述

使用 Web Audio API 播放解码后的 PCM 音频数据，并与视频保持同步。

#### 3.4.2 详细需求

| 需求编号 | 需求描述 | 优先级 |
|---------|---------|-------|
| AUD-001 | 支持 PCM 音频数据播放 | P0 |
| AUD-002 | 支持音量控制（0-100%） | P1 |
| AUD-003 | 支持静音/取消静音 | P1 |
| AUD-004 | 实现音视频同步机制 | P0 |
| AUD-005 | 支持音频缓冲管理 | P1 |
| AUD-006 | 处理浏览器自动播放策略限制 | P0 |

---

### 3.5 播放控制模块

#### 3.5.1 功能描述

提供播放器的基础控制功能和用户界面交互。

#### 3.5.2 详细需求

| 需求编号 | 需求描述 | 优先级 |
|---------|---------|-------|
| CTL-001 | 支持播放/暂停控制 | P0 |
| CTL-002 | 显示当前播放状态 | P1 |
| CTL-003 | 显示网络状态指示 | P1 |
| CTL-004 | 显示视频分辨率和帧率信息 | P2 |
| CTL-005 | 显示延迟统计信息 | P2 |
| CTL-006 | 支持键盘快捷键操作 | P2 |

---

## 4. 非功能需求

### 4.1 性能需求

| 需求编号 | 需求描述 | 指标 |
|---------|---------|------|
| PERF-001 | 视频解码帧率 | ≥ 30fps（1080p） |
| PERF-002 | 端到端延迟 | ≤ 500ms（理想状态） |
| PERF-003 | CPU 占用率 | ≤ 30%（主流设备） |
| PERF-004 | 内存占用 | ≤ 200MB |
| PERF-005 | 首帧显示时间 | ≤ 1s |

### 4.2 兼容性需求

| 需求编号 | 需求描述 |
|---------|---------|
| COMP-001 | Chrome 80+ |
| COMP-002 | Firefox 75+ |
| COMP-003 | Safari 14+ |
| COMP-004 | Edge 80+ |
| COMP-005 | 支持移动端浏览器 |

### 4.3 可靠性需求

| 需求编号 | 需求描述 |
|---------|---------|
| REL-001 | 网络断开后自动重连，恢复播放 |
| REL-002 | 解码错误时自动恢复，不崩溃 |
| REL-003 | 内存泄漏检测和防护 |
| REL-004 | 长时间运行稳定（≥24小时） |

---

## 5. 技术选型建议

### 5.1 核心依赖

| 组件 | 推荐方案 | 说明 |
|-----|---------|-----|
| FFmpeg WASM | ffmpeg.wasm | 成熟的 FFmpeg WebAssembly 移植版 |
| WebGL 封装 | 原生 WebGL 或 PixiJS | 根据复杂度选择 |
| 构建工具 | Vite / Webpack | 支持 WASM 加载 |
| TypeScript | 5.0+ | 类型安全 |

### 5.2 可选增强

| 组件 | 方案 | 说明 |
|-----|-----|-----|
| WebCodecs | 原生 API | Chrome 94+ 硬件加速解码 |
| SharedArrayBuffer | - | 多线程数据共享 |
| OffscreenCanvas | - | Worker 中渲染 |

---

## 6. 风险与约束

### 6.1 技术风险

| 风险 | 影响 | 缓解措施 |
|-----|-----|---------|
| FFmpeg WASM 体积较大（~25MB） | 首次加载慢 | 使用精简版本，CDN 加速 |
| 浏览器自动播放限制 | 音频无法自动播放 | 用户交互触发，提示引导 |
| 移动端性能受限 | 解码卡顿 | 降低分辨率，优化缓冲策略 |
| Safari WebGL 兼容性 | 渲染异常 | 降级方案，兼容性测试 |

### 6.2 约束条件

- 受限于浏览器安全策略，部分功能需要 HTTPS 环境
- SharedArrayBuffer 需要特定的 HTTP 响应头配置
- 移动端浏览器后台运行时可能被暂停

---

## 7. 里程碑规划

| 阶段 | 内容 | 周期 |
|-----|-----|-----|
| M1 | WebSocket 连接 + 基础架构搭建 | 1 周 |
| M2 | FFmpeg 解码集成 | 2 周 |
| M3 | WebGL 渲染实现 | 1 周 |
| M4 | 音视频同步 + 播放控制 | 1 周 |
| M5 | 性能优化 + 兼容性测试 | 1 周 |
| M6 | 文档编写 + 发布 | 1 周 |

**预计总工期：7 周**

---

## 8. 附录

### 8.1 术语表

| 术语 | 说明 |
|-----|-----|
| YUV420P | 一种视频色彩编码格式，Y 为亮度，UV 为色度 |
| PCM | 脉冲编码调制，未压缩的数字音频格式 |
| WASM | WebAssembly，浏览器中的二进制执行格式 |
| WebGL | Web 图形库，基于 OpenGL ES 的 JavaScript API |

### 8.2 参考资料

- [FFmpeg.wasm 官方文档](https://ffmpegwasm.netlify.app/)
- [WebGL 基础教程](https://webglfundamentals.org/)
- [Web Audio API 规范](https://www.w3.org/TR/webaudio/)
- [WebSocket API 规范](https://websockets.spec.whatwg.org/)

---

**文档修订记录**

| 版本 | 日期 | 修订内容 | 修订人 |
|-----|-----|---------|-------|
| 1.0 | 2025-12-29 | 初始版本 | - |
