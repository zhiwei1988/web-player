## ADDED Requirements

### Requirement: 协商消息信封格式

所有协商消息 SHALL 使用 WebSocket TEXT frame 传输，消息体为 JSON，包含 `type` 和 `payload` 两个顶级字段。

```json
{
  "type": "<message-type>",
  "payload": { ... }
}
```

#### Scenario: 收到合法 JSON 消息
- **WHEN** 服务端或客户端收到 WebSocket TEXT frame
- **THEN** SHALL 解析 JSON 并根据 `type` 字段分发处理

#### Scenario: 收到非法 JSON 消息
- **WHEN** 服务端收到无法解析为 JSON 的 TEXT frame，或缺少 `type` 字段
- **THEN** 服务端 SHALL 忽略该消息并记录警告日志

### Requirement: media-offer 消息

WebSocket 握手完成后，服务端 SHALL 立即向客户端发送 `media-offer` 消息，描述自身媒体流参数。

消息格式：
```json
{
  "type": "media-offer",
  "payload": {
    "version": 1,
    "streams": [
      {
        "type": "video",
        "codec": "h264|h265",
        "framerate": <number>
      }
    ]
  }
}
```

- `version`: 协议版本号，当前 MUST 为 `1`
- `streams`: 媒体流描述数组，MUST 至少包含一个元素
- `streams[].type`: 流类型，当前支持 `"video"`
- `streams[].codec`: 编码格式，MUST 为 `"h264"` 或 `"h265"`
- `streams[].framerate`: 帧率，单位 fps

#### Scenario: 服务端发送 H.264 视频的 offer
- **WHEN** 服务端配置为 H.264 编码，帧率 25fps，WebSocket 握手完成
- **THEN** 服务端 SHALL 发送 `media-offer`，`streams` 包含 `{"type":"video","codec":"h264","framerate":25.0}`

#### Scenario: 服务端发送 H.265 视频的 offer
- **WHEN** 服务端配置为 H.265 编码，WebSocket 握手完成
- **THEN** 服务端 SHALL 发送 `media-offer`，`streams` 包含 `{"type":"video","codec":"h265"}`

### Requirement: media-answer 消息

客户端收到 `media-offer` 后 SHALL 回复 `media-answer` 消息，表示接受或拒绝。

接受格式：
```json
{
  "type": "media-answer",
  "payload": {
    "accepted": true
  }
}
```

拒绝格式：
```json
{
  "type": "media-answer",
  "payload": {
    "accepted": false,
    "reason": "<拒绝原因>"
  }
}
```

- `accepted`: MUST 为布尔值
- `reason`: 当 `accepted` 为 `false` 时 SHALL 提供拒绝原因字符串

#### Scenario: 客户端接受 offer
- **WHEN** 客户端收到 `media-offer` 且支持所有流的编码格式
- **THEN** 客户端 SHALL 发送 `media-answer`，`accepted` 为 `true`

#### Scenario: 客户端拒绝 offer
- **WHEN** 客户端收到 `media-offer` 但不支持其中的编码格式
- **THEN** 客户端 SHALL 发送 `media-answer`，`accepted` 为 `false`，`reason` 描述不支持的原因

### Requirement: 连接状态机扩展

服务端连接状态机 SHALL 扩展为以下状态：

```
HANDSHAKING_TLS → HANDSHAKING_WS → CONNECTED → NEGOTIATING → STREAMING → CLOSING
```

- `CONNECTED`: WebSocket 握手完成，服务端发送 `media-offer` 后立即转为 `NEGOTIATING`
- `NEGOTIATING`: 等待客户端 `media-answer`
- `STREAMING`: 协商成功，开始发送媒体数据

#### Scenario: 握手完成后进入协商
- **WHEN** WebSocket 握手成功完成
- **THEN** 服务端 SHALL 发送 `media-offer` 并将连接状态设为 `NEGOTIATING`

#### Scenario: 协商成功进入流式传输
- **WHEN** 服务端在 `NEGOTIATING` 状态收到 `media-answer` 且 `accepted` 为 `true`
- **THEN** 服务端 SHALL 将连接状态设为 `STREAMING`，开始按定时器发送视频帧

#### Scenario: 协商被拒绝
- **WHEN** 服务端在 `NEGOTIATING` 状态收到 `media-answer` 且 `accepted` 为 `false`
- **THEN** 服务端 SHALL 记录拒绝原因日志，发送 WebSocket CLOSE frame 并关闭连接

#### Scenario: 非 STREAMING 状态不发送视频帧
- **WHEN** 定时器触发且连接状态不是 `STREAMING`
- **THEN** 服务端 SHALL 跳过该连接，不发送视频帧

### Requirement: 协商超时

服务端 SHALL 在发送 `media-offer` 后启动 5 秒超时计时。

#### Scenario: 超时未收到 answer
- **WHEN** 服务端在 `NEGOTIATING` 状态超过 5 秒未收到 `media-answer`
- **THEN** 服务端 SHALL 记录超时日志，发送 WebSocket CLOSE frame 并关闭连接

#### Scenario: 超时前收到 answer
- **WHEN** 服务端在 5 秒内收到 `media-answer`
- **THEN** 超时计时 SHALL 被取消，按 `accepted` 值正常处理

### Requirement: 客户端根据协商结果初始化解码器

客户端 SHALL 根据 `media-offer` 中的 `codec` 字段初始化对应的解码器，替代当前通过 URL 参数指定解码器类型的方式。

#### Scenario: 根据 offer 初始化 H.264 解码器
- **WHEN** 客户端收到 `media-offer`，`streams` 中视频流 `codec` 为 `"h264"`
- **THEN** 客户端 SHALL 使用 H.264 解码器初始化，并回复 `media-answer` 接受

#### Scenario: 根据 offer 初始化 H.265 解码器
- **WHEN** 客户端收到 `media-offer`，`streams` 中视频流 `codec` 为 `"h265"`
- **THEN** 客户端 SHALL 使用 H.265 解码器初始化，并回复 `media-answer` 接受
