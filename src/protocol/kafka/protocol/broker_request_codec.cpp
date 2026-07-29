module cnetmod.protocol.kafka.broker_request_codec;
import std;

namespace cnetmod::kafka::protocol {
auto encode_api_versions() -> bytes
{
    return {};
}

auto decode_api_versions(std::span<const std::byte> b, std::int16_t v)
    -> result<api_versions_response>
{
    decoder d(b);
    api_versions_response out;
    auto e = d.int16();
    auto n = d.int32();
    if (!e || !n || *n < 0)
        return std::unexpected(make_error(error_code::malformed_response,
            "invalid ApiVersions response"));
    out.error = static_cast<error_code>(*e);
    for (int i = 0; i < *n; ++i)
    {
        auto k = d.int16();
        auto lo = d.int16();
        auto hi = d.int16();
        if (!k || !lo || !hi)
            return std::unexpected(
                make_error(error_code::malformed_response, "truncated ApiVersions"));
        out.versions.push_back({static_cast<api_key>(*k), *lo, *hi});
    }
    if (v >= 1)
    {
        auto t = d.int32();
        if (t)
            out.throttle_ms = *t;
    }
    return out;
}

auto encode_metadata(std::span<const std::string> topics) -> bytes
{
    encoder e;
    e.int32(static_cast<std::int32_t>(topics.size()));
    for (auto& t : topics)
        e.string(t);
    e.boolean(true);
    e.boolean(false);
    return std::move(e).take();
}

auto decode_metadata(std::span<const std::byte> b, std::int16_t v)
    -> result<metadata_response>
{
    decoder d(b);
    metadata_response out;
    if (v >= 3)
    {
        auto x = d.int32();
        if (!x)
            return std::unexpected(x.error());
        out.throttle_ms = *x;
    }
    auto nb = d.int32();
    if (!nb || *nb < 0)
        return std::unexpected(
            make_error(error_code::malformed_response, "invalid broker array"));
    for (int i = 0; i < *nb; ++i)
    {
        auto id = d.int32();
        auto host = d.string();
        auto port = d.int32();
        if (!id || !host || !port)
            return std::unexpected(
                make_error(error_code::malformed_response, "truncated broker"));
        std::optional<std::string> rack;
        if (v >= 1)
        {
            auto x = d.nullable_string();
            if (!x)
                return std::unexpected(x.error());
            rack = std::move(*x);
        }
        out.brokers.push_back({*id, std::move(*host),
            static_cast<std::uint16_t>(*port), std::move(rack)});
    }
    if (v >= 2)
    {
        auto x = d.nullable_string();
        if (!x)
            return std::unexpected(x.error());
        out.cluster_id = std::move(*x);
    }
    if (v >= 1)
    {
        auto x = d.int32();
        if (!x)
            return std::unexpected(x.error());
        out.controller_id = *x;
    }
    auto nt = d.int32();
    if (!nt || *nt < 0)
        return std::unexpected(
            make_error(error_code::malformed_response, "invalid topic array"));
    for (int i = 0; i < *nt; ++i)
    {
        topic_metadata tm;
        auto e = d.int16();
        auto name = d.string();
        if (!e || !name)
            return std::unexpected(
                make_error(error_code::malformed_response, "truncated topic"));
        tm.error = static_cast<error_code>(*e);
        tm.name = std::move(*name);
        if (v >= 1)
        {
            auto x = d.boolean();
            if (!x)
                return std::unexpected(x.error());
            tm.internal = *x;
        }
        auto np = d.int32();
        if (!np || *np < 0)
            return std::unexpected(
                make_error(error_code::malformed_response, "invalid partitions"));
        for (int p = 0; p < *np; ++p)
        {
            partition_metadata pm;
            auto pe = d.int16();
            auto pi = d.int32();
            auto leader = d.int32();
            if (!pe || !pi || !leader)
                return std::unexpected(
                    make_error(error_code::malformed_response, "truncated partition"));
            pm.error = static_cast<error_code>(*pe);
            pm.partition = *pi;
            pm.leader = *leader;
            if (v >= 7)
            {
                auto le = d.int32();
                if (!le)
                    return std::unexpected(le.error());
                pm.leader_epoch = *le;
            }
            auto read_ids = [&](std::vector<std::int32_t>& ids) -> result<void>
            {
                auto n = d.int32();
                if (!n || *n < 0)
                    return std::unexpected(make_error(error_code::malformed_response,
                        "invalid replica array"));
                for (int q = 0; q < *n; ++q)
                {
                    auto x = d.int32();
                    if (!x)
                        return std::unexpected(x.error());
                    ids.push_back(*x);
                }
                return {};
            };
            if (auto x = read_ids(pm.replicas); !x)
                return std::unexpected(x.error());
            if (auto x = read_ids(pm.isr); !x)
                return std::unexpected(x.error());
            if (v >= 5)
            {
                std::vector<std::int32_t> offline;
                if (auto x = read_ids(offline); !x)
                    return std::unexpected(x.error());
            }
            tm.partitions.push_back(std::move(pm));
        }
        out.topics.push_back(std::move(tm));
    }
    return out;
}

auto encode_produce(const produce_request& r, std::int16_t) -> bytes
{
    encoder e;
    e.nullable_string(r.transactional_id);
    e.int16(static_cast<std::int16_t>(r.acks));
    e.int32(static_cast<std::int32_t>(r.timeout.count()));
    std::map<std::string, std::vector<const produce_partition*>> groups;
    for (auto& p : r.partitions)
        groups[p.target.topic].push_back(&p);
    e.int32(static_cast<std::int32_t>(groups.size()));
    for (auto& [topic, parts] : groups)
    {
        e.string(topic);
        e.int32(static_cast<std::int32_t>(parts.size()));
        for (auto* p : parts)
        {
            e.int32(p->target.partition);
            e.int32(static_cast<std::int32_t>(p->records.size()));
            e.raw(p->records);
        }
    }
    return std::move(e).take();
}

auto decode_produce(std::span<const std::byte> b, std::int16_t v,
    const produce_request& request)
    -> result<std::vector<produce_result>>
{
    decoder d(b);
    std::map<topic_partition, produce_result> decoded;
    auto topic_count = d.int32();
    if (!topic_count || *topic_count < 0)
        return std::unexpected(make_error(error_code::malformed_response,
            "invalid Produce topic array"));
    for (std::int32_t i = 0; i < *topic_count; ++i)
    {
        auto topic = d.string();
        auto partition_count = d.int32();
        if (!topic || !partition_count || *partition_count < 0)
            return std::unexpected(make_error(error_code::malformed_response,
                "truncated Produce topic response"));
        for (std::int32_t p = 0; p < *partition_count; ++p)
        {
            auto partition = d.int32();
            auto ec = d.int16();
            auto offset = d.int64();
            if (!partition || !ec || !offset)
                return std::unexpected(
                    make_error(error_code::malformed_response,
                        "truncated Produce partition response"));
            produce_result item{
                {*topic, *partition}, static_cast<error_code>(*ec), *offset, -1};
            if (v >= 2)
            {
                auto timestamp = d.int64();
                if (!timestamp)
                    return std::unexpected(timestamp.error());
                item.log_append_time = *timestamp;
            }
            if (v >= 5)
            {
                auto log_start = d.int64();
                if (!log_start)
                    return std::unexpected(log_start.error());
            }
            if (!decoded.emplace(item.target, item).second)
                return std::unexpected(
                    make_error(error_code::malformed_response,
                        "duplicate Produce partition response"));
        }
    }
    if (v >= 1)
    {
        auto throttle = d.int32();
        if (!throttle)
            return std::unexpected(throttle.error());
    }
    if (d.remaining() != 0)
        return std::unexpected(
            make_error(error_code::malformed_response,
                "Produce response contains trailing data"));
    std::vector<produce_result> out;
    out.reserve(request.partitions.size());
    for (auto& partition : request.partitions)
    {
        auto found = decoded.find(partition.target);
        if (found == decoded.end())
            return std::unexpected(
                make_error(error_code::malformed_response,
                    "Produce response omitted a requested partition"));
        out.push_back(found->second);
        decoded.erase(found);
    }
    if (!decoded.empty())
        return std::unexpected(
            make_error(error_code::malformed_response,
                "Produce response contains an unrequested partition"));
    return out;
}

auto encode_fetch(const fetch_request& r, std::int16_t v) -> bytes
{
    encoder e;
    e.int32(-1);
    e.int32(static_cast<std::int32_t>(r.max_wait.count()));
    e.int32(r.min_bytes);
    if (v >= 3)
        e.int32(r.max_bytes);
    if (v >= 4)
        e.int8(static_cast<std::int8_t>(r.isolation));
    if (v >= 7)
    {
        e.int32(r.session_id);
        e.int32(r.session_epoch);
    }
    std::map<std::string, std::vector<const fetch_partition*>> groups;
    for (auto& p : r.partitions)
        groups[p.source.topic].push_back(&p);
    e.int32(static_cast<std::int32_t>(groups.size()));
    for (auto& [topic, parts] : groups)
    {
        e.string(topic);
        e.int32(static_cast<std::int32_t>(parts.size()));
        for (auto* p : parts)
        {
            e.int32(p->source.partition);
            if (v >= 9)
                e.int32(p->current_leader_epoch);
            e.int64(p->offset);
            if (v >= 5)
                e.int64(-1);
            e.int32(p->max_bytes);
        }
    }
    if (v >= 7)
    {
        std::map<std::string, std::vector<std::int32_t>, std::less<>> forgotten;
        for (auto& partition : r.forgotten_partitions)
            forgotten[partition.topic].push_back(partition.partition);
        e.int32(static_cast<std::int32_t>(forgotten.size()));
        for (auto& [topic, partitions] : forgotten)
        {
            e.string(topic);
            e.int32(static_cast<std::int32_t>(partitions.size()));
            for (auto partition : partitions)
                e.int32(partition);
        }
    }
    if (v >= 11)
        e.string({});
    return std::move(e).take();
}

auto decode_fetch(std::span<const std::byte> b, std::int16_t v)
    -> result<fetch_response>
{
    decoder d(b);
    fetch_response out;
    if (v >= 1)
    {
        auto x = d.int32();
        if (!x)
            return std::unexpected(x.error());
        out.throttle_ms = *x;
    }
    if (v >= 7)
    {
        auto e = d.int16();
        auto s = d.int32();
        if (!e || !s)
            return std::unexpected(make_error(error_code::malformed_response,
                "truncated Fetch session"));
        out.error = static_cast<error_code>(*e);
        out.session_id = *s;
    }
    auto nt = d.int32();
    if (!nt || *nt < 0)
        return std::unexpected(
            make_error(error_code::malformed_response, "invalid Fetch topics"));
    for (int i = 0; i < *nt; ++i)
    {
        auto topic = d.string();
        auto np = d.int32();
        if (!topic || !np || *np < 0)
            return std::unexpected(
                make_error(error_code::malformed_response, "truncated Fetch topic"));
        for (int p = 0; p < *np; ++p)
        {
            fetched_partition fp;
            fp.source.topic = *topic;
            auto id = d.int32();
            auto ec = d.int16();
            auto hw = d.int64();
            if (!id || !ec || !hw)
                return std::unexpected(make_error(error_code::malformed_response,
                    "truncated Fetch partition"));
            fp.source.partition = *id;
            fp.error = static_cast<error_code>(*ec);
            fp.high_watermark = *hw;
            if (v >= 4)
            {
                auto stable = d.int64();
                if (!stable)
                    return std::unexpected(stable.error());
                fp.last_stable_offset = *stable;
            }
            if (v >= 5)
            {
                auto start = d.int64();
                if (!start)
                    return std::unexpected(start.error());
            }
            if (v >= 4)
            {
                auto aborted = d.int32();
                if (!aborted)
                    return std::unexpected(aborted.error());
                if (*aborted >= 0)
                    for (int a = 0; a < *aborted; ++a)
                    {
                        auto pid = d.int64();
                        auto first = d.int64();
                        if (!pid || !first)
                            return std::unexpected(
                                make_error(error_code::malformed_response,
                                    "truncated aborted transaction"));
                        fp.aborted_transactions.push_back({*pid, *first});
                    }
            }
            if (v >= 11)
            {
                auto preferred = d.int32();
                if (!preferred)
                    return std::unexpected(preferred.error());
                fp.preferred_replica = *preferred;
            }
            auto records = d.byte_array();
            if (!records)
                return std::unexpected(records.error());
            if (*records)
                fp.records = std::move(**records);
            out.partitions.push_back(std::move(fp));
        }
    }
    return out;
}

auto encode_find_coordinator(std::string_view key, std::int16_t v, bool group)
    -> bytes
{
    encoder e;
    e.string(key);
    if (v >= 1)
        e.int8(group ? 0 : 1);
    return std::move(e).take();
}

auto decode_find_coordinator(std::span<const std::byte> b, std::int16_t v)
    -> result<coordinator_response>
{
    decoder d(b);
    coordinator_response out;
    if (v >= 1)
    {
        auto throttle = d.int32();
        if (!throttle)
            return std::unexpected(throttle.error());
        out.throttle_ms = *throttle;
    }
    auto ec = d.int16();
    if (!ec)
        return std::unexpected(ec.error());
    out.error = static_cast<error_code>(*ec);
    if (v >= 1)
    {
        auto message = d.nullable_string();
        if (!message)
            return std::unexpected(message.error());
        out.error_message = std::move(*message);
    }
    auto node = d.int32();
    auto host = d.string();
    auto port = d.int32();
    if (!node || !host || !port)
        return std::unexpected(make_error(error_code::malformed_response,
            "truncated FindCoordinator response"));
    out.coordinator = {
        *node, std::move(*host), static_cast<std::uint16_t>(*port), {}};
    return out;
}

auto encode_join_group(const join_group_request& r, std::int16_t v) -> bytes
{
    encoder e;
    e.string(r.group_id);
    e.int32(static_cast<std::int32_t>(r.session_timeout.count()));
    if (v >= 1)
        e.int32(static_cast<std::int32_t>(r.rebalance_timeout.count()));
    e.string(r.member_id);
    if (v >= 5)
        e.nullable_string(r.group_instance_id);
    e.string("consumer");
    e.int32(static_cast<std::int32_t>(r.protocols.size()));
    for (auto& protocol : r.protocols)
    {
        e.string(protocol.name);
        e.int32(static_cast<std::int32_t>(protocol.metadata.size()));
        e.raw(protocol.metadata);
    }
    return std::move(e).take();
}

auto decode_join_group(std::span<const std::byte> b, std::int16_t v)
    -> result<join_group_response>
{
    decoder d(b);
    join_group_response out;
    if (v >= 2)
    {
        auto throttle = d.int32();
        if (!throttle)
            return std::unexpected(throttle.error());
        out.throttle_ms = *throttle;
    }
    auto ec = d.int16();
    auto generation = d.int32();
    auto protocol_name = d.string();
    auto leader = d.string();
    auto member = d.string();
    auto count = d.int32();
    if (!ec || !generation || !protocol_name || !leader || !member || !count ||
        *count < 0)
        return std::unexpected(make_error(error_code::malformed_response,
            "truncated JoinGroup response"));
    out.error = static_cast<error_code>(*ec);
    out.generation = *generation;
    out.protocol_name = std::move(*protocol_name);
    out.leader_id = std::move(*leader);
    out.member_id = std::move(*member);
    for (std::int32_t i = 0; i < *count; ++i)
    {
        join_group_member item;
        auto id = d.string();
        if (!id)
            return std::unexpected(id.error());
        item.member_id = std::move(*id);
        if (v >= 5)
        {
            auto instance = d.nullable_string();
            if (!instance)
                return std::unexpected(instance.error());
            item.group_instance_id = std::move(*instance);
        }
        auto metadata = d.byte_array();
        if (!metadata || !*metadata)
            return std::unexpected(make_error(error_code::malformed_response,
                "JoinGroup member metadata is null"));
        item.metadata = std::move(**metadata);
        out.members.push_back(std::move(item));
    }
    return out;
}

auto encode_sync_group(const sync_group_request& r, std::int16_t v) -> bytes
{
    encoder e;
    e.string(r.identity.group_id);
    e.int32(r.identity.generation);
    e.string(r.identity.member_id);
    if (v >= 3)
        e.nullable_string(r.identity.group_instance_id);
    e.int32(static_cast<std::int32_t>(r.assignments.size()));
    for (auto& assignment : r.assignments)
    {
        e.string(assignment.member_id);
        e.int32(static_cast<std::int32_t>(assignment.assignment.size()));
        e.raw(assignment.assignment);
    }
    return std::move(e).take();
}

auto decode_sync_group(std::span<const std::byte> b, std::int16_t v)
    -> result<sync_group_response>
{
    decoder d(b);
    sync_group_response out;
    if (v >= 1)
    {
        auto throttle = d.int32();
        if (!throttle)
            return std::unexpected(throttle.error());
        out.throttle_ms = *throttle;
    }
    auto ec = d.int16();
    auto assignment = d.byte_array();
    if (!ec || !assignment || !*assignment)
        return std::unexpected(make_error(error_code::malformed_response,
            "truncated SyncGroup response"));
    out.error = static_cast<error_code>(*ec);
    out.assignment = std::move(**assignment);
    return out;
}

auto encode_heartbeat(const group_identity& g, std::int16_t v) -> bytes
{
    encoder e;
    e.string(g.group_id);
    e.int32(g.generation);
    e.string(g.member_id);
    if (v >= 3)
        e.nullable_string(g.group_instance_id);
    return std::move(e).take();
}

auto decode_heartbeat(std::span<const std::byte> b, std::int16_t v)
    -> result<group_operation_response>
{
    decoder d(b);
    group_operation_response out;
    if (v >= 1)
    {
        auto throttle = d.int32();
        if (!throttle)
            return std::unexpected(throttle.error());
        out.throttle_ms = *throttle;
    }
    auto ec = d.int16();
    if (!ec)
        return std::unexpected(ec.error());
    out.error = static_cast<error_code>(*ec);
    return out;
}

auto encode_leave_group(const group_identity& g, std::int16_t v) -> bytes
{
    encoder e;
    e.string(g.group_id);
    if (v >= 3)
    {
        e.int32(1);
        e.string(g.member_id);
        e.nullable_string(g.group_instance_id);
    }
    else
        e.string(g.member_id);
    return std::move(e).take();
}

auto decode_leave_group(std::span<const std::byte> b, std::int16_t v)
    -> result<group_operation_response>
{
    decoder d(b);
    group_operation_response out;
    if (v >= 1)
    {
        auto throttle = d.int32();
        if (!throttle)
            return std::unexpected(throttle.error());
        out.throttle_ms = *throttle;
    }
    auto ec = d.int16();
    if (!ec)
        return std::unexpected(ec.error());
    out.error = static_cast<error_code>(*ec);
    if (v >= 3)
    {
        auto count = d.int32();
        if (!count || *count < 0)
            return std::unexpected(make_error(error_code::malformed_response,
                "invalid LeaveGroup member array"));
        for (std::int32_t i = 0; i < *count; ++i)
        {
            auto member = d.string();
            auto instance = d.nullable_string();
            auto member_error = d.int16();
            if (!member || !instance || !member_error)
                return std::unexpected(
                    make_error(error_code::malformed_response,
                        "truncated LeaveGroup member response"));
            if (*member_error != 0 && out.error == error_code::none)
                out.error = static_cast<error_code>(*member_error);
        }
    }
    return out;
}

auto encode_offset_commit(
    const group_identity& g,
    const std::map<topic_partition, std::int64_t>& offsets, std::int16_t v)
    -> bytes
{
    encoder e;
    e.string(g.group_id);
    e.int32(g.generation);
    e.string(g.member_id);
    if (v >= 7)
        e.nullable_string(g.group_instance_id);
    if (v >= 2 && v <= 4)
        e.int64(-1);
    std::map<std::string, std::vector<std::pair<std::int32_t, std::int64_t>>,
        std::less<>>
        topics;
    for (auto& [tp, o] : offsets)
        topics[tp.topic].push_back({tp.partition, o});
    e.int32(static_cast<std::int32_t>(topics.size()));
    for (auto& [t, ps] : topics)
    {
        e.string(t);
        e.int32(static_cast<std::int32_t>(ps.size()));
        for (auto& [p, o] : ps)
        {
            e.int32(p);
            e.int64(o);
            if (v >= 6)
                e.int32(-1);
            e.string({});
        }
    }
    return std::move(e).take();
}

auto encode_offset_fetch(std::string_view group,
    std::span<const topic_partition> parts) -> bytes
{
    encoder e;
    e.string(group);
    std::map<std::string, std::vector<std::int32_t>, std::less<>> topics;
    for (auto& p : parts)
        topics[p.topic].push_back(p.partition);
    e.int32(static_cast<std::int32_t>(topics.size()));
    for (auto& [t, ps] : topics)
    {
        e.string(t);
        e.int32(static_cast<std::int32_t>(ps.size()));
        for (auto p : ps)
            e.int32(p);
    }
    return std::move(e).take();
}

auto decode_offset_fetch(std::span<const std::byte> b, std::int16_t v)
    -> result<offset_fetch_response>
{
    decoder d(b);
    offset_fetch_response out;
    if (v >= 3)
    {
        auto throttle = d.int32();
        if (!throttle)
            return std::unexpected(throttle.error());
        out.throttle_ms = *throttle;
    }
    auto topic_count = d.int32();
    if (!topic_count || *topic_count < 0)
        return std::unexpected(make_error(error_code::malformed_response,
            "invalid OffsetFetch topic array"));
    for (std::int32_t i = 0; i < *topic_count; ++i)
    {
        auto topic = d.string();
        auto partition_count = d.int32();
        if (!topic || !partition_count || *partition_count < 0)
            return std::unexpected(make_error(error_code::malformed_response,
                "truncated OffsetFetch topic"));
        for (std::int32_t p = 0; p < *partition_count; ++p)
        {
            committed_offset offset;
            offset.source.topic = *topic;
            auto partition = d.int32();
            auto value = d.int64();
            if (!partition || !value)
                return std::unexpected(make_error(error_code::malformed_response,
                    "truncated OffsetFetch partition"));
            offset.source.partition = *partition;
            offset.offset = *value;
            if (v >= 5)
            {
                auto epoch = d.int32();
                if (!epoch)
                    return std::unexpected(epoch.error());
                offset.leader_epoch = *epoch;
            }
            auto metadata = d.nullable_string();
            auto ec = d.int16();
            if (!metadata || !ec)
                return std::unexpected(
                    make_error(error_code::malformed_response,
                        "truncated OffsetFetch partition state"));
            if (*metadata)
                offset.metadata = std::move(**metadata);
            offset.error = static_cast<error_code>(*ec);
            out.offsets.push_back(std::move(offset));
        }
    }
    if (v >= 2)
    {
        auto ec = d.int16();
        if (!ec)
            return std::unexpected(ec.error());
        out.error = static_cast<error_code>(*ec);
    }
    return out;
}

auto encode_list_offsets(std::span<const list_offset_partition> partitions,
    isolation_level isolation, std::int16_t v) -> bytes
{
    encoder e;
    e.int32(-1);
    if (v >= 2)
        e.int8(static_cast<std::int8_t>(isolation));
    std::map<std::string, std::vector<const list_offset_partition*>, std::less<>>
        topics;
    for (auto& partition : partitions)
        topics[partition.source.topic].push_back(&partition);
    e.int32(static_cast<std::int32_t>(topics.size()));
    for (auto& [topic, entries] : topics)
    {
        e.string(topic);
        e.int32(static_cast<std::int32_t>(entries.size()));
        for (auto* entry : entries)
        {
            e.int32(entry->source.partition);
            if (v >= 4)
                e.int32(entry->current_leader_epoch);
            e.int64(entry->timestamp);
            if (v == 0)
                e.int32(1);
        }
    }
    return std::move(e).take();
}

auto decode_list_offsets(std::span<const std::byte> b, std::int16_t v)
    -> result<std::vector<listed_offset>>
{
    decoder d(b);
    if (v >= 2)
    {
        auto throttle = d.int32();
        if (!throttle)
            return std::unexpected(throttle.error());
    }
    auto topic_count = d.int32();
    if (!topic_count || *topic_count < 0)
        return std::unexpected(make_error(error_code::malformed_response,
            "invalid ListOffsets topic array"));
    std::vector<listed_offset> out;
    for (std::int32_t i = 0; i < *topic_count; ++i)
    {
        auto topic = d.string();
        auto partition_count = d.int32();
        if (!topic || !partition_count || *partition_count < 0)
            return std::unexpected(make_error(error_code::malformed_response,
                "truncated ListOffsets topic"));
        for (std::int32_t p = 0; p < *partition_count; ++p)
        {
            listed_offset item;
            item.source.topic = *topic;
            auto partition = d.int32();
            auto ec = d.int16();
            if (!partition || !ec)
                return std::unexpected(make_error(error_code::malformed_response,
                    "truncated ListOffsets partition"));
            item.source.partition = *partition;
            item.error = static_cast<error_code>(*ec);
            if (v == 0)
            {
                auto count = d.int32();
                if (!count || *count < 0)
                    return std::unexpected(
                        make_error(error_code::malformed_response,
                            "invalid legacy ListOffsets array"));
                for (std::int32_t n = 0; n < *count; ++n)
                {
                    auto offset = d.int64();
                    if (!offset)
                        return std::unexpected(offset.error());
                    if (n == 0)
                        item.offset = *offset;
                }
            }
            else
            {
                auto timestamp = d.int64();
                auto offset = d.int64();
                if (!timestamp || !offset)
                    return std::unexpected(make_error(error_code::malformed_response,
                        "truncated ListOffsets value"));
                item.timestamp = *timestamp;
                item.offset = *offset;
                if (v >= 4)
                {
                    auto epoch = d.int32();
                    if (!epoch)
                        return std::unexpected(epoch.error());
                    item.leader_epoch = *epoch;
                }
            }
            out.push_back(std::move(item));
        }
    }
    return out;
}

auto encode_end_transaction(std::string_view id, std::int64_t producer,
    std::int16_t epoch, bool commit) -> bytes
{
    encoder e;
    e.string(id);
    e.int64(producer);
    e.int16(epoch);
    e.boolean(commit);
    return std::move(e).take();
}

auto encode_transaction_offset_commit(
    std::string_view transactional, std::string_view group, std::int64_t pid,
    std::int16_t epoch, std::int32_t generation, std::string_view member,
    const std::map<topic_partition, std::int64_t>& offsets) -> bytes
{
    encoder e;
    e.string(transactional);
    e.string(group);
    e.int64(pid);
    e.int16(epoch);
    e.int32(generation);
    e.string(member);
    std::map<std::string, std::vector<std::pair<std::int32_t, std::int64_t>>,
        std::less<>>
        topics;
    for (auto& [tp, o] : offsets)
        topics[tp.topic].push_back({tp.partition, o});
    e.int32(static_cast<std::int32_t>(topics.size()));
    for (auto& [topic, ps] : topics)
    {
        e.string(topic);
        e.int32(static_cast<std::int32_t>(ps.size()));
        for (auto& [p, o] : ps)
        {
            e.int32(p);
            e.int64(o);
            e.int32(-1);
            e.string({});
        }
    }
    return std::move(e).take();
}
} // namespace cnetmod::kafka::protocol
