# cnetmod Documentation (English)

[中文](../zh/README.md) | **English**

---

Welcome to the cnetmod documentation! This comprehensive guide covers everything from basic concepts to advanced usage patterns.

## 📚 Getting Started

Start here if you're new to cnetmod:

1. **[Installation Guide](installation.md)** - Set up cnetmod on Windows, Linux, or macOS
2. **[Quick Start](getting-started.md)** - Your first cnetmod program in 5 minutes
3. **[Architecture Overview](architecture.md)** - Understand how cnetmod works
4. **[Examples Index](examples.md)** - Browse complete example applications

## 🎯 Core Concepts

Master the fundamentals:

- **[Coroutines](core/coroutines.md)** - `task<T>`, `spawn()`, async/await patterns
- **[I/O Context](core/io-context.md)** - Event loop, scheduling, and thread model
- **[Synchronization Primitives](core/sync-primitives.md)** - `mutex`, `channel`, `semaphore`, `wait_group`

## 🌐 Protocol Guides

Build networked applications:

- **[TCP/UDP](protocols/tcp-udp.md)** - Low-level socket programming
- **[HTTP Server](protocols/http.md)** - Build REST APIs and web services
- **[WebSocket](protocols/websocket.md)** - Real-time bidirectional communication
- **[MQTT](protocols/mqtt.md)** - IoT messaging (broker + client)
- **[MySQL](protocols/mysql.md)** - Database access with ORM
- **[Redis](protocols/redis.md)** - In-memory data store client

## 🚀 Advanced Topics

Optimize and scale:

- **[Coroutine Bridge](advanced/coroutine-bridge.md)** - Integrate with other frameworks
- **[Mutex Guide](advanced/mutex-guide.md)** - Thread-safe programming
- **[ORM Guide](advanced/orm-guide.md)** - Database ORM in depth

## 📖 Quick Reference

- [Installation](installation.md)
- [Quick Start](getting-started.md)
- [Architecture](architecture.md)
- [Examples](examples.md)

---

## Contributing

Found an error or want to improve the documentation? Please submit a pull request or open an issue on [GitHub](https://github.com/banderzhm/cnetmod).

## License

Documentation is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Code examples are licensed under MIT.
