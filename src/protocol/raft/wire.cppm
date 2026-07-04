export module cnetmod.protocol.raft:wire;

import std;
import :types;

namespace cnetmod::raft
{
    export enum class raft_rpc_type : std::uint8_t
    {
        pre_vote = 1,
        request_vote = 2,
        request_vote_response = 3,
        append_entries = 4,
        append_entries_response = 5,
        install_snapshot = 6,
        install_snapshot_response = 7,
        timeout_now = 8,
    };

    export struct raft_rpc_message
    {
        raft_rpc_type type = raft_rpc_type::append_entries;
        node_id from;
        node_id to;
        std::string auth_token;
        request_vote_request vote;
        request_vote_response vote_response;
        timeout_now_request timeout_now;
        append_entries_request append;
        append_entries_response append_response;
        install_snapshot_request snapshot;
        install_snapshot_response snapshot_response;
    };

    namespace wire_detail
    {
        inline constexpr std::uint32_t magic = 0x52414654u; // RAFT
        inline constexpr std::uint8_t version = 1;
        inline constexpr std::uint32_t max_frame_size = 64u * 1024u * 1024u;

        inline void put_u8(std::vector<std::byte>& out, std::uint8_t value)
        {
            out.push_back(static_cast<std::byte>(value));
        }

        inline void put_u32(std::vector<std::byte>& out, std::uint32_t value)
        {
            for (auto shift : {24, 16, 8, 0})
                out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
        }

        inline void put_u64(std::vector<std::byte>& out, std::uint64_t value)
        {
            for (auto shift : {56, 48, 40, 32, 24, 16, 8, 0})
                out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
        }

        inline auto get_u8(std::span<const std::byte> in, std::size_t& pos) -> std::uint8_t
        {
            if (pos + 1 > in.size()) throw std::runtime_error("truncated raft frame");
            return static_cast<std::uint8_t>(in[pos++]);
        }

        inline auto get_u32(std::span<const std::byte> in, std::size_t& pos) -> std::uint32_t
        {
            if (pos + 4 > in.size()) throw std::runtime_error("truncated raft frame");
            std::uint32_t value = 0;
            for (auto i = 0; i < 4; ++i)
                value = (value << 8) | static_cast<std::uint8_t>(in[pos++]);
            return value;
        }

        inline auto get_u64(std::span<const std::byte> in, std::size_t& pos) -> std::uint64_t
        {
            if (pos + 8 > in.size()) throw std::runtime_error("truncated raft frame");
            std::uint64_t value = 0;
            for (auto i = 0; i < 8; ++i)
                value = (value << 8) | static_cast<std::uint8_t>(in[pos++]);
            return value;
        }

