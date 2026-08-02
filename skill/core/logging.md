# Logging

> 提供结构化异步日志系统，支持控制台/文件输出、多级别过滤、文本/JSON 格式及文件轮转。

**import**: `import cnetmod.core.log;`
**源码**: `src/core/log.cppm`, `src/core/log_config.cppm`

## 场景导航
- 我要初始化日志系统 → [看这里](#场景初始化日志)
- 我要输出不同级别的日志 → [看这里](#场景输出日志)
- 我要同时输出到文件和控制台 → [看这里](#场景文件输出)
- 我要切换 JSON 格式输出 → [看这里](#场景json-格式)
- 我要运行时调整日志级别 → [看这里](#场景动态调整级别)
- 我要自定义日志 sink → [看这里](#场景自定义-sink)

## API 参考

### `logger::level` 枚举
**签名**: `export enum class level`（在 `logger` 命名空间中）

| 值 | 说明 |
|---|------|
| `trace` | 最详细的追踪信息 |
| `debug` | 调试信息 |
| `info` | 一般信息 |
| `warn` | 警告 |
| `error` | 错误 |
| `critical` | 严重错误 |
| `off` | 关闭所有日志 |

### `logger::output_format` 枚举
**签名**: `export enum class output_format`

| 值 | 说明 |
|---|------|
| `text` | 纯文本格式 |
| `json` | JSON 格式 |

### `logger::rotation_options`
**签名**: `export struct rotation_options`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_file_size` | `size_t` | `0` | 单文件最大字节数（0=不按大小轮转） |
| `max_files` | `size_t` | `0` | 保留的最大轮转文件数（0=全部保留） |
| `daily` | `bool` | `false` | 是否按天轮转 |

### `logger::sink`
**签名**: `using sink = std::function<void(std::string_view)>`

自定义日志输出回调函数类型。

### 初始化函数

#### `logger::init`
**签名**:
```cpp
void init(const std::string& name = "cnetmod",
    level lv = level::info,
    output_format fmt = output_format::text);
```
**参数**:
- `name` — 日志器名称
- `lv` — 最低输出级别
- `fmt` — 输出格式

#### `logger::init_with_file`
**签名**:
```cpp
void init_with_file(const std::string& name, const std::string& filepath,
    level lv = level::info,
    output_format fmt = output_format::text,
    bool echo_console = true);
```
**参数**:
- `name` — 日志器名称
- `filepath` — 日志文件路径
- `lv` — 最低输出级别
- `fmt` — 输出格式
- `echo_console` — 是否同时输出到控制台

### 配置函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `set_level` | `void set_level(level lv)` | 设置最低日志级别 |
| `set_format` | `void set_format(output_format fmt)` | 设置输出格式 |
| `set_console_enabled` | `void set_console_enabled(bool enabled)` | 启用/禁用控制台输出 |
| `set_file_output` | `auto set_file_output(const std::string& filepath, bool append = true) -> bool` | 配置文件输出 |
| `disable_file_output` | `void disable_file_output()` | 禁用文件输出 |
| `set_async_queue_limit` | `void set_async_queue_limit(std::size_t max_queue)` | 设置异步队列上限 |
| `dropped_messages` | `auto dropped_messages() -> std::uint64_t` | 获取丢弃的消息计数 |
| `flush` | `void flush()` | 刷新缓冲区 |
| `shutdown` | `void shutdown()` | 关闭日志系统 |

### 级别输出

每个级别都是一个结构体，支持 `std::format` 风格的格式化字符串和纯字符串两种调用方式。

**签名**:
```cpp
struct trace { /* level::trace */ };
struct debug { /* level::debug */ };
struct info  { /* level::info  */ };
struct warn  { /* level::warn  */ };
struct error { /* level::error */ };
struct critical { /* level::critical */ };
```

**用法**:
```cpp
// std::format 风格
logger::info("Server started on port {}", port);
logger::error("Connection failed: {}", error.message());

// 纯字符串
logger::debug("entering main loop");
```

所有级别输出自动捕获 `std::source_location`，记录调用位置。

## 场景：初始化日志

```cpp
import std;
import cnetmod.core.log;

// 默认初始化（名称 "cnetmod"，info 级别，纯文本）
logger::init();

// 自定义名称和级别
logger::init("my_app", logger::level::debug);

// 带文件输出
logger::init_with_file("my_app", "app.log",
    logger::level::debug, logger::output_format::text, true);
```

## 场景：输出日志

```cpp
import std;
import cnetmod.core.log;

logger::trace("entering function {}", __func__);
logger::debug("cache hit ratio: {:.2f}", 0.95);
logger::info("listening on {}:{}", host, port);
logger::warn("deprecated API called");
logger::error("failed to open file: {}", path);
logger::critical("out of memory!");
```

## 场景：文件输出

```cpp
import std;
import cnetmod.core.log;

// 初始化时配置文件
logger::init_with_file("server", "/var/log/server.log",
    logger::level::info, logger::output_format::text,
    /* echo_console = */ true);

// 运行时切换文件
bool ok = logger::set_file_output("/var/log/server2.log", /* append = */ true);

// 禁用文件输出
logger::disable_file_output();
```

## 场景：JSON 格式

```cpp
import std;
import cnetmod.core.log;

logger::init("api_server", logger::level::info, logger::output_format::json);
logger::info("request handled in {}ms", elapsed);
// 输出 JSON 格式的日志条目
```

## 场景：动态调整级别

```cpp
import std;
import cnetmod.core.log;

// 运行时提高日志级别（减少输出）
logger::set_level(logger::level::warn);

// 运行时降低日志级别（增加输出）
logger::set_level(logger::level::trace);

// 关闭控制台，只写文件
logger::set_console_enabled(false);
```

## 场景：自定义 sink

```cpp
import std;
import cnetmod.core.log;

// 自定义输出目标（如发送到远程日志服务）
logger::sink my_sink = [](std::string_view msg) {
    // 自定义处理逻辑
    send_to_remote(msg);
};
```

## 场景：异步队列管理

```cpp
import std;
import cnetmod.core.log;

// 限制异步队列大小，防止内存溢出
logger::set_async_queue_limit(10000);

// 监控丢弃的消息数
auto dropped = logger::dropped_messages();
if (dropped > 0) {
    std::println("WARNING: {} log messages dropped", dropped);
}

// 程序退出前刷新
logger::flush();
logger::shutdown();
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 程序启动时调用 `logger::init` | 未初始化就输出日志 |
| 使用 `{}` 格式化占位符 | 用字符串拼接构造日志消息 |
| 退出前调用 `logger::shutdown()` | 直接 `exit()` 导致日志丢失 |
| 用 `set_async_queue_limit` 防溢出 | 不设限制导致内存无限增长 |
| 用 `logger::error` 等结构化输出 | 用 `std::cout` 打印错误信息 |
| 利用 source_location 自动捕获位置 | 手动拼写文件名和行号 |

## 参考源码
- `src/core/log.cppm` — 公共日志 API、级别输出结构体
- `src/core/log_config.cppm` — level 枚举、output_format 枚举、rotation_options、sink 类型
