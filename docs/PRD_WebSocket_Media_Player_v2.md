# 产品需求文档 (PRD)

## WebSocket 音视频流媒体播放器

**版本：** 2.0  
**日期：** 2025年1月3日  
**状态：** 草案

---

## 1. 概述

### 1.1 产品背景

随着实时音视频通信需求的增长，需要开发一款基于浏览器的流媒体播放器，能够接收来自媒体服务器的实时音视频数据，并在 Web 端实现低延迟、高性能的播放体验。

### 1.2 产品定义

本产品是一个纯 Web 端的音视频播放器应用，通过 WebSocket 协议从媒体服务器接收编码后的音视频数据，使用基于 FFmpeg 官方源码构建的自定义 WASM 解码器进行解码，并通过 WebGL 技术实现高性能渲染。

### 1.3 目标用户

- 需要在浏览器中播放实时音视频流的开发者
- 视频监控、在线直播、远程会议等场景的最终用户

### 1.4 版本变更说明

| 版本 | 主要变更 |
|-----|---------|
| v1.0 → v2.0 | 解码方案由 ffmpeg.wasm 第三方库改为基于 FFmpeg 官方源码的库模式原生构建，自行实现 C 接口（Glue Code）并通过 Emscripten 编译为 WASM |

---

## 2. 技术架构

### 2.1 系统架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              Web 应用程序                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌──────────────┐    ┌────────────────────────────┐    ┌──────────────┐│
│  │   WebSocket  │───▶│   FFmpeg WASM 解码模块     │───▶│    WebGL     ││
│  │    客户端    │    │  ┌──────────────────────┐  │    │    渲染器    ││
│  │              │    │  │   Glue Code (C)      │  │    │              ││
│  └──────────────┘    │  │   ┌──────────────┐   │  │    └──────────────┘│
│         ▲            │  │   │ libavcodec   │   │  │          │         │
│         │            │  │   │ libavformat  │   │  │          ▼         │
│         │            │  │   │ libavutil    │   │  │    ┌──────────────┐│
│         │            │  │   │ libswscale   │   │  │    │    Canvas    ││
│         │            │  │   │ libswresample│   │  │    │    元素      ││
│         │            │  │   └──────────────┘   │  │    └──────────────┘│
│         │            │  └──────────────────────┘  │                     │
│         │            │         │                  │                     │
│         │            └─────────│──────────────────┘                     │
│         │                      ▼                                        │
│         │               ┌──────────────┐                                │
│         │               │  Web Audio   │                                │
│         │               │     API      │                                │
│         │               └──────────────┘                                │
│         │                                                               │
└─────────│───────────────────────────────────────────────────────────────┘
          │
          │ WebSocket (ws/wss)
          ▼
┌──────────────────┐
│    媒体服务器    │
│  (编码音视频流)  │
└──────────────────┘
```

### 2.2 WASM 解码模块架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    WASM 解码模块详细架构                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   JavaScript 层                                                 │
│  ┌────────────────────────────────────────────────────────┐    │
│  │  decoder.js (ES Module)                                │    │
│  │  ┌─────────────────┐  ┌─────────────────────────────┐  │    │
│  │  │ DecoderWrapper  │  │  Emscripten Module Loader   │  │    │
│  │  │ (TypeScript)    │◀▶│  (decoder.wasm + glue.js)   │  │    │
│  │  └─────────────────┘  └─────────────────────────────┘  │    │
│  └────────────────────────────────────────────────────────┘    │
│                              │                                  │
│                              ▼ WASM Function Calls              │
│   WASM 层                                                       │
│  ┌────────────────────────────────────────────────────────┐    │
│  │  Glue Code (decoder_glue.c)                            │    │
│  │  ┌─────────────────────────────────────────────────┐   │    │
│  │  │ 导出函数:                                        │   │    │
│  │  │  • decoder_init(codec_type)                     │   │    │
│  │  │  • decoder_decode(data_ptr, data_len)           │   │    │
│  │  │  • decoder_get_video_frame()                    │   │    │
│  │  │  • decoder_get_audio_frame()                    │   │    │
│  │  │  • decoder_flush()                              │   │    │
│  │  │  • decoder_destroy()                            │   │    │
│  │  └─────────────────────────────────────────────────┘   │    │
│  │                         │                              │    │
│  │                         ▼                              │    │
│  │  ┌─────────────────────────────────────────────────┐   │    │
│  │  │ FFmpeg 库 (静态链接)                             │   │    │
│  │  │  • libavcodec.a    (解码器核心)                  │   │    │
│  │  │  • libavformat.a   (容器格式处理)                │   │    │
│  │  │  • libavutil.a     (工具函数)                    │   │    │
│  │  │  • libswscale.a    (图像缩放/格式转换)           │   │    │
│  │  │  • libswresample.a (音频重采样)                  │   │    │
│  │  └─────────────────────────────────────────────────┘   │    │
│  └────────────────────────────────────────────────────────┘    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 2.3 数据流程

```
媒体服务器 ──▶ WebSocket传输 ──▶ 接收缓冲 ──▶ WASM解码模块 ──▶ YUV/RGB数据 ──▶ WebGL渲染 ──▶ 屏幕显示
                                                    │
                                                    ▼
                                              PCM音频数据 ──▶ Web Audio播放
