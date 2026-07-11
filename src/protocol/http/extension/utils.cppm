export module cnetmod.protocol.http:utils;
import std;
import :router;
export namespace cnetmod::http
{
    [[nodiscard]] auto resolve_client_ip(const request_context&, std::string_view fallback = "unknown") -> std::string;

    [[nodiscard]] auto parse_query_param(std::string_view query, std::string_view key) -> std::string;

    [[nodiscard]] auto parse_query_params(std::string_view query) -> std::unordered_map<std::string, std::string>;
}
/**/