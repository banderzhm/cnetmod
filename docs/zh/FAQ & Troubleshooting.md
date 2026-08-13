# FAQ & 故障排除

本文档汇总了 cnetmod HTTP/3 模块开发中常见的问题和解决方案，涵盖编译错误、TLS 握手失败、平台差异等方面。

---

## 常见编译错误及解决方案

### Q1: "undefined reference to `SSL_set_quic_method`"

**原因**: BoringSSL 库未正确链接或版本太低，导致 QUIC API 符号缺失。

**解决方案**：

```bash
# 1. 确认 BoringSSL 已正确安装
ls /usr/local/lib/libssl.a /usr/local/lib/libcrypto.a

# 2. 检查符号是否存在
nm /usr/local/lib/libcrypto.a | grep SSL_set_quic_method
# 期望输出：0000000000000000 T SSL_set_quic_method

# 3. 显式指定 BoringSSL 路径
cmake -DCNETMOD_ENABLE_BORINGSSL_QUIC=ON \
      -DBORINGSSL_ROOT="/usr/local" \
      -DBORINGSSL_INCLUDE_DIR="/usr/local/include" \
      -DBORINGSSL_LIBRARY="/usr/local/lib/libssl.a" \
      -DBORINGSSL_CRYPTO_LIBRARY="/usr/local/lib/libcrypto.a" \
      ...

# 4. 如果符号不存在，需要升级 BoringSSL
cd /opt/boringssl
git pull
cmake --build build
sudo cp build/libcrypto.a build/libssl.a /usr/local/lib/
```

**Windows (vcpkg) 解决方案**：

```powershell
# 重新安装 BoringSSL
.\vcpkg remove boringssl:x64-windows
.\vcpkg install boringssl:x64-windows --recurse

# 验证
.\vcpkg list | findstr boringssl
```

---

### Q2: "C++ module scan failed"

**原因**: MSVC 无法识别 `.cppm` 文件扩展名或模块扫描配置不正确。

**解决方案**：

```cmake
# 方案 1：显式启用模块扫描
if(MSVC)
    add_compile_options(
        /experimental:cxxmodules    # 启用模块支持
        /std:c++latest              # C++23 标准
        /Zc:__cplusplus             # 正确的宏值
    )
    
    # 设置模块生成模式
    set(CMAKE_CXX_MODULE_GENERATION_MODE "SEPARATE")
endif()

# 方案 2：手动指定模块文件
add_library(cnetmod_http3)
target_sources(cnetmod_http3
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/http/v3/http3_server.cppm
            src/http/v3/http3_client.cppm
)

# 方案 3：复制 .cppm 为 .ixx（MSVC 原生扩展名）
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/module.ixx
    COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_CURRENT_SOURCE_DIR}/module.cppm 
            ${CMAKE_CURRENT_BINARY_DIR}/module.ixx
    DEPENDS module.cppm
)
```

**Clang 解决方案**：

```cmake
# Clang 需要 -fmodules 标志
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmodules -stdlib=libc++")

# 或使用 C++20 modules（Clang 14+）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
```

---

### Q3: "invalid varint encoding"

**原因**: 尝试编码超过 2^62-1 的值，违反 RFC 9000 §16 varint 限制。

**解决方案**：

```cpp
// 添加范围检查
expected<varint, error_code> encode_varint_safe(uint64_t value) {
    constexpr uint64_t kMaxVarint = (1ULL << 62) - 1;  // 4611686018427387903
    
    if (value > kMaxVarint) {
        return unexpected(std::make_error_code(quic_errc::varint_overflow));
    }
    
    return varint{value};
}

// 使用示例
auto result = encode_varint_safe(stream_id);
if (!result) {
    logger::error("Stream ID too large: {}", stream_id);
    return result.error();
}
```

**调试技巧**：

