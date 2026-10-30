module cnetmod.protocol.mongodb;
import std;
import :error;
import :server_address;

namespace cnetmod::mongodb {
auto parse_server_address(std::string_view input, std::uint16_t default_port)
    -> result<server_address>
{
    if (input.empty())
        return std::unexpected(make_error(error_code::protocol_error,
            "empty MongoDB server address"));
    std::string_view host = input;
    std::string_view port;
    if (input.front() == '[')
    {
        auto closing = input.find(']');
        if (closing == std::string_view::npos)
            return std::unexpected(make_error(
                error_code::protocol_error, "invalid bracketed MongoDB IPv6 address"));
        host = input.substr(1, closing - 1);
        if (closing + 1 < input.size())
        {
            if (input[closing + 1] != ':')
                return std::unexpected(make_error(
                    error_code::protocol_error, "invalid MongoDB address suffix"));
            port = input.substr(closing + 2);
        }
    }
    else if (auto colon = input.rfind(':'); colon != std::string_view::npos &&
        input.find(':') == colon)
    {
        host = input.substr(0, colon);
        port = input.substr(colon + 1);
    }
    std::uint16_t parsed_port = default_port;
    if (!port.empty())
    {
        unsigned number{};
        auto [end, ec] = std::from_chars(port.data(), port.data() + port.size(), number);
        if (ec != std::errc{} || end != port.data() + port.size() || number == 0 || number > 65535)
            return std::unexpected(make_error(error_code::protocol_error,
                "invalid MongoDB server port"));
        parsed_port = static_cast<std::uint16_t>(number);
    }
    if (host.empty())
        return std::unexpected(make_error(error_code::protocol_error,
            "empty MongoDB server host"));
    return server_address{std::string(host), parsed_port};
}

} // namespace cnetmod::mongodb
