module cnetmod.protocol.mongodb;

import std;
import :error;
import :bson_document;
import :server_description;

namespace cnetmod::mongodb {
namespace {
    auto integer(const bson_value* value) -> std::optional<std::int64_t>
    {
        if (!value)
            return {};
        if (auto number = value->get_if<std::int32_t>())
            return *number;
        if (auto number = value->get_if<std::int64_t>())
            return *number;
        return {};
    }

    auto flag(const bson_document& document, std::string_view key) -> bool
    {
        auto value = document.find(key);
        return value && value->get_if<bool>() && *value->get_if<bool>();
    }

    auto string(const bson_value* value) -> std::optional<std::string>
    {
        if (!value)
            return {};
        if (auto text = value->get_if<std::string>())
            return *text;
        return {};
    }
} // namespace

auto server_description::readable() const noexcept -> bool
{
    return kind == server_kind::standalone || kind == server_kind::mongos ||
        kind == server_kind::replica_primary || kind == server_kind::replica_secondary ||
        kind == server_kind::load_balancer;
}

auto server_description::writable() const noexcept -> bool
{
    return kind == server_kind::standalone || kind == server_kind::mongos ||
        kind == server_kind::replica_primary || kind == server_kind::load_balancer;
}

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

auto describe_server(const server_address& address, const bson_document& hello,
    std::chrono::milliseconds rtt) -> result<server_description>
{
    server_description description;
    description.address = address;
    description.round_trip_time = rtt;
    description.last_update = std::chrono::steady_clock::now();
    description.minimum_wire_version = static_cast<std::int32_t>(
        integer(hello.find("minWireVersion")).value_or(0));
    description.maximum_wire_version = static_cast<std::int32_t>(
        integer(hello.find("maxWireVersion")).value_or(0));
    description.replica_set_name = string(hello.find("setName")).value_or("");
    description.primary = string(hello.find("primary"));
    description.set_version = integer(hello.find("setVersion"));
    if (auto id = hello.find("electionId"); id && id->get_if<bson_object_id>())
        description.election_id = *id->get_if<bson_object_id>();
    if (auto timeout = integer(hello.find("logicalSessionTimeoutMinutes")); timeout && *timeout >= 0)
        description.logical_session_timeout = std::chrono::minutes(*timeout);
    if (string(hello.find("msg")).value_or("") == "isdbgrid")
        description.kind = server_kind::mongos;
    else if (hello.contains("serviceId"))
        description.kind = server_kind::load_balancer;
    else if (!description.replica_set_name.empty())
    {
        if (flag(hello, "isWritablePrimary") || flag(hello, "ismaster"))
            description.kind = server_kind::replica_primary;
        else if (flag(hello, "secondary"))
            description.kind = server_kind::replica_secondary;
        else if (flag(hello, "arbiterOnly"))
            description.kind = server_kind::replica_arbiter;
        else
            description.kind = server_kind::replica_other;
    }
    else
        description.kind = server_kind::standalone;
    for (auto field : {"hosts", "passives", "arbiters"})
        if (auto list = hello.find(field); list && list->as_array())
            for (const auto& item : *list->as_array())
                if (auto text = item.get_if<std::string>())
                    if (auto parsed = parse_server_address(*text); parsed)
                        description.hosts.push_back(*parsed);
    if (auto tags = hello.find("tags"); tags && tags->as_document())
        for (const auto& [key, value] : tags->as_document()->elements())
            if (auto text = value.get_if<std::string>())
                description.tags.emplace(key, *text);
    return description;
}
} // namespace cnetmod::mongodb
