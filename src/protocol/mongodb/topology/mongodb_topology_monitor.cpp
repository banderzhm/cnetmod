module cnetmod.protocol.mongodb;

import std;
import cnetmod.coro.cancel;
import :error;
import :authentication;
import :bson_document;
import :server_description;
import :topology_monitor;
import :connection;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::mongodb {
topology_monitor::topology_monitor(std::optional<std::string> required)
    : required_replica_set_(std::move(required)) {}

void topology_monitor::update(server_description description)
{
    concurrent_containers::exclusive_latch_guard lock{topology_latch_};
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

void topology_monitor::retain_servers(std::span<const server_address> addresses)
{
    concurrent_containers::exclusive_latch_guard lock{topology_latch_};
    std::erase_if(servers_, [&](const auto& item)
        {
            return std::ranges::find(addresses, item.first) == addresses.end();
        });
    recompute_kind_locked();
}

void topology_monitor::mark_unknown(const server_address& address, error reason)
{
    concurrent_containers::exclusive_latch_guard lock{topology_latch_};
    auto& server = servers_[address];
    server.address = address;
    server.kind = server_kind::unknown;
    server.last_error = std::move(reason);
    server.last_update = std::chrono::steady_clock::now();
    recompute_kind_locked();
}

auto topology_monitor::kind() const noexcept -> topology_kind
{
    concurrent_containers::shared_latch_guard lock{topology_latch_};
    return kind_;
}

auto topology_monitor::snapshot() const -> std::vector<server_description>
{
    concurrent_containers::shared_latch_guard lock{topology_latch_};
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
    const auto invalid = [](std::string message) -> result<server_description>
    {
        return std::unexpected(make_error(error_code::server_selection_failed, std::move(message)));
    };
    const auto heartbeat = heartbeat_ms_.load(std::memory_order_relaxed);
    if (static_cast<unsigned>(options.preference) > static_cast<unsigned>(read_preference::nearest) ||
        options.local_threshold.count() < 0 || options.maximum_staleness.count() < -1)
        return invalid("negative MongoDB selection threshold");
    if (options.preference == read_preference::primary &&
        (std::ranges::any_of(options.tag_sets, [](const auto& tags)
             {
                 return !tags.empty();
             }) ||
            options.maximum_staleness.count() > 0))
        return invalid("primary read preference cannot specify tags or max staleness");
    auto servers = snapshot();
    const auto primary = std::ranges::find_if(servers, [](const auto& server)
        {
            return server.kind == server_kind::replica_primary;
        });
    const bool replica_set = std::ranges::any_of(servers, [](const auto& server)
        {
            return server.kind == server_kind::replica_primary || server.kind == server_kind::replica_secondary;
        });
    const long double maximum_staleness = static_cast<long double>(options.maximum_staleness.count()) * 1000;
    if (replica_set && maximum_staleness > 0 &&
        (options.maximum_staleness < std::chrono::seconds{90} || maximum_staleness < heartbeat + 10000.0L))
        return invalid("MongoDB max staleness must be at least 90 seconds and heartbeat plus 10 seconds");
    std::optional<std::chrono::milliseconds> newest_secondary_write;
    for (const auto& server : servers)
        if (server.kind == server_kind::replica_secondary && server.last_write_date &&
            (!newest_secondary_write || *server.last_write_date > *newest_secondary_write))
            newest_secondary_write = server.last_write_date;
    const auto stale = [&](const server_description& server)
    {
        if (maximum_staleness <= 0 || server.kind != server_kind::replica_secondary)
            return false;
        if (!server.last_write_date)
            return true;
        long double lag{};
        if (primary != servers.end())
        {
            if (!primary->last_write_date)
                return true;
            const auto update_difference = std::chrono::duration<long double, std::milli>(
                server.last_update - primary->last_update)
                                               .count();
            lag = update_difference - static_cast<long double>(server.last_write_date->count()) +
                static_cast<long double>(primary->last_write_date->count()) + heartbeat;
        }
        else
            lag = static_cast<long double>(newest_secondary_write->count()) -
                static_cast<long double>(server.last_write_date->count()) + heartbeat;
        return lag > maximum_staleness;
    };
    std::vector<server_description> candidates;
    // Routers and direct standalone servers choose locally without applying
    // replica-set tags. Routers receive $readPreference in the command itself.
    for (const auto& server : servers)
        if (server.kind == server_kind::mongos || server.kind == server_kind::load_balancer ||
            server.kind == server_kind::standalone)
            candidates.push_back(server);
    if (candidates.empty())
    {
        const bool prefer_primary = options.preference == read_preference::primary ||
            (options.preference == read_preference::primary_preferred && primary != servers.end());
        if (prefer_primary)
        {
            if (primary != servers.end())
                candidates.push_back(*primary);
        }
        else
        {
            for (const auto& server : servers)
                if ((server.kind == server_kind::replica_secondary ||
                        (options.preference == read_preference::nearest && server.kind == server_kind::replica_primary)) &&
                    !stale(server))
                    candidates.push_back(server);
            if (!options.tag_sets.empty())
            {
                std::vector<server_description> tagged;
                for (const auto& tags : options.tag_sets)
                {
                    for (const auto& server : candidates)
                        if (std::ranges::all_of(tags, [&](const auto& tag)
                                {
                                    const auto found = server.tags.find(tag.first);
                                    return found != server.tags.end() && found->second == tag.second;
                                }))
                            tagged.push_back(server);
                    if (!tagged.empty())
                        break;
                }
                candidates = std::move(tagged);
            }
            // Fall back only after filtering secondaries by staleness and tags.
            if (candidates.empty() && options.preference == read_preference::secondary_preferred &&
                primary != servers.end())
                candidates.push_back(*primary);
        }
    }
    servers = std::move(candidates);
    if (servers.empty())
        return std::unexpected(make_error(error_code::server_selection_failed,
            "no MongoDB server satisfies read preference, tags, and staleness constraints"));
    auto fastest = std::ranges::min_element(servers, {}, [](const auto& server)
        {
            return server.round_trip_time.value_or(std::chrono::milliseconds::max());
        });
    auto limit = fastest->round_trip_time.value_or(std::chrono::milliseconds::max());
    if (limit != std::chrono::milliseconds::max())
        limit += std::min(options.local_threshold, std::chrono::milliseconds::max() - limit);
    std::erase_if(servers, [&](const auto& server)
        {
            return server.round_trip_time.value_or(
                       std::chrono::milliseconds::max()) > limit;
        });
    static std::atomic<std::uint64_t> round_robin{};
    return servers[round_robin.fetch_add(1, std::memory_order_relaxed) % servers.size()];
}

void topology_monitor::set_heartbeat_frequency(std::chrono::milliseconds frequency) noexcept
{
    heartbeat_ms_.store(std::max<std::int64_t>(500, frequency.count()), std::memory_order_relaxed);
}

auto topology_monitor::check_server(io_context& context, connection_options options, cancel_token* cancellation)
    -> task<result<server_description>>
{
    server_address address{options.host, options.port};
    connection probe(context);
    // Monitoring sockets must not authenticate. Measure a hello round trip,
    // excluding connection setup and authentication from the latency window.
    options.authentication = authentication_mechanism::automatic;
    options.username.clear();
    options.password.clear();
    auto connected = cancellation ? co_await probe.connect(std::move(options), *cancellation)
                                  : co_await probe.connect(std::move(options));
    if (!connected)
    {
        mark_unknown(address, connected.error());
        co_return std::unexpected(connected.error());
    }
    const auto started = std::chrono::steady_clock::now();
    auto hello = cancellation ? co_await probe.command("admin", bson_document{{"hello", std::int32_t{1}}}, *cancellation)
                              : co_await probe.command("admin", bson_document{{"hello", std::int32_t{1}}});
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (!hello)
    {
        mark_unknown(address, hello.error());
        co_return std::unexpected(hello.error());
    }
    auto description = describe_server(address, *hello, elapsed);
    if (!description)
    {
        mark_unknown(address, description.error());
        co_return std::unexpected(description.error());
    }
    update(*description);
    co_return *description;
}
} // namespace cnetmod::mongodb
