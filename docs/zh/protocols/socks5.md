# SOCKS5 协议实现

cnetmod 的完整 SOCKS5 (RFC 1928) 代理协议实现。

## 功能特性

- ✅ 完整的 SOCKS5 协议支持 (RFC 1928)
- ✅ 多种认证方式：
  - 无需认证
  - 用户名/密码认证 (RFC 1929)
- ✅ 地址类型：
  - IPv4
  - IPv6
  - 域名
- ✅ CONNECT 命令支持
- ✅ 基于协程的异步 API
- ✅ 客户端和服务器实现
- ✅ 多核服务器支持
- ✅ 连接数限制
- ✅ 双向数据中继

## 客户端使用

```cpp
import cnetmod.io.io_context;
import cnetmod.protocol.socks5;

using namespace cnetmod;
using namespace cnetmod::socks5;

auto example(io_context& ctx) -> task<void> {
    // 创建客户端
    client socks_client(ctx);
    
    // 连接到代理服务器
    co_await socks_client.connect("127.0.0.1", 1080);
    
    // 认证（如果需要）
    co_await socks_client.authenticate("username", "password");
    
    // 通过代理连接到目标服务器
    co_await socks_client.connect_target("www.example.com", 80);
    
    // 使用 socket 进行数据传输
    auto& sock = socks_client.socket();
    // ... 发送/接收数据 ...
}
```

## 服务器使用

### 单线程服务器

```cpp
import cnetmod.io.io_context;
import cnetmod.protocol.socks5;

using namespace cnetmod;
using namespace cnetmod::socks5;

auto example(io_context& ctx) -> task<void> {
    // 配置服务器
    server_config config;
    config.allow_no_auth = true;
    config.allow_username_password = true;
    config.authenticator = [](std::string_view user, std::string_view pass) {
        return user == "admin" && pass == "secret";
    };
    config.max_connections = 100;
    
    // 创建并运行服务器
    server socks_server(ctx, config);
    co_await socks_server.listen("0.0.0.0", 1080);
    co_await socks_server.run();
}
```

### 多核服务器

```cpp
import cnetmod.executor.pool;
import cnetmod.protocol.socks5;

using namespace cnetmod;
using namespace cnetmod::socks5;

auto example(server_context& sctx) -> task<void> {
    server_config config;
    config.allow_no_auth = true;
    config.max_connections = 1000;
    
    // 创建多核服务器
    server socks_server(sctx, config);
    co_await socks_server.listen("0.0.0.0", 1080);
    co_await socks_server.run();
}

int main() {
    // 创建包含 4 个工作线程的服务器上下文
    server_context sctx(4);
    sync_wait(example(sctx));
}
```

## 协议详情

### 认证方法

- `0x00` - 无需认证
- `0x01` - GSSAPI（未实现）
- `0x02` - 用户名/密码
- `0xFF` - 无可接受的方法

### 命令

- `0x01` - CONNECT（已支持）
- `0x02` - BIND（未实现）
- `0x03` - UDP ASSOCIATE（未实现）

### 地址类型

- `0x01` - IPv4 地址（4 字节）
- `0x03` - 域名（可变长度）
- `0x04` - IPv6 地址（16 字节）

### 响应代码

- `0x00` - 成功
- `0x01` - 一般 SOCKS 服务器故障
- `0x02` - 规则不允许连接
- `0x03` - 网络不可达
- `0x04` - 主机不可达
- `0x05` - 连接被拒绝
- `0x06` - TTL 过期
- `0x07` - 不支持的命令
- `0x08` - 不支持的地址类型

## 架构

```
src/protocol/socks5/
├── types.cppm           # 协议类型和结构
├── types_impl.cpp       # 序列化/解析实现
├── client.cppm          # SOCKS5 客户端接口
├── client_impl.cpp      # 客户端实现
├── server.cppm          # SOCKS5 服务器接口
├── server_impl.cpp      # 服务器实现
└── README.md            # 文档

src/protocol/socks5.cppm # 主模块导出
```

## 示例

查看 `examples/socks5/` 获取完整示例：
- `client_demo.cpp` - 包含 4 个使用示例的 SOCKS5 客户端
- `server_demo.cpp` - 多核 SOCKS5 服务器

## 测试

可以使用标准工具测试 SOCKS5 实现：

```bash
# 使用 curl 通过 SOCKS5 代理测试
curl --socks5 127.0.0.1:1080 http://www.example.com

# 使用用户名/密码测试
curl --socks5 admin:secret@127.0.0.1:1080 http://www.example.com

# 测试 HTTPS
curl --socks5 127.0.0.1:1080 https://www.example.com
```

## 限制

- 未实现 BIND 命令
- 未实现 UDP ASSOCIATE 命令
- 未实现 GSSAPI 认证
- 服务器中的 DNS 解析使用简单的 IP 解析（没有实际的 DNS 查询）
- 双向中继是简化的（生产环境应使用并发任务）

## 参考资料

- [RFC 1928](https://tools.ietf.org/html/rfc1928) - SOCKS 协议版本 5
- [RFC 1929](https://tools.ietf.org/html/rfc1929) - SOCKS V5 的用户名/密码认证