```cpp
// 打印 varint 编码详情
void debug_varint(uint64_t value) {
    logger::debug("Varint encode: value={}, size={} bytes",
                  value, varint_size(value));
    
    if (value <= 0x3F) {
        logger::debug("  Format: 00xxxxxx (1 byte)");
    } else if (value <= 0x3FFF) {
        logger::debug("  Format: 01xxxxxx (2 bytes)");
    } else if (value <= 0x3FFFFFFF) {
        logger::debug("  Format: 10xxxxxx (4 bytes)");
    } else if (value <= 0x3FFFFFFFFFFFFFFF) {
        logger::debug("  Format: 11xxxxxx (8 bytes)");
    } else {
        logger::error("  ERROR: value exceeds 2^62-1!");
    }
}
```

---

## TLS handshake 失败排查步骤

### Step 1: 检查证书有效期

```bash
# 使用 openssl 检查证书
openssl x509 -in cert.pem -text -noout

# 关键输出字段
# Not Before: Jan  1 00:00:00 2024 GMT
# Not After : Dec 31 23:59:59 2026 GMT
# ✓ 必须在当前时间范围内

# 检查证书链
openssl verify -CAfile ca.pem cert.pem

# 如果证书过期，重新生成
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365
```

### Step 2: 验证 ALPN 协商

**Python 测试客户端**：

```python
from aioquic.quic.configuration import Configuration
from aioquic.asyncio import connect
import asyncio

async def test_alpn():
    config = Configuration(
        alpn_protocols=["h3"],  # ⚠️ 必须是 "h3" 不是 "http/3"
        verify_mode=False,       # 测试环境禁用验证
    )
    
    try:
        async with connect("localhost", 4433, configuration=config) as protocol:
            print("✓ ALPN negotiation: h3")
            print(f"✓ Connection established: {protocol.connection_id}")
    except Exception as e:
        print(f"✗ Failed: {e}")
        if "ALPN" in str(e):
            print("  → Check server ALPN configuration")

asyncio.run(test_alpn())
```

**cnetmod Server 端配置**：

```cpp
auto ssl_ctx = cnetmod::ssl_context::quic_server();
ssl_ctx.load_cert_file("cert.pem");
ssl_ctx.load_key_file("key.pem");
ssl_ctx.set_alpn_protocols({"h3"});  // 必须包含 "h3"

// 验证 ALPN 设置
auto protocols = ssl_ctx.alpn_protocols();
assert(protocols.size() == 1 && protocols[0] == "h3");
```

### Step 3: 检查 Transport Parameters

**启用详细日志**：

```cpp
// cnetmod 侧启用调试日志
cnetmod::logging::set_level(cnetmod::logging::level::debug);

auto server = cnetmod::http::v3::make_http3_server(ctx, ssl_ctx, port, router);

// 连接建立后打印参数
server->on_connection_established([](const quic_connection& conn) {
    const auto& params = conn.peer_transport_params();
    logger::debug("Peer Transport Parameters:");
    logger::debug("  max_idle_timeout: {}ms", params.max_idle_timeout);
    logger::debug("  initial_max_data: {} bytes", params.initial_max_data);
    logger::debug("  initial_max_streams_bidi: {}", params.initial_max_streams_bidi);
    logger::debug("  max_udp_payload_size: {}", params.max_udp_payload_size);
});
```

**Python 客户端检查**：

```python
from aioquic.quic.configuration import Configuration

config = Configuration(
    alpn_protocols=["h3"],
    max_datagram_frame_size=65536,
)

# 打印本地参数
print("Local Transport Parameters:")
print(f"  max_idle_timeout: {config.max_idle_timeout}ms")
print(f"  initial_max_data: {config.initial_max_data} bytes")
print(f"  initial_max_streams_bidi: {config.initial_max_streams_bidi}")
```

### Step 4: 最小复现脚本

**Python 客户端**：

```python
import asyncio
from aioquic.asyncio import connect
from aioquic.quic.configuration import Configuration

async def minimal_test():
    config = Configuration(
        alpn_protocols=["h3"],
        verify_mode=False,  # 跳过证书验证
    )
    
    try:
        async with connect("localhost", 4433, configuration=config) as protocol:
            print("✓ Connection established")
            
            # 发送简单 HTTP 请求
            await protocol.send_request(
                method="GET",
                path="/hello",
                headers={"User-Agent": "aioquic/1.0"}
            )
            
            # 接收响应
            response = await protocol.receive_response()
            print(f"✓ Status: {response.status_code}")
            print(f"✓ Body: {response.body.decode()}")
            
    except Exception as e:
        print(f"✗ Failed: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    asyncio.run(minimal_test())
```

