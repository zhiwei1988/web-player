## Why

当前系统仅支持视频流推送。为提供完整的媒体播放体验，需要增加音频支持。媒体源为包含音视频轨道的 MP4 文件，播放时需要音视频同步。

## What Changes

- 服务端：将裸 H.264/H.265 文件读取替换为 MP4 解封装（libavformat），提取音频和视频轨道
- 服务端：在现有帧协议中增加音频帧编码（`MsgType::AUDIO`），定义音频扩展头
- 服务端：扩展 media-offer/answer 协商，包含音频流参数
- WASM 解码器：增加 G.711a、G.711u、G.726、AAC 音频解码支持
- 客户端 JS：增加音频解码管线（WorkerBridge、decode-worker），与视频管线并行
- 客户端 JS：通过 Web Audio API 实现音频播放，并与视频同步
- 帧协议：定义 `AudioCodec` 枚举和音频扩展头格式

## Capabilities

### New Capabilities
- `mp4-demux`：服务端 MP4 容器解封装，提取音频和视频基本流及时间戳信息
- `audio-codec`：WASM 解码器中的 G.711a、G.711u、G.726、AAC 音频解码支持
- `audio-transport`：帧协议音频扩展头、音频流协商、WebSocket 音频帧传输
- `audio-playback`：客户端通过 Web Audio API 进行音频渲染及音视频同步

### Modified Capabilities
- `fps-detection`：帧率检测从 NAL 层 SPS 解析改为从 MP4 容器元数据中提取

## Impact

- **服务端代码**：`main.cpp` 大幅变更 — NalParser 替换为 MP4 解封装器；定时器逻辑改为基于实际媒体时间戳
- **帧协议**：`frame_protocol.h/.cpp` — 新增 `AudioCodec` 枚举、`EncodeAudioFrame()`、音频扩展头写入
- **WASM 解码器**：`decoder_wasm.c/.h` — 新增 `decoder_init_audio`、`decoder_send_audio_packet`、`decoder_receive_audio_frame` 接口
- **构建系统**：FFmpeg WASM 构建需增加音频编解码库；服务端构建需引入 libavformat
- **客户端 JS**：新增音频解码和播放模块；WorkerBridge 和 decode-worker 增加音频消息类型
- **协商协议**：media-offer/answer JSON schema 扩展，包含音频流描述
- **依赖**：服务端新增 libavformat/libavcodec 依赖用于 MP4 解封装
