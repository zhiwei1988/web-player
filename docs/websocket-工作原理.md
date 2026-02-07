# WebSocket 工作原理详解

## 类比理解

这个文件就像一个**翻译员**，负责处理 WebSocket 协议的"握手"和"对话"：

- **握手阶段**：像两个人见面时的握手礼仪，客户端说"你好"（HTTP 升级请求），服务器回应"你好"（101 切换协议）
- **对话阶段**：像发送加密信件，每条消息都装在特定格式的"信封"里（WebSocket 帧），有些信件还会被"密封"（mask）

## 架构图

```
客户端 WebSocket 连接流程
════════════════════════════════════════════════════

1. HTTP 握手阶段
   ┌─────────────┐                    ┌──────────────┐
   │   客户端     │──GET /upgrade──▶   │   服务器      │
   │             │   Sec-WebSocket-   │              │
   │             │   Key: xxx         │              │
   └─────────────┘                    └──────────────┘
                  ◀──101 Switching──
                     Sec-WebSocket-
                     Accept: yyy

2. 数据传输阶段
   ┌─────────────┐                    ┌──────────────┐
   │   客户端     │──WebSocket Frame─▶ │   服务器      │
   │             │  [FIN|Opcode|Mask] │              │
   │             │  [Payload Length]  │              │
   │             │  [Mask Key]        │              │
   │             │  [Payload Data]    │              │
   └─────────────┘                    └──────────────┘
```

## 代码分段讲解

### 1. 握手处理（`HandleHandshake`）

```
步骤流程：
HTTP 请求 → 提取 Sec-WebSocket-Key → 计算 Accept Key → 返回 101 响应

详细步骤：
┌─────────────────────────────────────────┐
│ 1. 查找 "Sec-WebSocket-Key:" 头部        │
│    ↓                                    │
│ 2. 提取 key 值（去掉前后空格）           │
│    ↓                                    │
│ 3. 计算 Accept Key:                     │
│    key + GUID → SHA1 → Base64           │
│    ↓                                    │
│ 4. 构造 HTTP 101 响应                    │
└─────────────────────────────────────────┘
```

**关键点**：
- `WS_GUID` 是 WebSocket 协议规定的魔术字符串
- Accept Key = Base64(SHA1(key + GUID))
- 这个计算保证了握手的安全性

### 2. 帧解析（`ParseFrame`）

WebSocket 帧结构像俄罗斯套娃，一层层拆开：

```
WebSocket 帧格式（客户端→服务器）
════════════════════════════════════════

Byte 0:  [FIN(1bit)][RSV(3bits)][Opcode(4bits)]
         ↓
         FIN=1: 最后一帧
         Opcode: 0x1=文本, 0x2=二进制, 0x8=关闭, 0x9=PING, 0xA=PONG

Byte 1:  [Mask(1bit)][Payload Len(7bits)]
         ↓
         Mask=1: 客户端必须设置
         Len=126: 后续2字节表示长度
         Len=127: 后续8字节表示长度

可选:    [Extended Payload Length] (2 or 8 bytes)
可选:    [Masking Key] (4 bytes)
         [Payload Data] (实际数据)
```

**解析流程**：

```cpp
// src/server/websocket.cpp:83-140

if (len < 2) return false;  // 至少需要2字节

// 第1字节：FIN + Opcode
frame.fin = (data[0] & 0x80) != 0;      // 最高位
frame.opcode = data[0] & 0x0F;          // 低4位

// 第2字节：Mask + 初始长度
frame.masked = (data[1] & 0x80) != 0;   // 最高位
payloadLen = data[1] & 0x7F;            // 低7位

// 根据长度值读取实际长度
if (payloadLen == 126) {
    // 接下来2字节是长度（大端序）
} else if (payloadLen == 127) {
    // 接下来8字节是长度（大端序）
}

// 如果有 mask，读取4字节 mask key
// 读取 payload 数据
// 用 mask key 解密数据（XOR 操作）
```

### 3. 帧编码（`EncodeFrame`）

服务器发送数据时打包成帧：

```
编码流程：
┌─────────────────────────────────────┐
│ 输入：Opcode + Payload 数据          │
│   ↓                                 │
│ 1. 写入 FIN + Opcode (0x80 | op)    │
│   ↓                                 │
│ 2. 写入长度：                        │
│    < 126: 直接写                     │
│    ≤ 65535: 写126 + 2字节长度        │
│    > 65535: 写127 + 8字节长度        │
│   ↓                                 │
│ 3. 写入原始 payload（服务器不mask）  │
└─────────────────────────────────────┘
```

### 4. 加密算法

**SHA1 + Base64**：

```
握手 Accept Key 计算过程：
═════════════════════════

输入: "dGhlIHNhbXBsZSBub25jZQ=="  (客户端 key)
  ↓
+ "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  (GUID)
  ↓
= "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
  ↓
SHA1 → 20字节二进制 hash
  ↓
Base64 → "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="  (返回给客户端)
```

## 常见陷阱

### 陷阱1：字节序问题
```cpp
// 错误：直接转换可能字节序错误
payloadLen = *(uint16_t*)(data + 2);  // ❌

// 正确：手动组装大端序
payloadLen = (data[2] << 8) | data[3];  // ✅
```

### 陷阱2：mask 解密遗漏
```cpp
// 客户端发送的数据必须 mask，如果忘记解密：
frame.payload[i];  // ❌ 得到加密后的乱码

// 正确：
frame.payload[i] ^= frame.maskKey[i % 4];  // ✅
```

### 陷阱3：长度判断不完整
```cpp
// 错误：只检查前2字节
if (len < 2) return false;
payloadLen = data[1] & 0x7F;
// 如果 payloadLen == 126，后续访问 data[2] 会越界！

// 正确：分段检查
if (payloadLen == 126 && len < 4) return false;
```

## 总结

这个文件实现了 WebSocket 协议的核心功能：

1. **握手阶段**：通过 SHA1+Base64 验证客户端
2. **数据接收**：解析客户端发来的 mask 加密帧
3. **数据发送**：编码服务器响应（无 mask）
4. **控制帧**：处理 PING/PONG、CLOSE 等控制消息

关键设计：
- 使用 mbedtls 库处理 SHA1 和 Base64
- 严格遵循 RFC 6455 WebSocket 协议规范
- 小心处理字节序（大端序）和边界检查
