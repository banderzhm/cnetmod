/// cnetmod.protocol.openai client — connection and HTTP transport

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;

import cnetmod.protocol.http;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.semantics;
#ifdef CNETMOD_HAS_PROTOCOL_SOCKS5
import cnetmod.protocol.socks5;
#endif
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :client;
import :foundation;
import cnetmod.json;

namespace cnetmod::openai {

client::client(io_context& ctx) noexcept
    : ctx_(ctx) {}

client::~client()
{
    close();
}

auto client::connect(connect_options opts)
    -> task<std::expected<void, std::string>>
{
    cancel_token cancellation;
    co_return co_await connect(std::move(opts), cancellation);
}

auto client::connect(connect_options opts, cancel_token& cancellation)
    -> task<std::expected<void, std::string>>
{
    if (cancellation.is_cancelled())
        co_return std::unexpected("connect cancelled");
    close();
    opts_ = std::move(opts);
    auto url_r = http::url::parse(opts_.api_base);
    if (!url_r)
        co_return std::unexpected("invalid api_base URL: " + url_r.error());
    url_ = std::move(*url_r);
    auto result = opts_.proxy_host.empty()
        ? co_await connect_direct(cancellation)
        : co_await connect_proxy(cancellation);
    if (!result)
        co_return std::unexpected(result.error());
    if (url_.scheme == "https")
    {
        auto tls = co_await start_tls(cancellation);
        if (!tls)
        {
            close();
            co_return std::unexpected(tls.error());
        }
    }
    connected_ = true;
    co_return std::expected<void, std::string>{};
}

auto client::connect_direct(cancel_token& cancellation)
    -> task<std::expected<void, std::string>>
{
    auto addr_r = ip_address::from_string(url_.host);
    if (!addr_r)
    {
        auto dns_r = co_await async_resolve(ctx_, url_.host);
        if (!dns_r || dns_r->empty())
        {
            co_return std::unexpected("cannot resolve host: " + url_.host);
        }
        addr_r = ip_address::from_string((*dns_r)[0]);
        if (!addr_r)
        {
            co_return std::unexpected("invalid resolved address");
        }
    }

    auto family = addr_r->is_v4() ? address_family::ipv4 : address_family::ipv6;
    auto sock_r = socket::create(family, socket_type::stream);
    if (!sock_r)
    {
        co_return std::unexpected(std::string("socket create failed"));
    }
    sock_ = std::move(*sock_r);

    auto cr = co_await async_connect(ctx_, sock_,
        endpoint{*addr_r, url_.port}, cancellation);
    if (!cr)
    {
        sock_.close();
        co_return std::unexpected("connect failed: " + cr.error().message());
    }
    co_return std::expected<void, std::string>{};
}

auto client::connect_proxy(cancel_token& cancellation)
    -> task<std::expected<void, std::string>>
{
#ifdef CNETMOD_HAS_PROTOCOL_SOCKS5
    if (opts_.proxy_port == 0)
        co_return std::unexpected("SOCKS5 proxy port is required");
    socks5::client proxy{ctx_};
    auto connected = co_await proxy.connect(opts_.proxy_host, opts_.proxy_port);
    if (!connected)
        co_return std::unexpected("SOCKS5 connection failed: " + connected.error().message());
    if (cancellation.is_cancelled())
        co_return std::unexpected("connect cancelled");
    if ((!opts_.proxy_username.empty() || !opts_.proxy_password.empty()) &&
        proxy.selected_auth() != socks5::auth_method::username_password)
        co_return std::unexpected("SOCKS5 proxy did not select password authentication");
    auto authenticated = co_await proxy.authenticate(opts_.proxy_username, opts_.proxy_password);
    if (!authenticated)
        co_return std::unexpected("SOCKS5 authentication failed: " + authenticated.error().message());
    auto target = co_await proxy.connect_target(url_.host, url_.port);
    if (!target)
        co_return std::unexpected("SOCKS5 target connection failed: " + target.error().message());
    if (cancellation.is_cancelled())
        co_return std::unexpected("connect cancelled");
    sock_ = proxy.release_socket();
    co_return std::expected<void, std::string>{};
#else
    (void)cancellation;
    co_return std::unexpected("SOCKS5 support is not enabled");
#endif
}

auto client::start_tls(cancel_token& cancellation)
    -> task<std::expected<void, std::string>>
{
#ifdef CNETMOD_HAS_SSL
    {
        auto ssl_ctx_r = ssl_context::client();
        if (!ssl_ctx_r)
        {
            sock_.close();
            co_return std::unexpected("ssl context: " + ssl_ctx_r.error().message());
        }
        ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));
        ssl_ctx_->set_verify_peer(opts_.tls_verify);

