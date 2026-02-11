module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::redis {

// =============================================================================
// RESP 协议类型
// =============================================================================

export enum class resp_type { simple, error, integer, bulk, array, nil };

export struct resp_value {
    resp_type type = resp_type::nil;
    std::string str;
    std::int64_t num = 0;
    std::vector<resp_value> elems;

    auto ok()     const noexcept -> bool { return type == resp_type::simple && str == "OK"; }
    auto is_err() const noexcept -> bool { return type == resp_type::error; }
    auto is_nil() const noexcept -> bool { return type == resp_type::nil; }

    auto to_string() const -> std::string {
        switch (type) {
        case resp_type::simple:  return std::format("\"{}\"", str);
        case resp_type::error:   return std::format("(error) {}", str);
        case resp_type::integer: return std::format("(integer) {}", num);
        case resp_type::bulk:    return std::format("\"{}\"", str);
        case resp_type::nil:     return "(nil)";
        case resp_type::array: {
            std::string s = "[";
            for (std::size_t i = 0; i < elems.size(); ++i) {
                if (i) s += ", ";
                s += elems[i].to_string();
            }
            return s + "]";
        }
        }
        return "?";
    }
};

// =============================================================================
// RESP 编码
// =============================================================================

/// 将命令参数编码为 RESP array of bulk strings
/// {"SET","k","v"} -> "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n"
export auto resp_encode(std::initializer_list<std::string_view> args) -> std::string {
    std::string o;
    o.reserve(args.size() * 16);
    std::format_to(std::back_inserter(o), "*{}\r\n", args.size());
    for (auto a : args) {
        std::format_to(std::back_inserter(o), "${}\r\n", a.size());
        o.append(a);
        o.append("\r\n");
    }
    return o;
}

/// span 重载，适用于动态构造的参数列表
export auto resp_encode(std::span<const std::string_view> args) -> std::string {
    std::string o;
    o.reserve(args.size() * 16);
    std::format_to(std::back_inserter(o), "*{}\r\n", args.size());
    for (auto a : args) {
        std::format_to(std::back_inserter(o), "${}\r\n", a.size());
        o.append(a);
        o.append("\r\n");
    }
    return o;
}

// =============================================================================
// 连接选项
// =============================================================================

export struct connect_options {
    std::string host     = "127.0.0.1";
    std::uint16_t port   = 6379;
    std::string password;            // AUTH 密码（空 = 不认证）
    std::string username;            // Redis 6+ ACL 用户名（空 = default）
    std::uint32_t db     = 0;       // SELECT 数据库号（0 = 默认）
};

// =============================================================================
// redis::client — 单连接异步客户端
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx) noexcept : ctx_(ctx) {}

    /// 连接 Redis，自动处理 AUTH 和 SELECT
    auto connect(connect_options opts = {}) -> task<resp_value> {
        // 解析地址
        auto addr_r = ip_address::from_string(opts.host);
        if (!addr_r) co_return resp_value{.type = resp_type::error, .str = "invalid host"};

        auto family = addr_r->is_v4() ? address_family::ipv4 : address_family::ipv6;
        auto sock_r = socket::create(family, socket_type::stream);
        if (!sock_r) co_return resp_value{.type = resp_type::error, .str = "socket create failed"};
        sock_ = std::move(*sock_r);

        auto cr = co_await async_connect(ctx_, sock_, endpoint{*addr_r, opts.port});
        if (!cr) {
            sock_.close();
            co_return resp_value{.type = resp_type::error, .str = cr.error().message()};
        }

        // AUTH
        if (!opts.password.empty()) {
            resp_value auth_r;
            if (opts.username.empty()) {
                auth_r = co_await raw_send(resp_encode({"AUTH", opts.password}));
            } else {
                auth_r = co_await raw_send(resp_encode({"AUTH", opts.username, opts.password}));
            }
            if (!auth_r.ok()) {
                sock_.close();
                co_return auth_r;
            }
        }

        // SELECT db
        if (opts.db > 0) {
            auto db_str = std::to_string(opts.db);
            auto sel_r = co_await raw_send(resp_encode({"SELECT", db_str}));
            if (!sel_r.ok()) {
                sock_.close();
                co_return sel_r;
            }
        }

        co_return resp_value{.type = resp_type::simple, .str = "OK"};
    }

    /// 发送单条命令，等待回复
    /// 非协程包装：initializer_list 在此同步消费，安全跨越 co_await
    auto cmd(std::initializer_list<std::string_view> args) -> task<resp_value> {
        return raw_send(resp_encode(args));
    }

    /// Pipeline: 多条命令在一个 TCP write 中发送
    auto pipe(std::initializer_list<std::initializer_list<std::string_view>> cmds)
        -> task<std::vector<resp_value>>
    {
        std::string batch;
        std::size_t n = 0;
        for (auto& c : cmds) { batch += resp_encode(c); ++n; }
        return raw_recv_batch(std::move(batch), n);
    }

    auto is_open() const noexcept -> bool { return sock_.is_open(); }
    void close() noexcept { sock_.close(); }