```

### 2.4 构建工具链

```
┌─────────────────────────────────────────────────────────────────┐
│                        构建流程                                  │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. FFmpeg 源码编译                                              │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ ./configure --enable-cross-compile                      │    │
│  │             --target-os=none                            │    │
│  │             --arch=wasm32                               │    │
│  │             --cc=emcc                                   │    │
│  │             --enable-static                             │    │
│  │             --disable-shared                            │    │
│  │             --disable-programs                          │    │
│  │             --disable-doc                               │    │
│  │             --disable-network                           │    │
│  │             --enable-decoder=h264,hevc,aac,opus         │    │
│  │             ...                                         │    │
│  └─────────────────────────────────────────────────────────┘    │
│                              │                                  │
│                              ▼                                  │
│  2. Glue Code 编译                                              │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ emcc decoder_wasm.c                                     │    │
│  │      -I${FFMPEG_DIR}/include                            │    │
│  │      -L${FFMPEG_DIR}/lib                                │    │
│  │      -lavcodec -lavformat -lavutil -lswscale -lswresample│   │
│  │      -s WASM=1                                          │    │
│  │      -s MODULARIZE=1                                    │    │
│  │      -s EXPORT_ES6=1                                    │    │
│  │      -s EXPORTED_FUNCTIONS=[...]                        │    │
│  │      -s ALLOW_MEMORY_GROWTH=1                           │    │
│  │      -O3                                                │    │
│  │      -o decoder.js                                      │    │
│  └─────────────────────────────────────────────────────────┘    │
│                              │                                  │
│                              ▼                                  │
│  3. 输出产物                                                     │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ • decoder.js      (Emscripten 生成的 JS Glue)           │    │
│  │ • decoder.wasm    (WASM 二进制模块)                     │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
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

### 3.2 FFmpeg WASM 解码模块（原生构建）

#### 3.2.1 功能描述

基于 FFmpeg 官方源码，采用"库模式"（Library Mode）进行原生构建，自行编写 C 语言 Glue Code 封装解码接口，使用 Emscripten 工具链编译为 WebAssembly 模块。解码后输出原始音视频帧数据供渲染和播放。

#### 3.2.2 构建方案说明

| 项目 | 说明 |
|-----|-----|
| FFmpeg 版本 | 7.1.2 LTS 或更高稳定版本 |
| 编译工具链 | Emscripten (emcc) 3.1.50+ |
| 构建模式 | 库模式（静态链接 .a 文件） |
| 目标格式 | WASM (wasm32-unknown-emscripten) |
| Glue Code | 自行实现 C 接口层 |

#### 3.2.3 FFmpeg 库依赖

| 库名称 | 用途 | 是否必需 |
|-------|-----|---------|
| libavcodec | 音视频编解码核心 | 是 |
| libavformat | 容器格式解析（可选，用于容器解封装） | 可选 |
| libavutil | 基础工具函数、内存管理 | 是 |
| libswscale | 图像缩放、像素格式转换 | 是 |
| libswresample | 音频重采样、格式转换 | 是 |

#### 3.2.4 详细需求

| 需求编号 | 需求描述 | 优先级 |
|---------|---------|-------|
| DEC-001 | 支持 H.264/AVC 视频编码格式解码 | P0 |
| DEC-002 | 支持 H.265/HEVC 视频编码格式解码 | P1 |
| DEC-003 | 支持 AAC 音频编码格式解码 | P0 |
| DEC-004 | 解码后视频输出 YUV420P 格式 | P0 |
| DEC-006 | 解码后音频输出 PCM 格式（Float32） | P0 |
| DEC-007 | 支持在 Web Worker 中运行，避免阻塞主线程 | P0 |
| DEC-008 | 实现解码缓冲管理，平衡延迟与流畅度 | P1 |
| DEC-009 | 支持硬件加速解码（WebCodecs API 可用时） | P2 |
| DEC-010 | 提供解码性能统计（帧率、解码耗时等） | P2 |
| DEC-011 | Glue Code 需实现内存管理，避免内存泄漏 | P0 |
| DEC-012 | WASM 模块支持 ES Module 导出格式 | P0 |
| DEC-013 | 支持动态内存增长（ALLOW_MEMORY_GROWTH） | P0 |
| DEC-014 | 构建产物体积优化，仅包含必要解码器 | P1 |

