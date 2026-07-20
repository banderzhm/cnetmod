module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.dns.codec;

import std;
import cnetmod.core.error;
import cnetmod.core.address;
import cnetmod.protocol.dns.types;

export namespace cnetmod::dns {

auto parse_message(std::span<const std::byte> data)
    -> std::expected<message, std::error_code>;
auto serialize_message(const message &msg)
    -> std::expected<std::vector<std::byte>, std::error_code>;
auto make_query(std::string_view name, record_type type, std::uint16_t id = 0)
    -> message;
auto a_record(std::string_view name, const ipv4_address &addr,
              std::uint32_t ttl = 60) -> resource_record;
auto aaaa_record(std::string_view name, const ipv6_address &addr,
                 std::uint32_t ttl = 60) -> resource_record;
auto txt_record(std::string_view name, std::string_view text,
                std::uint32_t ttl = 60)
    -> std::expected<resource_record, std::error_code>;
auto cname_record(std::string_view name, std::string_view canonical,
                  std::uint32_t ttl = 60)
    -> std::expected<resource_record, std::error_code>;

} // namespace cnetmod::dns
