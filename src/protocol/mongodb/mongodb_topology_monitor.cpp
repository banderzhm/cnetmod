module cnetmod.protocol.mongodb;

import std;
import :error;
import :server_description;
import :topology_monitor;
import :connection;

namespace cnetmod::mongodb {
namespace {
    auto matches_tags(const server_description& server,
        const std::vector<std::map<std::string, std::string>>& tag_sets) -> bool
    {
        if (tag_sets.empty())
            return true;
        return std::ranges::any_of(tag_sets, [&](const auto& set)
            {
                return std::ranges::all_of(set, [&](const auto& tag)
                    {
                        auto found = server.tags.find(tag.first);
                        return found != server.tags.end() && found->second == tag.second;
                    });
            });
    }
} // namespace

topology_monitor::topology_monitor(std::optional<std::string> required)
    : required_replica_set_(std::move(required)) {}

void topology_monitor::update(server_description description)
{
    std::scoped_lock lock(mutex_);
    if (required_replica_set_ && !description.replica_set_name.empty() &&
        description.replica_set_name != *required_replica_set_)
    {
        description.kind = server_kind::unknown;
        description.last_error = make_error(error_code::server_selection_failed,
            "MongoDB replica set name does not match configuration");
    }
    if (description.kind == server_kind::replica_primary)
    {
        for (auto& [_, existing] : servers_)
        {
            if (existing.kind != server_kind::replica_primary ||
                existing.address == description.address)
                continue;
            const bool older_set_version = description.set_version && existing.set_version &&
                *description.set_version < *existing.set_version;
            const bool older_election = description.set_version && existing.set_version &&
                *description.set_version == *existing.set_version && description.election_id &&
                existing.election_id && description.election_id->bytes < existing.election_id->bytes;
            if (older_set_version || older_election)
            {
                description.kind = server_kind::unknown;
                description.last_error = make_error(error_code::server_selection_failed,
                    "stale MongoDB replica set primary description");
                break;
            }
            existing.kind = server_kind::unknown;
            existing.last_error = make_error(error_code::server_selection_failed,
                "MongoDB replica set reported a newer primary");
        }
    }
    for (const auto& discovered : description.hosts)
        if (!servers_.contains(discovered))
            servers_.emplace(discovered, server_description{.address = discovered});
    servers_.insert_or_assign(description.address, std::move(description));
    recompute_kind_locked();
}

void topology_monitor::mark_unknown(const server_address& address, error reason)
{
    std::scoped_lock lock(mutex_);
    auto& server = servers_[address];
    server.address = address;
    server.kind = server_kind::unknown;
    server.last_error = std::move(reason);
    server.last_update = std::chrono::steady_clock::now();
    recompute_kind_locked();
}

auto topology_monitor::kind() const noexcept -> topology_kind
{
    std::scoped_lock lock(mutex_);
    return kind_;
}

auto topology_monitor::snapshot() const -> std::vector<server_description>
{
    std::scoped_lock lock(mutex_);
    std::vector<server_description> result;
    result.reserve(servers_.size());
    for (const auto& [_, server] : servers_)
        result.push_back(server);
    return result;
}

void topology_monitor::recompute_kind_locked()
{
    if (std::ranges::any_of(servers_, [](const auto& item)
            {
                return item.second.kind == server_kind::load_balancer;
            }))
        kind_ = topology_kind::load_balanced;
    else if (std::ranges::any_of(servers_, [](const auto& item)
                 {
                     return item.second.kind == server_kind::mongos;
                 }))
        kind_ = topology_kind::sharded;
    else if (std::ranges::any_of(servers_, [](const auto& item)
                 {
                     return item.second.kind == server_kind::replica_primary;
                 }))
        kind_ = topology_kind::replica_set_with_primary;
    else if (std::ranges::any_of(servers_, [](const auto& item)
                 {
                     return item.second.kind == server_kind::replica_secondary || item.second.kind == server_kind::replica_other;
                 }))
        kind_ = topology_kind::replica_set_no_primary;
    else if (servers_.size() == 1 && servers_.begin()->second.kind == server_kind::standalone)
        kind_ = topology_kind::single;
    else
        kind_ = topology_kind::unknown;
}

auto topology_monitor::select_server(server_selection_options options) const
    -> result<server_description>
{
    auto servers = snapshot();
    auto primary = [](const server_description& s)
    {
        return s.writable();
    };
    auto secondary = [](const server_description& s)
    {
        return s.kind == server_kind::replica_secondary;
    };
    auto eligible = [&](const server_description& s)
    {
        bool role = false;
        switch (options.preference)
        {
        case read_preference::primary:
            role = primary(s);
            break;
        case read_preference::secondary:
            role = secondary(s);
            break;
        case read_preference::nearest:
            role = s.readable();
            break;
        case read_preference::primary_preferred:
            role = std::ranges::any_of(servers, primary) ? primary(s) : secondary(s);
            break;
        case read_preference::secondary_preferred:
            role = std::ranges::any_of(servers, secondary) ? secondary(s) : primary(s);
            break;
        }
        if (!role || !matches_tags(s, options.tag_sets))
            return false;
        if (options.maximum_staleness.count() > 0 && s.kind == server_kind::replica_secondary &&
            std::chrono::steady_clock::now() - s.last_update > options.maximum_staleness)
            return false;
        return true;
    };
    std::erase_if(servers, [&](const auto& server)
        {
            return !eligible(server);
        });
    if (servers.empty())
        return std::unexpected(make_error(error_code::server_selection_failed,
            "no MongoDB server satisfies read preference, tags, and staleness constraints"));
    auto fastest = std::ranges::min_element(servers, {}, [](const auto& server)
        {
            return server.round_trip_time.value_or(std::chrono::milliseconds::max());
        });
    auto limit = fastest->round_trip_time.value_or(std::chrono::milliseconds::max());
    if (limit != std::chrono::milliseconds::max())
        limit += options.local_threshold;
    std::erase_if(servers, [&](const auto& server)
        {
            return server.round_trip_time.value_or(
                       std::chrono::milliseconds::max()) > limit;
        });
    static std::atomic<std::uint64_t> round_robin{};
    return servers[round_robin.fetch_add(1, std::memory_order_relaxed) % servers.size()];
}

auto topology_monitor::check_server(io_context& context, connection_options options)
    -> task<result<server_description>>
{
    server_address address{options.host, options.port};
    connection probe(context);
    auto started = std::chrono::steady_clock::now();
    auto connected = co_await probe.connect(std::move(options));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (!connected)
    {
        mark_unknown(address, connected.error());
        co_return std::unexpected(connected.error());
    }
    auto description = describe_server(address, probe.hello_response(), elapsed);
    if (!description)
    {
        mark_unknown(address, description.error());
        co_return std::unexpected(description.error());
    }
    update(*description);
    co_return *description;
}
} // namespace cnetmod::mongodb
