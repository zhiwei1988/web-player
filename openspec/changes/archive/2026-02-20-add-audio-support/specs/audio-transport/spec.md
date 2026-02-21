## ADDED Requirements

### Requirement: Audio frame protocol encoding
帧协议 SHALL 提供 `EncodeAudioFrame()` 方法，使用 `MsgType::AUDIO (0x02)` 编码音频帧。音频扩展头格式为 4 字节：audio_codec (uint8)、sample_rate 编码值 (uint8, 0=8000/1=16000/2=44100/3=48000)、channels (uint8)、reserved (uint8)。

#### Scenario: Encode AAC audio frame
- **WHEN** 编码一个 AAC 音频帧，采样率 44100，双声道
- **THEN** SHALL 生成帧协议数据，msg_type 为 0x02，音频扩展头中 audio_codec=4, sample_rate=2, channels=2

#### Scenario: Encode G.711A audio frame
- **WHEN** 编码一个 G.711A 音频帧，采样率 8000，单声道
- **THEN** SHALL 生成帧协议数据，msg_type 为 0x02，音频扩展头中 audio_codec=1, sample_rate=0, channels=1

### Requirement: Audio codec enumeration
帧协议 SHALL 定义 `AudioCodec` 枚举：G711A=1, G711U=2, G726=3, AAC=4。

#### Scenario: AudioCodec values
- **WHEN** 引用 AudioCodec 枚举值
- **THEN** G711A SHALL 为 1，G711U SHALL 为 2，G726 SHALL 为 3，AAC SHALL 为 4

### Requirement: Client-side audio frame parsing
客户端帧协议解析器 SHALL 识别 `msg_type=0x02` 的音频帧，从音频扩展头中提取 audio_codec、sample_rate、channels，并返回音频 payload 数据。

#### Scenario: Parse audio frame from binary data
- **WHEN** 客户端收到一个 msg_type 为 0x02 的帧协议包
- **THEN** ParsedFrameInfo 中 msgType SHALL 为 0x02，codec 字段 SHALL 为音频编码值，payload SHALL 为音频编码数据

### Requirement: Media offer includes audio stream
服务端 media-offer SHALL 在 streams 数组中包含音频流描述，格式为 `{"type":"audio","codec":"<name>","sampleRate":<rate>,"channels":<ch>}`。

#### Scenario: MP4 with AAC audio
- **WHEN** 服务端加载包含 AAC 音频的 MP4 文件并发送 media-offer
- **THEN** media-offer 的 streams 数组 SHALL 包含 `{"type":"audio","codec":"aac","sampleRate":44100,"channels":2}`

#### Scenario: MP4 without audio
- **WHEN** 服务端加载不包含音频轨道的 MP4 文件
- **THEN** media-offer 的 streams 数组 SHALL 仅包含视频流描述，不包含音频条目

### Requirement: Interleaved audio and video sending
服务端 SHALL 按照 PTS 顺序交错发送音频帧和视频帧。使用基准定时器（10ms tick），每次 tick 检查是否有到时的音频或视频帧需要发送。

#### Scenario: Audio and video frames with different rates
- **WHEN** 视频帧率为 25fps，音频帧间隔约 23ms（1024 samples @ 44100Hz）
- **THEN** 系统 SHALL 按各自 PTS 时间戳交错发送，不固定先后顺序
