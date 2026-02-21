## ADDED Requirements

### Requirement: Demux MP4 container to extract audio and video streams
系统 SHALL 使用 libavformat 打开 MP4 文件，识别并选取视频轨道和音频轨道。系统 SHALL 输出带有 PTS 时间戳的独立音频包和视频包。

#### Scenario: MP4 file with both audio and video tracks
- **WHEN** 加载一个包含音频和视频轨道的 MP4 文件
- **THEN** 系统 SHALL 成功识别两条轨道，并能分别读取音频包和视频包

#### Scenario: MP4 file with video only
- **WHEN** 加载一个仅包含视频轨道的 MP4 文件
- **THEN** 系统 SHALL 正常工作，仅输出视频包，不报错

#### Scenario: MP4 file with unsupported audio codec
- **WHEN** 加载一个音频编码不在支持列表（G.711a/G.711u/G.726/AAC）中的 MP4 文件
- **THEN** 系统 SHALL 忽略音频轨道，仅输出视频包，并输出警告日志

### Requirement: Extract media metadata from MP4 container
系统 SHALL 从 MP4 容器中提取视频编码类型、帧率、音频编码类型、采样率、声道数等元数据。

#### Scenario: Query video metadata
- **WHEN** MP4 文件加载完成后查询视频元数据
- **THEN** 系统 SHALL 返回视频编码类型（H.264/H.265）和帧率

#### Scenario: Query audio metadata
- **WHEN** MP4 文件加载完成后查询音频元数据
- **THEN** 系统 SHALL 返回音频编码类型、采样率和声道数

### Requirement: Preserve original PTS from MP4 container
系统 SHALL 将 MP4 容器中的 PTS 转换为毫秒单位，保持音频包和视频包的原始时间关系。

#### Scenario: Audio and video PTS alignment
- **WHEN** 读取音频和视频包
- **THEN** 输出的 PTS 值 SHALL 基于同一时间基准（毫秒），可用于客户端同步

### Requirement: Support input type detection
系统 SHALL 根据文件扩展名区分输入类型：`.mp4` 使用 MP4 解封装路径，`.h264`/`.h265` 使用现有裸流解析路径。

#### Scenario: MP4 file input
- **WHEN** 指定的输入文件扩展名为 `.mp4`
- **THEN** 系统 SHALL 使用 Mp4Demuxer 进行解封装

#### Scenario: Raw bitstream file input
- **WHEN** 指定的输入文件扩展名为 `.h264` 或 `.h265`
- **THEN** 系统 SHALL 使用现有 NalParser 进行解析，行为与当前一致

### Requirement: Pre-read and index all packets
系统 SHALL 在启动时将 MP4 文件中所有音频和视频包预读到内存，按 PTS 排序存储，支持循环发送。

#### Scenario: Cyclic playback
- **WHEN** 所有包发送完毕
- **THEN** 系统 SHALL 从第一个包重新开始发送，实现循环播放
