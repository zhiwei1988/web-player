## Why

当前 WebSocket 服务端以明文(ws://)传输视频数据，存在安全隐患，且现代浏览器在混合内容场景下会拒绝非加密连接。添加 WSS 支持可实现加密传输，提升浏览器兼容性。

## What Changes

- **BREAKING**: 用 wss:// 替换 ws://，不再支持明文 WebSocket 连接
- 基于 mbedtls（项目已有依赖）在现有 TCP socket 之上增加 TLS 加密层
- 支持通过命令行参数指定外部 TLS 证书/密钥文件路径
- 支持生成自签名证书用于开发测试
- 客户端 WebSocket 连接 URL 从 `ws://` 改为 `wss://`

## Capabilities

### New Capabilities
- `tls-transport`: 基于 mbedtls 的 TLS 加密传输层，包括证书加载和自签名证书生成
- `wss-protocol`: WebSocket Secure 协议 - 在 TLS 握手完成后进行 WebSocket 升级

### Modified Capabilities
（无 - 当前没有已有 specs）

## Impact

- **服务端代码**: `tcp_server.cpp/h` - TCP 读写替换为 TLS 加密读写; `websocket.cpp/h` - 基于 TLS 流进行握手
- **客户端代码**: `src/js/` - WebSocket URL scheme 从 `ws://` 改为 `wss://`
- **构建**: CMakeLists.txt - 扩展 mbedtls 链接（mbedtls, mbedx509, mbedcrypto）
- **依赖**: 完整 mbedtls TLS 栈（当前仅使用 SHA1/Base64 模块）
- **部署**: 生产环境需要证书文件(.pem/.crt/.key)；开发环境可选（自签名）
