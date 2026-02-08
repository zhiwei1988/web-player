## Context

当前服务端 `main.cpp` 硬编码 `NAL_SEND_INTERVAL_MS = 40`（25fps），`OnTimer()` 每次触发时逐个发送 NAL 单元。实际视频帧率可能是 24/25/30/60fps 等不同值，导致播放速度不正确。

视频帧率信息存储在 SPS（Sequence Parameter Set）NAL 单元中：
- H.264: SPS 的 VUI 参数中包含 `time_scale` 和 `num_units_in_tick`，fps = time_scale / (2 * num_units_in_tick)
- H.265: SPS 的 VUI 参数中同样包含 `vui_time_scale` 和 `vui_num_units_in_tick`，fps = vui_time_scale / vui_num_units_in_tick

## Goals / Non-Goals

**Goals:**
- 从裸码流（raw bitstream）中解析 SPS，提取帧率
- 按实际帧率间隔发送视频帧
- 支持 H.264 和 H.265 两种编码格式
- 帧率解析失败时回退到默认 25fps

**Non-Goals:**
- 不引入 FFmpeg 等外部库做解析，手动解析 SPS 中所需的最少字段
- 不处理 VFR（可变帧率）视频
- 不改变客户端行为

## Decisions

### 1. SPS 解析方式：手动实现 bitstream reader + SPS 部分解析

**理由**: SPS 解析只需读取到 VUI timing_info 部分，不需要完整解析。引入 FFmpeg 等库太重，只需一个简单的 bit-level reader（处理 Exp-Golomb 编码）即可。

**替代方案**: 用 FFmpeg libavcodec 解析 —— 过重，服务端当前无此依赖。

### 2. 新增 `SpsParser` 类，独立于 `NalParser`

**理由**: SPS 解析逻辑（bit-level 操作、Exp-Golomb 解码）与 NAL 单元分割是不同层次的职责。`SpsParser` 负责从 SPS NAL 数据中提取帧率，`NalParser` 在 `LoadFile` 时调用它。

### 3. Access Unit 分组策略：在 `NalParser` 中按帧分组

**理由**: 当前 `OnTimer()` 每次发送一个 NAL 单元，但一帧视频可能由多个 NAL 组成（SPS + PPS + IDR slice，或多个 slice）。应在解析阶段将 NAL 单元按 Access Unit 分组，每次定时器触发发送一整个 Access Unit。

分组规则：
- H.264: 遇到 `access_unit_delimiter`（NAL type 9）或新的 slice（NAL type 1/5 且 `first_mb_in_slice == 0`）时开始新的 AU
- H.265: 遇到 `access_unit_delimiter`（NAL type 35）或新的 VCL NAL（type 0-31 且 `first_slice_segment_in_pic_flag == 1`）时开始新的 AU
- 简化实现：SPS/PPS/VPS 等参数集 NAL 归入其后的第一个 VCL NAL 所在的 AU

### 4. Connection 中 `nalIndex` 改为 `auIndex`（Access Unit index）

**理由**: 发送单位从 NAL 变为 Access Unit，索引语义需相应调整。

## Risks / Trade-offs

- **[某些裸码流不含 VUI timing_info]** -> 回退到默认 25fps，启动时打印警告
- **[SPS 解析器仅实现最少字段跳过]** -> 若遇到特殊 profile 的 SPS 可能跳过字段数不对导致解析失败，此时回退默认帧率
- **[Access Unit 分组依赖 NAL type 判断]** -> 对于不含 AUD 的码流，需检查 slice header 的 first_mb/first_slice_segment 标志，增加了少量复杂度