**cnetmod 服务端最小配置**：

```cpp
#include <cnetmod/protocol/http/v3.hpp>

int main() {
    auto ctx = cnetmod::make_io_context();
    auto ssl_ctx = cnetmod::ssl_context::quic_server();
    ssl_ctx.load_cert_file("cert.pem");
    ssl_ctx.load_key_file("key.pem");
    ssl_ctx.set_alpn_protocols({"h3"});
    
    auto router = std::make_unique<cnetmod::http::router>();
    router->get("/hello", [](cnetmod::http::request_context& ctx) {
        ctx.response().set_status(200);
        ctx.response().body() = "Hello, QUIC!";
    });
    
    auto server = cnetmod::http::v3::make_http3_server(*ctx, ssl_ctx, 4433, *router);
    cnetmod::spawn(*ctx, server->start());
    
    logger::info("Server listening on :4433");
    ctx->run();
    
    return 0;
}
```

---

## UDP socket 行为差异说明

### Windows (IOCP)

**已知问题**：

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| SO_REUSEPORT 不支持 | Windows socket 实现差异 | 使用 SO_REUSEADDR 替代 |
| UDP 端口独占失败 | 其他进程占用端口 | 使用 `netstat -ano` 检查 |
| 多 worker 线程冲突 | IOCP 完成队列竞争 | 单 worker 线程或 SO_EXCLUSIVEADDRUSE |

**配置示例**：

```cpp
// Windows 专用配置
cnetmod::udp_socket_options opts;
opts.exclusive_address_use = true;  // SO_EXCLUSIVEADDRUSE
opts.receive_buffer_size = 4 * 1024 * 1024;  // 4MB 接收缓冲
opts.send_buffer_size = 1024 * 1024;  // 1MB 发送缓冲
```

**防火墙配置**：

```powershell
# 添加防火墙规则
netsh advfirewall firewall add rule name="cnetmod HTTP3" `
    dir=in action=allow protocol=UDP localport=4433 profile=private

# 验证规则
netsh advfirewall firewall show rule name="cnetmod HTTP3"
```

### Linux (epoll/io_uring)

**已知问题**：

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| UDP datagram 丢失 | 接收缓冲溢出 | 调大 `net.core.rmem_max` |
| 端口绑定失败 | 权限不足 | 使用 `setcap` 或 root 运行 |
| io_uring 不可用 | 内核版本过低 | 升级到 5.6+ 或回退到 epoll |

**系统调优**：

```bash
# 增加 UDP 缓冲区
sudo sysctl -w net.core.rmem_max=134217728  # 128MB
sudo sysctl -w net.core.wmem_max=134217728  # 128MB
sudo sysctl -w net.core.rmem_default=262144  # 256KB

