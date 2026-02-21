## Context

当前系统从裸 H.264/H.265 文件读取 NAL 单元，通过 WebSocket 推送至客户端，客户端使用 FFmpeg WASM 解码后渲染到 Canvas。整条链路仅支持视频。

现需从 MP4 文件同时提取音频和视频轨道，通过同一 WebSocket 连接传输，客户端解码后音视频同步播放。

## Goals / Non-Goals

**Goals:**
- 服务端支持 MP4 解封装，同时提取音视频轨道
- 支持 G.711a、G.711u、G.726、AAC 四种音频编码
- 复用现有 WebSocket 连接传输音频数据
- 客户端实现音频解码和 Web Audio API 播放
- 音视频基于 PTS 同步

**Non-Goals:**
- 不支持纯音频流（无视频的场景）
- 不支持音频编码/上行
- 不支持动态切换音频编码格式
- 不处理音频重采样（由 FFmpeg 解码器输出统一格式）

## Decisions

### D1: 服务端 MP4 解封装方案

**选择**: 使用 libavformat 进行 MP4 解封装

**理由**: 服务端已有 FFmpeg 构建基础设施，libavformat 可直接读取 MP4 容器并输出带时间戳的音视频包。相比手工解析 MP4 box，可靠性和兼容性更好。

**替代方案**:
- 手工解析 MP4 box：工作量大，边界条件多，不值得
- 使用 mp4ff 等轻量库：引入新依赖，不如复用已有的 FFmpeg

**影响**: NalParser 将被替换为基于 libavformat 的 Mp4Demuxer，服务端不再直接处理裸视频文件。现有裸 H.264/H.265 文件输入需保留为降级路径。

### D2: 音视频数据复用传输

**选择**: 音频和视频帧通过同一 WebSocket 连接发送，使用现有帧协议的 `MsgType::AUDIO (0x02)` 区分

**理由**: 帧协议已预留 AUDIO 消息类型，无需新增连接。单连接可依赖 WebSocket 的有序传输保证，简化同步逻辑。

**帧协议音频扩展头格式**:
```
音频扩展头 (audio ext header): 4 bytes
  [0]    audio_codec   : uint8_t  (1=G711A, 2=G711U, 3=G726, 4=AAC)
  [1]    sample_rate   : uint8_t  (编码值: 0=8000, 1=16000, 2=44100, 3=48000)
  [2]    channels      : uint8_t  (1=mono, 2=stereo)
  [3]    reserved      : uint8_t
```

### D3: 音频解码架构

**选择**: 在现有 WASM 解码器中增加音频解码接口，复用同一个 Web Worker

**理由**: 音视频共用 Worker 可简化消息传递，避免跨 Worker 同步开销。FFmpeg WASM 模块本身已包含音频解码器库，只需新增 C API 并在 JS 层调用。

**替代方案**:
- 独立 Audio Worker：增加了 Worker 间同步复杂度
- 主线程解码：音频解码开销小可行，但不一致的架构增加维护成本

**新增 WASM C API**:
```c
int decoder_init_audio(CodecType codec_type, int sample_rate, int channels);
DecodeStatus decoder_send_audio_packet(const uint8_t* data, int size, int64_t pts);
DecodeStatus decoder_receive_audio_frame(AudioFrameInfo* frame_info);
void decoder_flush_audio(void);
```

### D4: 音频播放与同步策略

**选择**: 使用 Web Audio API (AudioContext + AudioBufferSourceNode) 播放，以音频时钟为主时钟进行同步

**理由**: Web Audio API 提供精确的音频时钟 (`AudioContext.currentTime`)，适合作为主时钟。视频帧渲染根据音频时钟调整节奏。

**同步机制**:
1. 音频帧解码后按 PTS 排序，通过 AudioBufferSourceNode 以准确时间调度播放
2. 视频帧渲染前将视频 PTS 与音频时钟比较：
   - 视频超前 > 阈值(30ms)：延迟渲染
   - 视频落后 > 阈值(60ms)：跳过该帧
   - 在阈值内：立即渲染

**替代方案**:
- 以视频帧率为主时钟：音频会出现卡顿或加速，体验差
- 独立时钟 + 定期校正：实现复杂且效果不如音频主时钟

### D5: 媒体协商扩展

**选择**: 扩展现有 media-offer/answer JSON，在 streams 数组中增加音频流描述

**media-offer 示例**:
```json
{
  "type": "media-offer",
  "payload": {
    "version": 1,
    "streams": [
      {"type": "video", "codec": "h264", "framerate": 25.0},
      {"type": "audio", "codec": "aac", "sampleRate": 44100, "channels": 2}
    ]
  }
}
```

### D6: FFmpeg WASM 构建调整

**选择**: 在现有 WASM 构建配置中启用所需音频解码器

**需要启用的解码器**:
- `--enable-decoder=pcm_alaw` (G.711A)
- `--enable-decoder=pcm_mulaw` (G.711U)
- `--enable-decoder=g726` (G.726)
- `--enable-decoder=aac` (AAC)

WASM 二进制大小预估增加约 200-400KB。

### D7: 服务端发送节奏

**选择**: 基于 MP4 中的 PTS 差值动态调整发送间隔，而非使用固定帧率定时器

**理由**: MP4 中音频和视频的帧率不同（视频 25fps vs 音频 ~43fps@1024samples/44100Hz），需按各自时间戳发送。使用统一的基准定时器（如 10ms），每次 tick 检查是否有到时的音频或视频帧需要发送。

## Risks / Trade-offs

- **WASM 体积增加** → 音频解码器增加约 200-400KB，可接受
- **G.726 解码支持** → FFmpeg 的 G.726 解码器需要 `adpcm` 支持，需确认 WASM 构建是否正确包含 → 构建阶段验证
- **音视频同步精度** → WebSocket 传输引入不确定延迟 → 客户端基于 PTS 的同步机制可容忍网络抖动
- **MP4 文件格式兼容性** → 部分 MP4 文件可能使用非标准 box → 依赖 libavformat 的广泛兼容性
- **裸流降级** → 引入 MP4 解封装后需保留对裸 H.264/H.265 输入的支持 → 通过文件扩展名或命令行参数区分输入类型