        if (!opts_.tls_ca_file.empty())
        {
            auto r = ssl_ctx_->load_ca_file(opts_.tls_ca_file);
            if (!r)
            {
                sock_.close();
                co_return std::unexpected("ssl ca: " + r.error().message());
            }
        }
        else if (opts_.tls_verify)
        {
            (void)ssl_ctx_->set_default_ca();
        }

        ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
        ssl_->set_connect_state();
        ssl_->set_hostname(url_.host);

        auto hs = co_await ssl_->async_handshake(cancellation);
        if (!hs)
        {
            sock_.close();
            co_return std::unexpected("ssl handshake: " + hs.error().message());
        }
    }
#else
    (void)cancellation;
    co_return std::unexpected(
        std::string("SSL not available (compiled without OpenSSL)"));
#endif
    co_return std::expected<void, std::string>{};
}

[[nodiscard]] auto client::is_connected() const noexcept -> bool
{
    return connected_ && sock_.is_open();
}

void client::close() noexcept
{
#ifdef CNETMOD_HAS_SSL
    ssl_.reset();
    ssl_ctx_.reset();
#endif
    sock_.close();
    connected_ = false;
    rbuf_.clear();
}

auto client::do_write(const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await do_write(buf, cancellation);
}

auto client::do_write(const_buffer buf, cancel_token& cancellation)
    -> task<std::expected<std::size_t, std::error_code>>
{
#ifdef CNETMOD_HAS_SSL
    if (ssl_)
    {
        auto r = co_await ssl_->async_write_all(buf, cancellation);
        if (!r)
            co_return std::unexpected(r.error());
        co_return buf.size;
    }
#endif
    auto r = co_await async_write_all(ctx_, sock_, buf, cancellation);
    if (!r)
        co_return std::unexpected(r.error());
    co_return buf.size;
}

auto client::do_read(mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
#ifdef CNETMOD_HAS_SSL
    if (ssl_)
    {
        co_return co_await ssl_->async_read(buf);
    }
#endif
    co_return co_await async_read(ctx_, sock_, buf);
}

auto client::do_read_some() -> task<std::optional<std::string>>
{
    std::array<std::byte, 8192> buf{};
    auto r = co_await do_read(mutable_buffer{buf.data(), buf.size()});
    if (!r || *r == 0)
    {
        // A failed/EOF read makes a keep-alive exchange incomplete.  The
        // native socket can still look open after a peer idle timeout, so it
        // must never be returned to a model pool as reusable.
        close();
        co_return std::nullopt;
    }
    co_return std::string(reinterpret_cast<const char*>(buf.data()), *r);
}

auto client::do_read_some(cancel_token& token)
    -> task<std::expected<std::string, std::error_code>>
{
    std::array<std::byte, 8192> buf{};
#ifdef CNETMOD_HAS_SSL
    auto read = ssl_
        ? co_await ssl_->async_read(
              mutable_buffer{buf.data(), buf.size()}, token)
        : co_await async_read(ctx_, sock_,
              mutable_buffer{buf.data(), buf.size()}, token);
#else
    auto read = co_await async_read(ctx_, sock_,
        mutable_buffer{buf.data(), buf.size()}, token);
#endif
    if (!read)
    {
        close();
        co_return std::unexpected(read.error());
    }
    if (*read == 0)
    {
        close();
        co_return std::unexpected(make_error_code(errc::end_of_file));
    }
    co_return std::string(reinterpret_cast<const char*>(buf.data()), *read);
}