#### 3.2.5 Glue Code 接口设计（C 层）

#### 3.2.6 Emscripten 编译配置

#### 3.2.7 FFmpeg 编译配置脚本

#### 3.2.8 JavaScript 封装层接口设计

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

#### 3.3.4 Shader 设计

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
| PERF-004 | 内存占用 | ≤ 150MB（不含 WASM 模块） |
| PERF-005 | 首帧显示时间 | ≤ 1s |
| PERF-006 | WASM 模块加载时间 | ≤ 2s（缓存后 ≤ 500ms） |
| PERF-007 | WASM 模块体积 | ≤ 5MB（gzip 压缩后） |

### 4.2 兼容性需求

| 需求编号 | 需求描述 |
|---------|---------|
| COMP-001 | Chrome 80+ |
| COMP-002 | Firefox 75+ |
| COMP-003 | Safari 14+ |
| COMP-004 | Edge 80+ |
| COMP-005 | 支持移动端浏览器 |
| COMP-006 | 支持 WebAssembly SIMD（可选，提升性能） |

### 4.3 可靠性需求

| 需求编号 | 需求描述 |
|---------|---------|
| REL-001 | 网络断开后自动重连，恢复播放 |
| REL-002 | 解码错误时自动恢复，不崩溃 |
| REL-003 | WASM 内存泄漏检测和防护 |
| REL-004 | 长时间运行稳定（≥24小时） |
| REL-005 | Glue Code 异常处理完善，防止 WASM 崩溃 |

---

## 5. 技术选型

### 5.1 核心依赖

| 组件 | 方案 | 版本 | 说明 |
|-----|-----|-----|-----|
| FFmpeg | 官方源码 | 6.1 LTS+ | 库模式静态编译 |
| Emscripten | emcc/em++ | 3.1.50+ | WASM 编译工具链 |
| Glue Code | 自研 C 代码 | - | 封装 FFmpeg API |
| WebGL 封装 | 原生 WebGL | - | YUV 渲染 |
| 构建工具 | Vite / Webpack | - | 支持 WASM 加载 |
| TypeScript | 5.0+ | - | 类型安全 |

### 5.2 FFmpeg 编译选项

| 选项 | 说明 |
|-----|-----|
| --enable-static | 静态链接库 |
| --disable-shared | 禁用动态库 |
| --disable-programs | 不编译 ffmpeg/ffprobe 等程序 |
| --disable-doc | 不生成文档 |
| --disable-network | 禁用网络功能 |
| --enable-decoder=h264,hevc,aac,opus | 仅启用必要解码器 |

### 5.3 Emscripten 编译选项

| 选项 | 说明 |
|-----|-----|
| -s WASM=1 | 输出 WASM 格式 |
| -s MODULARIZE=1 | 模块化输出 |
| -s EXPORT_ES6=1 | ES6 模块格式 |
| -s ALLOW_MEMORY_GROWTH=1 | 允许动态内存增长 |
| -s FILESYSTEM=0 | 禁用文件系统（减小体积） |
| -O3 | 最高级别优化 |

### 5.4 可选增强

| 组件 | 方案 | 说明 |
|-----|-----|-----|
| WebCodecs | 原生 API | Chrome 94+ 硬件加速解码 |
| SharedArrayBuffer | - | 多线程数据共享 |
| OffscreenCanvas | - | Worker 中渲染 |
| SIMD | WASM SIMD | 加速解码运算 |

---

## 6. 项目结构

```
project/
├── build/                         # 构建脚本
│   ├── build_ffmpeg.sh            # FFmpeg 编译脚本
│   ├── build_wasm.sh              # WASM 模块编译脚本
│   └── Makefile                   # 主构建文件
├── src/
│   ├── wasm/                      # WASM 模块源码
│   │   ├── decoder_wasm.c         # Glue Code 实现
│   │   ├── decoder_wasm.h         # Glue Code 头文件
│   │   └── memory_utils.c         # 内存管理工具
│   ├── js/                        # JavaScript/TypeScript 源码
│   │   ├── decoder.ts             # WASM 解码器封装
│   │   ├── websocket.ts           # WebSocket 客户端
│   │   ├── renderer.ts            # WebGL 渲染器
│   │   ├── audio.ts               # 音频播放器
│   │   ├── player.ts              # 播放器主控制
│   │   └── types.ts               # 类型定义
│   └── worker/                    # Web Worker
│       └── decode-worker.ts       # 解码 Worker
├── dist/                          # 构建输出
│   ├── decoder.js                 # Emscripten 生成的 JS
│   ├── decoder.wasm               # WASM 二进制
│   └── player.js                  # 播放器 bundle
├── ffmpeg-build/                  # FFmpeg 编译输出
│   ├── include/                   # 头文件
│   └── lib/                       # 静态库 (.a)
├── examples/                      # 示例代码
├── docs/                          # 文档
└── tests/                         # 测试用例
```