# 持久化配置
echo "net.core.rmem_max=134217728" | sudo tee -a /etc/sysctl.conf
echo "net.core.wmem_max=134217728" | sudo tee -a /etc/sysctl.conf
```

**cnetmod 配置**：

```cpp
// Linux 专用配置
cnetmod::udp_socket_options opts;
opts.use_io_uring = true;  // 优先使用 io_uring
opts.receive_buffer_size = 16 * 1024 * 1024;  // 16MB
opts.gro_enabled = true;  // Generic Receive Offload（内核 ≥ 5.3）
```

### macOS (kqueue)

**已知问题**：

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| 包号乱序概率高 | kqueue 事件排序不严格 | 增大接收缓冲 + 乱序重组 |
| UDP 端口复用限制 | macOS socket 实现 | 使用 SO_REUSEPORT |

**配置示例**：

```cpp
// macOS 专用配置
cnetmod::udp_socket_options opts;
opts.reuse_port = true;
opts.receive_buffer_size = 8 * 1024 * 1024;  // 8MB
opts.reorder_buffer_size = 1000;  // 乱序重组缓冲
```

---

## 已知限制和未来路线图

### 当前版本（v1.0-MVP）

#### ✅ 已实现功能

- **QUIC 传输层核心功能**
  - 连接建立/关闭状态机
  - 多路复用流控制
  - 丢包检测与重传
  - NewReno 拥塞控制
  - AEAD 加密（AES-128-GCM / ChaCha20-Poly1305）

- **HTTP/3 帧编解码**
  - DATA / HEADERS / SETTINGS 帧
  - GOAWAY 基础支持
  - Router 和中间件链

- **QPACK 静态表**
  - RFC 9204 完整静态表
  - Literal with Indexing
  - Never Indexed 敏感头部

#### 实验性且已有端到端门禁的功能

以下能力不再是“规划中”：CI 在 Windows Debug/Release 与 Linux 上使用
aioquic 强制验证；缺少 aioquic 会直接使门禁失败，而不是跳过。

| 功能 | 当前状态 | 使用边界 |
|------|---------|---------|
| 0-RTT early data | 已实现，显式 opt-in | 仅幂等、可安全重放的请求；服务端必须提供共享 anti-replay cache |
| 连接迁移 | 已实现 | 已覆盖 UDP 源端口切换；复杂 NAT/丢包 soak 仍需持续验证 |
| QPACK 动态表 | 已实现 | 已覆盖阻塞恢复；第三方长时间 soak 仍在进行 |
| WebTransport | 实验性已验证 | aioquic E2E 覆盖 GET、Extended CONNECT、子流、DATAGRAM、Close Capsule |

#### ❌ 未实现功能（计划中）

| 功能 | 计划版本 | 状态 |
|------|---------|------|
| BBRv1 拥塞控制 | 已实现 | `quic_config::congestion_algorithm = bbr` |
| 服务端推送 | v1.2 | 未开始 |

### 下一步优化

**v1.1 里程碑**（2026-Q4）：
1. **Priority 与性能回归**：验证 RFC 9218 调度改善高优先级尾延迟且不饿死低优先级流
2. **性能基准测试框架**：吞吐量/延迟指标收集工具
3. **文档完善**：API 参考手册和示例代码库

**v1.2 里程碑**（2027-Q1）：
1. **0-RTT ticket 跨进程持久化 soak**：扩大接受、拒绝与 anti-replay 验证覆盖
2. **Multipath QUIC**：需要每路径 CID、RTT、拥塞控制、丢包恢复和调度器；当前
   仅支持单活跃路径的连接迁移与并发路径验证，不能把它称为 multipath。
3. **服务端推送**：HTTP/3 PUSH_PROMISE 支持
4. **Windows IOCP 深度集成**：GSO/GRO 支持

**v1.3 里程碑**（2027-Q2）：
1. **连接迁移**：Path Validation + Address Resolution
2. **Multi-path QUIC**：多路径并发传输
3. **跨语言 SDK 生态**：已提供稳定 C ABI、Rust 安全封装和 CPython 原生扩展；
   仍可继续补充 asyncio 与高层 Rust async API。

---

## 贡献指南

### 问题报告流程

如有问题发现，请按以下步骤提交：

1. **提交 GitHub Issue**
   - 标题格式：`[HTTP3] 简短描述`
   - 标签：`bug`, `http3`, `quic`

2. **提供详细信息**
   - cnetmod 版本号（`git log -1`）
   - 操作系统和编译器版本
   - CMake 构建配置（`CMakeCache.txt` 关键行）

3. **附 minimal reproduction example**
   - 最小可复现代码（<50 行）
   - 期望行为 vs 实际行为
   - 错误日志或堆栈跟踪

### 代码贡献规范

```bash
# 1. Fork 仓库并创建分支
git checkout -b feature/my-feature

# 2. 遵循编码规范
clang-format -i src/http/v3/*.cpp

# 3. 编写测试
# 在 testing/http3/ 目录添加测试用例

# 4. 提交 PR
# 描述清楚改动目的和影响
```

### 联系方式

- **Issue Tracker**: https://github.com/cnetmod/cnetmod/issues
- **邮件**: dev@cnetmod.org
- **讨论区**: https://github.com/cnetmod/cnetmod/discussions

---

*本文档为 cnetmod FAQ & 故障排除 v1.0-MVP，最后更新：2026-08-03*
