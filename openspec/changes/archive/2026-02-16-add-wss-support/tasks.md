## 1. CMake 构建配置

- [x] 1.1 修改 `src/server/CMakeLists.txt`，将 mbedtls 链接扩展为 mbedtls、mbedx509、mbedcrypto 三个库

## 2. TLS 上下文与证书管理

- [x] 2.1 新建 `src/server/tls_context.h/cpp`，封装 mbedtls TLS 全局上下文（`mbedtls_ssl_config`、`mbedtls_ctr_drbg_context`、`mbedtls_entropy_context`、证书/密钥）的初始化和销毁
- [x] 2.2 实现从 PEM 文件加载证书和私钥的功能，包含文件不存在和格式错误的错误处理
- [x] 2.3 实现 RSA 2048 自签名证书动态生成功能（使用 `mbedtls_x509write_cert` API）

## 3. TlsServer 核心实现

- [x] 3.1 新建 `src/server/tls_server.h/cpp`，定义 `TlsServer` 类，内部持有 `TcpServer`，对外暴露相同的回调接口（`TcpCallbacks`）
- [x] 3.2 实现每连接 `mbedtls_ssl_context` 的创建、存储（`unordered_map<int32_t, ssl_context>`）和销毁
- [x] 3.3 实现 mbedtls I/O 回调函数，将 `mbedtls_ssl_read`/`mbedtls_ssl_write` 绑定到底层 socket fd
- [x] 3.4 实现 non-blocking TLS 握手逻辑：在 `onConnect` 回调中启动握手，处理 `WANT_READ`/`WANT_WRITE`，握手完成后触发上层 `onConnect`
- [x] 3.5 实现 `SendData` 方法，使用 `mbedtls_ssl_write` 加密发送
- [x] 3.6 实现数据接收逻辑，在 `onData` 回调中使用 `mbedtls_ssl_read` 解密后传递给上层
- [x] 3.7 实现连接关闭逻辑，调用 `mbedtls_ssl_close_notify` 后释放资源

## 4. 连接状态机更新

- [x] 4.1 修改 `src/server/connection.h`，在 `ConnState` 枚举中新增 `HANDSHAKING_TLS` 状态，原 `HANDSHAKING` 改名为 `HANDSHAKING_WS`

## 5. VideoServer 集成

- [x] 5.1 修改 `src/server/main.cpp`，将 `TcpServer` 替换为 `TlsServer`
- [x] 5.2 新增 `--cert` 和 `--key` 命令行参数解析，增加配对校验逻辑
- [x] 5.3 更新 `PrintUsage` 帮助信息，包含新增的 TLS 参数说明

## 6. 客户端修改

- [x] 6.1 修改客户端 JS 代码中 WebSocket 连接 URL 的 scheme，从 `ws://` 改为 `wss://`
