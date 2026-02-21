## ADDED Requirements

### Requirement: Initialize audio decoder
WASM 解码器 SHALL 提供 `decoder_init_audio(codec_type, sample_rate, channels)` 接口，根据编码类型初始化对应的 FFmpeg 音频解码器。支持的编码类型：G.711A (CODEC_G711A)、G.711U (CODEC_G711U)、G.726 (CODEC_G726)、AAC (CODEC_AAC)。

#### Scenario: Initialize AAC decoder
- **WHEN** 调用 `decoder_init_audio` 并指定 codec_type 为 CODEC_AAC，sample_rate 为 44100，channels 为 2
- **THEN** SHALL 返回 0 表示成功，内部创建 AAC 解码器上下文

#### Scenario: Initialize G.711A decoder
- **WHEN** 调用 `decoder_init_audio` 并指定 codec_type 为 CODEC_G711A，sample_rate 为 8000，channels 为 1
- **THEN** SHALL 返回 0 表示成功

#### Scenario: Initialize with unsupported codec
- **WHEN** 调用 `decoder_init_audio` 并指定不支持的 codec_type
- **THEN** SHALL 返回负值表示失败

### Requirement: Decode audio packet
WASM 解码器 SHALL 提供 `decoder_send_audio_packet(data, size, pts)` 接口，将编码的音频数据送入解码器。

#### Scenario: Send valid audio packet
- **WHEN** 向已初始化的音频解码器发送一个有效的编码音频包
- **THEN** SHALL 返回 DECODE_OK

#### Scenario: Send packet to uninitialized decoder
- **WHEN** 在未调用 `decoder_init_audio` 的情况下发送音频包
- **THEN** SHALL 返回 DECODE_ERROR

### Requirement: Receive decoded audio frame
WASM 解码器 SHALL 提供 `decoder_receive_audio_frame(frame_info)` 接口，从解码器获取解码后的音频帧。输出格式为交错 PCM float32 数据。

#### Scenario: Receive decoded frame after sending packet
- **WHEN** 发送音频包后调用 `decoder_receive_audio_frame`
- **THEN** SHALL 填充 AudioFrameInfo 结构体，包含 sample_rate、channels、nb_samples、pts 和 PCM float 数据指针

#### Scenario: No frame available
- **WHEN** 解码器缓冲区中没有可用帧时调用 `decoder_receive_audio_frame`
- **THEN** SHALL 返回 DECODE_NEED_MORE_DATA

### Requirement: Flush audio decoder
WASM 解码器 SHALL 提供 `decoder_flush_audio()` 接口，刷新音频解码器缓冲区中的剩余帧。

#### Scenario: Flush with buffered frames
- **WHEN** 调用 `decoder_flush_audio` 后连续调用 `decoder_receive_audio_frame`
- **THEN** SHALL 依次返回缓冲区中所有剩余解码帧，直到返回 DECODE_NEED_MORE_DATA

### Requirement: Audio and video decoder coexistence
音频解码器和视频解码器 SHALL 能够同时存在并独立工作，互不干扰。

#### Scenario: Concurrent audio and video decoding
- **WHEN** 同时初始化了视频解码器和音频解码器
- **THEN** 分别向两个解码器发送数据和接收帧 SHALL 独立工作，不产生交叉影响
