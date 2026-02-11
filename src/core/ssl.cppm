module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#endif

export module cnetmod.core.ssl;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod {

#ifdef CNETMOD_HAS_SSL

// =============================================================================
// OpenSSL 全局初始化（线程安全，只执行一次）
// =============================================================================

namespace detail {

inline void ssl_global_init() noexcept {
    static bool done = [] {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                         OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
        return true;
    }();
    (void)done;
}

} // namespace detail

// =============================================================================
// ssl_error_category — OpenSSL 错误映射到 std::error_code
// =============================================================================

namespace detail {

class ssl_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> const char* override {
        return "openssl";
    }
    auto message(int ev) const -> std::string override {
        if (ev == 0) return "success";
        char buf[256]{};
        ERR_error_string_n(static_cast<unsigned long>(ev), buf, sizeof(buf));
        return buf;
    }
};

inline auto ssl_category() -> const std::error_category& {
    static const ssl_error_category_impl instance;
    return instance;
}

} // namespace detail

/// 从 OpenSSL 错误栈获取 error_code
export inline auto make_ssl_error() -> std::error_code {
    auto e = ERR_get_error();
    if (e == 0) return {};
    return {static_cast<int>(e), detail::ssl_category()};
}

/// 从 SSL_get_error 结果创建 error_code
export inline auto make_ssl_error(int ssl_err) -> std::error_code {
    // 对于 WANT_READ/WANT_WRITE 不应到达这里
    // 对于其他错误，从错误栈取详细信息
    auto e = ERR_get_error();
    if (e != 0)
        return {static_cast<int>(e), detail::ssl_category()};
    // 回退到 ssl_err 本身
    return {ssl_err, detail::ssl_category()};
}

// =============================================================================
// ssl_context — SSL_CTX 的 RAII 封装
// =============================================================================

export class ssl_context {
public:
    ~ssl_context() {
        if (ctx_) SSL_CTX_free(ctx_);
    }

    // 不可复制
    ssl_context(const ssl_context&) = delete;
    auto operator=(const ssl_context&) -> ssl_context& = delete;

    // 可移动
    ssl_context(ssl_context&& o) noexcept : ctx_(std::exchange(o.ctx_, nullptr)) {}
    auto operator=(ssl_context&& o) noexcept -> ssl_context& {
        if (this != &o) {
            if (ctx_) SSL_CTX_free(ctx_);
            ctx_ = std::exchange(o.ctx_, nullptr);
        }
        return *this;
    }

    /// 创建 TLS 客户端上下文
    [[nodiscard]] static auto client() -> std::expected<ssl_context, std::error_code> {
        detail::ssl_global_init();
        auto* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) return std::unexpected(make_ssl_error());
        // 设置合理的最小 TLS 版本
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        return ssl_context{ctx};
    }

    /// 创建 TLS 服务器上下文
    [[nodiscard]] static auto server() -> std::expected<ssl_context, std::error_code> {
        detail::ssl_global_init();
        auto* ctx = SSL_CTX_new(TLS_server_method());
        if (!ctx) return std::unexpected(make_ssl_error());
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        return ssl_context{ctx};
    }

    /// 加载证书文件（PEM）
    [[nodiscard]] auto load_cert_file(std::string_view path)
        -> std::expected<void, std::error_code>
    {
        std::string p(path);
        if (SSL_CTX_use_certificate_chain_file(ctx_, p.c_str()) != 1)
            return std::unexpected(make_ssl_error());
        return {};
    }

    /// 加载私钥文件（PEM）
    [[nodiscard]] auto load_key_file(std::string_view path)
        -> std::expected<void, std::error_code>
    {
        std::string p(path);
        if (SSL_CTX_use_PrivateKey_file(ctx_, p.c_str(), SSL_FILETYPE_PEM) != 1)
            return std::unexpected(make_ssl_error());
        return {};
    }

    /// 加载 CA 证书文件
    [[nodiscard]] auto load_ca_file(std::string_view path)
        -> std::expected<void, std::error_code>
    {
        std::string p(path);
        if (SSL_CTX_load_verify_locations(ctx_, p.c_str(), nullptr) != 1)
            return std::unexpected(make_ssl_error());
        return {};
    }

    /// 加载系统默认 CA 证书
    [[nodiscard]] auto set_default_ca()
        -> std::expected<void, std::error_code>
    {
        if (SSL_CTX_set_default_verify_paths(ctx_) != 1)
            return std::unexpected(make_ssl_error());
        return {};
    }

    /// 设置是否验证对端证书
    void set_verify_peer(bool verify) noexcept {
        SSL_CTX_set_verify(ctx_,
            verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE,
            nullptr);
    }

    /// 获取原生 SSL_CTX 指针
    [[nodiscard]] auto native() const noexcept -> SSL_CTX* { return ctx_; }