[[nodiscard]] auto client::build_path(std::string_view suffix) const
    -> std::string
{
    auto base = url_.path;
    if (base.empty() || base == "/")
    {
        base = "/v1";
    }
    return std::string(base) + std::string(suffix);
}

void client::apply_common_headers(http::request& req, std::string_view accept)
{
    req.set_header("Host", url_.host);
    req.set_header("Authorization", "Bearer " + opts_.api_key);
    req.set_header("Content-Type", "application/json");
    req.set_header("Accept", std::string(accept));
    req.set_header("Connection", "keep-alive");
    for (auto& [k, v] : opts_.extra_headers)
    {
        req.set_header(k, v);
    }
}

auto client::ensure_connected() -> task<std::expected<void, std::string>>
{
    if (is_connected())
    {
        co_return std::expected<void, std::string>{};
    }
    close();
    co_return co_await connect(opts_);
}

auto client::send_http_request(const http::request& req)
    -> task<std::expected<void, std::string>>
{
    cancel_token cancellation;
    auto result = co_await send_http_request(req, cancellation);
    if (!result)
        co_return std::unexpected("write failed: " + result.error().message());
    co_return std::expected<void, std::string>{};
}

auto client::send_http_request(const http::request& req,
    cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    auto data = req.serialize();
    auto wr = co_await do_write(
        const_buffer{data.data(), data.size()}, cancellation);
    if (!wr)
    {
        close();
        co_return std::unexpected(wr.error());
    }
    co_return std::expected<void, std::error_code>{};
}

auto client::read_full_response()
    -> task<std::expected<std::pair<int, std::string>, std::string>>
{
    http::response_parser parser;

    while (!parser.ready())
    {
        auto chunk = co_await do_read_some();
        if (!chunk)
        {
            co_return std::unexpected(
                std::string("connection closed during response"));
        }

        rbuf_ += *chunk;
        auto consumed = parser.consume(rbuf_.data(), rbuf_.size());
        if (!consumed)
        {
            co_return std::unexpected("HTTP parse error: " +
                consumed.error().message());
        }
        rbuf_.erase(0, *consumed);
    }

    co_return std::pair{parser.status_code(), std::string(parser.body())};
}

auto client::read_response_header()
    -> task<std::expected<response_header, std::string>>
{
    cancel_token cancellation;
    auto result = co_await read_response_header(cancellation);
    if (!result)
        co_return std::unexpected("connection closed during header: " +
            result.error().message());
    co_return std::move(*result);
}

