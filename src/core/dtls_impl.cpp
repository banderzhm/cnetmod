module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <algorithm>
#endif

module cnetmod.core.dtls;

import std;
import cnetmod.core.error;
import cnetmod.executor.async_op;

namespace cnetmod {

#ifdef CNETMOD_HAS_SSL

namespace {

auto endpoint_key(const endpoint& ep) -> std::string {
    return ep.to_string();
}

} // namespace

struct dtls_datagram_session::impl {
    impl(ssl_context& ssl_ctx,
         io_context& io_ctx,
         socket& sock,
         endpoint peer,
         dtls_role role,
         dtls_datagram_options options)
        : io_ctx(io_ctx)
        , sock(sock)
        , peer(std::move(peer))
        , peer_key(endpoint_key(this->peer))
        , options(options)
    {
        ssl = SSL_new(ssl_ctx.native());
        rbio = BIO_new(BIO_s_mem());
        wbio = BIO_new(BIO_s_mem());
        BIO_set_mem_eof_return(rbio, -1);
        BIO_set_mem_eof_return(wbio, -1);
        SSL_set_bio(ssl, rbio, wbio);
        SSL_set_mtu(ssl, static_cast<unsigned int>(std::max<std::size_t>(options.mtu, 576)));
#ifdef DTLS_set_link_mtu
        DTLS_set_link_mtu(ssl, static_cast<unsigned int>(std::max<std::size_t>(options.mtu, 576)));
#endif
        if (role == dtls_role::client) {
            SSL_set_connect_state(ssl);
        } else {
            SSL_set_accept_state(ssl);
        }
    }

    ~impl() {
        if (ssl) {
            SSL_free(ssl);
        }
    }

    auto flush_wbio() -> task<std::expected<void, std::error_code>> {
        std::vector<std::byte> out;
        out.resize(65536);

        for (;;) {
            auto pending = static_cast<int>(BIO_ctrl_pending(wbio));
            if (pending <= 0) {
                break;
            }

            const auto want = std::min<int>(pending, static_cast<int>(out.size()));
            const int n = BIO_read(wbio, out.data(), want);
            if (n <= 0) {
                break;
            }

            auto sent = co_await async_sendto(
                io_ctx,
                sock,
                const_buffer{out.data(), static_cast<std::size_t>(n)},
                peer);
            if (!sent) {
                co_return std::unexpected(sent.error());
            }
        }

        co_return {};
    }

    auto fill_rbio() -> task<std::expected<void, std::error_code>> {
        if (!queued.empty()) {
            BIO_write(rbio, queued.data(), static_cast<int>(queued.size()));
            queued.clear();
            co_return {};
        }

        if (receive) {
            auto datagram = co_await receive();
            if (!datagram) {
                co_return std::unexpected(datagram.error());
            }
            if (datagram->empty()) {
                co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
            }
            BIO_write(rbio, datagram->data(), static_cast<int>(datagram->size()));
            co_return {};
        }

        std::vector<std::byte> in;
        in.resize(std::max<std::size_t>(options.recv_buffer_size, 2048));

        for (;;) {
            endpoint from;
            auto n = co_await async_recvfrom(
                io_ctx,
                sock,
                mutable_buffer{in.data(), in.size()},
                from);
            if (!n) {
                co_return std::unexpected(n.error());
            }
            if (endpoint_key(from) != peer_key) {
                continue;
            }
            if (*n == 0) {
                continue;
            }
            BIO_write(rbio, in.data(), static_cast<int>(*n));
            co_return {};
        }
    }

