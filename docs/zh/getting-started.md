# cnetmod 快速入门

本指南将帮助你快速上手 cnetmod。

## 前置条件

- **C++23 编译器**：MSVC 2022 17.12+、clang-21+ 或 GCC 14+（支持模块）
- **CMake 4.0+**：C++23 模块构建支持所需
- **平台特定依赖**：
  - Windows：Visual Studio 2022 with C++23 模块
  - Linux：liburing-dev（用于 io_uring 支持）
  - macOS：Homebrew LLVM

## 安装

### 1. 克隆仓库

```bash
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod
git submodule update --init --recursive
```

### 2. 平台特定设置

#### Windows (MSVC)

```bash
# 打开 Visual Studio 2022 Developer Command Prompt
cmake -B build -G "Visual Studio 17 2022" -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

#### Linux (clang + libc++)

```bash
# 安装依赖
sudo apt install clang-21 libc++-21-dev libc++abi-21-dev liburing-dev

# 构建
cmake -B build -DCMAKE_CXX_COMPILER=clang++-21 -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build
```

#### macOS (Homebrew LLVM)

```bash
# 安装依赖
brew install llvm ninja cmake

# 设置环境
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"  # Apple Silicon
# export PATH="/usr/local/opt/llvm/bin:$PATH"   # Intel Mac

# 构建
cmake -B build -G Ninja -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build
```

## 你的第一个程序

### Echo 服务器

创建一个简单的 TCP echo 服务器：

```cpp
import cnetmod;
using namespace cnetmod;

task<void> handle_client(tcp_socket client) {
    char buffer[1024];
    while (true) {
        int n = co_await client.async_recv(buffer, sizeof(buffer));
        if (n <= 0) break;
        co_await client.async_send(buffer, n);
    }
}

task<void> server(io_context& ctx) {
    tcp_acceptor acceptor(ctx, 8080);
    std::println("Server listening on port 8080");
    
    while (true) {
        auto [client, addr] = co_await acceptor.async_accept();
        std::println("Client connected: {}", addr.to_string());
        spawn(ctx, handle_client(std::move(client)));
    }
}

int main() {
    net_init guard;  // 初始化网络（Windows 上的 WSAStartup）
    io_context ctx;
    spawn(ctx, server(ctx));
    ctx.run();
}
```

### HTTP 服务器

创建一个带路由的简单 HTTP 服务器：

```cpp
import cnetmod;
import cnetmod.protocol.http;
using namespace cnetmod;

int main() {
    net_init guard;
    io_context ctx;
    
    http::server srv(ctx);
    
    // 添加路由
    srv.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("Hello, World!");
        co_return;
    });
    
    srv.get("/api/users/:id", [](http::request_context& ctx) -> task<void> {
        auto id = ctx.param("id");
        ctx.resp().json({{"id", id}, {"name", "User " + std::string(id)}});
        co_return;
    });
    
    srv.listen(8080);
    std::println("HTTP server listening on http://localhost:8080");
    ctx.run();
}
```

### 定时器示例

使用异步定时器：

```cpp
import cnetmod;
using namespace cnetmod;

task<void> delayed_task(io_context& ctx) {
    std::println("Starting task...");
    co_await async_sleep(ctx, 2s);
    std::println("2 seconds elapsed!");
}

int main() {
    io_context ctx;
    spawn(ctx, delayed_task(ctx));
    ctx.run();
}
```

## 下一步

- 阅读[架构概述](architecture.md)了解 cnetmod 的工作原理
- 探索[核心概念](core/coroutines.md)学习协程和 I/O
- 查看[示例](examples.md)获取更完整的应用程序
- 学习 [HTTP 中间件](middleware/http-middleware.md)构建 Web 服务

## 常见问题

### 模块路径检测失败

如果 CMake 无法找到标准库模块，手动指定路径：

```bash
# Linux/macOS
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/usr/lib/llvm-21/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/usr/lib/llvm-21/include/c++/v1

# Windows
cmake -B build \
  -DLIBCXX_MODULE_DIRS="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/modules"
```

### MSVC 错误 C1605（目标文件过大）

参见 [MSVC_C1605_Issue.md](../MSVC_C1605_Issue.md) 了解解决方法。使用 Release 构建或拆分大型模块。

### Linux 上的链接错误

确保使用 libc++（而不是 libstdc++）：

```bash
cmake -B build -DCMAKE_CXX_FLAGS="-stdlib=libc++"
```

## 获取帮助

- **GitHub Issues**：报告错误或提问
- **示例**：查看 `examples/` 目录中的可运行代码
- **文档**：浏览 `docs/` 中的完整文档
