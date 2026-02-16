## Context

当前服务端架构：`TcpServer`(epoll) → `WebSocket`(协议处理) → `VideoServer`(业务逻辑)。TCP 层直接使用 `recv`/`send` 进行明文读写。项目已依赖 mbedtls（仅用 SHA1/Base64），需扩展为完整 TLS 栈。

## Goals / Non-Goals

**Goals:**
- 在 TCP 连接建立后、WebSocket 握手前，插入 TLS 加密层
- 支持命令行指定证书/密钥文件路径（`--cert`, `--key`）
- 支持运行时自动生成自签名证书用于开发测试
- 客户端自动使用 `wss://` 连接

**Non-Goals:**
- 不支持双向 TLS（客户端证书验证）
- 不支持 ws:// 和 wss:// 共存
- 不支持 TLS 会话恢复/缓存
- 不支持在线证书更新（需重启服务）

## Decisions

### 1. TLS 集成层级：新增 TlsServer 包装 TcpServer

**方案 A（选定）**: 新增 `TlsServer` 类，内部持有 `TcpServer`，拦截读写操作替换为 TLS 加密版本。对外暴露与 `TcpServer` 相同的回调接口，`VideoServer` 将 `TcpServer` 替换为 `TlsServer`。

**方案 B（放弃）**: 直接修改 `TcpServer`，在 `recv`/`send` 处加入 TLS 逻辑。侵入性强，破坏单一职责。

**理由**: `TlsServer` 作为独立层，职责清晰，`TcpServer` 的 TCP/epoll 逻辑无需变动。

### 2. TLS 握手时机：accept 后立即握手

TCP accept 成功后，立即进行 TLS 握手（`mbedtls_ssl_handshake`）。握手完成后再触发 `onConnect` 回调。

**注意**: mbedtls 的 TLS 握手在 non-blocking 模式下会返回 `MBEDTLS_ERR_SSL_WANT_READ/WRITE`，需要在 epoll 事件循环中多次调用直到完成。`TlsServer` 需要维护每个连接的握手状态。

### 3. 数据读写替换

- 读：`recv(fd, ...)` → `mbedtls_ssl_read(ssl, ...)`
- 写：`send(fd, ...)` → `mbedtls_ssl_write(ssl, ...)`
- mbedtls 的 I/O 回调绑定到底层 fd 的 `recv`/`send`

每个连接需要独立的 `mbedtls_ssl_context`，在 `TlsServer` 中用 `std::unordered_map<int32_t, mbedtls_ssl_context>` 管理。

### 4. 自签名证书生成

使用 mbedtls 的 `mbedtls_x509write_cert` API 在启动时动态生成：
- RSA 2048 密钥对
- 自签名 X.509 证书（有效期 1 年）
- 仅当未指定外部证书时自动生成

### 5. 命令行参数

新增参数：
- `--cert <path>`: 证书文件路径（PEM 格式）
- `--key <path>`: 私钥文件路径（PEM 格式）
- 两者必须同时指定或同时不指定

### 6. 客户端修改

`src/js/` 中 WebSocket URL 的 scheme 从 `ws://` 改为 `wss://`。由于使用自签名证书，浏览器会拒绝连接，需要用户先在浏览器中访问 `https://` 地址信任证书，或使用正式证书。

## Risks / Trade-offs

- **[性能]** TLS 加解密增加 CPU 开销 → 视频流带宽相对较低（通常 < 10Mbps），影响可忽略
- **[non-blocking 握手复杂度]** TLS 握手需要多次 epoll 事件 → 需维护连接状态机（HANDSHAKING_TLS → HANDSHAKING_WS → CONNECTED）
- **[自签名证书]** 浏览器默认不信任 → 开发环境需手动信任证书，生产环境应使用正式证书
- **[mbedtls 版本兼容]** 不同版本 API 可能有差异 → 使用系统 pkg-config 提供的版本，保持与现有代码一致
