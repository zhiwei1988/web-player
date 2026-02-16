## ADDED Requirements

### Requirement: TLS 加密传输层初始化
系统 SHALL 在启动时初始化 mbedtls TLS 上下文（`mbedtls_ssl_config`、`mbedtls_ctr_drbg_context`、`mbedtls_entropy_context`），配置为服务端模式（`MBEDTLS_SSL_IS_SERVER`）。

#### Scenario: 使用外部证书启动
- **WHEN** 用户通过 `--cert` 和 `--key` 参数指定 PEM 格式的证书和私钥文件路径
- **THEN** 系统 SHALL 加载指定的证书和私钥文件，初始化 TLS 上下文并成功启动

#### Scenario: 证书文件不存在
- **WHEN** 用户指定的 `--cert` 或 `--key` 文件路径不存在
- **THEN** 系统 SHALL 输出错误信息并退出，返回非零退出码

#### Scenario: 证书和密钥参数不配对
- **WHEN** 用户仅指定 `--cert` 而未指定 `--key`（或反之）
- **THEN** 系统 SHALL 输出错误信息并退出，返回非零退出码

### Requirement: 自签名证书自动生成
当未指定外部证书时，系统 SHALL 在启动时自动生成 RSA 2048 自签名证书用于 TLS 加密。

#### Scenario: 无证书参数启动
- **WHEN** 用户未指定 `--cert` 和 `--key` 参数
- **THEN** 系统 SHALL 自动生成 RSA 2048 密钥对和自签名 X.509 证书（有效期 1 年），并使用该证书初始化 TLS

#### Scenario: 自签名证书生成失败
- **WHEN** 自签名证书生成过程中发生错误（如熵源不可用）
- **THEN** 系统 SHALL 输出错误信息并退出，返回非零退出码

### Requirement: 每连接 TLS 会话管理
系统 SHALL 为每个 TCP 连接创建独立的 `mbedtls_ssl_context`，并在连接关闭时释放。

#### Scenario: 新连接建立
- **WHEN** TCP accept 接受一个新连接
- **THEN** 系统 SHALL 为该连接创建独立的 `mbedtls_ssl_context`，绑定底层 socket fd 作为 I/O 回调

#### Scenario: 连接关闭
- **WHEN** 连接断开（正常关闭或异常断开）
- **THEN** 系统 SHALL 调用 `mbedtls_ssl_close_notify` 通知对端，然后释放该连接的 `mbedtls_ssl_context`

### Requirement: Non-blocking TLS 握手
系统 SHALL 支持在 non-blocking 模式下完成 TLS 握手，与 epoll 事件循环集成。

#### Scenario: TLS 握手正常完成
- **WHEN** TCP 连接建立后开始 TLS 握手，且客户端正确响应
- **THEN** 系统 SHALL 在多次 epoll 事件回调中逐步完成 TLS 握手，握手成功后触发上层 `onConnect` 回调

#### Scenario: TLS 握手超时或失败
- **WHEN** TLS 握手过程中客户端无响应或发送非法数据
- **THEN** 系统 SHALL 关闭该 TCP 连接并释放相关资源

### Requirement: TLS 加密数据读写
系统 SHALL 使用 `mbedtls_ssl_read` 和 `mbedtls_ssl_write` 替代原始的 `recv`/`send` 进行数据传输。

#### Scenario: 加密数据发送
- **WHEN** 上层调用 SendData 发送数据
- **THEN** 系统 SHALL 通过 `mbedtls_ssl_write` 加密后发送，返回实际发送字节数

#### Scenario: 加密数据接收
- **WHEN** epoll 检测到 socket 可读事件
- **THEN** 系统 SHALL 通过 `mbedtls_ssl_read` 解密数据，然后触发上层 `onData` 回调
