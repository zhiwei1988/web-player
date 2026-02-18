## Context

当前 WebSocket 连接建立后，服务端立即按定时器节奏发送视频帧（`OnTimer` 中遍历所有 `CONNECTED` 状态的连接）。编码格式由服务端启动参数决定，客户端通过 URL 参数独立指定解码器类型，两端无任何运行时信息交换。

连接状态机: `HANDSHAKING_TLS → HANDSHAKING_WS → CONNECTED → CLOSING`

服务端使用 WebSocket TEXT frame 接收文本消息（当前仅打印日志），BINARY frame 发送视频数据。这为在 TEXT 通道上叠加 JSON 信令提供了天然基础。

## Goals / Non-Goals

**Goals:**
- WebSocket 连接建立后，在视频数据传输前完成媒体能力协商
- 服务端通过 JSON 消息告知客户端自身媒体流参数（编码格式、帧率等）
- 客户端根据协商结果自动初始化正确的解码器，无需 URL 参数手动指定
- 协商失败时给出明确错误信息

**Non-Goals:**
- 不支持运行时动态切换编码格式（协商仅在连接建立时进行一次）
- 不实现音频流的实际传输，仅在协议中预留音频描述字段
- 不实现能力协商（客户端告知服务端自己支持什么、服务端据此选择格式），仅做服务端单向通告 + 客户端确认
- 不考虑旧版本客户端兼容

## Decisions

### 1. 协商信道：复用 WebSocket TEXT frame

**选择**: 在现有 WebSocket 连接上使用 TEXT frame 传输 JSON 协商消息，BINARY frame 继续用于视频帧数据。

**备选方案**:
- 独立的 HTTP 接口协商 → 增加复杂度，需要额外关联 HTTP 会话和 WebSocket 连接
- WebSocket 子协议（Sec-WebSocket-Protocol）→ 只能传递简单字符串，无法携带详细媒体参数

**理由**: 服务端已有 TEXT frame 的接收逻辑框架（`HandleWebSocketFrame` 中 `WsOpcode::TEXT` 分支），改造最小。TEXT/BINARY 二分天然区分了信令和数据通道。

### 2. 协商流程：服务端主导的 Offer/Answer 模型

**选择**: 两步协商
1. 服务端 → 客户端: `media-offer`（携带媒体流描述）
2. 客户端 → 服务端: `media-answer`（确认接受或拒绝）

**备选方案**:
- 客户端主动请求 → 服务端被动，需要额外的请求消息类型
- 三步握手（offer → answer → ack）→ 增加延迟，当前场景无必要

**理由**: 服务端掌握媒体源信息，由服务端主动发送 offer 最自然。两步即可完成，简单高效。

### 3. 连接状态机扩展

**选择**: 在 `CONNECTED` 和数据发送之间新增 `NEGOTIATING` 状态

```
HANDSHAKING_TLS → HANDSHAKING_WS → CONNECTED → NEGOTIATING → STREAMING → CLOSING
```

- `CONNECTED`: WebSocket 握手完成，服务端立即发送 `media-offer`，进入 `NEGOTIATING`
- `NEGOTIATING`: 等待客户端 `media-answer`，定时器跳过此状态的连接
- `STREAMING`: 协商成功，定时器开始发送视频帧（等价于原来的 `CONNECTED` 行为）

**理由**: `OnTimer` 中检查 `conn.state == ConnState::STREAMING` 替代原来的 `ConnState::CONNECTED`，改动最小且语义清晰。

### 4. JSON 消息格式

所有协商消息共享统一信封：

```json
{
  "type": "<message-type>",
  "payload": { ... }
}
```

**media-offer** (服务端 → 客户端):
```json
{
  "type": "media-offer",
  "payload": {
    "version": 1,
    "streams": [
      {
        "type": "video",
        "codec": "h264",
        "framerate": 25.0
      }
    ]
  }
}
```

**media-answer** (客户端 → 服务端):
```json
{
  "type": "media-answer",
  "payload": {
    "accepted": true
  }
}
```

或拒绝:
```json
{
  "type": "media-answer",
  "payload": {
    "accepted": false,
    "reason": "unsupported codec"
  }
}
```

**理由**: 统一信封便于消息分发。`streams` 数组设计为未来音频流预留扩展位。`version` 字段用于协议版本管理。

### 5. 超时处理

**选择**: 服务端发送 offer 后启动 5 秒超时计时器。超时未收到 answer 则发送 WebSocket CLOSE frame 断开连接。

**理由**: 防止客户端连接后不响应导致连接资源泄漏。5 秒对于协商足够宽裕。

### 6. 服务端 JSON 解析

**选择**: 手写轻量 JSON 解析，仅支持协商消息所需的有限 JSON 子集。

**备选方案**:
- 引入 nlohmann/json 或 rapidjson → 增加外部依赖，项目当前无第三方 C++ 库依赖
- cJSON → C 风格，与项目 C++ 风格不一致

**理由**: 协商消息结构固定且简单（仅需解析 `type` 和 `payload.accepted`），手写解析代码量小，避免引入外部依赖。

## Risks / Trade-offs

- **[破坏性变更]** 旧客户端无法与新服务端通信 → 属于预期行为，项目处于早期阶段，无需兼容
- **[手写 JSON 解析]** 可能存在边界情况处理不全 → 协商消息格式固定，风险可控；未来如需复杂 JSON 操作再引入库
- **[单向通告模式]** 服务端不关心客户端能力，可能推送客户端不支持的编码 → 客户端通过 `accepted: false` 拒绝并在 `reason` 中说明原因，用户可据此调整服务端配置