private:
    explicit ssl_context(SSL_CTX* ctx) noexcept : ctx_(ctx) {}
    SSL_CTX* ctx_ = nullptr;
};

// =============================================================================
// ssl_stream — 基于 Memory BIO 的异步 SSL 流
// =============================================================================

export class ssl_stream {
public:
    /// 构造 ssl_stream，绑定到已有的 ssl_context、io_context 和 socket
    /// socket 必须已经通过 async_connect 连接（客户端）或从 async_accept 获得（服务器）
    ssl_stream(ssl_context& ssl_ctx, io_context& io_ctx, socket& sock)
        : io_ctx_(io_ctx), sock_(sock)
    {
        ssl_ = SSL_new(ssl_ctx.native());

        // 创建 memory BIO 对
        rbio_ = BIO_new(BIO_s_mem());
        wbio_ = BIO_new(BIO_s_mem());

        // 关联到 SSL：SSL 从 rbio_ 读（网络数据进入），向 wbio_ 写（加密数据输出）
        // SSL_set_bio 会接管 BIO 的所有权
        SSL_set_bio(ssl_, rbio_, wbio_);
    }

    ~ssl_stream() {
        if (ssl_) {
            // SSL_free 会同时释放关联的 BIO
            SSL_free(ssl_);
        }
    }

    // 不可复制
    ssl_stream(const ssl_stream&) = delete;
    auto operator=(const ssl_stream&) -> ssl_stream& = delete;

    // 可移动
    ssl_stream(ssl_stream&& o) noexcept
        : io_ctx_(o.io_ctx_), sock_(o.sock_)
        , ssl_(std::exchange(o.ssl_, nullptr))
        , rbio_(std::exchange(o.rbio_, nullptr))
        , wbio_(std::exchange(o.wbio_, nullptr))
    {}

    /// 设置 SNI 主机名（必须在 handshake 之前调用）
    void set_hostname(std::string_view hostname) {
        std::string h(hostname);
        SSL_set_tlsext_host_name(ssl_, h.c_str());
        // 同时设置证书验证的主机名
        auto* param = SSL_get0_param(ssl_);
        X509_VERIFY_PARAM_set1_host(param, h.c_str(), h.size());
    }

    /// 设置为客户端模式（connect）
    void set_connect_state() noexcept { SSL_set_connect_state(ssl_); }

    /// 设置为服务器模式（accept）
    void set_accept_state() noexcept { SSL_set_accept_state(ssl_); }

    // =========================================================================
    // 异步操作
    // =========================================================================

