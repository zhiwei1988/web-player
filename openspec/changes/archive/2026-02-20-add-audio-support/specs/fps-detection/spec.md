## MODIFIED Requirements

### Requirement: Expose detected frame rate via NalParser
`NalParser` SHALL 在 `LoadFile` 完成后提供一个接口返回检测到的帧率（浮点数，单位 fps）。若未检测到帧率，SHALL 返回默认值 25.0。

当输入为 MP4 文件时，帧率 SHALL 从 MP4 容器元数据中提取（通过 Mp4Demuxer），而非从 SPS 中解析。NalParser 仅在裸流输入时使用。

#### Scenario: Query frame rate after loading raw bitstream file
- **WHEN** 调用 `NalParser::LoadFile` 加载裸 H.264/H.265 文件后查询帧率
- **THEN** SHALL 返回从 SPS 中解析出的帧率值，或默认值 25.0

#### Scenario: Query frame rate from MP4 file
- **WHEN** 输入为 MP4 文件，通过 Mp4Demuxer 加载后查询帧率
- **THEN** SHALL 返回从 MP4 容器元数据中提取的帧率值

### Requirement: Send video frames at actual frame rate
服务端 SHALL 使用从媒体文件中检测到的帧率计算定时器间隔（`interval_ms = 1000.0 / fps`），替代硬编码的 40ms 间隔。当输入为 MP4 文件时，定时器间隔 SHALL 基于 MP4 容器中的帧率元数据。

#### Scenario: Video with 30fps frame rate
- **WHEN** 加载的视频帧率为 30fps
- **THEN** 定时器间隔 SHALL 设置为约 33ms（1000/30）

#### Scenario: Frame rate detection fails
- **WHEN** 帧率检测失败，回退到默认 25fps
- **THEN** 定时器间隔 SHALL 设置为 40ms（1000/25），与当前行为一致
