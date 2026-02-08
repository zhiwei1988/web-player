## Why

服务端当前以硬编码的 40ms 间隔（25fps）发送 NAL 单元，不考虑视频文件的实际帧率。不同帧率的视频（如 30fps、60fps）会以错误的速度播放。需要从视频码流中解析实际帧率，并据此调整发送节奏。

## What Changes

- 解析 SPS（Sequence Parameter Set）NAL 单元，提取 H.264/H.265 的时序信息（`time_scale`、`num_units_in_tick`）以计算实际帧率
- `NalParser` 对外暴露解析出的帧率
- 用实际帧率动态计算定时器间隔，替换硬编码的 `NAL_SEND_INTERVAL_MS = 40`
- 按 Access Unit（一帧）为单位发送，而非逐个 NAL 单元发送

## Capabilities

### New Capabilities
- `fps-detection`: 从 H.264/H.265 SPS NAL 单元中解析视频帧率，供发送逻辑使用

### Modified Capabilities
（无 - 当前没有已有 spec）

## Impact

- `src/server/nal_parser.h/cpp` - 新增 SPS 解析和帧率检测，NAL 单元按 Access Unit 分组
- `src/server/main.cpp` - 使用检测到的帧率计算定时器间隔，替代硬编码 40ms
- 无客户端改动
- 无协议变更
