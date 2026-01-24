# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WebSocket-based video streaming player with FFmpeg WASM decoder. Receives H.264/H.265 encoded video via WebSocket, decodes using FFmpeg compiled to WASM, renders on HTML5 Canvas.

## Build Commands

```bash
npm install                  # Install dependencies
npm run build:ffmpeg         # Build FFmpeg static libs for WASM (requires Emscripten)
npm run build:wasm           # Build WASM decoder module (dist/decoder.js, dist/decoder.wasm)
npm run build:decoder        # Build both FFmpeg and WASM
npm run build:ts             # Compile TypeScript
npm run build:all            # Build everything
npm run clean                # Clean build artifacts
```

Emscripten SDK path: `/home/zhiwei/OpenSource/emsdk`

## Development Environment

Server (Linux 192.168.50.101):
```bash
./bin/video_server           # WebSocket server on port 8080 (default: H.264)
CODEC_TYPE=h265 ./bin/video_server  # WebSocket server with H.265
python3 -m http.server 8888  # HTTP server for static files
```

Client browser access:
- `http://192.168.50.101:8888/` - H.264 decoder (default)
- `http://192.168.50.101:8888/?codec=h265` - H.265 decoder
- WebSocket connects to `ws://192.168.50.101:8080`

## Architecture

```
Browser                           Server
┌─────────────────────────┐      ┌─────────────────┐
│ index.html              │      │ video_server    │
│  └─ app.js (WebSocket)  │ ◄──► │  NAL parser     │
│  └─ decoder-init.js     │      │  Access Unit    │
│      └─ WorkerBridge    │      │  grouping       │
│          └─ Worker      │      └─────────────────┘
│              └─ DecoderWrapper
│                  └─ decoder.wasm (FFmpeg)
└─────────────────────────┘
```

**Key modules:**
- `src/wasm/decoder_wasm.c` - FFmpeg C wrapper, exports decoder API
- `src/js/decoder/DecoderWrapper.ts` - WASM module loader and decoder wrapper
- `src/js/decoder/WorkerBridge.ts` - Main thread ↔ Web Worker communication
- `src/worker/decode-worker.ts` - Runs decoder in separate thread
- `src/js/decoder/types.ts` - TypeScript type definitions
- `src/server/` - C++ WebSocket server, parses H.264/H.265 bitstream into NAL units

**Data flow:**
1. Server reads raw H.264/H.265 file, parses NAL units, groups into Access Units
2. Server sends Access Units via WebSocket (~25fps)
3. Client receives, buffers in queue, sends to Web Worker
4. Worker uses DecoderWrapper to decode via WASM
5. Decoded YUV frames transferred back to main thread
6. Canvas renders video

## WASM Decoder API

C functions exported (see `src/wasm/decoder_wasm.h`):
- `decoder_init_video(codec_type)` - Init decoder (0=H.264, 1=H.265)
- `decoder_send_video_packet(data, size, pts)` - Feed encoded data
- `decoder_receive_video_frame(frame_info)` - Get decoded YUV frame
- `decoder_flush_video()` - Flush decoder buffer
- `decoder_destroy()` - Cleanup
- `decoder_malloc/free` - Memory allocation for data transfer

## Test Video Files

- `tests/fixtures/test_video.h264` - H.264 test stream
- `tests/fixtures/test_video.h265` - H.265 test stream

## 调试方法

我的服务端是部署在 Linux 服务器上的，服务器 ip 192.168.50.101

我通过 ./bin/video_server 启动服务端，通过环境变量 CODEC_TYPE 可以指定服务端发送的视频编码格式，例如：CODEC_TYPE=h265 ./bin/video_server，这里会起一个 websocket 服务，在 8080 端口上

我同时通过 python 起了一个 http 服务，python3 -m http.server 8888

我在另一台 Mac 电脑上调试该功能，使用浏览器访问 http://192.168.50.101:8888/?codec=h265，codec 参数可以指定客户端解码器类型，缺省情况下使用 h264，客户端还会通过 ws://192.168.50.101:8080 从 websocket 服务取编码数据