auto client::read_response_header(cancel_token& cancellation)
    -> task<std::expected<response_header, std::error_code>>
{
    while (true)
    {
        auto header_end = rbuf_.find("\r\n\r\n");
        if (header_end != std::string::npos)
        {
            auto first_line_end = rbuf_.find("\r\n");
            auto status_line = rbuf_.substr(0, first_line_end);

            int status = 0;
            auto sp1 = status_line.find(' ');
            if (sp1 != std::string::npos)
            {
                auto sp2 = status_line.find(' ', sp1 + 1);
                auto code_str = status_line.substr(
                    sp1 + 1,
                    (sp2 != std::string::npos ? sp2 - sp1 - 1 : std::string::npos));
                std::from_chars(code_str.data(), code_str.data() + code_str.size(),
                    status);
            }

            std::string ct;
            auto ct_pos = rbuf_.find("Content-Type:");
            if (ct_pos == std::string::npos)
            {
                ct_pos = rbuf_.find("content-type:");
            }
            if (ct_pos != std::string::npos)
            {
                auto val_start = ct_pos + 13;
                while (val_start < rbuf_.size() && rbuf_[val_start] == ' ')
                {
                    ++val_start;
                }
                auto val_end = rbuf_.find("\r\n", val_start);
                ct = rbuf_.substr(val_start, val_end - val_start);
            }

            auto header_text = rbuf_.substr(0, header_end);
            std::ranges::transform(header_text, header_text.begin(),
                [](unsigned char value)
                {
                    return static_cast<char>(std::tolower(value));
                });
            const auto chunked = header_text.find("transfer-encoding: chunked") !=
                std::string::npos;
            const auto connection_pos = header_text.find("\r\nconnection:");
            bool connection_close = status_line.starts_with("HTTP/1.0");
            if (connection_pos != std::string::npos)
            {
                const auto connection_end = header_text.find("\r\n", connection_pos + 2);
                const auto value = std::string_view{header_text}.substr(
                    connection_pos, connection_end - connection_pos);
                if (value.contains("close")) connection_close = true;
                else if (value.contains("keep-alive")) connection_close = false;
            }

            std::optional<std::size_t> content_length;
            if (const auto length_pos = header_text.find("content-length:");
                length_pos != std::string::npos)
            {
                auto value_start = length_pos + 15U;
                while (value_start < header_text.size() &&
                    (header_text[value_start] == ' ' ||
                        header_text[value_start] == '\t'))
                    ++value_start;
                const auto value_end = header_text.find("\r\n", value_start);
                const auto value = std::string_view{header_text}.substr(
                    value_start, value_end - value_start);
                std::size_t parsed_length = 0;
                const auto parsed = std::from_chars(value.data(),
                    value.data() + value.size(), parsed_length);
                if (parsed.ec == std::errc{} &&
                    parsed.ptr == value.data() + value.size())
                    content_length = parsed_length;
            }

            rbuf_.erase(0, header_end + 4);
            co_return response_header{.status = status,
                .content_type = std::move(ct),
                .chunked = chunked,
                .content_length = content_length,
                .connection_close = connection_close};
        }

        auto chunk = co_await do_read_some(cancellation);
        if (!chunk)
            co_return std::unexpected(chunk.error());
        rbuf_ += *chunk;
    }
}

auto client::read_remaining_body() -> task<std::string>
{
    cancel_token cancellation;
    auto result = co_await read_remaining_body(cancellation);
    co_return result ? std::move(*result) : std::exchange(rbuf_, {});
}

auto client::read_remaining_body(cancel_token& cancellation)
    -> task<std::expected<std::string, std::error_code>>
{
    for (int i = 0; i < 5; ++i)
    {
        auto chunk = co_await do_read_some(cancellation);
        if (!chunk)
        {
            if (chunk.error() != make_error_code(errc::end_of_file))
                co_return std::unexpected(chunk.error());
            break;
        }
        rbuf_ += *chunk;
    }
    auto result = std::move(rbuf_);
    rbuf_.clear();
    co_return result;
}

auto client::read_binary_response() -> task<
    std::expected<std::pair<int, std::vector<std::byte>>, std::string>>
{
    http::response_parser parser;

    while (!parser.ready())
    {
        auto chunk = co_await do_read_some();
        if (!chunk)
        {
            co_return std::unexpected(
                std::string("connection closed during response"));
        }

        rbuf_ += *chunk;
        auto consumed = parser.consume(rbuf_.data(), rbuf_.size());
        if (!consumed)
        {
            co_return std::unexpected("HTTP parse error: " +
                consumed.error().message());
        }
        rbuf_.erase(0, *consumed);
    }

    auto body = parser.body();
    std::vector<std::byte> data(body.size());
    std::memcpy(data.data(), body.data(), body.size());
    co_return std::pair{parser.status_code(), std::move(data)};
}

} // namespace cnetmod::openai
