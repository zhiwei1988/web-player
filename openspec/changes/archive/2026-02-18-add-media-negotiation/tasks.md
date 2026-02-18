## 1. 服务端连接状态机扩展

- [x] 1.1 在 `ConnState` 枚举中新增 `NEGOTIATING` 和 `STREAMING` 状态
- [x] 1.2 在 `Connection` 结构体中增加协商超时时间戳字段
- [x] 1.3 修改 `HandleHandshake`：握手完成后发送 `media-offer` JSON 消息，状态设为 `NEGOTIATING`

## 2. 服务端 JSON 消息处理

- [x] 2.1 实现 `media-offer` JSON 构造函数，根据当前 codec 和帧率生成 offer 消息
- [x] 2.2 实现轻量 JSON 解析，支持从 `media-answer` 中提取 `type`、`accepted`、`reason` 字段
- [x] 2.3 修改 `HandleWebSocketFrame` 中 `TEXT` 分支：解析 JSON 消息，分发到协商处理逻辑
- [x] 2.4 实现 `media-answer` 处理：`accepted=true` 时状态转为 `STREAMING`，`accepted=false` 时记录日志并关闭连接

## 3. 服务端协商超时

- [x] 3.1 在发送 `media-offer` 时记录发送时间戳
- [x] 3.2 在 `OnTimer` 中检查 `NEGOTIATING` 状态连接是否超过 5 秒，超时则关闭连接

## 4. 服务端视频发送逻辑调整

- [x] 4.1 修改 `OnTimer` 中视频帧发送条件：从 `ConnState::CONNECTED` 改为 `ConnState::STREAMING`

## 5. 客户端协商处理

- [x] 5.1 在 WebSocket `onmessage` 中区分 TEXT 和 BINARY 消息，TEXT 消息按 JSON 信令处理
- [x] 5.2 实现 `media-offer` 接收处理：解析 streams 中的 codec 信息，据此初始化解码器
- [x] 5.3 实现 `media-answer` 发送：解码器初始化成功后回复 `accepted: true`，失败则回复 `accepted: false` 并附带 reason
- [x] 5.4 移除客户端通过 URL 参数 `?codec=` 指定解码器类型的逻辑，改为依赖协商结果