        inline void put_string(std::vector<std::byte>& out, std::string_view value)
        {
            if (value.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("raft string is too large");
            put_u32(out, static_cast<std::uint32_t>(value.size()));
            auto bytes = std::as_bytes(std::span{value.data(), value.size()});
            out.insert(out.end(), bytes.begin(), bytes.end());
        }

        inline void put_bytes(std::vector<std::byte>& out, std::span<const std::byte> value)
        {
            if (value.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("raft byte payload is too large");
            put_u32(out, static_cast<std::uint32_t>(value.size()));
            out.insert(out.end(), value.begin(), value.end());
        }

        inline auto get_string(std::span<const std::byte> in, std::size_t& pos) -> std::string
        {
            const auto size = get_u32(in, pos);
            if (pos + size > in.size()) throw std::runtime_error("truncated raft string");
            std::string out(reinterpret_cast<const char*>(in.data() + pos), size);
            pos += size;
            return out;
        }

        inline auto get_bytes(std::span<const std::byte> in, std::size_t& pos)
            -> std::vector<std::byte>
        {
            const auto size = get_u32(in, pos);
            if (pos + size > in.size()) throw std::runtime_error("truncated raft byte payload");
            std::vector<std::byte> out(in.begin() + static_cast<std::ptrdiff_t>(pos),
                                       in.begin() + static_cast<std::ptrdiff_t>(pos + size));
            pos += size;
            return out;
        }

        inline void put_nodes(std::vector<std::byte>& out, const std::vector<node_id>& ids)
        {
            if (ids.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("too many raft nodes");
            put_u32(out, static_cast<std::uint32_t>(ids.size()));
            for (const auto& id : ids) put_string(out, id);
        }

        inline auto get_nodes(std::span<const std::byte> in, std::size_t& pos)
            -> std::vector<node_id>
        {
            const auto size = get_u32(in, pos);
            std::vector<node_id> out;
            out.reserve(size);
            for (std::uint32_t i = 0; i < size; ++i)
                out.push_back(get_string(in, pos));
            return out;
        }

        inline void put_config(std::vector<std::byte>& out, const configuration_state& conf)
        {
            put_nodes(out, conf.voters);
            put_nodes(out, conf.old_voters);
            put_nodes(out, conf.learners);
        }

        inline auto get_config(std::span<const std::byte> in, std::size_t& pos)
            -> configuration_state
        {
            return {
                .voters = get_nodes(in, pos),
                .old_voters = get_nodes(in, pos),
                .learners = get_nodes(in, pos),
            };
        }

        inline void put_entry(std::vector<std::byte>& out, const log_entry& entry)
        {
            put_u64(out, entry.index);
            put_u64(out, entry.term);
            put_u8(out, static_cast<std::uint8_t>(entry.type));
            put_string(out, entry.command);
            put_config(out, entry.configuration);
        }

        inline auto get_entry(std::span<const std::byte> in, std::size_t& pos) -> log_entry
        {
            return {
                .index = get_u64(in, pos),
                .term = get_u64(in, pos),
                .type = static_cast<entry_type>(get_u8(in, pos)),
                .command = get_string(in, pos),
                .configuration = get_config(in, pos),
            };
        }

        inline void put_snapshot(std::vector<std::byte>& out, const snapshot_metadata& meta)
        {
            put_u64(out, meta.last_included_index);
            put_u64(out, meta.last_included_term);
            put_string(out, meta.uri);
            put_config(out, meta.configuration);
        }

        inline auto get_snapshot(std::span<const std::byte> in, std::size_t& pos)
            -> snapshot_metadata
        {
            return {
                .last_included_index = get_u64(in, pos),
                .last_included_term = get_u64(in, pos),
                .uri = get_string(in, pos),
                .configuration = get_config(in, pos),
            };
        }

        inline void put_vote(std::vector<std::byte>& out, const request_vote_request& vote)
        {
            put_u64(out, vote.term);
            put_string(out, vote.candidate_id);
            put_u64(out, vote.last_log_index);
            put_u64(out, vote.last_log_term);
            put_u8(out, vote.pre_vote ? 1 : 0);
        }

        inline auto get_vote(std::span<const std::byte> in, std::size_t& pos)
            -> request_vote_request
        {
            return {
                .term = get_u64(in, pos),
                .candidate_id = get_string(in, pos),
                .last_log_index = get_u64(in, pos),
                .last_log_term = get_u64(in, pos),
                .pre_vote = get_u8(in, pos) != 0,
            };
        }

        inline void put_vote_response(std::vector<std::byte>& out,
                                      const request_vote_response& response)
        {
            put_u64(out, response.term);
            put_u8(out, response.vote_granted ? 1 : 0);
            put_u8(out, response.pre_vote ? 1 : 0);
        }

        inline auto get_vote_response(std::span<const std::byte> in, std::size_t& pos)
            -> request_vote_response
        {
            return {
                .term = get_u64(in, pos),
                .vote_granted = get_u8(in, pos) != 0,
                .pre_vote = get_u8(in, pos) != 0,
            };
        }

        inline void put_timeout_now(std::vector<std::byte>& out,
                                    const timeout_now_request& request)
        {
            put_u64(out, request.term);
            put_string(out, request.leader_id);
        }

        inline auto get_timeout_now(std::span<const std::byte> in, std::size_t& pos)
            -> timeout_now_request
        {
            return {
                .term = get_u64(in, pos),
                .leader_id = get_string(in, pos),
            };
        }

        inline void put_append(std::vector<std::byte>& out, const append_entries_request& append)
        {
            put_u64(out, append.term);
            put_string(out, append.leader_id);
            put_u64(out, append.prev_log_index);
            put_u64(out, append.prev_log_term);
            put_u64(out, append.leader_commit);
            if (append.entries.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("too many raft entries");
            put_u32(out, static_cast<std::uint32_t>(append.entries.size()));
            for (const auto& entry : append.entries) put_entry(out, entry);
            if (append.read_contexts.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("too many raft read-index contexts");
            put_u32(out, static_cast<std::uint32_t>(append.read_contexts.size()));
            for (const auto& read : append.read_contexts)
            {
                put_u64(out, read.id);
                put_string(out, read.context);
            }
        }

        inline auto get_append(std::span<const std::byte> in, std::size_t& pos)
            -> append_entries_request
        {
            append_entries_request out;
            out.term = get_u64(in, pos);
            out.leader_id = get_string(in, pos);
            out.prev_log_index = get_u64(in, pos);
            out.prev_log_term = get_u64(in, pos);
            out.leader_commit = get_u64(in, pos);
            const auto size = get_u32(in, pos);
            out.entries.reserve(size);
            for (std::uint32_t i = 0; i < size; ++i)
                out.entries.push_back(get_entry(in, pos));
            if (pos < in.size())
            {
                const auto read_size = get_u32(in, pos);
                out.read_contexts.reserve(read_size);
                for (std::uint32_t i = 0; i < read_size; ++i)
                {
                    out.read_contexts.push_back(read_index_request{
                        .id = get_u64(in, pos),
                        .context = get_string(in, pos),
                    });
                }
            }
            return out;
        }

        inline void put_append_response(std::vector<std::byte>& out,
                                        const append_entries_response& response)
        {
            put_u64(out, response.term);
            put_u8(out, response.success ? 1 : 0);
            put_u64(out, response.match_index);
            put_u64(out, response.conflict_index);
            put_u64(out, response.conflict_term);
            if (response.read_acks.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("too many raft read-index acknowledgements");
            put_u32(out, static_cast<std::uint32_t>(response.read_acks.size()));
            for (auto id : response.read_acks) put_u64(out, id);
        }

        inline auto get_append_response(std::span<const std::byte> in, std::size_t& pos)
            -> append_entries_response
        {
            append_entries_response out{
                .term = get_u64(in, pos),
                .success = get_u8(in, pos) != 0,
                .match_index = get_u64(in, pos),
                .conflict_index = get_u64(in, pos),
                .conflict_term = get_u64(in, pos),
            };
            if (pos < in.size())
            {
                const auto ack_size = get_u32(in, pos);
                out.read_acks.reserve(ack_size);
                for (std::uint32_t i = 0; i < ack_size; ++i)
                    out.read_acks.push_back(get_u64(in, pos));
            }
            return out;
        }
    } // namespace wire_detail

    export auto encode_raft_message(const raft_rpc_message& message) -> std::vector<std::byte>
    {
        std::vector<std::byte> body;
        wire_detail::put_u8(body, static_cast<std::uint8_t>(message.type));
        wire_detail::put_string(body, message.from);
        wire_detail::put_string(body, message.to);
        wire_detail::put_string(body, message.auth_token);

        switch (message.type)
        {
        case raft_rpc_type::pre_vote:
        case raft_rpc_type::request_vote:
            wire_detail::put_vote(body, message.vote);
            break;
        case raft_rpc_type::request_vote_response:
            wire_detail::put_vote_response(body, message.vote_response);
            break;
        case raft_rpc_type::timeout_now:
            wire_detail::put_timeout_now(body, message.timeout_now);
            break;
        case raft_rpc_type::append_entries:
            wire_detail::put_append(body, message.append);
            break;
        case raft_rpc_type::append_entries_response:
            wire_detail::put_append_response(body, message.append_response);
            break;
        case raft_rpc_type::install_snapshot:
            wire_detail::put_u64(body, message.snapshot.term);
            wire_detail::put_string(body, message.snapshot.leader_id);
            wire_detail::put_snapshot(body, message.snapshot.metadata);
            wire_detail::put_string(body, message.snapshot.uri);
            wire_detail::put_string(body, message.snapshot.snapshot_id);
            wire_detail::put_u64(body, message.snapshot.offset);
            wire_detail::put_u64(body, message.snapshot.total_size);
            wire_detail::put_u32(body, message.snapshot.chunk_crc32);
            wire_detail::put_u32(body, message.snapshot.file_crc32);
            wire_detail::put_u8(body, message.snapshot.done ? 1 : 0);
            wire_detail::put_bytes(body, message.snapshot.data);
            break;
        case raft_rpc_type::install_snapshot_response:
            wire_detail::put_u64(body, message.snapshot_response.term);
            wire_detail::put_u8(body, message.snapshot_response.success ? 1 : 0);
            wire_detail::put_string(body, message.snapshot_response.snapshot_id);
            wire_detail::put_u64(body, message.snapshot_response.accepted_offset);
            wire_detail::put_string(body, message.snapshot_response.error);
            break;
        }

        if (body.size() > wire_detail::max_frame_size)
            throw std::length_error("raft frame is too large");

        std::vector<std::byte> frame;
        frame.reserve(9 + body.size());
        wire_detail::put_u32(frame, wire_detail::magic);
        wire_detail::put_u8(frame, wire_detail::version);
        wire_detail::put_u32(frame, static_cast<std::uint32_t>(body.size()));
        frame.insert(frame.end(), body.begin(), body.end());
        return frame;
    }

    export auto decode_raft_message(std::span<const std::byte> frame) -> raft_rpc_message
    {
        std::size_t pos = 0;
        if (wire_detail::get_u32(frame, pos) != wire_detail::magic)
            throw std::runtime_error("invalid raft frame magic");
        if (wire_detail::get_u8(frame, pos) != wire_detail::version)
            throw std::runtime_error("unsupported raft frame version");
        const auto size = wire_detail::get_u32(frame, pos);
        if (size > wire_detail::max_frame_size || pos + size != frame.size())
            throw std::runtime_error("invalid raft frame size");

        auto body = frame.subspan(pos, size);
        pos = 0;
        raft_rpc_message message;
        message.type = static_cast<raft_rpc_type>(wire_detail::get_u8(body, pos));
        message.from = wire_detail::get_string(body, pos);
        message.to = wire_detail::get_string(body, pos);
        message.auth_token = wire_detail::get_string(body, pos);

        switch (message.type)
        {
        case raft_rpc_type::pre_vote:
        case raft_rpc_type::request_vote:
            message.vote = wire_detail::get_vote(body, pos);
            break;
        case raft_rpc_type::request_vote_response:
            message.vote_response = wire_detail::get_vote_response(body, pos);
            break;
        case raft_rpc_type::timeout_now:
            message.timeout_now = wire_detail::get_timeout_now(body, pos);
            break;
        case raft_rpc_type::append_entries:
            message.append = wire_detail::get_append(body, pos);
            break;
        case raft_rpc_type::append_entries_response:
            message.append_response = wire_detail::get_append_response(body, pos);
            break;
        case raft_rpc_type::install_snapshot:
            message.snapshot.term = wire_detail::get_u64(body, pos);
            message.snapshot.leader_id = wire_detail::get_string(body, pos);
            message.snapshot.metadata = wire_detail::get_snapshot(body, pos);
            message.snapshot.uri = wire_detail::get_string(body, pos);
            if (pos < body.size())
            {
                message.snapshot.snapshot_id = wire_detail::get_string(body, pos);
                message.snapshot.offset = wire_detail::get_u64(body, pos);
                message.snapshot.total_size = wire_detail::get_u64(body, pos);
                message.snapshot.chunk_crc32 = wire_detail::get_u32(body, pos);
                message.snapshot.file_crc32 = wire_detail::get_u32(body, pos);
                message.snapshot.done = wire_detail::get_u8(body, pos) != 0;
                message.snapshot.data = wire_detail::get_bytes(body, pos);
            }
            break;
        case raft_rpc_type::install_snapshot_response:
            message.snapshot_response.term = wire_detail::get_u64(body, pos);
            message.snapshot_response.success = wire_detail::get_u8(body, pos) != 0;
            if (pos < body.size())
            {
                message.snapshot_response.snapshot_id = wire_detail::get_string(body, pos);
                message.snapshot_response.accepted_offset = wire_detail::get_u64(body, pos);
                message.snapshot_response.error = wire_detail::get_string(body, pos);
            }
            break;
        }
        if (pos != body.size())
            throw std::runtime_error("raft frame has trailing bytes");
        return message;
    }

    export auto raft_frame_payload_size(std::span<const std::byte, 9> header)
        -> std::uint32_t
    {
        std::size_t pos = 0;
        if (wire_detail::get_u32(header, pos) != wire_detail::magic)
            throw std::runtime_error("invalid raft frame magic");
        if (wire_detail::get_u8(header, pos) != wire_detail::version)
            throw std::runtime_error("unsupported raft frame version");
        auto size = wire_detail::get_u32(header, pos);
        if (size > wire_detail::max_frame_size)
            throw std::runtime_error("raft frame exceeds maximum size");
        return size;
    }
} // namespace cnetmod::raft
