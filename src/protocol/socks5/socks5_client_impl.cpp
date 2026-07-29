module;

module cnetmod.protocol.socks5;

import std;
import :types;
import :client;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::socks5 {

namespace {

    auto read_exact(io_context& ctx, socket& sock, mutable_buffer buffer)
        -> task<std::expected<void, std::error_code>>
    {
        auto* data = static_cast<std::byte*>(buffer.data);
        std::size_t offset = 0;
        while (offset < buffer.size)
        {
            auto read = co_await cnetmod::async_read(
                ctx, sock, mutable_buffer{data + offset, buffer.size - offset});
            if (!read)
            {
                co_return std::unexpected(read.error());
            }
            if (*read == 0)
            {
                co_return std::unexpected(
                    make_error_code(std::errc::connection_reset));
            }
            offset += *read;
        }
        co_return {};
    }

    auto valid_protection_level(std::byte value) noexcept -> bool
    {
        const auto level = static_cast<gssapi_protection_level>(
            std::to_integer<std::uint8_t>(value));
        return level == gssapi_protection_level::integrity ||
            level == gssapi_protection_level::confidentiality ||
            level == gssapi_protection_level::selective;
    }

} // namespace

client::client(io_context& ctx)
    : ctx_(ctx) {}

void client::set_gssapi_context(gssapi_context context,
    gssapi_protection_level protection)
{
    gssapi_context_ = std::move(context);
    gssapi_protection_ = protection;
    gssapi_ready_ = false;
    gssapi_read_buffer_.clear();
    gssapi_read_offset_ = 0;
}

auto& client::socket()
{
    return sock_;
}

auto& client::socket() const
{
    return sock_;
}

auto client::release_socket() -> cnetmod::socket
{
    return std::move(sock_);
}

void client::close()
{
    if (sock_.is_open())
    {
        sock_.close();
    }
    gssapi_ready_ = false;
    gssapi_read_buffer_.clear();
    gssapi_read_offset_ = 0;
}

auto client::connect(std::string_view proxy_host, std::uint16_t proxy_port)
    -> task<std::expected<void, std::error_code>>
{

    gssapi_ready_ = false;
    gssapi_read_buffer_.clear();
    gssapi_read_offset_ = 0;

    auto connect_r =
        co_await async_connect_happy_eyeballs(ctx_, proxy_host, proxy_port);
    if (!connect_r)
    {
        co_return std::unexpected(connect_r.error());
    }
    sock_ = std::move(connect_r->sock);

    // Send authentication method negotiation
    auth_request auth_req;
    if (gssapi_context_ && static_cast<bool>(*gssapi_context_))
    {
        auth_req.methods.push_back(auth_method::gssapi);
    }
    auth_req.methods.push_back(auth_method::no_auth);
    auth_req.methods.push_back(auth_method::username_password);

    auto auth_data = auth_req.serialize();
    auto write_r = co_await async_write_all(
        ctx_, sock_, const_buffer{auth_data.data(), auth_data.size()});
    if (!write_r)
    {
        co_return std::unexpected(write_r.error());
    }

    // Receive authentication method selection
    std::array<std::byte, 2> auth_resp_buf;
    auto read_r = co_await cnetmod::async_read(
        ctx_, sock_, mutable_buffer{auth_resp_buf.data(), auth_resp_buf.size()});
    if (!read_r || *read_r != 2)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    auto auth_resp = auth_response::parse(auth_resp_buf.data(), *read_r);
    if (!auth_resp)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    if (auth_resp->method == auth_method::no_acceptable)
    {
        co_return std::unexpected(make_error_code(std::errc::permission_denied));
    }

    selected_auth_ = auth_resp->method;

    co_return {};
}

auto client::authenticate(std::string_view username, std::string_view password)
    -> task<std::expected<void, std::error_code>>
{

    if (selected_auth_ == auth_method::gssapi)
    {
        co_return co_await authenticate_gssapi();
    }

    if (selected_auth_ != auth_method::username_password)
    {
        // No authentication needed
        co_return {};
    }

    // Send username/password
    username_password_request up_req;
    up_req.username = std::string(username);
    up_req.password = std::string(password);

    auto up_data = up_req.serialize();
    auto write_r = co_await async_write_all(
        ctx_, sock_, const_buffer{up_data.data(), up_data.size()});
    if (!write_r)
    {
        co_return std::unexpected(write_r.error());
    }

    // Receive authentication response
    std::array<std::byte, 2> up_resp_buf;
    auto read_r = co_await cnetmod::async_read(
        ctx_, sock_, mutable_buffer{up_resp_buf.data(), up_resp_buf.size()});
    if (!read_r || *read_r != 2)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    auto up_resp = username_password_response::parse(up_resp_buf.data(), *read_r);
    if (!up_resp || up_resp->status != 0x00)
    {
        co_return std::unexpected(make_error_code(std::errc::permission_denied));
    }

    co_return {};
}

auto client::read_gssapi_message()
    -> task<std::expected<gssapi_message, std::error_code>>
{
    std::array<std::byte, 2> prefix{};
    if (auto read = co_await read_exact(
            ctx_, sock_, mutable_buffer{prefix.data(), prefix.size()});
        !read)
    {
        co_return std::unexpected(read.error());
    }
    if (static_cast<std::uint8_t>(prefix[0]) != GSSAPI_VERSION)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    const auto type = static_cast<gssapi_message_type>(prefix[1]);
    if (type == gssapi_message_type::abort)
    {
        co_return gssapi_message{.type = type};
    }
    if (type != gssapi_message_type::authentication &&
        type != gssapi_message_type::protection_level &&
        type != gssapi_message_type::encapsulated_data)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    std::array<std::byte, 2> length_bytes{};
    if (auto read = co_await read_exact(
            ctx_, sock_,
            mutable_buffer{length_bytes.data(), length_bytes.size()});
        !read)
    {
        co_return std::unexpected(read.error());
    }
    const auto length =
        (static_cast<std::uint16_t>(static_cast<std::uint8_t>(length_bytes[0]))
            << 8) |
        static_cast<std::uint16_t>(static_cast<std::uint8_t>(length_bytes[1]));

    gssapi_message message{.type = type};
    message.token.resize(length);
    if (length != 0)
    {
        if (auto read = co_await read_exact(
                ctx_, sock_,
                mutable_buffer{message.token.data(), message.token.size()});
            !read)
        {
            co_return std::unexpected(read.error());
        }
    }
    co_return message;
}

auto client::write_gssapi_message(const gssapi_message& message)
    -> task<std::expected<void, std::error_code>>
{
    auto serialized = message.serialize();
    if (!serialized)
    {
        co_return std::unexpected(serialized.error());
    }
    auto written = co_await async_write_all(
        ctx_, sock_, const_buffer{serialized->data(), serialized->size()});
    if (!written)
    {
        co_return std::unexpected(written.error());
    }
    co_return {};
}

auto client::authenticate_gssapi()
    -> task<std::expected<void, std::error_code>>
{
    if (!gssapi_context_ || !static_cast<bool>(*gssapi_context_))
    {
        co_return std::unexpected(make_error_code(std::errc::invalid_argument));
    }

    auto step = (*gssapi_context_).step({});
    if (!step)
    {
        co_return std::unexpected(step.error());
    }
    if (step->complete && step->output_token.empty())
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    while (true)
    {
        if (!step->output_token.empty() || !step->complete)
        {
            if (auto write = co_await write_gssapi_message(gssapi_message{
                    .type = gssapi_message_type::authentication,
                    .token = std::move(step->output_token)});
                !write)
            {
                co_return std::unexpected(write.error());
            }
        }

        auto response = co_await read_gssapi_message();
        if (!response)
        {
            co_return std::unexpected(response.error());
        }
        if (response->type == gssapi_message_type::abort)
        {
            co_return std::unexpected(make_error_code(std::errc::permission_denied));
        }
        if (response->type != gssapi_message_type::authentication)
        {
            co_return std::unexpected(make_error_code(std::errc::protocol_error));
        }
        if (step->complete && response->token.empty())
        {
            break;
        }

        step = (*gssapi_context_).step(response->token);
        if (!step)
        {
            co_return std::unexpected(step.error());
        }
        if (step->complete && step->output_token.empty())
        {
            break;
        }
    }

    const std::array<std::byte, 1> requested{
        static_cast<std::byte>(gssapi_protection_)};
    auto wrapped = (*gssapi_context_).wrap(requested, false);
    if (!wrapped)
    {
        co_return std::unexpected(wrapped.error());
    }
    if (auto write = co_await write_gssapi_message(gssapi_message{
            .type = gssapi_message_type::protection_level,
            .token = std::move(*wrapped)});
        !write)
    {
        co_return std::unexpected(write.error());
    }

    auto response = co_await read_gssapi_message();
    if (!response)
    {
        co_return std::unexpected(response.error());
    }
    if (response->type == gssapi_message_type::abort)
    {
        co_return std::unexpected(make_error_code(std::errc::permission_denied));
    }
    if (response->type != gssapi_message_type::protection_level)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    auto unwrapped = (*gssapi_context_).unwrap(response->token);
    if (!unwrapped)
    {
        co_return std::unexpected(unwrapped.error());
    }
    if (unwrapped->confidential || unwrapped->payload.size() != 1 ||
        !valid_protection_level(unwrapped->payload.front()))
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    const auto selected =
        static_cast<gssapi_protection_level>(
            std::to_integer<std::uint8_t>(unwrapped->payload.front()));
    if (gssapi_protection_ != gssapi_protection_level::selective &&
        selected != gssapi_protection_)
    {
        co_return std::unexpected(make_error_code(std::errc::permission_denied));
    }

    gssapi_protection_ = selected;
    gssapi_ready_ = true;
    co_return {};
}

auto client::connect_target(std::string_view target_host,
    std::uint16_t target_port)
    -> task<std::expected<void, std::error_code>>
{
    auto resp = co_await request(command::connect, target_host, target_port);
    if (!resp)
    {
        co_return std::unexpected(resp.error());
    }
    co_return {};
}

auto client::protect_payload(std::span<const std::byte> payload)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (!gssapi_ready_ || !gssapi_context_)
    {
        return std::vector<std::byte>{payload.begin(), payload.end()};
    }
    const bool confidential =
        gssapi_protection_ == gssapi_protection_level::confidentiality;
    auto wrapped = gssapi_context_->wrap(payload, confidential);
    if (!wrapped)
    {
        return std::unexpected(wrapped.error());
    }
    return gssapi_message{
        .type = gssapi_message_type::encapsulated_data,
        .token = std::move(*wrapped)}
        .serialize();
}

