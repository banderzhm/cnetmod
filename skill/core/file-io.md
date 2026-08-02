# File I/O

> 提供跨平台异步文件 I/O 支持，包括 RAII 文件句柄、打开模式、Direct I/O 对齐及文件传输策略选择。

**import**: `import cnetmod.core;` + `import cnetmod.executor;`
**源码**: `src/core/file.cppm`, `src/executor/async_op.cppm`

## 场景导航
- 我要同步打开/关闭文件 → [看这里](#场景同步文件操作)
- 我要异步读写文件 → [看这里](#场景异步文件读写)
- 我要一次性读取/写入整个文件 → [看这里](#场景整体读写)
- 我要批量读写多个文件 → [看这里](#场景批量-io)
- 我要流式处理大文件 → [看这里](#场景流式管道)
- 我要将文件直接发送到 socket → [看这里](#场景零拷贝文件传输)
- 我要选择最优传输策略 → [看这里](#场景传输策略选择)
- 我要 Direct I/O 对齐内存 → [看这里](#场景direct-io)

## API 参考

### `open_mode` 枚举
**签名**: `export enum class open_mode : std::uint32_t`

| 值 | 说明 |
|---|------|
| `read` | 只读 |
| `write` | 只写 |
| `read_write` | 读写 |
| `append` | 追加 |
| `create` | 不存在则创建 |
| `truncate` | 存在则截断 |
| `create_new` | 必须不存在 |
| `direct` | 绕过平台页缓存 |

支持 `|` 和 `&` 位运算组合，以及 `has_flag(mode, flag)` 检查。

### `file_strategy` 枚举
**签名**: `export enum class file_strategy`

| 值 | 说明 |
|---|------|
| `buffered` | 缓冲 I/O（默认） |
| `direct` | Direct I/O |
| `zero_copy` | 零拷贝（sendfile/TransmitFile） |

### `file_strategy_options`
**签名**: `export struct file_strategy_options`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `to_socket` | `bool` | `false` | 是否发送到 socket |
| `encrypted_transport` | `bool` | `false` | 是否加密传输 |
| `requires_processing` | `bool` | `false` | 是否需要处理 |
| `allow_direct` | `bool` | `false` | 是否允许 Direct I/O |
| `direct_threshold` | `uint64_t` | `16MB` | Direct I/O 阈值 |

### `select_file_strategy()`
**签名**:
```cpp
export constexpr auto select_file_strategy(
    std::uint64_t file_size, file_strategy_options options = {}) noexcept
    -> file_strategy;
```
根据文件大小和选项自动选择最优传输策略。

### `file_stat`
**签名**: `export struct file_stat`

| 字段 | 类型 | 说明 |
|------|------|------|
| `size` | `uint64_t` | 文件大小（字节） |
| `is_regular` | `bool` | 是否普通文件 |
| `is_directory` | `bool` | 是否目录 |

### `file`
**签名**: `export class file`

跨平台文件句柄封装（RAII），不可拷贝，仅可移动。

| 方法 | 签名 | 说明 |
|------|------|------|
| `open` | `static auto open(const filesystem::path&, open_mode) -> expected<file, error_code>` | 打开文件 |
| `stat` | `static auto stat(const filesystem::path&) -> expected<file_stat, error_code>` | 获取文件状态 |
| `close` | `void close() noexcept` | 关闭文件 |
| `size` | `auto size() const -> expected<uint64_t, error_code>` | 获取文件大小 |
| `native_handle` | `auto native_handle() const noexcept -> file_handle_t` | 获取原生句柄 |
| `release` | `auto release() noexcept -> file_handle_t` | 释放所有权（不关闭） |
| `is_open` | `auto is_open() const noexcept -> bool` | 是否有效 |

### 异步文件操作函数

所有函数返回 `task<expected<T, error_code>>`，使用 `co_await` 调用。

#### `async_file_open()`
**签名**:
```cpp
export auto async_file_open(io_context& ctx, const filesystem::path& path, open_mode mode)
    -> task<expected<file, error_code>>;
export auto async_file_open(io_context& ctx, const filesystem::path& path, open_mode mode,
    cancel_token& token) -> task<expected<file, error_code>>;
```

#### `async_file_stat()`
**签名**:
```cpp
export auto async_file_stat(io_context& ctx, const filesystem::path& path)
    -> task<expected<file_stat, error_code>>;
```

#### `async_file_read()`
**签名**:
```cpp
export auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
    std::uint64_t offset = 0) -> task<expected<size_t, error_code>>;
```

#### `async_file_write()`
**签名**:
```cpp
export auto async_file_write(io_context& ctx, file& f, const_buffer buf,
    std::uint64_t offset = 0) -> task<expected<size_t, error_code>>;
```

#### `async_file_read_all()` / `async_file_write_all()`
**签名**:
```cpp
export auto async_file_read_all(io_context& ctx, const filesystem::path& path)
    -> task<expected<std::string, error_code>>;
export auto async_file_write_all(io_context& ctx,
    const filesystem::path& path, std::string_view content)
    -> task<expected<void, error_code>>;
```

#### `async_file_close()`
**签名**:
```cpp
export auto async_file_close(io_context& ctx, file& f)
    -> task<expected<void, error_code>>;
```

#### `async_file_flush()`
**签名**:
```cpp
export auto async_file_flush(io_context& ctx, file& f)
    -> task<expected<void, error_code>>;
```

### 批量 I/O

#### `file_read_request` / `file_write_request`
**签名**:
```cpp
export struct file_read_request {
    file* source = nullptr;
    mutable_buffer destination{};
    std::uint64_t offset = 0;
};
export struct file_write_request {
    file* destination = nullptr;
    const_buffer source{};
    std::uint64_t offset = 0;
};
```

#### `async_file_read_batch()` / `async_file_write_batch()`
**签名**:
```cpp
export auto async_file_read_batch(io_context& ctx,
    std::span<const file_read_request> requests)
    -> task<std::vector<file_io_result>>;
export auto async_file_write_batch(io_context& ctx,
    std::span<const file_write_request> requests)
    -> task<std::vector<file_io_result>>;
```
其中 `file_io_result = std::expected<std::size_t, std::error_code>`。

### 流式管道

#### `file_pipeline_options`
**签名**: `export struct file_pipeline_options`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `offset` | `uint64_t` | `0` | 起始偏移 |
| `byte_count` | `uint64_t` | `max` | 读取字节数 |
| `chunk_size` | `size_t` | `256KB` | 分块大小 |

#### `async_file_read_pipeline()`
**签名**:
```cpp
export using file_chunk_handler = std::function<
    task<expected<void, error_code>>(const_buffer chunk, uint64_t offset)>;
export auto async_file_read_pipeline(io_context& ctx, file& source,
    file_chunk_handler handler, file_pipeline_options options = {})
    -> task<expected<uint64_t, error_code>>;
```
双缓冲流水线：处理当前块时并发读取下一块。

### 零拷贝传输

#### `async_send_file()`
**签名**:
```cpp
export auto async_send_file(io_context& ctx, socket& sock, file& source,
    std::uint64_t offset = 0,
    std::uint64_t byte_count = numeric_limits<uint64_t>::max())
    -> task<expected<uint64_t, error_code>>;
```

## 场景：同步文件操作

```cpp
import std;
import cnetmod.core;

// 打开文件
auto f = cnetmod::file::open("data.bin",
    cnetmod::open_mode::read_write | cnetmod::open_mode::create);
if (!f) { /* 错误处理 */ }

auto sz = f->size();
f->close();
```

## 场景：异步文件读写

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

auto run(cnetmod::io_context& ctx) -> cnetmod::task<void>
{
    namespace cn = cnetmod;
    auto f = cn::file::open("data.bin",
        cn::open_mode::write | cn::open_mode::create | cn::open_mode::truncate);

    std::string data = "Hello, async file I/O!";
    auto wr = co_await cn::async_file_write(ctx, *f,
        cn::const_buffer{data.data(), data.size()}, 0);
    co_await cn::async_file_flush(ctx, *f);
}
```

## 场景：整体读写

```cpp
// 一次性读取整个文件为 string
auto text = co_await cnetmod::async_file_read_all(ctx, "config.json");
if (text) std::println("content: {}", *text);

// 一次性写入 string
co_await cnetmod::async_file_write_all(ctx, "output.txt", "Hello World");
```

## 场景：批量 I/O

```cpp
std::vector<cnetmod::file_read_request> requests;
// ... 填充多个读请求 ...
auto results = co_await cnetmod::async_file_read_batch(ctx, requests);
for (auto& r : results) {
    if (r) std::println("read {} bytes", *r);
}
```

## 场景：流式管道

```cpp
auto handler = [](cnetmod::const_buffer chunk, std::uint64_t offset)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    // 处理每个数据块
    process_chunk(chunk, offset);
    co_return std::expected<void, std::error_code>{};
};

cnetmod::file_pipeline_options opts{.chunk_size = 512 * 1024};
auto total = co_await cnetmod::async_file_read_pipeline(ctx, file, handler, opts);
```

## 场景：零拷贝文件传输

```cpp
auto f = *cnetmod::file::open("large.bin", cnetmod::open_mode::read);
auto sent = co_await cnetmod::async_send_file(ctx, sock, f, 0);
if (sent) std::println("sent {} bytes via zero-copy", *sent);
```

## 场景：传输策略选择

```cpp
cnetmod::file_strategy_options opts{
    .to_socket = true,
    .encrypted_transport = false,
    .requires_processing = false,
};
auto strategy = cnetmod::select_file_strategy(file_size, opts);
// strategy == file_strategy::zero_copy (未加密 socket 直传)
```

## 场景：Direct I/O

```cpp
import std;
import cnetmod.core;

// 对齐缓冲区（4096 字节对齐）
cnetmod::aligned_buffer buf(65536, 4096);

// Direct I/O 打开文件
auto f = cnetmod::file::open("data.bin",
    cnetmod::open_mode::read | cnetmod::open_mode::direct);

// 读写必须使用对齐的缓冲区和大小
auto n = co_await cnetmod::async_file_read(ctx, *f, buf.writable(), 0);
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 用 `open_mode` 位运算组合模式 | 用字符串 "r+" / "wb" 等 |
| `async_file_write` 后调用 `async_file_flush` | 假设写入立即持久化 |
| Direct I/O 使用 `aligned_buffer` | 用未对齐内存做 Direct I/O |
| 大文件用 `async_send_file` 零拷贝 | 手动 read + write 循环传输 |
| 用 `select_file_strategy` 自动选策略 | 硬编码传输方式 |
| 大文件用 `async_file_read_pipeline` 分块处理 | 用 `async_file_read_all` 加载到内存 |

## 参考源码
- `src/core/file.cppm` — file 类、open_mode、file_stat、file_strategy
- `src/executor/async_op.cppm` — async_file_open/read/write/stat/flush/close/batch/pipeline/send_file
- `src/core/buffer.cppm` — aligned_buffer（Direct I/O 对齐）
- `examples/core/async_file.cpp` — 异步文件 I/O 完整示例
