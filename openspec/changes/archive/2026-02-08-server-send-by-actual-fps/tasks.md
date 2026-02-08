## 1. Bitstream Reader 基础工具

- [x] 1.1 新增 `src/server/bitstream_reader.h/cpp`，实现 bit-level 读取器，支持：read_bits(n)、read_bit()、read_ue()（Exp-Golomb 无符号）、read_se()（Exp-Golomb 有符号）、skip_bits(n)

## 2. SPS 解析器

- [x] 2.1 新增 `src/server/sps_parser.h/cpp`，定义 `SpsParser` 类
- [x] 2.2 实现 H.264 SPS 解析：去除 emulation prevention bytes (0x03)，解析 profile_idc、跳过 scaling list 等字段直到 VUI timing_info，提取 time_scale 和 num_units_in_tick，计算 fps = time_scale / (2 * num_units_in_tick)
- [x] 2.3 实现 H.265 SPS 解析：去除 emulation prevention bytes，跳过 VPS/profile/conformance 等字段直到 VUI timing_info，提取 vui_time_scale 和 vui_num_units_in_tick，计算 fps = vui_time_scale / vui_num_units_in_tick
- [x] 2.4 解析失败时返回默认帧率 25.0 并输出警告日志

## 3. NalParser 改造 - Access Unit 分组

- [x] 3.1 `NalParser` 新增 codec type 参数（`LoadFile` 增加 `bool isH265` 参数），用于区分 H.264/H.265 解析逻辑
- [x] 3.2 新增 `AccessUnit` 结构体（包含 `std::vector<NalUnit>`），替代当前的单个 NAL 单元列表
- [x] 3.3 实现 Access Unit 分组逻辑：H.264 按 AUD (type 9) 或 first_mb_in_slice==0 的 VCL NAL 分组；H.265 按 AUD (type 35) 或 first_slice_segment_in_pic_flag==1 的 VCL NAL 分组
- [x] 3.4 `NalParser` 暴露 `GetAccessUnitCount()`、`GetAccessUnit(index)` 接口
- [x] 3.5 在 `LoadFile` 中调用 `SpsParser` 解析帧率，暴露 `GetFrameRate()` 接口

## 4. VideoServer 适配

- [x] 4.1 `main.cpp` 中将 `isH265_` 传递给 `NalParser::LoadFile`
- [x] 4.2 用 `nalParser_.GetFrameRate()` 计算定时器间隔，替代硬编码 `NAL_SEND_INTERVAL_MS = 40`
- [x] 4.3 `OnTimer()` 改为按 Access Unit 发送：遍历当前 AU 中所有 NAL 单元逐个封装为 WebSocket 帧发送
- [x] 4.4 `Connection` 中 `nalIndex` 改为 `auIndex`
- [x] 4.5 启动时打印检测到的帧率和计算出的发送间隔

## 5. 构建集成

- [x] 5.1 更新 `src/server/` 的构建配置（CMakeLists.txt 或 Makefile），添加新文件 `bitstream_reader.cpp` 和 `sps_parser.cpp`
