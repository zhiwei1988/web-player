## ADDED Requirements

### Requirement: Audio decoding in Web Worker
decode-worker SHALL 识别 `msgType=0x02` 的帧协议解析结果，调用 WASM 音频解码接口进行解码，并将解码后的 PCM 数据通过 postMessage 发送到主线程。

#### Scenario: Receive and decode audio frame
- **WHEN** Worker 收到一个 msgType 为 0x02 的帧协议包
- **THEN** Worker SHALL 调用音频解码接口，解码成功后发送 `type: 'audioFrame'` 消息到主线程，包含 PCM float32 数据、采样率、声道数和 PTS

### Requirement: Audio playback via Web Audio API
客户端 SHALL 使用 AudioContext 和 AudioBufferSourceNode 播放解码后的 PCM 音频数据。

#### Scenario: Play decoded audio frames
- **WHEN** 主线程收到解码后的音频帧数据
- **THEN** SHALL 创建 AudioBuffer，填入 PCM 数据，通过 AudioBufferSourceNode 调度播放

#### Scenario: AudioContext initialization
- **WHEN** 收到第一个音频帧
- **THEN** SHALL 创建 AudioContext（如尚未创建），采样率与音频流匹配

### Requirement: Audio-video synchronization
客户端 SHALL 以音频时钟 (`AudioContext.currentTime`) 为主时钟，视频帧渲染根据 PTS 与音频时钟的差值调整。

#### Scenario: Video frame ahead of audio
- **WHEN** 视频帧 PTS 超前音频时钟 30ms 以上
- **THEN** SHALL 延迟该视频帧的渲染，等待音频时钟追上

#### Scenario: Video frame behind audio
- **WHEN** 视频帧 PTS 落后音频时钟 60ms 以上
- **THEN** SHALL 跳过该视频帧，不渲染

#### Scenario: Video frame within sync threshold
- **WHEN** 视频帧 PTS 与音频时钟的差值在 [-60ms, +30ms] 范围内
- **THEN** SHALL 立即渲染该视频帧

### Requirement: Audio decoder initialization from negotiation
客户端 SHALL 从 media-offer 中的音频流描述提取编码类型、采样率和声道数，用于初始化 WASM 音频解码器。

#### Scenario: Media offer with audio stream
- **WHEN** 收到包含音频流描述的 media-offer
- **THEN** SHALL 使用音频参数初始化 WASM 音频解码器，并在 media-answer 中确认接受

#### Scenario: Media offer without audio stream
- **WHEN** 收到不包含音频流描述的 media-offer
- **THEN** SHALL 不初始化音频解码器，仅处理视频流

### Requirement: Worker message types for audio
WorkerBridge 和 decode-worker 之间的消息类型 SHALL 扩展以支持音频：
- WorkerRequest 增加 `type: 'initAudio'` 和 `type: 'decodeAudio'`
- WorkerResponse 增加 `type: 'audioFrame'`，携带 PCM 数据、采样率、声道数和 PTS

#### Scenario: Audio init request
- **WHEN** 主线程发送 `type: 'initAudio'` 请求
- **THEN** Worker SHALL 初始化音频解码器并回复 `type: 'ready'`

#### Scenario: Audio frame response
- **WHEN** Worker 解码出一帧音频
- **THEN** SHALL 发送 `type: 'audioFrame'` 消息，PCM 数据通过 Transferable 传输以避免拷贝