    /// 异步 TLS 握手
    auto async_handshake() -> task<std::expected<void, std::error_code>> {
        for (;;) {
            int ret = SSL_do_handshake(ssl_);
            if (ret == 1) {
                // 握手完成，刷新 wbio_ 中可能残留的数据
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                co_return {};
            }

            int err = SSL_get_error(ssl_, ret);
            switch (err) {
            case SSL_ERROR_WANT_WRITE: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                break;
            }
            case SSL_ERROR_WANT_READ: {
                // 先刷新输出，再读取输入
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                break;
            }
            default:
                co_return std::unexpected(make_ssl_error(err));
            }
        }
    }

    /// 异步读取解密后的明文
    auto async_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        for (;;) {
            int ret = SSL_read(ssl_, buf.data, static_cast<int>(buf.size));
            if (ret > 0) {
                co_return static_cast<std::size_t>(ret);
            }

            int err = SSL_get_error(ssl_, ret);
            switch (err) {
            case SSL_ERROR_WANT_READ: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                break;
            }
            case SSL_ERROR_WANT_WRITE: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                break;
            }
            case SSL_ERROR_ZERO_RETURN:
                // 对端已关闭 TLS
                co_return static_cast<std::size_t>(0);
            default:
                co_return std::unexpected(make_ssl_error(err));
            }
        }
    }

    /// 异步写入明文（SSL 加密后发送）
    auto async_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        for (;;) {
            int ret = SSL_write(ssl_, buf.data, static_cast<int>(buf.size));
            if (ret > 0) {
                // 加密数据在 wbio_ 中，刷新到 socket
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                co_return static_cast<std::size_t>(ret);
            }

            int err = SSL_get_error(ssl_, ret);
            switch (err) {
            case SSL_ERROR_WANT_WRITE: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                break;
            }
            case SSL_ERROR_WANT_READ: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                break;
            }
            default:
                co_return std::unexpected(make_ssl_error(err));
            }
        }
    }

    /// 异步 TLS 关闭
    auto async_shutdown() -> task<std::expected<void, std::error_code>> {
        // SSL_shutdown 需要调用两次：发送 close_notify + 接收 close_notify
        for (int attempt = 0; attempt < 2; ++attempt) {
            int ret = SSL_shutdown(ssl_);
            if (ret == 1) {
                // 完全关闭
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                co_return {};
            }
            if (ret == 0) {
                // 发送了 close_notify，等待对方的
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                continue;
            }

            int err = SSL_get_error(ssl_, ret);
            switch (err) {
            case SSL_ERROR_WANT_WRITE: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                --attempt; // 重试
                break;
            }
            case SSL_ERROR_WANT_READ: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                --attempt; // 重试
                break;
            }
            default:
                // 关闭时的错误通常可以忽略
                co_return {};
            }
        }
        co_return {};
    }

    /// 获取原生 SSL 指针
    [[nodiscard]] auto native() const noexcept -> SSL* { return ssl_; }

private:
    // =========================================================================
    // BIO 刷新/填充辅助协程
    // =========================================================================

    /// 将 wbio_ 中的加密数据写入 socket
    auto flush_wbio() -> task<std::expected<void, std::error_code>> {
        char tmp[8192];
        for (;;) {
            auto pending = static_cast<int>(BIO_ctrl_pending(wbio_));
            if (pending <= 0) break;

            int n = BIO_read(wbio_, tmp, std::min(pending, static_cast<int>(sizeof(tmp))));
            if (n <= 0) break;

            // 全部写入 socket
            std::size_t written = 0;
            while (written < static_cast<std::size_t>(n)) {
                auto w = co_await cnetmod::async_write(io_ctx_, sock_,
                    const_buffer{tmp + written, static_cast<std::size_t>(n) - written});
                if (!w) co_return std::unexpected(w.error());
                written += *w;
            }
        }
        co_return {};
    }

    /// 从 socket 读取数据写入 rbio_
    auto fill_rbio() -> task<std::expected<void, std::error_code>> {
        std::array<std::byte, 8192> tmp{};
        auto r = co_await cnetmod::async_read(io_ctx_, sock_,
            mutable_buffer{tmp.data(), tmp.size()});
        if (!r) co_return std::unexpected(r.error());
        if (*r == 0) {
            co_return std::unexpected(
                std::make_error_code(std::errc::connection_reset));
        }
        BIO_write(rbio_, tmp.data(), static_cast<int>(*r));
        co_return {};
    }

    io_context& io_ctx_;
    socket&     sock_;
    SSL*        ssl_  = nullptr;
    BIO*        rbio_ = nullptr;  // 网络 -> SSL（接收方向）
    BIO*        wbio_ = nullptr;  // SSL -> 网络（发送方向）
};

#endif // CNETMOD_HAS_SSL

} // namespace cnetmod
