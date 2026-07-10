# cnetmod 文档（中文）

**中文** | [English](../en/README.md)

---

欢迎阅读 cnetmod 文档！本指南涵盖从基础概念到高级使用模式的所有内容。

## 📚 入门指南

如果你是 cnetmod 新手，从这里开始：

1. **[安装指南](installation.md)** - 在 Windows、Linux 或 macOS 上设置 cnetmod
2. **[快速开始](getting-started.md)** - 5 分钟编写你的第一个 cnetmod 程序
3. **[架构概览](architecture.md)** - 理解 cnetmod 的工作原理
4. **[示例索引](examples.md)** - 浏览完整的示例应用程序

## 🎯 核心概念

掌握基础知识：

- **[协程](core/coroutines.md)** - `task<T>`、`spawn()`、async/await 模式
- **[I/O 上下文](core/io-context.md)** - 事件循环、调度和线程模型
- **[同步原语](core/sync-primitives.md)** - `mutex`、`channel`、`semaphore`、`wait_group`

## 🌐 协议指南

构建网络应用程序：

- **[TCP/UDP](protocols/tcp-udp.md)** - 底层套接字编程
- **[HTTP 服务器](protocols/http.md)** - 构建 REST API 和 Web 服务
- **[WebSocket](protocols/websocket.md)** - 实时双向通信
- **[MQTT](protocols/mqtt.md)** - IoT 消息传递（broker + client）
- **[CoAP](protocols/coap.md)** - 面向受限设备的 UDP CoAP 客户端/服务端
- **[MySQL](protocols/mysql.md)** - 带 ORM 的数据库访问
- **[Redis](protocols/redis.md)** - 内存数据存储客户端
- **[Raft](protocols/raft.md)** - 复制状态机、成员变更、快照、TCP transport 和存储示例

## 🚀 高级主题

优化和扩展：

- **[协程桥接](advanced/coroutine-bridge.md)** - 与其他框架集成
- **[互斥锁指南](advanced/mutex-guide.md)** - 线程安全编程
- **[ORM 指南](advanced/orm-guide.md)** - 数据库 ORM 深入
- **[第三方依赖集成](advanced/thirdparty-dependency-integration.md)** - 嵌入 cnetmod 时复用宿主已有依赖 target

## 📖 快速参考

- [安装](installation.md)
- [快速开始](getting-started.md)
- [架构](architecture.md)
- [示例](examples.md)
- [Raft](protocols/raft.md)
- [CoAP](protocols/coap.md)
- [第三方依赖集成](advanced/thirdparty-dependency-integration.md)

---

## 贡献

发现错误或想改进文档？请在 [GitHub](https://github.com/banderzhm/cnetmod) 提交 pull request 或 issue。

## 许可证

文档采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) 许可。代码示例采用 MIT 许可。

