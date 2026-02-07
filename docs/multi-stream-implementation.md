# 多流视频播放器实现

## 概述

本文档描述了 WebSocket 视频播放器的多流视频解码和 UI 重构实现。

## 已完成工作

### 第一阶段：模块提取 ✅
- 将 `DataBufferQueue` 提取为独立的 TypeScript 模块 (`src/js/buffer/DataBufferQueue.ts`)
- 为接收的数据包提供环形缓冲区管理
- 类型安全的实现，包含完整的统计跟踪

### 第二阶段：流管理架构 ✅

#### StreamInstance (`src/js/stream/StreamInstance.ts`)
封装单个视频流的完整生命周期：
- WebSocket 连接管理
- 数据缓冲区队列
- 消费者定时器（以约 30fps 解码）
- 基于 Worker 的解码器
- Canvas 渲染
- 统计跟踪

特性：
- 每个流独立的 WebSocket 连接
- 自动重连处理
- 实时统计更新
- 错误处理和报告
- 状态变更通知

#### StreamManager (`src/js/stream/StreamManager.ts`)
管理多个流实例：
- 创建/销毁流实例
- 按 ID 查询流
- 获取所有已连接的流
- 批量操作（销毁所有）

### 第三阶段：UI 组件 ✅

#### StreamCard (`src/js/ui/StreamCard.ts`)
单个流卡片组件，包含：
- 用于视频渲染的 Canvas
- WebSocket URL 和编解码器配置
- 连接/断开控制
- 状态指示器（已连接/已断开/错误）
- 可折叠的统计面板
- 删除按钮

显示的统计信息：
- 数据速率 (KB/s)
- 消息计数
- 接收字节数
- 缓冲区队列状态
- 解码器 FPS
- 视频分辨率

#### GridLayout (`src/js/ui/GridLayout.ts`)
响应式网格布局管理器：
- 支持 1x1、2x2、3x3、4x4 模式
- 移动端/平板/桌面端的响应式断点
- 动态卡片添加/移除
- 使用 CSS Grid 和 Tailwind 类

### 第四阶段：应用重构 ✅

#### index.html (新的多流版本)
- 使用 Tailwind CSS 的现代 UI
- 带标题和网格模式选择器的头部
- 带"添加流"和"移除所有"按钮的工具栏
- 流计数器显示
- 无流时的空状态
- 完全响应式设计

#### app-multi.js
主应用逻辑：
- 初始化 StreamManager 和 GridLayout
- 处理流的创建和移除
- 连接事件处理器
- 更新 UI 状态

#### index-single.html (保留原始版本)
- 保留原始单流版本作为备份
- 使用原始的 `app.js` 和 `decoder-init.js`

### 第五阶段：构建系统 ✅
- 所有 TypeScript 代码编译成功
- 生成的 JavaScript 模块位于 `dist/js/js/`
- 正确的 ES 模块导入/导出
- 生成类型定义和源映射

## 使用方法

### 多流版本（新）
```
http://192.168.50.101:8888/index.html
```

### 单流版本（原始）
```
http://192.168.50.101:8888/index-single.html
```

## 功能特性

### 多流能力
1. **动态流管理**
   - 按需添加流
   - 移除单个流
   - 批量移除所有流

2. **独立配置**
   - 每个流有自己的 WebSocket URL
   - 每个流可选择编解码器（H.264/H.265）
   - 独立的连接/断开控制

3. **灵活布局**
   - 1x1：单个全宽流
   - 2x2：桌面端 2 列，移动端 1 列
   - 3x3：大屏幕 3 列
   - 4x4：超大屏幕 4 列

4. **丰富的统计信息**
   - 每个流的连接统计
   - 缓冲区队列指标
   - 解码器性能
   - 可折叠面板以节省空间

5. **响应式设计**
   - 移动优先方法
   - 自动布局适配
   - 触摸友好的控制

## 架构

