module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_NGHTTP2

export module cnetmod.protocol.http:stream_io;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::http {

// =============================================================================
// stream_io — Transport Abstraction Layer
// =============================================================================
// Type-erased async I/O wrapper that decouples HTTP session logic from the
// underlying transport. Supports:
//   - Plain TCP socket
//   - TLS via ssl_stream
//   - [Future] QUIC stream for HTTP/3
//
// Both http1_session and http2_session operate on stream_io instead of
// socket& directly. This is the key extensibility point for HTTP/3.

export class stream_io {
public:
    /// Construct from plain TCP socket (non-owning)
    stream_io(io_context& ctx, socket& sock) noexcept
        : ctx_(ctx), sock_(&sock) {}

#ifdef CNETMOD_HAS_SSL
    /// Construct from SSL stream (non-owning, socket accessed through ssl)
    stream_io(io_context& ctx, socket& sock, ssl_stream& ssl) noexcept
        : ctx_(ctx), sock_(&sock), ssl_(&ssl) {}
#endif

    // Non-copyable, movable
    stream_io(const stream_io&) = delete;
    auto operator=(const stream_io&) -> stream_io& = delete;
    stream_io(stream_io&&) noexcept = default;
    auto operator=(stream_io&&) noexcept -> stream_io& = default;

    /// Async read into buffer
    auto async_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) {
            co_return co_await ssl_->async_read(buf);
        }
#endif
        co_return co_await cnetmod::async_read(ctx_, *sock_, buf);
    }

    /// Async write from buffer
    auto async_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) {
            co_return co_await ssl_->async_write(buf);
        }
#endif
        co_return co_await cnetmod::async_write(ctx_, *sock_, buf);
    }

    /// Write all bytes (loops until complete)
    auto async_write_all(const void* data, std::size_t len)
        -> task<std::expected<void, std::error_code>>
    {
        auto* p = static_cast<const std::byte*>(data);
        std::size_t written = 0;
        while (written < len) {
            auto w = co_await async_write(
                const_buffer{p + written, len - written});
            if (!w) co_return std::unexpected(w.error());
            if (*w == 0)
                co_return std::unexpected(
                    std::make_error_code(std::errc::connection_reset));
            written += *w;
        }
        co_return {};
    }

    /// Close underlying transport
    void close() {
        if (sock_) sock_->close();
    }

    /// Check if this is a TLS connection
    [[nodiscard]] auto is_secure() const noexcept -> bool {
#ifdef CNETMOD_HAS_SSL
        return ssl_ != nullptr;
#else
        return false;
#endif
    }

    /// Access underlying io_context
    [[nodiscard]] auto io_ctx() noexcept -> io_context& { return ctx_; }

    /// Access underlying socket
    [[nodiscard]] auto raw_socket() noexcept -> socket& { return *sock_; }

private:
    io_context& ctx_;
    socket* sock_ = nullptr;
#ifdef CNETMOD_HAS_SSL
    ssl_stream* ssl_ = nullptr;
#endif
};

} // namespace cnetmod::http

#endif // CNETMOD_HAS_NGHTTP2
