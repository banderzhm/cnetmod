module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.dns.types;

import std;
import cnetmod.core.address;
import cnetmod.coro.task;

namespace cnetmod::dns {

export enum class record_type : std::uint16_t {
    A = 1,
    NS = 2,
    CNAME = 5,
    SOA = 6,
    PTR = 12,
    MX = 15,
    TXT = 16,
    AAAA = 28,
    SRV = 33,
    OPT = 41,
    HTTPS = 65,
};

export enum class record_class : std::uint16_t {
    IN = 1,
};

export enum class response_code : std::uint8_t {
    no_error = 0,
    format_error = 1,
    server_failure = 2,
    name_error = 3,
    not_implemented = 4,
    refused = 5,
};

export struct question {
    std::string name;
    record_type type = record_type::A;
    record_class cls = record_class::IN;
};

export struct resource_record {
    std::string name;
    record_type type = record_type::A;
    record_class cls = record_class::IN;
    std::uint32_t ttl = 0;
    std::vector<std::byte> data;
};

export struct message {
    std::uint16_t id = 0;
    bool query = true;
    bool authoritative = false;
    bool truncated = false;
    bool recursion_desired = true;
    bool recursion_available = false;
    response_code rcode = response_code::no_error;
    std::vector<question> questions;
    std::vector<resource_record> answers;
    std::vector<resource_record> authorities;
    std::vector<resource_record> additionals;
};

export using query_handler =
    std::function<cnetmod::task<message>(const message&, const endpoint&)>;

} // namespace cnetmod::dns
