## ADDED Requirements

### Requirement: Parse frame rate from H.264 SPS
系统 SHALL 从 H.264 裸码流的 SPS NAL 单元（NAL type 7）中解析 VUI timing_info 字段，提取 `time_scale` 和 `num_units_in_tick`，计算帧率为 `time_scale / (2 * num_units_in_tick)`。

#### Scenario: H.264 bitstream with VUI timing_info present
- **WHEN** 加载一个 H.264 视频文件，其 SPS 中包含 VUI timing_info
- **THEN** 系统 SHALL 正确解析出帧率值（如 30fps 的视频返回 30.0）

#### Scenario: H.264 bitstream without VUI timing_info
- **WHEN** 加载一个 H.264 视频文件，其 SPS 中不包含 VUI timing_info
- **THEN** 系统 SHALL 回退使用默认帧率 25fps，并输出警告日志

### Requirement: Parse frame rate from H.265 SPS
系统 SHALL 从 H.265 裸码流的 SPS NAL 单元（NAL type 33）中解析 VUI timing_info 字段，提取 `vui_time_scale` 和 `vui_num_units_in_tick`，计算帧率为 `vui_time_scale / vui_num_units_in_tick`。

#### Scenario: H.265 bitstream with VUI timing_info present
- **WHEN** 加载一个 H.265 视频文件，其 SPS 中包含 VUI timing_info
- **THEN** 系统 SHALL 正确解析出帧率值

#### Scenario: H.265 bitstream without VUI timing_info
- **WHEN** 加载一个 H.265 视频文件，其 SPS 中不包含 VUI timing_info
- **THEN** 系统 SHALL 回退使用默认帧率 25fps，并输出警告日志

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

### Requirement: Group NAL units into Access Units
`NalParser` SHALL 将解析出的 NAL 单元按 Access Unit（一帧）分组。每次定时器触发时发送一整个 Access Unit 的所有 NAL 单元，而非单个 NAL 单元。

#### Scenario: Access Unit containing multiple NAL units
- **WHEN** 一帧由 SPS + PPS + IDR slice 三个 NAL 组成
- **THEN** 系统 SHALL 在一次定时器触发中将这三个 NAL 全部发送

#### Scenario: Access Unit containing single slice NAL
- **WHEN** 一帧仅包含一个 slice NAL
- **THEN** 系统 SHALL 在一次定时器触发中发送该 NAL

### Requirement: Codec type awareness for SPS parsing
系统 SHALL 根据编解码器类型（H.264 或 H.265）选择正确的 SPS 解析逻辑。`NalParser` MUST 知道当前处理的是 H.264 还是 H.265 码流。

#### Scenario: H.264 codec type
- **WHEN** 指定编解码器为 H.264
- **THEN** SHALL 使用 H.264 SPS 解析逻辑（NAL type 7）

#### Scenario: H.265 codec type
- **WHEN** 指定编解码器为 H.265
- **THEN** SHALL 使用 H.265 SPS 解析逻辑（NAL type 33）
