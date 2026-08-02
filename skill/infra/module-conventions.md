# C++23 模块规范

> cnetmod 项目中 `.cppm` 模块接口与 `.cpp` 实现文件的编写约定。

## 文件结构

每个模块由两部分组成：

| 文件 | 后缀 | 作用 |
|------|------|------|
| 模块接口 | `.cppm` | 声明 `export module`，导出公开 API |
| 模块实现 | `.cpp` | 以 `module cnetmod.xxx;`（无 `export`）开始，提供具体定义 |

实现文件命名惯例：`<name>_impl.cpp` 或 `<name>.cpp`，放在对应 `.cppm` 同目录下。

## 模块声明语法

### 基本模块

```cpp
export module cnetmod.core.buffer;
```

命名层次为 `cnetmod.<层级>.<模块名>`，层级包括 `core`、`coro`、`io`、`executor`、`protocol` 等。

### 协议内部分区

协议模块可使用冒号分区组织子功能：

```cpp
export module cnetmod.protocol.http:server;
```

### 聚合模块

聚合模块通过 `export import` 将多个子模块组合为一个入口，用户只需一次 import 即可获得全部功能。

以 `src/core.cppm` 为例：

```cpp
export module cnetmod.core;

export import cnetmod.core.error;
export import cnetmod.core.buffer;
export import cnetmod.core.buffer_pool;
export import cnetmod.core.address;
export import cnetmod.core.socket;
export import cnetmod.core.net_init;
export import cnetmod.core.file;
export import cnetmod.core.serial_port;
export import cnetmod.core.log;
export import cnetmod.core.dns;
export import cnetmod.core.crash_dump;
```

当前项目中的聚合模块：

| 聚合模块 | 子模块数量 | 说明 |
|----------|-----------|------|
| `cnetmod.core` | 11 | 错误码、缓冲区、地址、套接字、日志等 |
| `cnetmod.coro` | 13 | task、spawn、timer、channel、mutex 等 |
| `cnetmod.executor` | 3 | async_op、scheduler、pool |
| `cnetmod.io` | 2 | io_context、io_operation |

## `import std;` 规则

**所有模块接口和实现文件中，标准库一律使用 `import std;`，禁止 `#include` 标准库头文件。**

```cpp
// 正确
import std;

// 错误 — 禁止在模块中使用
#include <vector>
#include <string>
#include <format>
```

使用 `std::println` / `std::format` 替代 iostream。

## 全局片段（Global Module Fragment）

当需要引入平台头文件或项目配置头时，使用 `module;` 开头的全局片段：

```cpp
module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
    #include <WinSock2.h>
#endif

export module cnetmod.core.error;

import std;
```

### 头文件例外

仅以下两个传统头文件允许通过 `#include` 引入：

| 头文件 | 用途 |
|--------|------|
| `cnetmod/config.hpp` | 平台检测宏、I/O 后端检测、系统头配置 |
| `cnetmod/orm.hpp` | ORM 层的传统宏定义 |

其余所有标准库功能必须通过 `import std;` 获取。

## `export` 规则

- **`export`**：标记需要对外暴露的类型、函数、变量、命名空间
- **无 `export`**：仅限模块内部使用的实现细节

```cpp
export namespace cnetmod {

// 公开 API — 使用 export
export struct const_buffer
{
    const void* data = nullptr;
    std::size_t size = 0;
};

// 内部辅助 — 不加 export
namespace detail {
    constexpr auto bswap16(std::uint16_t v) noexcept -> std::uint16_t
    {
        return static_cast<std::uint16_t>((v >> 8) | (v << 8));
    }
} // namespace detail

} // namespace cnetmod
```

`export namespace` 可以包裹整个公开 API 区域，其内部的所有声明自动导出。

## 子模块导出

聚合模块内部可以使用 `export import :分区名;` 导入自身分区：

```cpp
export module cnetmod.core.log;

import std;
export import :config;   // 导入同模块的 :config 分区并重新导出
```

## namespace 约定

所有公开 API 放在 `namespace cnetmod { ... }` 内：

```cpp
export module cnetmod.core.buffer;

import std;

namespace cnetmod {

export struct const_buffer { /* ... */ };
export struct mutable_buffer { /* ... */ };

} // namespace cnetmod
```

内部实现细节放在 `namespace detail { ... }` 子命名空间中。

日志模块是例外——它使用独立的 `namespace logger { ... }`。

## 条件编译

可选依赖使用宏保护：

```cpp
#ifdef CNETMOD_HAS_SSL
    // SSL 相关 API
#endif
```

常用条件编译宏：

| 宏 | 含义 |
|----|------|
| `CNETMOD_HAS_SSL` | OpenSSL 可用 |
| `CNETMOD_HAS_IOCP` | Windows IOCP 后端 |
| `CNETMOD_HAS_EPOLL` | Linux epoll 后端 |
| `CNETMOD_HAS_KQUEUE` | macOS kqueue 后端 |
| `CNETMOD_HAS_IO_URING` | Linux io_uring 可用 |
| `CNETMOD_HAS_PROTOCOL_XXX` | 特定协议模块已启用 |

## 参考源码
- `src/core.cppm` — 聚合模块示例（export import 子模块）
- `src/coro.cppm` — 协程聚合模块
- `src/executor.cppm` — 执行器聚合模块
- `src/io.cppm` — I/O 聚合模块
- `src/core/log.cppm` — 典型 .cppm 接口（含分区导出）
- `src/core/buffer.cppm` — 典型 .cppm 接口（含 namespace、export 规则）
- `src/core/error.cppm` — 全局片段 + 平台头文件引入示例
