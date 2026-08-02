# 错误处理与工具集

> 错误码体系、网络初始化、崩溃转储、串口，以及字节转换、字符解析、JSON 辅助、哈希等实用工具。

**import**: `import cnetmod.core.error;` / `import cnetmod.core.net_init;` / `import cnetmod.core.crash_dump;` / `import cnetmod.utils;`
**源码**: `src/core/error.cppm`, `src/core/net_init.cppm`, `src/core/crash_dump.cppm`, `src/core/serial_port.cppm`, `src/cnetmod_utils.cppm`, `src/utils/*.cppm`

## 场景导航

- 我要处理网络错误码 → [看这里](#errc-错误码)
- 我要在 Windows 上初始化网络库 → [看这里](#net_init-网络初始化)
- 我要捕获程序崩溃信息 → [看这里](#crash_dump-崩溃转储)
- 我要打开串口通信 → [看这里](#serial_port-串口)
- 我要做字节序/寄存器转换 → [看这里](#utilsconv-字节与寄存器转换)
- 我要解析字符串中的数字 → [看这里](#charconv-字符转换)
- 我要安全读取 JSON 字段 → [看这里](#json_utils-json-辅助)
- 我要计算 HMAC-SHA256 → [看这里](#hmac-sha256--sha256)
- 我要构建统一的应用结果类型 → [看这里](#utilsr-应用结果)

## API 参考

### `errc` 错误码

**签名**:
```cpp
export enum class errc {
    success = 0,
    // 连接
    connection_refused, connection_reset, connection_aborted,
    connection_timed_out, not_connected, already_connected,
    // 地址
    address_in_use, address_not_available, address_family_not_supported,
    // 操作
    operation_aborted, operation_in_progress, operation_not_supported,
    operation_would_block,
    // 资源
    too_many_files_open, no_buffer_space, out_of_memory,
    // 网络
    network_down, network_unreachable, host_unreachable, host_not_found,
    // I/O
    broken_pipe, end_of_file, bad_descriptor,
    // 通用
    permission_denied, invalid_argument, unknown_error,
};
```

**辅助函数**:
```cpp
export class network_error_category : public std::error_category { ... };
export auto network_category() noexcept -> const std::error_category&;
export auto make_error_code(errc e) noexcept -> std::error_code;
export auto from_native_error(int native_error) noexcept -> errc;
```

已注册 `std::is_error_code_enum<cnetmod::errc>`，可直接与 `std::error_code` 互操作。

```cpp
import std;
import cnetmod.core.error;

std::error_code ec = cnetmod::errc::connection_refused;
if (ec == cnetmod::errc::connection_refused) {
    std::println("connection refused: {}", ec.message());
}
```

---

### `net_init` 网络初始化

**签名**: `export class net_init;`（不可拷贝/移动）

RAII 守卫：构造时调用 `WSAStartup`（Windows），析构时调用 `WSACleanup`。Linux/macOS 为 no-op。

```cpp
import std;
import cnetmod.core.net_init;

int main() {
    cnetmod::net_init net;  // 必须在任何网络操作前创建
    // ... 网络操作 ...
}
```

---

### `crash_dump` 崩溃转储

**签名**:
```cpp
export struct crash_info {
    std::string signal_name;
    int signal_code{0};
    std::string timestamp;
    std::string stack_trace;
    std::string dump_file_path;
};

export class crash_dump {
    using callback_fn = std::function<void(const crash_info&)>;
    static void install(std::string dump_dir = "crash");
    static void set_callback(callback_fn fn);
    static void set_app_name(std::string name);
    static void trigger_crash_report(std::string_view reason);
};
```

```cpp
import std;
import cnetmod.core.crash_dump;

int main() {
    cnetmod::crash_dump::install("crash_reports");
    cnetmod::crash_dump::set_app_name("my_server");
    cnetmod::crash_dump::set_callback([](const cnetmod::crash_info& info) {
        std::println("Crash: {} at {}", info.signal_name, info.timestamp);
    });
    // ... 应用逻辑 ...
}
```

---

### `serial_port` 串口

**签名**:
```cpp
export enum class parity : std::uint8_t { none, odd, even, mark, space };
export enum class stop_bits : std::uint8_t { one, one_half, two };
export enum class flow_control : std::uint8_t { none, hardware, software };

export struct serial_config {
    std::uint32_t baud_rate = 9600;
    std::uint8_t data_bits = 8;
    stop_bits stop = stop_bits::one;
    parity par = parity::none;
    flow_control flow = flow_control::none;
    std::uint32_t read_timeout_ms = 1000;
    std::uint32_t write_timeout_ms = 1000;
};

export class serial_port {
    [[nodiscard]] static auto open(std::string_view name,
        const serial_config& config = {}) -> std::expected<serial_port, std::error_code>;
    void close() noexcept;
    [[nodiscard]] auto native_handle() const noexcept -> file_handle_t;
    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto config() const noexcept -> const serial_config&;
    [[nodiscard]] auto release() noexcept -> file_handle_t;  // 释放所有权
};
```

```cpp
import std;
import cnetmod.core.serial_port;

auto port = cnetmod::serial_port::open("COM3", { .baud_rate = 115200 });
if (port) {
    // 使用 port->native_handle() 进行 I/O
    port->close();
}
```

---

### `utils::conv` 字节与寄存器转换

**import**: `import cnetmod.utils;`（通过 `:converter` 子模块）
**命名空间**: `utils::conv`

| 函数/类 | 说明 |
|---------|------|
| `hton(v)` / `ntoh(v)` | 主机 ↔ 网络（大端）字节序，支持 16/32/64 位 |
| `htole(v)` / `letoh(v)` | 主机 ↔ 小端字节序 |
| `read_be16/32/64(span, offset)` | 从缓冲区读大端值 |
| `write_be16/32/64(span, value, offset)` | 写大端值到缓冲区 |
| `RegisterConverter` | Modbus 寄存器 ↔ int/float/double 转换 |
| `BitOps` | 位操作：get_bit, set_bit, to_bits, from_bits |
| `CRC16` | Modbus RTU CRC16 计算/校验 |
| `Hex` | 十六进制编解码 |

```cpp
import std;
import cnetmod.utils;

// 字节序转换
auto net_val = utils::conv::hton(uint32_t{0x12345678});

// Modbus 寄存器转 float
float f = utils::conv::RegisterConverter::to_float_hilo(reg_high, reg_low);

// Hex 编码
auto hex = utils::conv::Hex::encode(data_span);
```

---

### `charconv` 字符转换

**签名**:
```cpp
export auto from_chars_double(std::string_view sv, double& value) -> std::errc;
export auto from_chars_float(std::string_view sv, float& value) -> std::errc;
export template <std::integral T>
auto from_chars_int(std::string_view sv, T& value, int base = 10) -> std::errc;
```

跨平台 `std::from_chars` 封装，macOS 浮点支持回退到 `std::stod`。

---

### `json_utils` JSON 辅助

**命名空间**: `cnetmod::json_utils`

```cpp
template <typename JsonT> auto parse_object(std::string_view body) -> JsonT;
template <typename JsonT> auto to_int(const JsonT& j, const char* key, int def = 0) -> int;
template <typename JsonT> auto to_bool(const JsonT& j, const char* key, bool def = false) -> bool;
template <typename JsonT> auto to_string(const JsonT& j, const char* key, std::string def = {}) -> std::string;
template <typename JsonT> auto to_uint16_port(const JsonT& j, const char* key, std::uint16_t def) -> std::uint16_t;
```

安全读取 JSON 字段，缺失或类型不匹配时返回默认值。

---

### HMAC-SHA256 / SHA256

**import**: `import cnetmod.utils.hmac_sha256;` / `import cnetmod.utils.sha256;`

```cpp
// HMAC-SHA256
export auto hmac_sha256(std::string_view key, std::string_view data) -> hmac_sha256_digest;
export auto hmac_sha256_hex(std::string_view key, std::string_view data) -> std::string;
export auto hmac_sha256_base64(std::string_view key, std::string_view data) -> std::string;

// SHA256
export auto sha256(std::string_view input) -> sha256_digest;
export auto sha256_hex(std::string_view input) -> std::string;
```

---

### `utils::R` 应用结果

**签名**: `template <class T, class ErrorCode = std::int32_t> class R;`

统一的成功/失败结果类型，不耦合 HTTP/数据库特定错误码。

```cpp
using namespace cnetmod::utils;
auto ok_result = R<User>::ok(user, "found");
auto err_result = R<User>::error(404, "not found", "user_id=123");

if (result.ok()) { result.data(); }
else { result.failure().code; result.message(); }
```

## Do's & Don'ts

| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 在 `main()` 开头创建 `net_init` RAII 对象 | 忘记 `net_init`，在 Windows 上直接操作 socket |
| 用 `make_error_code(errc::xxx)` 生成 `std::error_code` | 手动构造 `std::error_code` 并猜测 category |
| 用 `from_native_error()` 转换平台错误码 | 硬编码 `#ifdef` 判断平台错误码值 |
| 用 `json_utils::to_int()` 安全读取可能缺失的字段 | 直接 `j["key"].get<int>()`（键缺失时抛异常） |
| 用 `serial_port::open()` 返回 `std::expected` 处理错误 | 忽略 `std::expected` 的错误路径 |
| 程序启动时调用 `crash_dump::install()` | 在崩溃已经发生后才尝试安装处理器 |

## 参考示例

- `src/core/error.cppm` — 错误码定义与转换
- `src/utils/converter.cppm` — 字节序、寄存器、CRC、Hex 工具
- `src/utils/json.cppm` — JSON 安全读取辅助
