export module cnetmod.protocol.mongodb:server_address;
import std;
import :error;

export namespace cnetmod::mongodb {
struct server_address
{
    std::string host;
    std::uint16_t port = 27017;
    auto operator<=>(const server_address&) const = default;
};

auto parse_server_address(std::string_view address,
    std::uint16_t default_port = 27017) -> result<server_address>;
} // namespace cnetmod::mongodb