```
主线程：
┌────────────────────────────────────────────────────────────┐
│  StreamManager                                             │
│  ├─ Stream[0]: WebSocket + Queue + Consumer → Worker[0]   │
│  ├─ Stream[1]: WebSocket + Queue + Consumer → Worker[1]   │
│  └─ Stream[2]: WebSocket + Queue + Consumer → Worker[2]   │
└────────────────────────────────────────────────────────────┘
                              ↓ postMessage
Worker 线程：
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ WASM H.264   │ │ WASM H.265   │ │ WASM H.264   │
└──────────────┘ └──────────────┘ └──────────────┘
```

### 关键设计决策

1. **主线程中的 WebSocket**
   - WebSocket API 在 Worker 中不可用
   - 主线程处理 WS 连接并接收数据
   - 数据传输到 Worker 进行解码

2. **独立队列**
   - 每个流有自己的 DataBufferQueue
   - 防止一个慢流阻塞其他流
   - 隔离的溢出处理

3. **独立的 Worker**
   - 每个流获得专用的 Worker + WASM 实例
   - 真正的并行解码
   - 流之间无共享状态

4. **消费者定时器模式**
   - 固定速率消费（约 30fps）
   - 无论网络抖动如何都能平滑播放
   - 自动队列管理

## 内存占用

每个流的估算：
- WebSocket 连接：~1KB
- 缓冲区队列（100 个数据包 × 10KB）：~1MB
- WASM 实例：~30-50MB
- **每个流总计：~35-55MB**

4 个流：**~140-220MB**（现代浏览器可接受）

## 文件结构

```
src/js/
├── buffer/
│   └── DataBufferQueue.ts      # 环形缓冲区队列
├── stream/
│   ├── StreamInstance.ts       # 单流管理
│   └── StreamManager.ts        # 多流管理器
├── ui/
│   ├── StreamCard.ts          # 流卡片组件
│   └── GridLayout.ts          # 网格布局管理器
├── decoder/
│   ├── WorkerBridge.ts        # (现有) 主线程 ↔ Worker 桥接
│   ├── DecoderWrapper.ts      # (现有) WASM 包装器
│   └── types.ts               # (现有) 类型定义
├── app-multi.js               # 多流应用
├── app.js                     # (现有) 单流应用
└── decoder-init.js            # (现有) 解码器初始化

index.html                     # 多流 UI
index-single.html              # 原始单流 UI
```

## 优势

### 隔离性
- 每个流完全独立
- 一个流的故障不影响其他流
- 易于调试单个流

### 可扩展性
- 线性资源使用
- 动态添加/移除流
- 无硬编码流限制

### 可维护性
- 清晰的关注点分离
- 类型安全的 TypeScript 代码
- 模块化架构
- 易于扩展

### 用户体验
- 现代、简洁的界面
- 直观的控制
- 实时反馈
- 在所有设备上响应式

## 未来增强

可能的改进：
1. 流预设/配置文件
2. 持久化流配置
3. 流录制/快照
4. 音频支持
5. 性能分析仪表板
6. 键盘快捷键
7. 拖放流重新排序
8. 流分组/标签

## 测试

验证实现：

1. 在浏览器中打开多流页面
2. 点击"添加流"添加 2-4 个流
3. 配置不同的 WebSocket URL 或编解码器
4. 在每个流上点击"连接"
5. 验证独立播放
6. 切换网格布局（1x1 → 2x2 → 3x3）
7. 测试单个流的断开/移除
8. 检查每个流的统计面板
9. 在移动设备上测试响应式布局

## 构建命令

```bash
npm run build:ts       # 编译 TypeScript
npm run build:all      # 构建所有内容（FFmpeg + WASM + TS）
npm run clean          # 清理构建产物
```

## 依赖项

- TypeScript 5.0+
- Tailwind CSS（通过 CDN）
- FFmpeg WASM（自定义构建）
- 支持 WebWorker 的现代浏览器

## 浏览器兼容性

- Chrome 90+
- Firefox 88+
- Safari 14+
- Edge 90+

要求：
- ES2020 支持
- Web Workers
- WebAssembly
- WebSocket
- Canvas API
