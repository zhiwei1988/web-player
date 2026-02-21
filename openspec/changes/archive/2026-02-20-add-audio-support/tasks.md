## 1. 帧协议扩展

- [x] 1.1 在 `frame_protocol.h` 中定义 `AudioCodec` 枚举（G711A=1, G711U=2, G726=3, AAC=4）和 `SampleRateCode` 枚举
- [x] 1.2 在 `frame_protocol.h/.cpp` 中实现 `WriteAudioExtHeader()` 和 `EncodeAudioFrame()` 方法
- [x] 1.3 在 WASM 侧帧协议解析器中增加 `msg_type=0x02` 音频帧的解析，提取 audio_codec、sample_rate、channels

## 2. 服务端 MP4 解封装

- [x] 2.1 创建 `mp4_demuxer.h/.cpp`，使用 libavformat 实现 MP4 文件打开、音视频轨道识别和元数据提取
- [x] 2.2 实现预读所有音视频包到内存，按 PTS（毫秒）排序存储，支持循环发送
- [x] 2.3 实现输入文件类型检测（`.mp4` vs `.h264`/`.h265`），选择 Mp4Demuxer 或 NalParser
- [x] 2.4 修改服务端 CMakeLists.txt，增加 libavformat/libavcodec 链接依赖

## 3. 服务端发送逻辑

- [x] 3.1 修改 `main.cpp`，当输入为 MP4 时使用 Mp4Demuxer 替代 NalParser
- [x] 3.2 实现基于 PTS 的音视频交错发送逻辑（基准定时器 tick 检查到时帧）
- [x] 3.3 扩展 `BuildMediaOffer()` 方法，在 streams 数组中增加音频流描述（codec、sampleRate、channels）
- [x] 3.4 当 MP4 无音频轨时，media-offer 仅包含视频流，发送逻辑退化为纯视频模式

## 4. WASM 音频解码器

- [x] 4.1 在 `decoder_wasm.h` 中增加音频编码类型枚举（CODEC_G711A 等）和音频解码 API 声明
- [x] 4.2 在 `decoder_wasm.c` 中实现 `decoder_init_audio()`，根据 codec_type 创建对应 FFmpeg 解码器上下文
- [x] 4.3 实现 `decoder_send_audio_packet()` 和 `decoder_receive_audio_frame()`，输出交错 PCM float32
- [x] 4.4 实现 `decoder_flush_audio()`
- [x] 4.5 修改 WASM 构建脚本，启用 pcm_alaw、pcm_mulaw、g726、aac 解码器，并导出新增的音频函数

## 5. 客户端音频解码管线

- [x] 5.1 在 `types.ts` 中增加 AudioFrame 类型定义和音频相关的 WorkerRequest/WorkerResponse 消息类型
- [x] 5.2 在 `DecoderWrapper.ts` 中增加音频解码方法（initAudio、decodeAudio、flushAudio），调用 WASM 音频 API
- [x] 5.3 修改 `decode-worker.ts`，识别 `msgType=0x02` 音频帧并调用音频解码，通过 postMessage 发送 PCM 数据
- [x] 5.4 在 `WorkerBridge.ts` 中增加音频初始化请求和 `onAudioFrame` 回调

## 6. 客户端音频播放与同步

- [x] 6.1 创建音频播放模块，使用 AudioContext + AudioBufferSourceNode 调度播放 PCM 数据
- [x] 6.2 实现音视频同步逻辑：以 AudioContext.currentTime 为主时钟，视频帧根据 PTS 差值判断延迟渲染/跳帧/立即渲染
- [x] 6.3 修改 `app.js` 中的协商逻辑，从 media-offer 提取音频参数并初始化音频解码器
- [x] 6.4 修改 `app.js` 中的帧回调，接入音频播放模块和同步逻辑

## 7. FFmpeg 构建配置

- [x] 7.1 修改 `build_ffmpeg.sh`（或等效脚本），在 WASM 构建中启用 `--enable-decoder=pcm_alaw,pcm_mulaw,g726,aac`
- [x] 7.2 验证构建产物大小增量在可接受范围内