auto client::unprotect_payload(std::span<const std::byte> frame)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (!gssapi_ready_ || !gssapi_context_)
    {
        return std::vector<std::byte>{frame.begin(), frame.end()};
    }
    auto message = gssapi_message::parse(frame.data(), frame.size());
    if (!message ||
        message->type != gssapi_message_type::encapsulated_data)
    {
        return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    auto unwrapped = gssapi_context_->unwrap(message->token);
    if (!unwrapped)
    {
        return std::unexpected(unwrapped.error());
    }
    if (gssapi_protection_ == gssapi_protection_level::integrity &&
        unwrapped->confidential)
    {
        return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    if (gssapi_protection_ == gssapi_protection_level::confidentiality &&
        !unwrapped->confidential)
    {
        return std::unexpected(make_error_code(std::errc::permission_denied));
    }
    return std::move(unwrapped->payload);
}

auto client::write_payload(const_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (!gssapi_ready_)
    {
        auto written = co_await async_write_all(ctx_, sock_, buffer);
        if (!written)
        {
            co_return std::unexpected(written.error());
        }
        co_return buffer.size;
    }
    const auto* data = static_cast<const std::byte*>(buffer.data);
    auto protected_data =
        protect_payload(std::span<const std::byte>{data, buffer.size});
    if (!protected_data)
    {
        co_return std::unexpected(protected_data.error());
    }
    auto written = co_await async_write_all(
        ctx_, sock_,
        const_buffer{protected_data->data(), protected_data->size()});
    if (!written)
    {
        co_return std::unexpected(written.error());
    }
    co_return buffer.size;
}

auto client::read_payload(mutable_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (!gssapi_ready_)
    {
        co_return co_await cnetmod::async_read(ctx_, sock_, buffer);
    }
    if (buffer.size == 0)
    {
        co_return std::size_t{0};
    }

    if (gssapi_read_offset_ >= gssapi_read_buffer_.size())
    {
        auto message = co_await read_gssapi_message();
        if (!message)
        {
            co_return std::unexpected(message.error());
        }
        if (message->type != gssapi_message_type::encapsulated_data ||
            !gssapi_context_)
        {
            co_return std::unexpected(make_error_code(std::errc::protocol_error));
        }
        auto unwrapped = gssapi_context_->unwrap(message->token);
        if (!unwrapped)
        {
            co_return std::unexpected(unwrapped.error());
        }
        if (gssapi_protection_ == gssapi_protection_level::integrity &&
            unwrapped->confidential)
        {
            co_return std::unexpected(make_error_code(std::errc::protocol_error));
        }
        if (gssapi_protection_ == gssapi_protection_level::confidentiality &&
            !unwrapped->confidential)
        {
            co_return std::unexpected(make_error_code(std::errc::permission_denied));
        }
        gssapi_read_buffer_ = std::move(unwrapped->payload);
        gssapi_read_offset_ = 0;
    }

    const auto available = gssapi_read_buffer_.size() - gssapi_read_offset_;
    const auto copied = std::min(buffer.size, available);
    std::memcpy(buffer.data, gssapi_read_buffer_.data() + gssapi_read_offset_,
        copied);
    gssapi_read_offset_ += copied;
    if (gssapi_read_offset_ == gssapi_read_buffer_.size())
    {
        gssapi_read_buffer_.clear();
        gssapi_read_offset_ = 0;
    }
    co_return copied;
}

auto client::async_read(mutable_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_return co_await read_payload(buffer);
}

auto client::async_write(const_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_return co_await write_payload(buffer);
}

auto client::protect_udp_datagram(std::span<const std::byte> datagram)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    return protect_payload(datagram);
}