---

## 7. 风险与约束

### 7.1 技术风险

| 风险 | 影响 | 可能性 | 缓解措施 |
|-----|-----|-------|---------|
| FFmpeg 编译配置复杂 | 构建失败、功能缺失 | 中 | 编写详细构建文档，提供 Docker 构建环境 |
| Emscripten 版本兼容性 | 编译失败或运行时问题 | 中 | 锁定 Emscripten 版本，充分测试 |
| WASM 模块体积较大 | 首次加载慢 | 中 | 精简解码器配置，启用压缩，CDN 加速 |
| Glue Code 内存管理不当 | 内存泄漏 | 高 | 严格的内存管理规范，自动化内存泄漏检测 |
| 浏览器自动播放限制 | 音频无法自动播放 | 高 | 用户交互触发，提示引导 |
| 移动端性能受限 | 解码卡顿 | 中 | 降低分辨率，优化缓冲策略 |
| Safari WebGL 兼容性 | 渲染异常 | 低 | 降级方案，兼容性测试 |
| WASM 调试困难 | 问题定位耗时 | 中 | 使用 DWARF 调试信息，Chrome DevTools 调试 |

### 7.2 约束条件

- 受限于浏览器安全策略，部分功能需要 HTTPS 环境
- SharedArrayBuffer 需要特定的 HTTP 响应头配置（COOP/COEP）
- 移动端浏览器后台运行时可能被暂停
- FFmpeg 源码需遵循 LGPL/GPL 许可证要求
- Emscripten 生成的代码需配合特定的加载方式

---

## 8. 里程碑规划

| 阶段 | 内容 | 周期 |
|-----|-----|-----|
| M1 | FFmpeg 源码编译环境搭建 + 基础 Glue Code | 2 周 |
| M2 | WASM 模块完整实现 + JS 封装层 | 2 周 |
| M3 | WebSocket 连接 + 解码集成测试 | 1 周 |
| M4 | WebGL 渲染实现 | 1 周 |
| M5 | 音视频同步 + 播放控制 | 1 周 |
| M6 | 性能优化 + 体积优化 | 1 周 |
| M7 | 兼容性测试 + Bug 修复 | 1 周 |
| M8 | 文档编写 + 发布 | 1 周 |

**预计总工期：10 周**

---

## 9. 附录

### 9.1 术语表

| 术语 | 说明 |
|-----|-----|
| YUV420P | 一种视频色彩编码格式，Y 为亮度，UV 为色度 |
| PCM | 脉冲编码调制，未压缩的数字音频格式 |
| WASM | WebAssembly，浏览器中的二进制执行格式 |
| WebGL | Web 图形库，基于 OpenGL ES 的 JavaScript API |
| Emscripten | LLVM 到 JavaScript/WASM 的编译器工具链 |
| Glue Code | 粘合代码，用于连接不同层级或系统的接口代码 |
| 库模式 | FFmpeg 作为库而非命令行程序使用的编译方式 |
| SIMD | 单指令多数据，一种并行计算技术 |

### 9.2 参考资料

- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [FFmpeg 编译指南](https://trac.ffmpeg.org/wiki/CompilationGuide)
- [Emscripten 官方文档](https://emscripten.org/docs/)
- [Emscripten 编译 FFmpeg 参考](https://github.com/nicholasyang/aspect)
- [WebGL 基础教程](https://webglfundamentals.org/)
- [Web Audio API 规范](https://www.w3.org/TR/webaudio/)
- [WebSocket API 规范](https://websockets.spec.whatwg.org/)

---

**文档修订记录**

| 版本 | 日期 | 修订内容 | 修订人 |
|-----|-----|---------|-------|
| 1.0 | 2025-12-29 | 初始版本 | - |
| 2.0 | 2025-01-03 | 将 ffmpeg.wasm 方案改为基于 FFmpeg 官方源码库模式原生构建 + 自定义 C 接口 Glue Code 方案 | - |