private:
    // ── 发送 + 接收 ─────────────────────────────────────────────────────

    auto raw_send(std::string data) -> task<resp_value> {
        auto w = co_await async_write(ctx_, sock_, buffer(std::string_view{data}));
        if (!w) co_return resp_value{.type = resp_type::error, .str = w.error().message()};
        co_return co_await parse_one();
    }

    auto raw_recv_batch(std::string batch, std::size_t count)
        -> task<std::vector<resp_value>>
    {
        auto w = co_await async_write(ctx_, sock_, buffer(std::string_view{batch}));
        std::vector<resp_value> out;
        if (!w) co_return out;
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            out.push_back(co_await parse_one());
        co_return out;
    }

    // ── RESP 解析器 ─────────────────────────────────────────────────────

    auto parse_one() -> task<resp_value> {
        auto line = co_await read_line();
        if (line.empty())
            co_return resp_value{.type = resp_type::error, .str = "connection closed"};

        auto body = std::string_view(line).substr(1);

        switch (line[0]) {
        case '+':
            co_return resp_value{.type = resp_type::simple, .str = std::string(body)};
        case '-':
            co_return resp_value{.type = resp_type::error, .str = std::string(body)};
        case ':': {
            std::int64_t v{};
            std::from_chars(body.data(), body.data() + body.size(), v);
            co_return resp_value{.type = resp_type::integer, .num = v};
        }
        case '$': {
            int len{};
            std::from_chars(body.data(), body.data() + body.size(), len);
            if (len < 0) co_return resp_value{};
            auto raw = co_await read_n(static_cast<std::size_t>(len) + 2);
            co_return resp_value{
                .type = resp_type::bulk,
                .str  = raw.substr(0, static_cast<std::size_t>(len))
            };
        }
        case '*': {
            int cnt{};
            std::from_chars(body.data(), body.data() + body.size(), cnt);
            if (cnt < 0) co_return resp_value{};
            resp_value arr{.type = resp_type::array};
            arr.elems.reserve(static_cast<std::size_t>(cnt));
            for (int i = 0; i < cnt; ++i)
                arr.elems.push_back(co_await parse_one());
            co_return arr;
        }
        default:
            co_return resp_value{.type = resp_type::error, .str = "unknown resp prefix"};
        }
    }

    // ── 读缓冲 ─────────────────────────────────────────────────────────

    auto read_line() -> task<std::string> {
        for (;;) {
            if (auto p = rbuf_.find("\r\n", rpos_); p != std::string::npos) {
                auto s = rbuf_.substr(rpos_, p - rpos_);
                rpos_ = p + 2;
                co_return s;
            }
            if (!co_await fill()) co_return "";
        }
    }

    auto read_n(std::size_t n) -> task<std::string> {
        while (rbuf_.size() - rpos_ < n)
            if (!co_await fill()) co_return "";
        auto s = rbuf_.substr(rpos_, n);
        rpos_ += n;
        if (rpos_ > 8192) { rbuf_.erase(0, rpos_); rpos_ = 0; }
        co_return s;
    }

    auto fill() -> task<bool> {
        std::array<std::byte, 4096> tmp{};
        auto r = co_await async_read(ctx_, sock_, buffer(tmp));
        if (!r || *r == 0) co_return false;
        rbuf_.append(reinterpret_cast<const char*>(tmp.data()), *r);
        co_return true;
    }

    io_context& ctx_;
    socket      sock_;
    std::string rbuf_;
    std::size_t rpos_ = 0;
};

} // namespace cnetmod::redis