    io_context& io_ctx;
    socket& sock;
    endpoint peer;
    std::string peer_key;
    dtls_datagram_options options;
    SSL* ssl = nullptr;
    BIO* rbio = nullptr;
    BIO* wbio = nullptr;
    std::vector<std::byte> queued;
    dtls_datagram_session::receive_handler receive;
};

dtls_datagram_session::dtls_datagram_session(ssl_context& ssl_ctx,
                                             io_context& io_ctx,
                                             socket& sock,
                                             endpoint peer,
                                             dtls_role role,
                                             dtls_datagram_options options)
    : impl_(std::make_unique<impl>(ssl_ctx, io_ctx, sock, std::move(peer), role, options))
{}

dtls_datagram_session::~dtls_datagram_session() = default;

dtls_datagram_session::dtls_datagram_session(dtls_datagram_session&&) noexcept = default;

auto dtls_datagram_session::operator=(dtls_datagram_session&&) noexcept
    -> dtls_datagram_session& = default;

void dtls_datagram_session::set_hostname(std::string_view hostname) {
    std::string h(hostname);
    auto* param = SSL_get0_param(impl_->ssl);

    if (auto ip = ip_address::from_string(h)) {
        auto literal = ip->to_string();
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        X509_VERIFY_PARAM_set1_ip_asc(param, literal.c_str());
#else
        X509_VERIFY_PARAM_set1_host(param, literal.c_str(), literal.size());
#endif
        return;
    }

    SSL_set_tlsext_host_name(impl_->ssl, h.c_str());
    X509_VERIFY_PARAM_set1_host(param, h.c_str(), h.size());
}

void dtls_datagram_session::queue_datagram(const_buffer datagram) {
    const auto* first = static_cast<const std::byte*>(datagram.data);
    impl_->queued.assign(first, first + datagram.size);
}

void dtls_datagram_session::set_receive_handler(receive_handler handler) {
    impl_->receive = std::move(handler);
}

auto dtls_datagram_session::peer() const noexcept -> const endpoint& {
    return impl_->peer;
}

auto dtls_datagram_session::native() const noexcept -> void* {
    return impl_->ssl;
}

auto dtls_datagram_session::async_handshake()
    -> task<std::expected<void, std::error_code>>
{
    for (;;) {
        const int ret = SSL_do_handshake(impl_->ssl);
        if (ret == 1) {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            co_return {};
        }

        const int err = SSL_get_error(impl_->ssl, ret);
        switch (err) {
        case SSL_ERROR_WANT_WRITE: {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_WANT_READ: {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await impl_->fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            break;
        }
        default:
            co_return std::unexpected(make_ssl_error(err));
        }
    }
}

auto dtls_datagram_session::async_read(mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    for (;;) {
        const int ret = SSL_read(impl_->ssl, buf.data, static_cast<int>(buf.size));
        if (ret > 0) {
            co_return static_cast<std::size_t>(ret);
        }

        const int err = SSL_get_error(impl_->ssl, ret);
        switch (err) {
        case SSL_ERROR_WANT_READ: {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await impl_->fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            break;
        }
        case SSL_ERROR_WANT_WRITE: {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_ZERO_RETURN:
            co_return static_cast<std::size_t>(0);
        default:
            co_return std::unexpected(make_ssl_error(err));
        }
    }
}

auto dtls_datagram_session::async_write(const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    for (;;) {
        const int ret = SSL_write(impl_->ssl, buf.data, static_cast<int>(buf.size));
        if (ret > 0) {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            co_return static_cast<std::size_t>(ret);
        }

        const int err = SSL_get_error(impl_->ssl, ret);
        switch (err) {
        case SSL_ERROR_WANT_WRITE: {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_WANT_READ: {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await impl_->fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            break;
        }
        default:
            co_return std::unexpected(make_ssl_error(err));
        }
    }
}

auto dtls_datagram_session::async_shutdown()
    -> task<std::expected<void, std::error_code>>
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        const int ret = SSL_shutdown(impl_->ssl);
        if (ret == 1) {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            co_return {};
        }
        if (ret == 0) {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await impl_->fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            continue;
        }

        const int err = SSL_get_error(impl_->ssl, ret);
        switch (err) {
        case SSL_ERROR_WANT_WRITE: {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            --attempt;
            break;
        }
        case SSL_ERROR_WANT_READ: {
            auto flushed = co_await impl_->flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await impl_->fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            --attempt;
            break;
        }
        default:
            co_return {};
        }
    }

    co_return {};
}

#endif // CNETMOD_HAS_SSL

} // namespace cnetmod