auto client::unprotect_udp_datagram(
    std::span<const std::byte> protected_datagram)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    return unprotect_payload(protected_datagram);
}

auto client::bind(std::string_view target_host, std::uint16_t target_port)
    -> task<std::expected<socks5_address, std::error_code>>
{
    auto resp = co_await request(command::bind, target_host, target_port);
    if (!resp)
    {
        co_return std::unexpected(resp.error());
    }
    co_return resp->bind_address;
}

auto client::wait_bind_peer()
    -> task<std::expected<socks5_address, std::error_code>>
{
    std::array<std::byte, 512> resp_buf;
    auto read_r = co_await read_payload(
        mutable_buffer{resp_buf.data(), resp_buf.size()});
    if (!read_r || *read_r < 4)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    auto resp = socks5_response::parse(resp_buf.data(), *read_r);
    if (!resp)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    if (resp->rep != reply::succeeded)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    co_return resp->bind_address;
}

auto client::udp_associate(std::string_view client_host,
    std::uint16_t client_port)
    -> task<std::expected<socks5_address, std::error_code>>
{
    auto resp =
        co_await request(command::udp_associate, client_host, client_port);
    if (!resp)
    {
        co_return std::unexpected(resp.error());
    }
    co_return resp->bind_address;
}

auto client::request(command cmd, std::string_view host, std::uint16_t port)
    -> task<std::expected<socks5_response, std::error_code>>
{
    // Build SOCKS5 request
    socks5_request req;
    req.cmd = cmd;

    // Determine address type
    auto addr_r = ip_address::from_string(host);
    if (addr_r)
    {
        // It's an IP address
        if (addr_r->is_v4())
        {
            req.address.type = address_type::ipv4;
        }
        else
        {
            req.address.type = address_type::ipv6;
        }
        req.address.host = std::string(host);
    }
    else
    {
        // It's a domain name
        req.address.type = address_type::domain_name;
        req.address.host = std::string(host);
    }
    req.address.port = port;

    // Send request
    auto req_data = req.serialize();
    auto write_r = co_await write_payload(
        const_buffer{req_data.data(), req_data.size()});
    if (!write_r)
    {
        co_return std::unexpected(write_r.error());
    }

    // Receive response
    std::array<std::byte, 512> resp_buf;
    auto read_r = co_await read_payload(
        mutable_buffer{resp_buf.data(), resp_buf.size()});
    if (!read_r || *read_r < 4)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    auto resp = socks5_response::parse(resp_buf.data(), *read_r);
    if (!resp)
    {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    if (resp->rep != reply::succeeded)
    {
        // Map SOCKS5 reply to error code
        switch (resp->rep)
        {
        case reply::connection_refused:
            co_return std::unexpected(make_error_code(std::errc::connection_refused));
        case reply::network_unreachable:
            co_return std::unexpected(
                make_error_code(std::errc::network_unreachable));
        case reply::host_unreachable:
            co_return std::unexpected(make_error_code(std::errc::host_unreachable));
        default:
            co_return std::unexpected(make_error_code(std::errc::protocol_error));
        }
    }

    co_return *resp;
}

} // namespace cnetmod::socks5
