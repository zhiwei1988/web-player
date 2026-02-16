## ADDED Requirements

### Requirement: WebSocket over TLS 协议栈顺序
系统 SHALL 按照 TCP → TLS → WebSocket 的顺序建立连接，TLS 握手 MUST 在 WebSocket 握手之前完成。

#### Scenario: 正常连接流程
- **WHEN** 客户端通过 `wss://` 发起连接
- **THEN** 系统 SHALL 先完成 TCP accept，然后完成 TLS 握手，最后处理 WebSocket HTTP 升级握手

#### Scenario: TLS 握手未完成时收到 WebSocket 数据
- **WHEN** TLS 握手尚未完成时 socket 上有可读数据
- **THEN** 系统 SHALL 将数据作为 TLS 握手数据处理，不得传递给 WebSocket 层

### Requirement: 连接状态机
系统 SHALL 维护三阶段连接状态：`HANDSHAKING_TLS` → `HANDSHAKING_WS` → `CONNECTED`。

#### Scenario: 状态转换 - TLS 握手完成
- **WHEN** TLS 握手成功完成
- **THEN** 连接状态 SHALL 从 `HANDSHAKING_TLS` 转为 `HANDSHAKING_WS`

#### Scenario: 状态转换 - WebSocket 握手完成
- **WHEN** WebSocket HTTP 升级握手成功完成
- **THEN** 连接状态 SHALL 从 `HANDSHAKING_WS` 转为 `CONNECTED`

### Requirement: 客户端使用 WSS 连接
客户端 SHALL 使用 `wss://` 协议连接服务端。

#### Scenario: 客户端 WebSocket URL
- **WHEN** 客户端初始化 WebSocket 连接
- **THEN** SHALL 使用 `wss://<host>:<port>` 格式的 URL

### Requirement: CMake 构建配置更新
构建系统 SHALL 链接完整的 mbedtls TLS 栈（mbedtls、mbedx509、mbedcrypto）。

#### Scenario: 编译链接
- **WHEN** 使用 CMake 构建 video_server
- **THEN** SHALL 成功链接 mbedtls、mbedx509、mbedcrypto 库，编译无错误
