# buffer

> 提供零拷贝缓冲区视图、动态可增长缓冲区、二进制序列化读写器及字节序转换工具。

**import**: `import cnetmod.core;` (聚合) 或 `import cnetmod.core.buffer;`
**源码**: `src/core/buffer.cppm`, `src/core/buffer_pool.cppm`

## 场景导航
- 我要传递只读/可写数据给异步 API → [看这里](#场景缓冲区视图)
- 我要接收不定长数据 → [看这里](#场景动态缓冲区)
- 我要解析/构建二进制协议报文 → [看这里](#场景二进制读写器)
- 我要做网络字节序转换 → [看这里](#场景字节序转换)
- 我要 Direct I/O 对齐内存 → [看这里](#场景对齐缓冲区)
- 我要高频复用固定大小缓冲区 → [看这里](#场景缓冲池)

## API 参考

### `const_buffer`
**签名**: `export struct const_buffer`
**成员**:
- `const void* data` — 数据指针（只读）
- `std::size_t size` — 数据大小

**构造**:
- `constexpr const_buffer() noexcept` — 默认空缓冲区
- `constexpr const_buffer(const void* p, std::size_t n) noexcept` — 从指针+大小构造
- `constexpr const_buffer(std::span<const std::byte> s) noexcept` — 从 span 构造

### `mutable_buffer`
**签名**: `export struct mutable_buffer`
**成员**:
- `void* data` — 数据指针（可写）
- `std::size_t size` — 数据大小

**构造**:
- `constexpr mutable_buffer() noexcept` — 默认空缓冲区
- `constexpr mutable_buffer(void* p, std::size_t n) noexcept` — 从指针+大小构造
- `constexpr mutable_buffer(std::span<std::byte> s) noexcept` — 从 span 构造
- `constexpr operator const_buffer() const noexcept` — 隐式转换为只读视图

### `buffer()` 工厂函数
**签名**（多个重载）:
```cpp
export constexpr auto buffer(const void* data, std::size_t size) noexcept -> const_buffer;
export constexpr auto buffer(void* data, std::size_t size) noexcept -> mutable_buffer;
export constexpr auto buffer(std::string_view sv) noexcept -> const_buffer;
export auto buffer(std::vector<std::byte>& v) noexcept -> mutable_buffer;
export auto buffer(const std::vector<std::byte>& v) noexcept -> const_buffer;
export template <std::size_t N>
constexpr auto buffer(std::array<std::byte, N>& a) noexcept -> mutable_buffer;
```

**示例**:
```cpp
import std;
import cnetmod.core.buffer;

// 从 string_view 创建只读缓冲区
auto msg = std::string_view{"Hello, cnetmod!"};
auto buf = cnetmod::buffer(msg);

// 从 array 创建可写缓冲区
std::array<std::byte, 1024> arr{};
auto wbuf = cnetmod::buffer(arr);

// 从 vector 创建
std::vector<std::byte> vec(512);
auto mbuf = cnetmod::buffer(vec);
```

### `dynamic_buffer`
**签名**: `export class dynamic_buffer`

**构造**: `explicit dynamic_buffer(std::size_t initial_capacity = 4096)`

| 方法 | 签名 | 说明 |
|------|------|------|
| `prepare` | `auto prepare(std::size_t n) -> mutable_buffer` | 获取 n 字节可写区域 |
| `commit` | `void commit(std::size_t n) noexcept` | 确认写入 n 字节 |
| `data` | `auto data() const noexcept -> const_buffer` | 获取可读数据视图 |
| `consume` | `void consume(std::size_t n) noexcept` | 消费（丢弃）前 n 字节 |
| `readable_bytes` | `auto readable_bytes() const noexcept -> std::size_t` | 当前可读字节数 |

**示例**:
```cpp
import std;
import cnetmod.core.buffer;

cnetmod::dynamic_buffer dyn;

// 准备写入区域
auto writable = dyn.prepare(128);
// ... 往 writable 写入数据 ...
dyn.commit(64); // 确认 64 字节

// 读取数据
auto readable = dyn.data();
// ... 处理 readable ...
dyn.consume(64); // 消费 64 字节
```

### `aligned_buffer`
**签名**: `export class aligned_buffer`

**构造**: `explicit aligned_buffer(std::size_t size, std::size_t alignment = 4096)`

| 方法 | 签名 | 说明 |
|------|------|------|
| `data` | `auto data() noexcept -> std::byte*` | 获取可写指针 |
| `data` | `auto data() const noexcept -> const std::byte*` | 获取只读指针 |
| `size` | `auto size() const noexcept -> std::size_t` | 缓冲区大小 |
| `alignment` | `auto alignment() const noexcept -> std::size_t` | 对齐值 |
| `writable` | `auto writable() noexcept -> mutable_buffer` | 转为可写视图 |
| `readable` | `auto readable() const noexcept -> const_buffer` | 转为只读视图 |

不可拷贝，仅可移动。配合 `open_mode::direct` 使用。

### `buffer_reader`
**签名**: `export class buffer_reader`

**构造**:
- `explicit buffer_reader(const_buffer buf) noexcept`
- `explicit buffer_reader(std::span<const std::byte> s) noexcept`

| 方法 | 签名 | 说明 |
|------|------|------|
| `remaining` | `auto remaining() const noexcept -> std::size_t` | 剩余可读字节数 |
| `position` | `auto position() const noexcept -> std::size_t` | 当前偏移 |
| `skip` | `auto skip(std::size_t n) noexcept -> bool` | 跳过 n 字节 |
| `read_bytes` | `auto read_bytes(void* dst, std::size_t n) noexcept -> bool` | 读取原始字节 |
| `read_u8` | `auto read_u8() noexcept -> std::optional<std::uint8_t>` | 读取 1 字节 |
| `read_u16_be` | `auto read_u16_be() noexcept -> std::optional<std::uint16_t>` | 大端读取 2 字节 |
| `read_u32_be` | `auto read_u32_be() noexcept -> std::optional<std::uint32_t>` | 大端读取 4 字节 |
| `read_u64_be` | `auto read_u64_be() noexcept -> std::optional<std::uint64_t>` | 大端读取 8 字节 |
| `read_u16_le` | `auto read_u16_le() noexcept -> std::optional<std::uint16_t>` | 小端读取 2 字节 |
| `read_u32_le` | `auto read_u32_le() noexcept -> std::optional<std::uint32_t>` | 小端读取 4 字节 |
| `read_u64_le` | `auto read_u64_le() noexcept -> std::optional<std::uint64_t>` | 小端读取 8 字节 |

**示例**:
```cpp
import std;
import cnetmod.core.buffer;

std::array<std::byte, 64> raw{};
// ... 填充数据 ...
cnetmod::buffer_reader reader(cnetmod::buffer(raw));
auto version = reader.read_u8();
auto length = reader.read_u32_be(); // 网络字节序
auto flags = reader.read_u16_le();  // 小端序
```

### `buffer_writer`
**签名**: `export class buffer_writer`

**构造**:
- `explicit buffer_writer(mutable_buffer buf) noexcept`
- `explicit buffer_writer(std::span<std::byte> s) noexcept`

| 方法 | 签名 | 说明 |
|------|------|------|
| `remaining` | `auto remaining() const noexcept -> std::size_t` | 剩余可写字节数 |
| `written` | `auto written() const noexcept -> std::size_t` | 已写入字节数 |
| `write_bytes` | `auto write_bytes(const void* src, std::size_t n) noexcept -> bool` | 写入原始字节 |
| `write_u8` | `auto write_u8(std::uint8_t v) noexcept -> bool` | 写入 1 字节 |
| `write_u16_be` | `auto write_u16_be(std::uint16_t v) noexcept -> bool` | 大端写入 2 字节 |
| `write_u32_be` | `auto write_u32_be(std::uint32_t v) noexcept -> bool` | 大端写入 4 字节 |
| `write_u64_be` | `auto write_u64_be(std::uint64_t v) noexcept -> bool` | 大端写入 8 字节 |
| `write_u16_le` | `auto write_u16_le(std::uint16_t v) noexcept -> bool` | 小端写入 2 字节 |
| `write_u32_le` | `auto write_u32_le(std::uint32_t v) noexcept -> bool` | 小端写入 4 字节 |
| `write_u64_le` | `auto write_u64_le(std::uint64_t v) noexcept -> bool` | 小端写入 8 字节 |

**示例**:
```cpp
import std;
import cnetmod.core.buffer;

std::array<std::byte, 256> raw{};
cnetmod::buffer_writer writer(cnetmod::buffer(raw));
writer.write_u8(0x01);
writer.write_u32_be(1024);     // 网络字节序
writer.write_u16_le(0xABCD);   // 小端序
std::println("written {} bytes", writer.written());
```

### 字节序转换函数
**签名**:
```cpp
// host <-> network (big-endian)
export constexpr auto hton(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto hton(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto hton(std::uint64_t v) noexcept -> std::uint64_t;
export constexpr auto ntoh(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto ntoh(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto ntoh(std::uint64_t v) noexcept -> std::uint64_t;

// host <-> little-endian
export constexpr auto htole(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto htole(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto htole(std::uint64_t v) noexcept -> std::uint64_t;
export constexpr auto letoh(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto letoh(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto letoh(std::uint64_t v) noexcept -> std::uint64_t;

// Generic byte swap
export constexpr auto byte_swap(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto byte_swap(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto byte_swap(std::uint64_t v) noexcept -> std::uint64_t;
```

### `byte_order` 枚举
**签名**: `export enum class byte_order`
| 值 | 说明 |
|---|------|
| `little_endian` | 小端字节序 |
| `big_endian` | 大端字节序 |
| `native` | 当前平台原生字节序（编译期确定） |

### `pooled_buffer` & `buffer_pool`
**签名**: `export class pooled_buffer` / `export class buffer_pool`

`buffer_pool` 提供线程安全的固定大小块分配：

**构造**: `explicit buffer_pool(std::size_t block_size = 4096, std::size_t max_blocks = 1024) noexcept`

| 方法 | 签名 | 说明 |
|------|------|------|
| `acquire` | `auto acquire() -> pooled_buffer` | 获取一个块 |
| `pool_size` | `auto pool_size() const noexcept -> std::size_t` | 当前池中可用块数 |
| `block_size` | `auto block_size() const noexcept -> std::size_t` | 块大小 |

`pooled_buffer` 是 RAII 句柄，析构时自动归还：

| 方法 | 签名 | 说明 |
|------|------|------|
| `data` | `auto data() noexcept -> void*` | 块指针 |
| `size` | `auto size() const noexcept -> std::size_t` | 块大小 |
| `valid` | `auto valid() const noexcept -> bool` | 是否有效 |
| `release` | `void release() noexcept` | 提前归还 |
| — | `operator mutable_buffer()` / `operator const_buffer()` | 隐式转换为缓冲区视图 |

**示例**:
```cpp
import std;
import cnetmod.core.buffer_pool;

cnetmod::buffer_pool pool(4096, 256);

{
    auto blk = pool.acquire();
    std::println("got block of {} bytes", blk.size());
    // 使用 blk.data() ...
} // 析构时自动归还

std::println("pool has {} free blocks", pool.pool_size());
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 使用 `buffer(string_view)` 创建只读缓冲区 | 手动构造 `const_buffer` 时忘记设置 size |
| `dynamic_buffer` 先 `prepare` → 写入 → `commit` | 只 `prepare` 不 `commit` 就调用 `data()` |
| 用 `buffer_reader` 解析协议报文 | 手动做指针偏移和字节序转换 |
| `aligned_buffer` 配合 `open_mode::direct` | 用 `new` 分配未对齐内存做 Direct I/O |
| `pooled_buffer` 利用 RAII 自动归还 | 长期持有 `pooled_buffer` 不释放 |

## 参考源码
- `src/core/buffer.cppm` — 缓冲区视图、动态缓冲区、读写器、字节序转换
- `src/core/buffer_pool.cppm` — 线程安全缓冲池
