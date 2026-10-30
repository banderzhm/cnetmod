export module cnetmod.protocol.mongodb:srv_seedlist;
import std;
import cnetmod.coro.task;
import :error;
import :server_address;

export namespace cnetmod::mongodb {
struct srv_record
{
    server_address address;
    std::chrono::seconds ttl{60};
};

struct srv_dns_records
{
    std::vector<srv_record> servers;
    /// One entry per TXT RR, with its character strings already concatenated.
    std::vector<std::string> txt_records;
};

struct srv_seedlist
{
    std::vector<server_address> seeds;
    std::optional<std::string> txt_options;
    std::chrono::seconds ttl{60};
};

using srv_lookup_callback = std::function<task<result<srv_dns_records>>(std::string, std::string)>;
auto normalize_dns_hostname(std::string_view hostname) -> result<std::string>;
/// Validates every target before selecting any hosts. Priority/weight are ignored.
auto validate_srv_seedlist(std::string_view hostname, srv_dns_records records,
    std::size_t maximum_hosts = 0, std::span<const server_address> existing = {}) -> result<srv_seedlist>;
} // namespace cnetmod::mongodb
