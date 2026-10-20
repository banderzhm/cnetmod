module;

#include <cnetmod/config.hpp>

#include <openssl/rand.h>

module cnetmod.protocol.quic;

import std;

#ifdef CNETMOD_HAS_SSL
    #ifdef CNETMOD_ENABLE_QUIC

import :connection;
import :crypto;
import :stream;
import cnetmod.core.error;
import cnetmod.coro.mutex;
import cnetmod.coro.channel;
import cnetmod.io.io_context;
import :write_queue;
import cnetmod.protocol.quic.send_pipeline;
import :recovery;

namespace cnetmod::quic {
namespace {
auto pending_stream_header(std::span<std::byte> output, std::uint64_t stream,
    std::uint64_t offset, std::size_t count, bool fin) -> std::size_t
{
    std::size_t used{};
    const auto append = [&](std::uint64_t value)
    {
        const auto encoded = encode_varint_to(value, output.subspan(used));
        if (!encoded) return false;
        used += *encoded;
        return true;
    };
    const auto type = 0x0aU | (offset != 0U ? 0x04U : 0U) | (fin ? 0x01U : 0U);
    if (!append(type) || !append(stream) || (offset != 0U && !append(offset)) || !append(count))
        return 0U;
    return used;
}
} // namespace


void retired_stream_history::insert(stream_id id, final_sizes sizes)
{
    auto& ranges = ranges_[id & 3U];
    auto low = id / 4U;
    auto high = low;
    auto next = ranges.upper_bound(low);
    if (next != ranges.begin())
    {
        const auto previous = std::prev(next);
        if (previous->second >= low)
            return;
        if (previous->second + 1U == low)
        {
            low = previous->first;
            ranges.erase(previous);
        }
    }
    if (next != ranges.end() && high + 1U == next->first)
    {
        high = next->second;
        ranges.erase(next);
    }
    ranges.emplace(low, high);
    recent_.emplace(id, sizes);
    order_.push_back(id);
    if (order_.size() > final_size_capacity)
    {
        recent_.erase(order_.front());
        order_.pop_front();
    }
}

auto retired_stream_history::contains(stream_id id) const -> bool
{
    const auto& ranges = ranges_[id & 3U];
    const auto next = ranges.upper_bound(id / 4U);
    return next != ranges.begin() && std::prev(next)->second >= id / 4U;
}

auto retired_stream_history::find_final_sizes(stream_id id) const
    -> std::optional<final_sizes>
{
    const auto found = recent_.find(id);
    return found == recent_.end() ? std::nullopt
                                  : std::optional{found->second};
}

auto retired_stream_history::range_count() const -> std::size_t
{
    std::size_t result{};
    for (const auto& ranges : ranges_)
        result += ranges.size();
    return result;
}

auto retired_stream_history::final_size_count() const -> std::size_t
{
    return recent_.size();
}

namespace {

    class stream_wait_cancellation
    {
    public:
        stream_wait_cancellation(io_context& io, cancel_token& token,
            std::shared_ptr<channel<std::monostate>> progress)
            : io_(io), token_(token), progress_(std::move(progress))
        {
            node_.callback_arg = this;
            node_.callback = [](void* value)
            {
                auto& self = *static_cast<stream_wait_cancellation*>(value);
                self.dispatched_ = true;
                const auto waiter = self.waiter_;
                auto progress = self.progress_;
                // Sending may resume and destroy this registration.
                (void)progress->try_send({});
                if (waiter)
                    waiter.resume();
            };
            const auto notify = [](void* value) noexcept
            {
                auto& self = *static_cast<stream_wait_cancellation*>(value);
                self.io_.post_node_raw(&self.node_);
            };
            if (!token_.register_callback(this, notify))
                notify(this);
        }
        auto await_ready() noexcept -> bool
        {
            return token_.complete_callback(this) || dispatched_;
        }
        void await_suspend(std::coroutine_handle<> waiter) noexcept
        {
            waiter_ = waiter;
        }
        void await_resume() noexcept
        {
            token_.finish_callback(this);
        }
    private:
        io_context& io_;
        cancel_token& token_;
        std::shared_ptr<channel<std::monostate>> progress_;
        post_node node_;
        std::coroutine_handle<> waiter_;
        bool dispatched_{};
    };

    // RFC 9000 §19.3 encodes ACK ranges from the largest packet number down.
    // Keep this transformation beside the transport state rather than relying on
    // callers to hand-build gap values (an off-by-one here causes peers to declare
    // healthy packets lost).
    auto take_ack_frame(std::set<std::uint64_t>& received)
        -> std::optional<ack_frame>
    {
        if (received.empty())
            return std::nullopt;

        struct interval
        {
            std::uint64_t low;
            std::uint64_t high;
        };

        std::vector<interval> intervals;
        auto it = received.rbegin();
        while (it != received.rend())
        {
            interval current{*it, *it};
            while (++it != received.rend() && *it + 1 == current.low)
                current.low = *it;
            intervals.push_back(current);
        }

        ack_frame ack{};
        ack.largest_acked = intervals.front().high;
        ack.first_ack_range = intervals.front().high - intervals.front().low;
        for (std::size_t index = 1; index < intervals.size(); ++index)
        {
            const auto& previous = intervals[index - 1];
            const auto& current = intervals[index];
            ack.ack_ranges.push_back(ack_range{
                previous.low - current.high - 2,
                current.high - current.low});
        }
        ack.ack_range_count = ack.ack_ranges.size();
        return ack;
    }

    auto record_ack_eliciting_packet(std::set<std::uint64_t>& received,
        std::uint64_t packet_number) -> void
    {
        // ACK state is peer-controlled input.  Retaining an unbounded sparse set
        // would let an authenticated peer turn reordering into memory growth.
        // 256 ranges is well above normal reordering while bounding the state.
        constexpr std::size_t maximum_tracked_packet_numbers = 256;
        received.insert(packet_number);
        while (received.size() > maximum_tracked_packet_numbers)
            received.erase(received.begin());
    }

    auto queue_crypto_frames(std::span<const std::byte> data,
        std::uint64_t& next_offset, std::size_t maximum_encoded_size,
        std::deque<std::vector<std::byte>>& destination) -> bool
    {
        auto candidate_offset = next_offset;
        std::deque<std::vector<std::byte>> candidates;
        std::size_t position{};
        while (position < data.size())
        {
            const auto remaining = data.size() - position;
            const auto offset_size = static_cast<std::size_t>(varint_size(candidate_offset));
            if (maximum_encoded_size <= 1U + offset_size + 1U)
                return false;

            auto chunk_size = std::min(remaining,
                maximum_encoded_size - 1U - offset_size - 1U);
            while (chunk_size != 0U)
            {
                const auto encoded_size = 1U + offset_size +
                    static_cast<std::size_t>(varint_size(chunk_size)) + chunk_size;
                if (encoded_size <= maximum_encoded_size)
                    break;
                chunk_size = maximum_encoded_size - 1U - offset_size -
                    static_cast<std::size_t>(varint_size(chunk_size));
            }
            if (chunk_size == 0U)
                return false;

            const auto chunk = data.subspan(position, chunk_size);
            candidates.push_back(encode_frame(crypto_frame{candidate_offset, chunk}));
            candidate_offset += chunk_size;
            position += chunk_size;
        }
        while (!candidates.empty())
        {
            destination.push_back(std::move(candidates.front()));
            candidates.pop_front();
        }
        next_offset = candidate_offset;
        return true;
    }

    auto fit_front_crypto_frame(std::deque<std::vector<std::byte>>& frames,
        std::size_t maximum_encoded_size) -> bool
    {
        if (frames.empty() || frames.front().size() <= maximum_encoded_size)
            return true;

        const auto& oversized = frames.front();
        const auto decoded = decode_frame(oversized);
        if (!decoded || decoded->second != oversized.size())
            return false;
        const auto* crypto = std::get_if<crypto_frame>(&decoded->first);
        if (!crypto)
            return false;

        std::deque<std::vector<std::byte>> fragments;
        auto offset = crypto->offset;
        if (!queue_crypto_frames(crypto->data, offset, maximum_encoded_size, fragments))
            return false;
        frames.pop_front();
        while (!fragments.empty())
        {
            frames.push_front(std::move(fragments.back()));
            fragments.pop_back();
        }
        return true;
    }

    constexpr auto packet_number_space_for(encryption_level level) noexcept -> pn_space
    {
        switch (level)
        {
        case encryption_level::initial:
            return pn_space::initial;
        case encryption_level::handshake:
            return pn_space::handshake;
        case encryption_level::application:
            return pn_space::application;
        case encryption_level::early_data:
            return pn_space::application;
        }
        return pn_space::application;
    }

    constexpr auto encryption_level_for(pn_space space) noexcept -> encryption_level
    {
        switch (space)
        {
        case pn_space::initial:
            return encryption_level::initial;
        case pn_space::handshake:
            return encryption_level::handshake;
        case pn_space::application:
            return encryption_level::application;
        }
        return encryption_level::application;
    }

    auto transport_params_from_config(const quic_config& config) -> transport_params
    {
        transport_params params{};
        params.max_udp_payload_size = std::clamp<std::uint64_t>(
            config.max_udp_payload_size, min_initial_pkt_size, max_udp_receive_payload);
        params.max_datagram_frame_size = std::min(config.max_datagram_frame_size,
            params.max_udp_payload_size);
        params.initial_max_data = config.max_data;
        params.initial_max_stream_data_bidi_local = config.max_stream_data;
        params.initial_max_stream_data_bidi_remote = config.max_stream_data;
        params.initial_max_stream_data_uni = config.max_stream_data;
        params.initial_max_streams_bidi = config.max_streams_bidi;
        params.initial_max_streams_uni = config.max_streams_uni;
        params.active_connection_id_limit = config.active_connection_id_limit;
        params.initial_max_path_id = config.multipath_initial_max_path_id;
        params.idle_timeout = config.idle_timeout;
        params.server_name = config.server_name;
        return params;
    }

    auto make_stateless_reset_token(const quic_config& config, const connection_id& cid)
        -> std::expected<std::array<std::byte, 16>, std::error_code>
    {
        if (config.stateless_reset_token_generator)
            return config.stateless_reset_token_generator(cid);

        std::array<std::byte, 16> token{};
        if (RAND_bytes(reinterpret_cast<unsigned char*>(token.data()), token.size()) != 1)
            return std::unexpected(std::make_error_code(std::errc::io_error));
        return token;
    }

    // UDP may report an ICMP error from a previous datagram on a later
    // recvfrom/sendto call. RFC 9000 requires a QUIC implementation to treat
    // that as a path signal, not as a connection-fatal transport error: a
    // different validated path can still carry the connection and PMTU/path
    // validation needs its normal PTO timeout to make the final decision.
    [[nodiscard]] auto is_nonfatal_udp_path_error(const std::error_code& error) noexcept -> bool
    {
        const auto condition = error.default_error_condition();
        return error == make_error_code(errc::connection_refused) ||
            error == make_error_code(errc::connection_reset) ||
            error == make_error_code(errc::connection_aborted) ||
            error == make_error_code(errc::host_unreachable) ||
            error == make_error_code(errc::network_down) ||
            error == make_error_code(errc::network_unreachable) ||
            condition == std::errc::connection_refused ||
            condition == std::errc::connection_reset ||
            condition == std::errc::connection_aborted ||
            condition == std::errc::host_unreachable ||
            condition == std::errc::network_down ||
            condition == std::errc::network_reset ||
            condition == std::errc::network_unreachable;
    }

    [[nodiscard]] auto is_terminal_udp_send_error(const std::error_code& error) noexcept -> bool
    {
        const auto condition = error.default_error_condition();
        return !is_nonfatal_udp_path_error(error) &&
            error != make_error_code(errc::operation_would_block) &&
            condition != std::errc::operation_would_block &&
            condition != std::errc::resource_unavailable_try_again &&
            condition != std::errc::interrupted;
    }


} // namespace

struct quic_connection::quic_connection_impl
{
    io_context& ctx;
    io_context& socket_context;
    std::optional<udp::udp_socket> owned_socket;
    std::vector<std::byte> receive_storage;
    udp::udp_socket* socket{};
    endpoint peer;
    quic_role connection_role;
    quic_config config;
    connection_state connection_state{connection_state::idle};
    std::optional<connection_id> local_connection_id;
    std::optional<connection_id> peer_connection_id;
    std::optional<connection_id> initial_destination_id;
    std::optional<connection_id> original_destination_id;
    std::optional<quic_initial_keys> initial_keys;
    quic_version version{quic_version::v1};
    std::vector<std::byte> retry_token;
    bool retry_received{};
    std::optional<ssl_context> owned_tls_context;
    ssl_context* tls_context{};
    std::unique_ptr<quic_tls_session> tls;
    std::unordered_map<connection_id, quic_connection*> cids;

    struct local_cid_info
    {
        connection_id cid;
        std::array<std::byte, 16> stateless_reset_token{};
        std::uint32_t path_id{};
    };

    std::map<std::uint64_t, local_cid_info> local_connection_ids;
    // Path IDs other than zero have independent CID sequence spaces.  Keep
    // the RFC 9000 path-zero table unchanged and store the draft extension
    // tables separately so an identical sequence on two paths is valid.
    std::map<std::uint32_t, std::map<std::uint64_t, local_cid_info>>
        local_path_connection_ids;
    std::map<std::uint32_t, std::uint64_t> next_local_path_cid_sequences;
    std::vector<local_cid_info> retired_local_connection_ids;
    std::uint64_t next_local_cid_sequence{1};
    // Published to the UDP dispatcher, which may observe a connection while
    // its packet-owner advances the local CID set.
    std::atomic<std::uint64_t> local_cid_route_generation{1};

    struct peer_cid_info
    {
        connection_id cid;
        std::array<std::byte, 16> stateless_reset_token{};
        std::uint32_t path_id{};
    };

    std::map<std::uint64_t, peer_cid_info> peer_connection_ids;
    std::map<std::uint32_t, std::map<std::uint64_t, peer_cid_info>>
        peer_path_connection_ids;
    // Short-header DCIDs select the Path ID before packet-number recovery and
    // AEAD. A CID is immutable in that association for its full lifetime.
    std::unordered_map<connection_id, std::uint32_t> local_cid_path_ids;
    std::uint64_t peer_retire_prior_to{};
    std::optional<std::uint64_t> active_peer_cid_sequence;

    struct path_validation
    {
        endpoint peer;
        std::array<std::byte, 8> challenge{};
        time_point started;
        std::uint32_t path_id{};
    };

    // Keyed by the stable textual endpoint representation because endpoint
    // itself intentionally exposes no ordering relation. This is a bounded
    // control-plane map, never a packet hot-path routing table.
    std::map<std::string, path_validation, std::less<>> pending_path_validations;
    std::optional<endpoint> current_packet_sender;

    // Keep a stream and its one-token readiness latch in one tree entry.  A
    // QUIC STREAM frame used to probe two independent maps (and allocated two
    // map nodes) before it could notify its reader.  The connection's packet
    // owner serialises access, so co-locating the two pieces of per-stream
    // state removes that hot-path lookup without changing readiness semantics.
    struct stream_entry
    {
        std::unique_ptr<quic_stream> value;
        // It is intentionally bounded: a fast peer cannot accumulate
        // unbounded wake notifications while an application is processing a
        // previous read.
        std::shared_ptr<channel<std::monostate>> readiness;
        std::uint64_t deferred_reset_error{};
        bool local_send_accepted : 1 {};
        bool deferred_reset_stop_peer : 1 {};
        bool has_deferred_reset : 1 {};
        bool deferred_reset_queued : 1 {};
        bool deferred_stop_queued : 1 {};
    };

    std::unordered_map<stream_id, stream_entry> streams;
    // Request handlers own these tokens; entries are removed before a handler
    // returns. QUIC packet processing and handlers share the same io_context,
    // so raw observer pointers need no cross-thread synchronization.
    std::map<stream_id, cancel_token*> stream_cancellation_observers;
    /**
     * Preserves remote stream cancellation until a late application observer
     * is registered, preventing RESET_STREAM or STOP_SENDING from being lost.
     */
    std::set<stream_id> remotely_cancelled_streams;

    retired_stream_history retired_streams;
    channel<stream_id> accepted_streams;
    std::uint64_t peer_max_data;
    std::uint64_t sent_stream_data{};
    std::error_code terminal_send_error;
    std::error_code queued_protocol_error;
    std::uint64_t local_advertised_max_data;
    std::uint64_t received_stream_data{};
    std::uint64_t locally_consumed_data{};
    std::uint64_t peer_max_streams_bidi;
    std::uint64_t peer_max_streams_uni;
    std::uint64_t local_max_streams_bidi;
    std::uint64_t local_max_streams_uni;
    std::uint64_t retired_peer_streams_bidi{};
    std::uint64_t retired_peer_streams_uni{};
    // Authenticated MAX frames only; never provisional local configuration.
    std::uint64_t peer_max_data_from_frames{};
    std::uint64_t peer_max_streams_bidi_from_frames{};
    std::uint64_t peer_max_streams_uni_from_frames{};
    bool has_deferred_stream_cancellations{};
    bool peer_transport_parameters_applied{};

    // Pending 0-RTT retains its existing provisional initialization.
    // Ordinary 1-RTT starts at zero until authenticated peer limits apply.
    [[nodiscard]] auto initial_local_stream_send_limit(stream_id id) const
        -> std::optional<std::uint64_t>
    {
        if (!peer_transport_parameters_applied)
        {
            if (connection_role == quic_role::client &&
                tls->early_data_status() == early_data_state::pending)
                return std::nullopt;
            return 0U;
        }
        const auto& params = tls->received_transport_params();
        return is_unidirectional(id) ? params.initial_max_stream_data_uni
                                    : params.initial_max_stream_data_bidi_remote;
    }

    [[nodiscard]] auto can_defer_local_zero_send(stream_id id, const stream_entry& stream) const -> bool
    {
        const auto limit = is_bidirectional(id) ? peer_max_streams_bidi : peer_max_streams_uni;
        return (!peer_transport_parameters_applied || id / 4U + 1U > limit) &&
            is_client_initiated(id) == (connection_role == quic_role::client) &&
            !stream.local_send_accepted && !early_data_streams.contains(id) &&
            !early_data_rejected_observed &&
            (connection_role == quic_role::server || tls->early_data_status() == early_data_state::disabled) &&
            stream.value->bytes_sent() == 0U;
    }

    auto ensure_local_stream(stream_id id) -> stream_entry&
    {
        auto it = streams.find(id);
        if (it == streams.end())
        {
            // Publish only complete entries. Construction or map allocation
            // failure must leave both the map and next SID unchanged.
            stream_entry entry;
            entry.value = std::make_unique<quic_stream>(id, connection_role,
                is_bidirectional(id));
            if (const auto limit = initial_local_stream_send_limit(id))
                entry.value->set_initial_send_limit(*limit);
            else
                entry.value->update_send_limit(config.max_stream_data);
            entry.value->set_initial_receive_limit(config.max_stream_data);
            entry.value->init();
            entry.readiness = std::make_shared<channel<std::monostate>>(1);
            it = streams.try_emplace(id, std::move(entry)).first;
        }
        auto& next = is_bidirectional(id) ? next_bidi_stream : next_uni_stream;
        next = std::max(next, id + 4U);
        return it->second;
    }

    auto apply_initial_local_stream_send_limits() -> void
    {
        const auto& params = tls->received_transport_params();
        for (auto& [id, stream] : streams)
        {
            if (is_client_initiated(id) != (connection_role == quic_role::client))
                continue;
            const auto limit = is_unidirectional(id)
                ? params.initial_max_stream_data_uni
                : params.initial_max_stream_data_bidi_remote;
            // Streams begin at zero outside 0-RTT. Keep any genuine MAX_STREAM_DATA
            // already processed before this one-time transport-parameter step.
            stream.value->update_send_limit(limit);
        }
    }

    auto apply_initial_peer_send_limits() -> void
    {
        const auto& params = tls->received_transport_params();
        peer_max_data = std::max(params.initial_max_data, peer_max_data_from_frames);
        peer_max_streams_bidi = std::max(params.initial_max_streams_bidi,
            peer_max_streams_bidi_from_frames);
        peer_max_streams_uni = std::max(params.initial_max_streams_uni,
            peer_max_streams_uni_from_frames);
        apply_initial_local_stream_send_limits();
        if (has_deferred_stream_cancellations)
            send_flush_requested.store(true, std::memory_order_release);
    }

    bool handshake_confirmed{};
    bool multipath_negotiated{};
    std::uint32_t peer_max_path_id{};
    std::uint64_t next_bidi_stream;
    std::uint64_t next_uni_stream;
    std::deque<quic_frame_variant> send_queue;
    std::deque<quic_send_frame> encoded_send_frames;
    quic_stream_scheduler stream_scheduler;
    std::set<stream_id> pending_priority_streams;

    [[nodiscard]] auto has_prioritized_stream_frames() const noexcept -> bool
    {
        return !stream_scheduler.empty();
    }

    [[nodiscard]] auto prioritized_stream_frame_count() const noexcept -> std::size_t
    {
        return stream_scheduler.size();
    }

    auto enqueue_stream_frame(stream_id stream, quic_send_frame frame) -> void
    {
        stream_scheduler.enqueue(stream, std::move(frame));
    }

    [[nodiscard]] auto take_prioritized_stream_frame()
        -> std::optional<scheduled_quic_frame>
    {
        return stream_scheduler.take();
    }

    auto requeue_prioritized_stream_frame(scheduled_quic_frame frame) -> void
    {
        stream_scheduler.requeue(std::move(frame));
    }

    auto requeue_application_frame(quic_send_frame frame) -> void
    {
        if (!stream_scheduler.requeue_retransmission(frame))
            encoded_send_frames.push_front(std::move(frame));
    }

    auto set_stream_priority(stream_id stream, std::uint8_t urgency,
        bool incremental) noexcept -> void
    {
        if (retired_streams.contains(stream))
            return;
        if (!streams.contains(stream))
        {
            const bool local = is_client_initiated(stream) ==
                (connection_role == quic_role::client);
            const auto limit = is_bidirectional(stream)
                ? (local ? peer_max_streams_bidi : local_max_streams_bidi)
                : (local ? peer_max_streams_uni : local_max_streams_uni);
            // RFC 9218 permits future-stream updates. Retain a bounded set
            // inside the authenticated stream credit, without allocating a
            // QUIC stream merely for a priority hint.
            if (stream / 4U + 1U > limit ||
                (!pending_priority_streams.contains(stream) &&
                    pending_priority_streams.size() >= 256U))
                return;
            pending_priority_streams.insert(stream);
        }
        else
            pending_priority_streams.erase(stream);
        stream_scheduler.set_priority(stream, urgency, incremental);
    }

    auto return_receive_credit(std::uint64_t bytes) -> void
    {
        locally_consumed_data += bytes;
        if (local_advertised_max_data - locally_consumed_data <= config.max_data / 2U)
        {
            local_advertised_max_data = locally_consumed_data + config.max_data;
            encoded_send_frames.push_back(encode_frame(max_data_frame{
                local_advertised_max_data}));
        }
    }

    auto queue_protocol_close(quic_errc error, frame_type triggering_frame) -> void
    {
        if (queued_protocol_error)
            return;
        queued_protocol_error = make_error_code(error);
        connection_state = quic::connection_state::closing;
        send_queue.clear();
        encoded_send_frames.clear();
        unreliable_send_frames.clear();
        early_data_send_frames.clear();
        stream_scheduler = quic_stream_scheduler{};
        pending_priority_streams.clear();
        encoded_send_frames.push_back(encode_frame(connection_close_frame{
            static_cast<std::uint64_t>(error), static_cast<std::uint64_t>(triggering_frame),
            queued_protocol_error.message(), false}));
        send_flush_requested.store(true, std::memory_order_release);
    }

    // One coroutine-owned state domain serializes packet numbers, stream
    // state, recovery, and every frame queue. Packet construction enters this
    // domain briefly; UDP sends happen only after it is released.
    std::function<void(std::optional<std::chrono::steady_clock::time_point>)>
        timer_deadline_observer;
    async_mutex receive_mutex;
    // Publishes a snapshot while its state is still serialized, then releases
    // the mutex before notifying a shared listener. Covers every early return
    // without another task frame or an unlocked read of recovery state.
    struct listener_state_guard
    {
        quic_connection& connection;
        quic_connection_impl& state;
        bool locked{true};
        ~listener_state_guard() noexcept(false) { unlock_and_notify(); }
        auto unlock_and_notify() -> void
        {
            if (!locked)
                return;
            const auto deadline = state.timer_deadline_observer
                ? connection.next_timer_deadline() : std::nullopt;
            locked = false;
            state.receive_mutex.unlock();
            // As for every async member operation, its caller retains the
            // connection until this task completes. The observer is immutable.
            if (deadline)
                state.timer_deadline_observer(deadline);
        }
    };

    // Windows overlapped UDP sends on one socket must not be issued from
    // multiple coroutines concurrently; serialize the actual socket write
    // separately from packet construction.
    async_mutex socket_send_mutex;
    cancel_token send_cancel_token;
    // DATAGRAM frames are deliberately kept out of retransmission metadata.
    bounded_datagram_queue unreliable_send_frames;
    channel<std::vector<std::byte>> received_application_datagrams{128};
    // Packet parsing and socket writes are separate phases. A connection may
    // have many concurrent stream producers, but packet number, congestion
    // and pacing state have exactly one packet writer. `flush_mutex` is that
    // writer token; `send_flush_requested` is only a cross-coroutine wakeup
    // bit and must never be a plain bool.
    async_mutex flush_mutex;
    std::atomic<bool> send_flush_requested{};
    auto can_start_requested_flush() -> bool
    {
        if (!send_flush_requested.load(std::memory_order_acquire) ||
            !flush_mutex.try_lock())
            return false;
        // An existing writer consumes this edge before releasing ownership.
        // Its socket completion must be allowed to run: an immediate receive
        // deadline while it is suspended would spin without submitting I/O.
        // Preserve the edge and use the actual PTO/idle deadline when busy.
        flush_mutex.unlock();
        return true;
    }
    static constexpr std::size_t max_send_batch_datagrams{16U};
    // The packet owner is unique for a connection, including across the
    // asynchronous socket submission below. Keep its MTU-sized ciphertext
    // allocations and reuse them on the next burst instead of allocating one
    // vector for every packet of every static response.
    std::array<std::vector<std::byte>, max_send_batch_datagrams>
        application_packet_storage;
    std::array<std::vector<std::byte>, max_send_batch_datagrams>
        application_payload_storage;

    // Application coroutines publish Command objects to the single packet
    // owner. The queue owns its fixed completion Object Pool.
    quic_write_queue write_commands;

    // A client connection may be blocked in a UDP receive with no outstanding
    // packet when a request coroutine creates new retransmittable data. Wake
    // that receive so it recalculates its PTO deadline instead of waiting for
    // the idle timeout. The token is owned by this connection and is reset
    // only after its completed receive operation has unwound.
    // Shared listeners never allocate a private receive cancellation token.
    std::shared_ptr<cancel_token> receive_wait_token;
    std::atomic<bool> receive_rearm_requested{};
    // 0-RTT bytes are deliberately isolated from normal application frames.
    // A rejected offer is never replayed by the transport: only the HTTP
    // layer knows whether a request is idempotent and may retry it at 1-RTT.
    std::deque<std::vector<std::byte>> early_data_send_frames;
    std::unordered_set<stream_id> early_data_streams;
    bool early_data_rejected_observed{};

    static auto early_frame(std::vector<std::byte> bytes) -> quic_send_frame
    {
        std::optional<stream_id> id;
        const auto decoded = decode_frame(bytes);
        if (decoded)
            if (const auto* stream = std::get_if<stream_frame>(&decoded->first))
                id = stream->stream_id;
        auto result = quic_send_frame::owned(std::move(bytes), id);
        result.mark_early_data();
        return result;
    }

    auto discard_queued_early_data() -> void
    {
        stream_scheduler.discard_early_data();
        std::erase_if(encoded_send_frames, [](const auto& frame) { return frame.is_early_data(); });
    }

    auto reject_early_data_streams() -> void
    {
        // A request stream can be opened while TLS still reports 0-RTT as
        // pending, then lose the race to the handshake result before its
        // first STREAM frame is classified as early data.  Treat every
        // client-initiated bidirectional stream from that boundary as
        // retryable transport work too. HTTP/3 already prevents unsafe
        // methods from using this path and decides whether to replay it.
        std::set<stream_id> rejected{early_data_streams.begin(),
            early_data_streams.end()};
        if (connection_role == quic_role::client)
        {
            for (const auto& [sid, _] : streams)
            {
                if (is_client_initiated(sid) && !is_unidirectional(sid))
                    rejected.insert(sid);
            }
        }
        for (const auto sid : rejected)
        {
            const auto stream = streams.find(sid);
            if (stream == streams.end())
                continue;
            const auto final_size = stream->second.value->bytes_sent();
            stream->second.value->stop_local();
            // A rejected 0-RTT stream is aborted in both directions.  Merely
            // queuing RESET_STREAM/STOP_SENDING leaves the local receive side
            // in `half_closed_local`, so an HTTP/3 response reader can wait
            // forever for readability even though the early bytes are gone.
            // Mark the receive direction reset as well; the readiness wakeup
            // below then makes async_recv() return EOF and lets the HTTP layer
            // reconnect and replay only idempotent requests at 1-RTT.
            (void)stream->second.value->reset_remote(stream->second.value->bytes_received());
            encoded_send_frames.push_back(encode_frame(reset_stream_frame{
                sid, 0x010cU, final_size}));
            if (is_bidirectional(sid))
                encoded_send_frames.push_back(encode_frame(stop_sending_frame{
                    sid, 0x010cU}));
            auto readiness = stream->second.readiness;
            (void)readiness->try_send({});
        }
        early_data_streams.clear();
    }

    // BoringSSL can expose a rejected 0-RTT offer by its terminal reason at
    // the same time that a handshake drive reports completion, rather than
    // by a distinct SSL_ERROR_EARLY_DATA_REJECTED result.  Observe the TLS
    // outcome at every handshake boundary so an affected HTTP/3 stream is
    // always woken and its owner can safely reconnect at 1-RTT.
    auto settle_early_data_outcome() -> std::expected<void, std::error_code>
    {
        if (connection_role != quic_role::client || early_data_rejected_observed)
            return {};
        if (tls->early_data_status() != early_data_state::rejected)
        {
            // Once application keys exist RFC 9001 forbids more 0-RTT.
            // Promote only work not yet sealed; sent early packets remain in
            // the shared Application Data recovery ledger until ACK/loss.
            if (tls->write_keys(encryption_level::application))
                while (!early_data_send_frames.empty())
                {
                    requeue_application_frame(early_frame(std::move(early_data_send_frames.back())));
                    early_data_send_frames.pop_back();
                }
            return {};
        }

        early_data_send_frames.clear();
        discard_queued_early_data();
        (void)application_paths.at(0U)->recovery.take_early_data_frames();
        early_data_rejected_observed = true;
        reject_early_data_streams();
        return tls->reset_after_early_data_rejection();
    }

    // Initial and Handshake packet number spaces remain connection-wide.
    // draft-ietf-quic-multipath-12 creates a separate Application Data packet
    // number space for every Path ID; all mutable 1-RTT recovery state lives
    // here rather than being shared by candidate paths.
    struct application_path_state
    {
        std::uint32_t id{};
        endpoint peer;
        std::optional<connection_id> local_connection_id;
        std::optional<connection_id> peer_connection_id;
        // Path zero uses the connection's original socket. Non-zero paths may
        // borrow an application-owned socket so each path can use a distinct
        // local UDP tuple without making QUIC own an unsafe receiver task.
        udp::udp_socket* path_socket{};
        std::optional<std::uint64_t> active_peer_cid_sequence;
        std::uint64_t peer_retire_prior_to{};
        std::uint64_t next_local_cid_sequence{};
        std::uint64_t last_path_status_sequence{};
        std::uint64_t next_local_path_status_sequence{};
        bool peer_marks_backup{};
        bool locally_marks_backup{};
        bool validated{};
        bool locally_abandoned{};
        bool peer_abandoned{};
        std::optional<time_point> abandonment_deadline;
        std::uint64_t bytes_received{};
        std::uint64_t bytes_sent{};
        std::uint64_t next_send_packet_number{};
        std::optional<std::uint64_t> largest_received_packet_number;
        std::set<std::uint64_t> received_ack_eliciting_packet_numbers;
        quic_recovery_controller recovery;
        std::optional<time_point> pacing_credit_updated_at;
        double pacing_credit_bytes{};
        // RFC 9000 starts every path at the 1200-byte minimum. The ceiling is
        // raised only by an acknowledged padded PING probe, independently for
        // each Path ID so a narrow/encapsulated route cannot regress another.
        std::size_t discovered_max_datagram_payload{min_initial_pkt_size};
        bool path_mtu_fast_probe{true};
        std::optional<std::size_t> path_mtu_probe_target;
        std::optional<std::size_t> path_mtu_probe_in_flight;
        std::optional<time_point> next_path_mtu_probe_at;

        application_path_state(std::uint32_t path_id, endpoint remote,
            const quic_config& options)
            : id(path_id), peer(std::move(remote)), recovery(options)
        {
        }
    };

    std::map<std::uint32_t, std::unique_ptr<application_path_state>> application_paths;
    // A Path ID participates in packet-protection nonces and is permanently
    // consumed once abandoned.  This outlives the three-PTO resource grace
    // state so delayed PATH_NEW_CONNECTION_ID frames can never resurrect it.
    std::set<std::uint32_t> retired_path_ids;
    std::uint32_t sending_application_path_id{};
    std::uint32_t receiving_application_path_id{};
    std::uint32_t next_application_path_to_send{};

    [[nodiscard]] auto sending_application_path() -> application_path_state&
    {
        return *application_paths.at(sending_application_path_id);
    }

    [[nodiscard]] auto sending_application_path() const -> const application_path_state&
    {
        return *application_paths.at(sending_application_path_id);
    }

    [[nodiscard]] auto receiving_application_path() -> application_path_state&
    {
        return *application_paths.at(receiving_application_path_id);
    }

    [[nodiscard]] auto path_peer_connection_id(const application_path_state& path)
        const -> const connection_id*
    {
        if (path.peer_connection_id)
            return std::addressof(*path.peer_connection_id);
        if (path.id == 0U && peer_connection_id)
            return std::addressof(*peer_connection_id);
        return nullptr;
    }

    [[nodiscard]] static auto path_validation_timeout(
        const application_path_state& path) noexcept -> time_point::duration
    {
        // A freshly observed path has no trustworthy RTT sample of its own.
        // Reusing a very small localhost or previous-path PTO can otherwise
        // expire validation in the same event batch that delivers a relayed
        // PATH_RESPONSE. Retain QUIC's initial RTT floor until validation.
        const auto initial_rtt = std::chrono::duration_cast<time_point::duration>(
            std::chrono::milliseconds{333});
        return std::max(path.recovery.pto_duration(), initial_rtt) * 3;
    }

    [[nodiscard]] auto select_application_path_for_send() -> std::uint32_t
    {
        // Path zero remains the pre-negotiation and fallback path.  Once
        // additional paths have completed validation, round-robin only over
        // paths that have a matching peer CID and are not backup paths.
        if (!multipath_negotiated)
            return 0U;
        const auto select = [this](bool allow_backup) -> std::optional<std::uint32_t>
        {
            for (const auto& [id, path] : application_paths)
            {
                if (id < next_application_path_to_send || path->locally_abandoned ||
                    path->peer_abandoned || !path->validated ||
                    (!allow_backup && (path->peer_marks_backup || path->locally_marks_backup)) ||
                    !path_peer_connection_id(*path))
                    continue;
                return id;
            }
            for (const auto& [id, path] : application_paths)
            {
                if (path->locally_abandoned || path->peer_abandoned || !path->validated ||
                    (!allow_backup && (path->peer_marks_backup || path->locally_marks_backup)) ||
                    !path_peer_connection_id(*path))
                    continue;
                return id;
            }
            return std::nullopt;
        };
        const auto chosen = select(false).or_else([&]
            {
                return select(true);
            });
        if (!chosen)
            return 0U;
        next_application_path_to_send = *chosen == std::numeric_limits<std::uint32_t>::max()
            ? 0U
            : *chosen + 1U;
        return *chosen;
    }

    [[nodiscard]] auto path_with_pending_ack() const -> std::optional<std::uint32_t>
    {
        for (const auto& [id, path] : application_paths)
        {
            // A PATH_ACK names the packet-number space being acknowledged,
            // not the path that carries the frame.  Keep this state visible
            // during the three-PTO abandonment grace period: a surviving
            // path can still carry the acknowledgement for a recently
            // abandoned path.  Suppressing it here makes the peer retain
            // recoverable bytes until its loss timer fires.
            if (!path->received_ack_eliciting_packet_numbers.empty())
                return id;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto path_with_pending_mtu_probe() const -> std::optional<std::uint32_t>
    {
        for (const auto& [id, path] : application_paths)
        {
            if (path->path_mtu_probe_target && !path->locally_abandoned &&
                !path->peer_abandoned && path->validated &&
                path_peer_connection_id(*path))
                return id;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto path_mtu_probe_limit() const noexcept -> std::size_t
    {
        const auto peer_limit = tls ? tls->received_transport_params().max_udp_payload_size
                                    : std::uint64_t{min_initial_pkt_size};
        const auto configured = std::min(config.max_udp_payload_size,
            config.max_path_mtu);
        return static_cast<std::size_t>(std::clamp<std::uint64_t>(
            std::min(configured, peer_limit), min_initial_pkt_size,
            max_udp_receive_payload));
    }

    [[nodiscard]] auto packet_payload_budget(const application_path_state& path) const noexcept
        -> std::size_t
    {
        const auto* destination_cid = path_peer_connection_id(path);
        const auto* keys = tls ? tls->write_keys(encryption_level::application) : nullptr;
        // Frame encodings are already part of the plaintext payload. Only the
        // short header, packet number, and AEAD tag sit outside it, so account
        // their exact negotiated sizes instead of leaving unused bytes in
        // every application datagram.
        const auto overhead = 1U + (destination_cid ? destination_cid->size() : 20U) +
            4U + (keys ? keys->tag_len : 16U);
        return path.discovered_max_datagram_payload > overhead
            ? path.discovered_max_datagram_payload - overhead
            : 0U;
    }

    auto arm_path_mtu_discovery(time_point now) noexcept -> void
    {
        if (!config.enable_path_mtu_discovery)
            return;
        for (auto& [_, path] : application_paths)
        {
            if (path->validated && !path->locally_abandoned && !path->peer_abandoned &&
                path->discovered_max_datagram_payload < path_mtu_probe_limit() &&
                !path->path_mtu_probe_target && !path->path_mtu_probe_in_flight &&
                !path->next_path_mtu_probe_at)
            {
                path->next_path_mtu_probe_at = now +
                    config.path_mtu_initial_probe_delay;
            }
        }
    }

    auto schedule_due_path_mtu_probes(time_point now) noexcept -> bool
    {
        if (!config.enable_path_mtu_discovery)
            return false;
        bool scheduled{};
        const auto ceiling = path_mtu_probe_limit();
        for (auto& [_, path] : application_paths)
        {
            if (!path->validated || path->locally_abandoned || path->peer_abandoned ||
                path->path_mtu_probe_target || path->path_mtu_probe_in_flight ||
                !path->next_path_mtu_probe_at ||
                *path->next_path_mtu_probe_at > now ||
                path->discovered_max_datagram_payload >= ceiling)
                continue;
            // Keep the first probe conservative. Its ACK unlocks fast growth
            // until a live probe is lost or reaches PTO, then keep +128.
            // confirmed < ceiling above makes subtraction/addition bounded.
            const auto confirmed = path->discovered_max_datagram_payload;
            const auto increment = std::min(ceiling - confirmed,
                path->path_mtu_fast_probe && confirmed > min_initial_pkt_size
                    ? confirmed : std::size_t{128U});
            path->path_mtu_probe_target = confirmed + increment;
            path->next_path_mtu_probe_at.reset();
            scheduled = true;
        }
        return scheduled;
    }

    [[nodiscard]] auto earliest_path_mtu_probe_deadline() const
        -> std::optional<time_point>
    {
        std::optional<time_point> earliest;
        for (const auto& [_, path] : application_paths)
        {
            if (path->next_path_mtu_probe_at &&
                (!earliest || *path->next_path_mtu_probe_at < *earliest))
                earliest = path->next_path_mtu_probe_at;
        }
        return earliest;
    }

    [[nodiscard]] auto path_can_carry_application_packet(std::uint32_t path_id) const -> bool
    {
        const auto path = application_paths.find(path_id);
        return path != application_paths.end() && !path->second->locally_abandoned &&
            !path->second->peer_abandoned && path->second->validated &&
            path_peer_connection_id(*path->second);
    }

    [[nodiscard]] auto can_send_on_path(const application_path_state& path,
        std::size_t bytes) const noexcept -> bool
    {
        // RFC 9000 section 8 applies independently to every unvalidated
        // server-side path.  Count the bytes when packet construction commits
        // to the UDP submission; a failed submission therefore consumes
        // credit conservatively, which is safe and avoids an async race.
        if (connection_role != quic_role::server || path.validated)
            return true;
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        const auto allowed = path.bytes_received > maximum / 3U
            ? maximum
            : path.bytes_received * 3U;
        return path.bytes_sent <= allowed && bytes <= allowed - path.bytes_sent;
    }

    auto account_path_send(application_path_state& path, std::size_t bytes) noexcept -> void
    {
        const auto increment = static_cast<std::uint64_t>(bytes);
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        path.bytes_sent = path.bytes_sent > maximum - increment
            ? maximum
            : path.bytes_sent + increment;
    }

    [[nodiscard]] auto earliest_application_pto() const
        -> std::optional<std::tuple<std::uint32_t, time_point, pn_space>>
    {
        std::optional<std::tuple<std::uint32_t, time_point, pn_space>> earliest;
        for (const auto& [path_id, path] : application_paths)
        {
            const auto deadline = path->recovery.next_pto_deadline();
            if (deadline && (!earliest || deadline->first < std::get<1>(*earliest)))
                earliest.emplace(path_id, deadline->first, deadline->second);
        }
        return earliest;
    }

    [[nodiscard]] auto earliest_abandonment_deadline() const
        -> std::optional<time_point>
    {
        std::optional<time_point> earliest;
        for (const auto& [_, path] : application_paths)
        {
            if (path->abandonment_deadline &&
                (!earliest || *path->abandonment_deadline < *earliest))
                earliest = path->abandonment_deadline;
        }
        return earliest;
    }

    auto retire_expired_abandoned_paths(time_point now) -> void
    {
        for (auto path = application_paths.begin(); path != application_paths.end();)
        {
            const auto path_id = path->first;
            auto& state = *path->second;
            if (path_id == 0U || !state.abandonment_deadline ||
                now < *state.abandonment_deadline)
            {
                ++path;
                continue;
            }

            // Once the three-PTO grace period elapses, RFC 9002 recovery
            // records cannot be useful anymore.  Preserve application data by
            // treating every outstanding packet as lost and returning its
            // retransmittable frames to a surviving path's send queue.
            auto outstanding = state.recovery.take_outstanding_frames(
                pn_space::application);
            for (auto frame = outstanding.rbegin(); frame != outstanding.rend(); ++frame)
                encoded_send_frames.push_front(std::move(*frame));
            state.recovery.discard_packet_number_space(pn_space::application);

            if (const auto local = local_path_connection_ids.find(path_id);
                local != local_path_connection_ids.end())
            {
                for (const auto& [_, cid] : local->second)
                {
                    cids.erase(cid.cid);
                    local_cid_path_ids.erase(cid.cid);
                    retired_local_connection_ids.push_back(cid);
                }
                local_path_connection_ids.erase(local);
                next_local_path_cid_sequences.erase(path_id);
                local_cid_route_generation.fetch_add(1U, std::memory_order_release);
            }
            peer_path_connection_ids.erase(path_id);
            path = application_paths.erase(path);
        }
    }

    quic_recovery_controller recovery;
    std::optional<time_point> pacing_credit_updated_at;
    double pacing_credit_bytes{};
    std::array<std::uint64_t, encryption_level_count> next_send_packet_number{};
    // Packet numbers are independent for Initial, Handshake and Application
    // data.  Keep the largest successfully authenticated peer packet for
    // each space so truncated packet numbers can be recovered per RFC 9000
    // Appendix A.
    std::array<std::optional<std::uint64_t>, encryption_level_count>
        largest_received_packet_number{};
    std::array<std::set<std::uint64_t>, encryption_level_count>
        received_ack_eliciting_packet_numbers{};
    std::array<std::map<std::uint64_t, std::vector<std::byte>>,
        encryption_level_count>
        crypto_fragments;
    std::array<std::uint64_t, encryption_level_count> next_crypto_offset{};
    std::array<std::uint64_t, encryption_level_count> next_send_crypto_offset{};
    std::array<std::deque<std::vector<std::byte>>, encryption_level_count>
        retransmit_crypto_frames{};
    encryption_level receiving_level{encryption_level::initial};
    std::optional<time_point> idle_deadline;
    // RFC 9000 §10.2: after receiving CONNECTION_CLOSE, endpoints enter
    // draining for at least three PTOs.  Keep an explicit deadline instead
    // of leaving a connection permanently in the intermediate state.
    std::optional<time_point> draining_deadline;
    std::optional<time_point> next_diagnostic_snapshot;

    quic_connection_impl(io_context& context, udp::udp_socket&& udp_socket,
        endpoint remote, quic_role role, quic_config options,
        ssl_context* supplied_tls_context = nullptr)
        : ctx(context), socket_context(context), owned_socket(std::move(udp_socket)), receive_storage(max_udp_receive_payload), socket(std::addressof(*owned_socket)), peer(std::move(remote)), connection_role(role), config(options), accepted_streams(std::max<std::uint64_t>(1U, std::min(options.max_streams_bidi, std::uint64_t{1024}) + std::min(options.max_streams_uni, std::uint64_t{1024}))), peer_max_data(options.max_data), local_advertised_max_data(options.max_data), peer_max_streams_bidi(options.max_streams_bidi), peer_max_streams_uni(options.max_streams_uni), local_max_streams_bidi(options.max_streams_bidi), local_max_streams_uni(options.max_streams_uni), next_bidi_stream(role == quic_role::client ? 0U : 1U), next_uni_stream(role == quic_role::client ? 2U : 3U), recovery(options)
    {
        streams.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(128U,
            options.max_streams_bidi + options.max_streams_uni)));
        if (config.multipath_initial_max_path_id && config.cid_length == 0U)
            throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                "Multipath QUIC requires non-empty connection IDs");
        application_paths.emplace(0U,
            std::make_unique<application_path_state>(0U, peer, config));
        application_paths.at(0U)->validated = true;
        if (supplied_tls_context)
            tls_context = supplied_tls_context;
        else
        {
            auto tls_context_result = role == quic_role::client ? ssl_context::quic_client()
                                                                : ssl_context::quic_server();
            if (!tls_context_result)
                throw std::system_error(tls_context_result.error(), "create QUIC TLS context");
            owned_tls_context = std::move(*tls_context_result);
            tls_context = std::addressof(*owned_tls_context);
        }
        if (connection_role == quic_role::server && config.early_data_tickets)
        {
            if (config.early_data_context.empty())
                throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                    "server 0-RTT requires an early-data context");
            auto configured = configure_server_early_data_tickets(
                *tls_context, *config.early_data_tickets);
            if (!configured)
                throw std::system_error(configured.error(),
                    "configure QUIC server ticket callbacks");
        }
        const auto transport_params = transport_params_from_config(options);
        auto tls_session = role == quic_role::client
            ? quic_tls_session::client(*tls_context, transport_params)
            : quic_tls_session::server(*tls_context, transport_params);
        if (!tls_session)
            throw std::system_error(tls_session.error(), "create QUIC TLS session");
        tls = std::move(*tls_session);
        if (connection_role == quic_role::server && !config.early_data_context.empty())
        {
            auto early_context = tls->set_early_data_context(config.early_data_context);
            if (!early_context)
                throw std::system_error(early_context.error(), "configure QUIC early-data context");
        }
        if (connection_role == quic_role::server && config.early_data_tickets)
        {
            auto enabled = tls->enable_server_early_data();
            if (!enabled)
                throw std::system_error(enabled.error(), "enable QUIC server 0-RTT");
        }
        // A client-chosen DCID is the salt input for RFC 9001 Initial keys.
        // Servers defer this until the peer's Initial header has been parsed.
        if (connection_role == quic_role::client)
        {
            std::array<std::byte, 8> local_cid_bytes{};
            std::array<std::byte, 8> destination_cid_bytes{};
            std::random_device random;
            for (auto& byte : local_cid_bytes)
                byte = static_cast<std::byte>(random() & 0xffU);
            for (auto& byte : destination_cid_bytes)
                byte = static_cast<std::byte>(random() & 0xffU);
            local_connection_id = connection_id{local_cid_bytes.data(),
                static_cast<std::uint8_t>(local_cid_bytes.size())};
            local_connection_ids.emplace(0U, local_cid_info{*local_connection_id, {}});
            local_cid_path_ids.emplace(*local_connection_id, 0U);
            initial_destination_id = connection_id{destination_cid_bytes.data(),
                static_cast<std::uint8_t>(destination_cid_bytes.size())};
            original_destination_id = *initial_destination_id;
            auto keys = derive_initial_keys(version, *initial_destination_id);
            if (!keys)
                throw std::system_error(keys.error(), "derive QUIC Initial keys");
            initial_keys = std::move(*keys);
        }
    }

    quic_connection_impl(io_context& context, io_context& datagram_context,
        udp::udp_socket& shared_socket, endpoint remote, quic_role role,
        quic_config options, ssl_context& supplied_tls_context)
        : ctx(context), socket_context(datagram_context), socket(std::addressof(shared_socket)), peer(std::move(remote)), connection_role(role), config(options), accepted_streams(std::max<std::uint64_t>(1U, std::min(options.max_streams_bidi, std::uint64_t{1024}) + std::min(options.max_streams_uni, std::uint64_t{1024}))), peer_max_data(options.max_data), local_advertised_max_data(options.max_data), peer_max_streams_bidi(options.max_streams_bidi), peer_max_streams_uni(options.max_streams_uni), local_max_streams_bidi(options.max_streams_bidi), local_max_streams_uni(options.max_streams_uni), next_bidi_stream(role == quic_role::client ? 0U : 1U), next_uni_stream(role == quic_role::client ? 2U : 3U), recovery(options)
    {
        streams.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(128U,
            options.max_streams_bidi + options.max_streams_uni)));
        if (config.multipath_initial_max_path_id && config.cid_length == 0U)
            throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                "Multipath QUIC requires non-empty connection IDs");
        application_paths.emplace(0U,
            std::make_unique<application_path_state>(0U, peer, config));
        application_paths.at(0U)->validated = true;
        tls_context = std::addressof(supplied_tls_context);
        if (connection_role == quic_role::server && config.early_data_tickets)
        {
            if (config.early_data_context.empty())
                throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                    "server 0-RTT requires an early-data context");
            auto configured = configure_server_early_data_tickets(
                *tls_context, *config.early_data_tickets);
            if (!configured)
                throw std::system_error(configured.error(),
                    "configure QUIC server ticket callbacks");
        }
        const auto transport_params = transport_params_from_config(options);
        auto tls_session = role == quic_role::client
            ? quic_tls_session::client(*tls_context, transport_params)
            : quic_tls_session::server(*tls_context, transport_params);
        if (!tls_session)
            throw std::system_error(tls_session.error(), "create QUIC TLS session");
        tls = std::move(*tls_session);
        if (connection_role == quic_role::server && !config.early_data_context.empty())
        {
            auto early_context = tls->set_early_data_context(config.early_data_context);
            if (!early_context)
                throw std::system_error(early_context.error(), "configure QUIC early-data context");
        }
        if (connection_role == quic_role::server && config.early_data_tickets)
        {
            auto enabled = tls->enable_server_early_data();
            if (!enabled)
                throw std::system_error(enabled.error(), "enable QUIC server 0-RTT");
        }
    }
};

quic_connection::quic_connection(io_context& ctx, udp::udp_socket&& sock,
    endpoint peer, quic_role role, quic_config config)
    : impl_(std::make_unique<quic_connection_impl>(ctx, std::move(sock),
          std::move(peer), role, config))
{
}

quic_connection::quic_connection(io_context& ctx, udp::udp_socket& shared_socket,
    endpoint peer, quic_role role, ssl_context& tls_context, quic_config config)
    : impl_(std::make_unique<quic_connection_impl>(ctx, ctx, shared_socket,
          std::move(peer), role, config, tls_context))
{
}

quic_connection::quic_connection(io_context& ctx, io_context& socket_context,
    udp::udp_socket& shared_socket, endpoint peer, quic_role role,
    ssl_context& tls_context, quic_config config)
    : impl_(std::make_unique<quic_connection_impl>(ctx, socket_context,
          shared_socket, std::move(peer), role, config, tls_context))
{
}

quic_connection::quic_connection(io_context& ctx, udp::udp_socket&& sock,
    endpoint peer, quic_role role, ssl_context& tls_context, quic_config config)
    : impl_(std::make_unique<quic_connection_impl>(ctx, std::move(sock),
          std::move(peer), role, config, std::addressof(tls_context)))
{
}

quic_connection::~quic_connection() = default;

auto quic_connection::send_datagram(std::span<const std::byte> datagram,
    const endpoint& destination, udp::udp_socket* path_socket)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_await impl_->socket_send_mutex.lock();
    async_lock_guard send_guard{impl_->socket_send_mutex, std::adopt_lock};
    auto& selected_socket = path_socket ? *path_socket : *impl_->socket;
    auto& selected_context = path_socket ? path_socket->context() : impl_->socket_context;
    const bool switch_context =
        std::addressof(impl_->ctx) != std::addressof(selected_context);
#ifdef CNETMOD_HAS_IOCP
    if (switch_context)
        co_return co_await async_sendto_on(selected_context, impl_->ctx,
            selected_socket.native_socket(), const_buffer{datagram.data(), datagram.size()},
            destination, impl_->send_cancel_token);
#endif
    if (switch_context)
        co_await post_awaitable{selected_context};

    auto sent = co_await async_sendto(selected_context,
        selected_socket.native_socket(),
        const_buffer{datagram.data(), datagram.size()}, destination, impl_->send_cancel_token);

    if (switch_context)
        co_await post_awaitable{impl_->ctx};
    co_return sent;
}

auto quic_connection::send_datagram_batch(
    std::span<const udp_send_datagram> datagrams)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (datagrams.empty())
        co_return std::size_t{};

    // Batch submission is deliberately limited to the connection's primary
    // socket. A Multipath path may be application-owned and have a different
    // executor; retaining the single-datagram path there avoids crossing an
    // executor with borrowed packet buffers.
    co_await impl_->socket_send_mutex.lock();
    async_lock_guard send_guard{impl_->socket_send_mutex, std::adopt_lock};

    const bool switch_context = std::addressof(impl_->ctx) != std::addressof(impl_->socket_context);
#ifdef CNETMOD_HAS_IOCP
    if (switch_context)
    {
        std::size_t submitted{};
        for (const auto& datagram : datagrams)
        {
            const auto sent = co_await async_sendto_on(impl_->socket_context, impl_->ctx,
                impl_->socket->native_socket(), datagram.bytes, datagram.peer, impl_->send_cancel_token);
            if (!sent)
                co_return std::unexpected(sent.error());
            ++submitted;
        }
        co_return submitted;
    }
#endif
    if (switch_context)
        co_await post_awaitable{impl_->socket_context};
    std::expected<std::size_t, std::error_code> result{std::size_t{}};
    std::size_t submitted{};
    while (submitted != datagrams.size())
    {
#ifdef CNETMOD_HAS_IO_URING
        auto accepted = impl_->config.enable_udp_segmentation_offload
            ? co_await async_sendto_segmented_batch(impl_->socket_context,
                  impl_->socket->native_socket(), datagrams.subspan(submitted),
                  impl_->send_cancel_token)
            : co_await async_sendto_batch(impl_->socket_context,
                  impl_->socket->native_socket(), datagrams.subspan(submitted),
                  impl_->send_cancel_token);
#else
        auto accepted = co_await async_sendto_batch(impl_->socket_context,
            impl_->socket->native_socket(), datagrams.subspan(submitted), impl_->send_cancel_token);
#endif
        if (!accepted || *accepted == 0U)
        {
            result = std::unexpected(!accepted ? accepted.error()
                : std::make_error_code(std::errc::operation_would_block));
            break;
        }
        submitted += *accepted;
    }
    if (switch_context)
        co_await post_awaitable{impl_->ctx};
    if (!result)
        co_return std::unexpected(result.error());
    co_return submitted;
}

auto quic_connection::async_stop_io() -> task<void>
{
    // Every platform adapter marshals cancellation to its own queue (or uses
    // thread-safe CancelIoEx). Do not retain an application path's old raw
    // context pointer after its last operation has completed.
    impl_->send_cancel_token.request_cancel();
    impl_->send_cancel_token.dispatch_cancel();
    // Cancellation is a request. Borrowed packet storage remains alive until
    // the original SENDMSG/IOCP completion releases the unique send owner.
    co_await impl_->socket_send_mutex.lock();
    impl_->socket_send_mutex.unlock();
}

auto quic_connection::set_resumption_ticket(const session_ticket& ticket)
    -> std::expected<void, std::error_code>
{
    if (impl_->connection_role != quic_role::client ||
        impl_->connection_state != connection_state::idle)
    {
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }
    return impl_->tls->set_resumption_ticket(ticket);
}

auto quic_connection::enable_early_data() -> std::expected<void, std::error_code>
{
    if (impl_->connection_role != quic_role::client ||
        impl_->connection_state != connection_state::idle)
    {
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }
    impl_->tls->enable_early_data(true);
    return {};
}

auto quic_connection::take_resumption_ticket()
    -> std::expected<session_ticket, std::error_code>
{
    return impl_->tls->take_resumption_ticket();
}

auto quic_connection::early_data_status() const noexcept -> early_data_state
{
    if (impl_->early_data_rejected_observed)
        return early_data_state::rejected;
    return impl_->tls->early_data_status();
}

auto quic_connection::early_data_write_ready() const noexcept -> bool
{
    return impl_->connection_role == quic_role::client && impl_->tls &&
        impl_->tls->early_data_status() == early_data_state::pending &&
        impl_->tls->write_keys(encryption_level::early_data) != nullptr;
}

auto quic_connection::initiate_key_update()
    -> std::expected<void, std::error_code>
{
    if (impl_->connection_state != connection_state::connected || !impl_->tls)
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    return impl_->tls->initiate_key_update();
}

auto quic_connection::run() -> task<std::expected<void, std::error_code>>
{
    auto result = co_await do_run();

    if (!result && !is_closed())
        close_transport(result.error());
    co_return result;
}

auto quic_connection::process_datagram(std::span<const std::byte> datagram,
    const endpoint& sender) -> task<std::expected<void, std::error_code>>
{
    co_await impl_->receive_mutex.lock();
    quic_connection_impl::listener_state_guard receive_guard{*this, *impl_};
    if (impl_->connection_state == connection_state::draining)
    {
        if (impl_->draining_deadline && std::chrono::steady_clock::now() >= *impl_->draining_deadline)
        {
            close_transport(std::make_error_code(std::errc::not_connected));
        }
        // RFC 9000 §10.2: discard all packets while draining.  This must not
        // produce a response (including CONNECTION_CLOSE) to the peer.
        co_return {};
    }
    if (is_closed())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    auto processed = co_await process_packet(datagram, sender);
    if (!processed)
        co_return std::unexpected(processed.error());
    schedule_idle_timeout();
    // Do not write from the packet-processing call chain.  In a shared-socket
    // server this coroutine is the listener's receive path; awaiting pacing
    // or UDP writability here leaves no receive posted and can deadlock the
    // connection under sustained multiplexed load.  Stream producers and the
    // listener timer drive the connection-level writer after parsing returns.
    if (!impl_->encoded_send_frames.empty() || impl_->has_prioritized_stream_frames() ||
        !impl_->unreliable_send_frames.empty() || impl_->path_with_pending_ack())
        impl_->send_flush_requested.store(true, std::memory_order_release);
    co_return {};
}

auto quic_connection::async_poll_timers() -> task<void>
{
    co_await impl_->receive_mutex.lock();
    quic_connection_impl::listener_state_guard receive_guard{*this, *impl_};
    if (is_closed())
        co_return;

    const auto now = std::chrono::steady_clock::now();

    impl_->retire_expired_abandoned_paths(now);
    if (impl_->schedule_due_path_mtu_probes(now))
        impl_->send_flush_requested.store(true, std::memory_order_release);

    if (impl_->connection_state == connection_state::draining)
    {
        if (impl_->draining_deadline && now >= *impl_->draining_deadline)
        {
            close_transport(std::make_error_code(std::errc::not_connected));
        }
        co_return;
    }
    if (impl_->idle_deadline && now >= *impl_->idle_deadline)
    {
        handle_idle_timeout();
        co_return;
    }

    const auto handshake_pto = impl_->recovery.next_pto_deadline();
    const auto application_pto = impl_->earliest_application_pto();
    const bool pto_due = !impl_->queued_protocol_error && ((handshake_pto && handshake_pto->first <= now) ||
        (application_pto && std::get<1>(*application_pto) <= now));
    const bool flush_requested =
        impl_->send_flush_requested.load(std::memory_order_acquire) ||
        !impl_->encoded_send_frames.empty() || impl_->has_prioritized_stream_frames() ||
        impl_->path_with_pending_ack().has_value() ||
        impl_->path_with_pending_mtu_probe().has_value();
    // Packet state above belongs to the receive serial domain.  UDP sending
    // may suspend on pacing or socket writability, so it must run after that
    // domain is released; otherwise the timer can stall all incoming packets.
    receive_guard.unlock_and_notify();
    if (pto_due)
    {
        const auto probed = co_await handle_pto();
        if (!probed)
        {
            close_transport(probed.error());
            co_return;
        }
    }
    if (flush_requested)
        co_await flush_send_queue();
}

auto quic_connection::set_timer_deadline_observer(std::function<void(
    std::optional<std::chrono::steady_clock::time_point>)> observer) -> void
{
    impl_->timer_deadline_observer = std::move(observer);
}

auto quic_connection::next_timer_deadline() const
    -> std::optional<std::chrono::steady_clock::time_point>
{
    if (is_closed())
        return std::nullopt;

    const auto now = std::chrono::steady_clock::now();
    std::optional<time_point> earliest;
    const auto consider = [&earliest](std::optional<time_point> deadline)
    {
        if (deadline && (!earliest || *deadline < *earliest))
            earliest = *deadline;
    };
    if (impl_->connection_state == connection_state::draining)
    {
        consider(impl_->draining_deadline);
        return earliest;
    }
    consider(impl_->idle_deadline);
    consider(impl_->earliest_abandonment_deadline());
    consider(impl_->earliest_path_mtu_probe_deadline());
    if (const auto pto = impl_->recovery.next_pto_deadline())
        consider(pto->first);
    if (const auto pto = impl_->earliest_application_pto())
        consider(std::get<1>(*pto));

    // A producer or packet-processing pass publishes this edge when fresh
    // work can be attempted immediately. Frames left queued after a flush are
    // congestion-blocked; scheduling them at `now` spins the listener timer
    // until an ACK arrives. Their recovery PTO is already considered above,
    // and processing that ACK publishes a new flush edge.
    if (impl_->can_start_requested_flush())
        consider(now);
    return earliest;
}

auto quic_connection::do_run() -> task<std::expected<void, std::error_code>>
{
    if (!impl_->socket->is_open())
        co_return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));

    if (impl_->connection_state == connection_state::idle)
    {
        impl_->connection_state = connection_state::handshaking;
        if (impl_->connection_role == quic_role::client)
        {
            auto configured = impl_->tls->configure_initial_source_connection_id(
                *impl_->local_connection_id);
            if (!configured)
                co_return std::unexpected(configured.error());
        }
        auto handshake = impl_->tls->do_handshake();
        if (!handshake)
            co_return std::unexpected(handshake.error());
        if (impl_->connection_role == quic_role::client)
        {
            auto sent = co_await send_initial_packets();
            if (!sent)
                co_return std::unexpected(sent.error());
            if (*sent == 0U)
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            // BoringSSL installs the client early-write secret while creating
            // the Initial flight.  Send queued replay-safe application bytes
            // only after that secret exists, in their own 0-RTT long packet.
            const auto early_sent = co_await pack_and_send_packets();
            if (!early_sent)
                co_return std::unexpected(early_sent.error());
        }
        schedule_idle_timeout();
    }

    while (!is_closed())
    {
        auto datagram = co_await recv_datagram();
        if (!datagram)
        {
            if (is_closed())
                break;
            if (datagram.error() == std::make_error_code(std::errc::operation_canceled))
            {
                if (impl_->send_flush_requested.load(std::memory_order_acquire))
                    co_await async_poll_timers();
                continue;
            }
            if (datagram.error() == std::make_error_code(std::errc::timed_out))
            {
                // A receive deadline can be an abandonment grace deadline,
                // not only a PTO.  Run the complete timer path so expired
                // Path ID state is retired instead of spinning on the same
                // already-past deadline.
                co_await async_poll_timers();
                continue;
            }
            if (is_nonfatal_udp_path_error(datagram.error()))
            {
                // ICMP errors are asynchronous UDP path feedback. Preserve
                // the connection and let normal PTO/path-validation state
                // decide whether this path eventually fails.
                continue;
            }
            co_return std::unexpected(datagram.error());
        }

        co_await impl_->receive_mutex.lock();
        async_lock_guard receive_guard{impl_->receive_mutex, std::adopt_lock};
        auto processed = co_await process_packet(datagram->bytes, datagram->sender);
        if (!processed)
        {

            co_return std::unexpected(processed.error());
        }

        schedule_idle_timeout();

        const bool flush_requested = !impl_->encoded_send_frames.empty() ||
            !impl_->early_data_send_frames.empty() ||
            impl_->has_prioritized_stream_frames() ||
            (impl_->connection_role == quic_role::server &&
                impl_->tls->has_pending_handshake_data(encryption_level::application)) ||
            impl_->path_with_pending_ack().has_value();
        receive_guard.release();
        impl_->receive_mutex.unlock();
        if (flush_requested)
            co_await flush_send_queue();
    }

    co_return {};
}

auto quic_connection::recv_datagram()
    -> task<std::expected<received_datagram, std::error_code>>
{
    auto& storage = impl_->receive_storage;
    endpoint sender;
    if (!impl_->receive_wait_token)
        impl_->receive_wait_token = std::make_shared<cancel_token>();
    auto& token = *impl_->receive_wait_token;
    // `with_timeout` has fully joined its operation and timer before this
    // function resumes, so the previous IOCP/epoll cancellation registration
    // is no longer live here.
    token.reset();
    (void)impl_->receive_rearm_requested.exchange(false,
        std::memory_order_acq_rel);
    auto pto = impl_->recovery.next_pto_deadline();
    if (const auto application_pto = impl_->earliest_application_pto();
        application_pto && (!pto || std::get<1>(*application_pto) < pto->first))
        pto = std::pair{std::get<1>(*application_pto), std::get<2>(*application_pto)};
    std::expected<std::size_t, std::error_code> received;
    std::optional<time_point> deadline;
    if (pto)
        deadline = pto->first;
    if (impl_->idle_deadline && (!deadline || *impl_->idle_deadline < *deadline))
        deadline = impl_->idle_deadline;
    if (const auto abandoned = impl_->earliest_abandonment_deadline();
        abandoned && (!deadline || *abandoned < *deadline))
        deadline = abandoned;
    if (const auto path_mtu_probe = impl_->earliest_path_mtu_probe_deadline();
        path_mtu_probe && (!deadline || *path_mtu_probe < *deadline))
        deadline = path_mtu_probe;
    if (impl_->can_start_requested_flush())
        deadline = std::chrono::steady_clock::now();
    if (deadline)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto timeout = *deadline > now ? *deadline - now
                                             : std::chrono::steady_clock::duration::zero();
        received = co_await with_timeout(impl_->ctx, timeout,
            async_recvfrom(impl_->ctx, impl_->socket->native_socket(),
                mutable_buffer{storage.data(), storage.size()}, sender, token),
            token);
        if (!received && token.is_cancelled())
        {
            const auto cancellation = token.reason();
            const bool rearm = impl_->receive_rearm_requested.exchange(false,
                std::memory_order_acq_rel);
            token.reset();
            if (rearm && cancellation == cancellation_reason::caller_cancelled)
            {
                co_return std::unexpected(
                    std::make_error_code(std::errc::operation_canceled));
            }
            if (impl_->idle_deadline && std::chrono::steady_clock::now() >= *impl_->idle_deadline)
                handle_idle_timeout();
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        }
    }
    else
    {
        received = co_await async_recvfrom(impl_->ctx, impl_->socket->native_socket(),
            mutable_buffer{storage.data(), storage.size()}, sender, token);
        if (!received && token.is_cancelled())
        {
            const bool rearm = impl_->receive_rearm_requested.exchange(false,
                std::memory_order_acq_rel);
            token.reset();
            if (rearm)
            {
                co_return std::unexpected(
                    std::make_error_code(std::errc::operation_canceled));
            }
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        }
    }
    if (!received)
    {
        token.reset();
        co_return std::unexpected(received.error());
    }

    token.reset();

    co_return received_datagram{
        std::span<const std::byte>{storage.data(), *received}, std::move(sender)};
}

auto quic_connection::process_packet(std::span<const std::byte> packet,
    const endpoint& sender)
    -> task<std::expected<void, std::error_code>>
{
    (void)sender; // path validation consumes sender in the next transport layer step
    if (packet.empty())
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    const auto first = std::to_integer<std::uint8_t>(packet.front());
    // Only long-header packets can be coalesced before another packet. A
    // short-header packet always consumes the remainder of its UDP datagram
    // (RFC 9000 section 12.2), so bypass the splitter and its packet-vector
    // allocation on the established-connection hot path.
    if ((first & 0x80U) != 0U)
    {
        auto coalesced = split_coalesced_packets(packet);
        if (!coalesced)
            co_return std::unexpected(coalesced.error());
        const bool packet_boundary_changed = coalesced->packets.size() != 1U ||
            coalesced->packets.front().data() != packet.data() ||
            coalesced->packets.front().size() != packet.size();
        if (packet_boundary_changed)
        {
            for (const auto individual : coalesced->packets)
            {
                auto processed = co_await process_packet(individual, sender);
                if (!processed)
                    co_return std::unexpected(processed.error());
            }
            co_return {};
        }
    }

    if ((first & 0x80U) != 0U)
    {
        auto header = decode_long_header(packet);
        if (!header)
            co_return std::unexpected(header.error());
        if (header->type == packet_type::version_negotiation)
        {
            if (impl_->connection_role != quic_role::client || impl_->retry_received ||
                !impl_->local_connection_id || !impl_->original_destination_id ||
                header->dcid != *impl_->local_connection_id ||
                header->scid != *impl_->original_destination_id ||
                header->payload.empty() || header->payload.size() % 4 != 0)
                co_return std::unexpected(make_error_code(quic_errc::protocol_violation));

            bool includes_current = false;
            bool supports_v2 = false;
            for (std::size_t offset = 0; offset < header->payload.size(); offset += 4)
            {
                const auto offered = (std::to_integer<std::uint32_t>(header->payload[offset]) << 24) |
                    (std::to_integer<std::uint32_t>(header->payload[offset + 1]) << 16) |
                    (std::to_integer<std::uint32_t>(header->payload[offset + 2]) << 8) |
                    std::to_integer<std::uint32_t>(header->payload[offset + 3]);
                includes_current = includes_current ||
                    offered == static_cast<std::uint32_t>(impl_->version);
                supports_v2 = supports_v2 || offered == quic_version_v2;
            }
            // A VN that advertises the version we selected is spoofable and
            // must be ignored (RFC 9000 §6.1); do not downgrade.
            if (includes_current)
                co_return {};
            if (!supports_v2 || impl_->version == quic_version::v2)
                co_return std::unexpected(make_error_code(quic_errc::protocol_violation));

            impl_->version = quic_version::v2;
            impl_->retry_token.clear();
            impl_->initial_destination_id = *impl_->original_destination_id;
            auto keys = derive_initial_keys(impl_->version, *impl_->initial_destination_id);
            if (!keys)
                co_return std::unexpected(keys.error());
            impl_->initial_keys = std::move(*keys);
            auto& pending = impl_->retransmit_crypto_frames[level_index(encryption_level::initial)];
            for (auto& frame : impl_->recovery.take_outstanding_frames(pn_space::initial))
                pending.push_back(frame.release_owned_bytes());
            impl_->recovery.discard_packet_number_space(pn_space::initial);
            impl_->next_send_packet_number[level_index(encryption_level::initial)] = 0;
            auto sent = co_await send_initial_packets();
            if (!sent)
                co_return std::unexpected(sent.error());
            if (*sent == 0U)
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            co_return {};
        }
        if (header->type == packet_type::retry)
        {
            if (impl_->connection_role != quic_role::client ||
                !impl_->original_destination_id || header->scid.empty())
                co_return std::unexpected(make_error_code(quic_errc::protocol_violation));
            // RFC 9000 §17.2.5: a client accepts at most one Retry. A
            // fragmented first flight can make a stateless server answer each
            // tokenless Initial fragment; every response after the first is
            // stale and must not replace the validated Retry SCID/token.
            if (impl_->retry_received ||
                impl_->largest_received_packet_number[level_index(encryption_level::initial)] ||
                impl_->largest_received_packet_number[level_index(encryption_level::handshake)])
                co_return {};
            auto verified = validate_retry_integrity_tag(impl_->version,
                *impl_->original_destination_id, packet);
            if (!verified)
            {

                co_return std::unexpected(verified.error());
            }

            // RFC 9000 §17.2.5: the Retry SCID becomes the DCID of the next
            // client Initial; its opaque token is copied verbatim.
            impl_->retry_token.assign(header->token.begin(), header->token.end());
            impl_->peer_connection_id = header->scid;
            impl_->initial_destination_id = header->scid;
            auto keys = derive_initial_keys(impl_->version, header->scid);
            if (!keys)
                co_return std::unexpected(keys.error());
            impl_->initial_keys = std::move(*keys);
            impl_->largest_received_packet_number[level_index(encryption_level::initial)].reset();
            auto& pending = impl_->retransmit_crypto_frames[level_index(encryption_level::initial)];
            for (auto& frame : impl_->recovery.take_outstanding_frames(pn_space::initial))
                pending.push_back(frame.release_owned_bytes());
            impl_->recovery.discard_packet_number_space(pn_space::initial);
            impl_->retry_received = true;
            // Retry changes the DCID, not packet numbers. Recover the early
            // packets the stateless server could not accept with the old CID.
            auto early = impl_->application_paths.at(0U)->recovery.take_early_data_frames();
            auto queued = impl_->stream_scheduler.take_early_data();
            std::erase_if(impl_->encoded_send_frames, [&](auto& frame)
                {
                    if (!frame.is_early_data())
                        return false;
                    queued.push_back(std::move(frame));
                    return true;
                });
            for (auto& frame : queued)
                if (std::ranges::find(early, frame) == early.end())
                    early.push_back(std::move(frame));
            for (auto frame = early.rbegin(); frame != early.rend(); ++frame)
                impl_->early_data_send_frames.push_front(frame->release_owned_bytes());
            auto sent = co_await send_initial_packets();
            if (!sent)
                co_return std::unexpected(sent.error());
            if (*sent == 0U)
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            co_return {};
        }
        // RFC 9001 §4.9 requires Initial keys to be discarded after the TLS
        // handshake completes. A delayed Initial is then only a duplicate
        // UDP datagram. In particular, a server must not derive a new Initial
        // key set from it: doing so would feed stale CRYPTO data back into a
        // completed BoringSSL state machine and can turn a legitimate 0-RTT
        // replay rejection into a connection-fatal protocol error.
        if (header->type == packet_type::initial &&
            impl_->tls->is_handshake_complete() && !impl_->initial_keys)
            co_return {};
        if (header->type == packet_type::initial &&
            impl_->connection_role == quic_role::client)
        {
            // The server's SCID becomes the destination CID for all client
            // packets after the first Initial (RFC 9000 §7.2).
            impl_->peer_connection_id = header->scid;
            if (!impl_->initial_keys)
                co_return {};
        }
        if (header->type == packet_type::initial &&
            impl_->connection_role == quic_role::server && !impl_->initial_keys)
        {
            // The server derives Initial protection from the client's DCID,
            // exactly as specified by RFC 9001 §5.2.
            const auto peer_version = static_cast<quic_version>(header->version);
            auto keys = derive_initial_keys(peer_version, header->dcid);
            if (!keys)
                co_return std::unexpected(keys.error());
            impl_->version = peer_version;
            impl_->initial_destination_id = header->dcid;
            impl_->initial_keys = std::move(*keys);
            impl_->peer_connection_id = header->scid;
            if (!impl_->local_connection_id)
            {
                std::array<std::byte, 8> local_cid_bytes{};
                std::random_device random;
                for (auto& byte : local_cid_bytes)
                    byte = static_cast<std::byte>(random() & 0xffU);
                impl_->local_connection_id = connection_id{local_cid_bytes.data(),
                    static_cast<std::uint8_t>(local_cid_bytes.size())};
                impl_->local_connection_ids.emplace(0U,
                    quic_connection_impl::local_cid_info{*impl_->local_connection_id, {}});
                impl_->local_cid_path_ids.emplace(*impl_->local_connection_id, 0U);
            }
            if (impl_->connection_role == quic_role::server)
            {
                auto configured = impl_->tls->configure_initial_source_connection_id(
                    *impl_->local_connection_id);
                if (!configured)
                    co_return std::unexpected(configured.error());
            }
        }
        if (header->type == packet_type::initial)
        {
            impl_->receiving_level = encryption_level::initial;
            // Locate the protected packet number using only fields that are
            // intentionally left visible in a long header.
            std::size_t offset = 1 + 4;
            if (offset >= packet.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));
            const auto dcid_length = std::to_integer<std::uint8_t>(packet[offset++]);
            offset += dcid_length;
            if (offset >= packet.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));
            const auto scid_length = std::to_integer<std::uint8_t>(packet[offset++]);
            offset += scid_length;
            auto token_length = decode_varint(packet.subspan(offset));
            if (!token_length)
                co_return std::unexpected(token_length.error());
            offset += token_length->second + static_cast<std::size_t>(token_length->first);
            auto payload_length = decode_varint(packet.subspan(offset));
            if (!payload_length)
                co_return std::unexpected(payload_length.error());
            offset += payload_length->second;
            if (!impl_->initial_keys || offset >= packet.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));

            std::vector<std::byte> wire(packet.begin(), packet.end());
            const auto& read_keys = impl_->connection_role == quic_role::client
                ? impl_->initial_keys->server
                : impl_->initial_keys->client;
            auto pn_length = unprotect_header(read_keys, wire, offset, true);
            if (!pn_length)
                co_return std::unexpected(pn_length.error());
            if (offset + *pn_length > wire.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));
            std::uint64_t truncated_packet_number{};
            for (std::size_t i = 0; i < *pn_length; ++i)
                truncated_packet_number = (truncated_packet_number << 8) |
                    std::to_integer<std::uint8_t>(wire[offset + i]);
            const auto level = encryption_level::initial;
            const auto packet_number = packet_number_decode(
                static_cast<std::uint32_t>(truncated_packet_number),
                static_cast<std::uint32_t>(*pn_length * 8),
                impl_->largest_received_packet_number[level_index(level)].value_or(0));
            auto plaintext = open_payload(read_keys,
                std::span<const std::byte>{wire}.subspan(offset + *pn_length),
                std::span<const std::byte>{wire}.first(offset + *pn_length), packet_number);
            if (!plaintext)
            {
                // RFC 9000 §5.2 / RFC 9001 §5.8: a packet that fails
                // authenticated decryption is indistinguishable from an
                // unrelated or delayed UDP datagram and must be discarded.
                // Treating it as a fatal driver error makes 0-RTT flaky: an
                // Initial/Handshake retransmission can arrive after packet
                // number/key state has advanced and otherwise tears down a
                // healthy connection with BoringSSL's BAD_DECRYPT.
                co_return {};
            }
            auto& largest = impl_->largest_received_packet_number[level_index(level)];
            if (!largest || packet_number > *largest)
                largest = packet_number;
            bool ack_eliciting = false;
            for (std::size_t frame_offset = 0; frame_offset < plaintext->size();)
            {
                // PADDING has no frame state and is legal at this encryption
                // level. Consume the run without constructing a variant and
                // two coroutine frames for every padding byte.
                while (frame_offset < plaintext->size() &&
                    (*plaintext)[frame_offset] == std::byte{})
                    ++frame_offset;
                if (frame_offset == plaintext->size())
                    break;
                auto frame = decode_frame(std::span<const std::byte>{*plaintext}.subspan(frame_offset));
                if (!frame || frame->second == 0)
                    co_return std::unexpected(frame ? std::make_error_code(std::errc::bad_message) : frame.error());
                ack_eliciting = ack_eliciting || is_ack_eliciting(frame->first);
                co_await process_frames(frame->first);

                frame_offset += frame->second;
            }
            if (ack_eliciting)
                record_ack_eliciting_packet(
                    impl_->received_ack_eliciting_packet_numbers[level_index(level)], packet_number);
            auto handshake = impl_->tls->do_handshake();
            if (!handshake)
                co_return std::unexpected(handshake.error());
            if (*handshake == handshake_result::early_data_rejected)
            {
                auto settled = impl_->settle_early_data_outcome();
                if (!settled)
                    co_return std::unexpected(settled.error());
            }
            if (impl_->tls->has_pending_handshake_data() ||
                !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::initial)].empty())
            {
                auto initial_count = co_await send_initial_packets();
                if (!initial_count)
                    co_return std::unexpected(initial_count.error());
                auto handshake_count = co_await send_handshake_packets();
                if (!handshake_count)
                    co_return std::unexpected(handshake_count.error());
                if (*initial_count == 0U && *handshake_count == 0U)
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }
        else if (header->type == packet_type::handshake)
        {
            // Handshake long headers omit the Initial token field.  Keys are
            // installed by BoringSSL after it processes the Initial flight.
            const auto* read_keys = impl_->tls->read_keys(encryption_level::handshake);
            if (!read_keys)
            {
                // UDP can deliver a Handshake packet before the Initial packet
                // that installs its read keys. RFC 9000 permits discarding a
                // packet whose keys are not available; loss recovery will
                // retransmit it. A post-handshake duplicate is discarded for
                // the same reason after the keys have been retired.
                co_return {};
            }
            std::size_t offset = 1 + 4;
            if (offset >= packet.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));
            const auto dcid_length = std::to_integer<std::uint8_t>(packet[offset++]);
            offset += dcid_length;
            if (offset >= packet.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));
            const auto scid_length = std::to_integer<std::uint8_t>(packet[offset++]);
            offset += scid_length;
            auto payload_length = decode_varint(packet.subspan(offset));
            if (!payload_length)
                co_return std::unexpected(payload_length.error());
            offset += payload_length->second;
            std::vector<std::byte> wire(packet.begin(), packet.end());
            auto pn_length = unprotect_header(*read_keys, wire, offset, true);
            if (!pn_length)
                co_return std::unexpected(pn_length.error());
            std::uint64_t truncated_packet_number{};
            for (std::size_t i = 0; i < *pn_length; ++i)
                truncated_packet_number = (truncated_packet_number << 8) |
                    std::to_integer<std::uint8_t>(wire[offset + i]);
            const auto level = encryption_level::handshake;
            const auto packet_number = packet_number_decode(
                static_cast<std::uint32_t>(truncated_packet_number),
                static_cast<std::uint32_t>(*pn_length * 8),
                impl_->largest_received_packet_number[level_index(level)].value_or(0));
            auto plaintext = open_payload(*read_keys,
                std::span<const std::byte>{wire}.subspan(offset + *pn_length),
                std::span<const std::byte>{wire}.first(offset + *pn_length), packet_number);
            if (!plaintext)
            {
                // Handshake datagrams are UDP and may be duplicated or
                // reordered.  As for Initial/application packets, failed
                // AEAD authentication is a drop, never a connection-fatal
                // transport error.
                co_return {};
            }
            // RFC 9001 §4.9: an authenticated Handshake packet proves both
            // peers have Handshake keys, so Initial keys and their CRYPTO/PN
            // state must no longer be retained.
            impl_->initial_keys.reset();
            impl_->crypto_fragments[level_index(encryption_level::initial)].clear();
            impl_->retransmit_crypto_frames[level_index(encryption_level::initial)].clear();
            impl_->recovery.discard_packet_number_space(pn_space::initial);
            auto& largest = impl_->largest_received_packet_number[level_index(level)];
            if (!largest || packet_number > *largest)
                largest = packet_number;
            impl_->receiving_level = encryption_level::handshake;
            bool ack_eliciting = false;
            for (std::size_t frame_offset = 0; frame_offset < plaintext->size();)
            {
                while (frame_offset < plaintext->size() &&
                    (*plaintext)[frame_offset] == std::byte{})
                    ++frame_offset;
                if (frame_offset == plaintext->size())
                    break;
                auto frame = decode_frame(std::span<const std::byte>{*plaintext}.subspan(frame_offset));
                if (!frame || frame->second == 0)
                    co_return std::unexpected(frame ? std::make_error_code(std::errc::bad_message) : frame.error());
                ack_eliciting = ack_eliciting || is_ack_eliciting(frame->first);
                co_await process_frames(frame->first);
                frame_offset += frame->second;
            }
            if (ack_eliciting)
                record_ack_eliciting_packet(
                    impl_->received_ack_eliciting_packet_numbers[level_index(level)], packet_number);
            auto handshake = impl_->tls->do_handshake();
            if (!handshake)
                co_return std::unexpected(handshake.error());
            if (*handshake == handshake_result::early_data_rejected)
            {
                auto settled = impl_->settle_early_data_outcome();
                if (!settled)
                    co_return std::unexpected(settled.error());
            }
            if (impl_->tls->has_pending_handshake_data() ||
                !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::handshake)].empty())
            {
                auto sent = co_await send_handshake_packets();
                if (!sent)
                    co_return std::unexpected(sent.error());
            }
        }
        else if (header->type == packet_type::zero_rtt)
        {
            // RFC 9001 section 4.6.1: only a server that authenticated and
            // atomically consumed the application-owned ticket may process
            // early STREAM data.  A rejected/replayed offer is silently
            // discarded; it must never reach the application.
            if (impl_->connection_role != quic_role::server ||
                !impl_->tls->early_data_accepted())
            {

                co_return {};
            }
            const auto* read_keys = impl_->tls->read_keys(encryption_level::early_data);
            if (!read_keys)
                co_return {};

            std::size_t offset = 1 + 4;
            if (offset >= packet.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));
            const auto dcid_length = std::to_integer<std::uint8_t>(packet[offset++]);
            offset += dcid_length;
            if (offset >= packet.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));
            const auto scid_length = std::to_integer<std::uint8_t>(packet[offset++]);
            offset += scid_length;
            auto payload_length = decode_varint(packet.subspan(offset));
            if (!payload_length)
                co_return std::unexpected(payload_length.error());
            offset += payload_length->second;
            if (offset >= packet.size())
                co_return std::unexpected(std::make_error_code(std::errc::bad_message));

            std::vector<std::byte> wire(packet.begin(), packet.end());
            auto pn_length = unprotect_header(*read_keys, wire, offset, true);
            if (!pn_length || offset + *pn_length > wire.size())
                co_return {};
            std::uint64_t truncated_packet_number{};
            for (std::size_t index = 0; index < *pn_length; ++index)
                truncated_packet_number = (truncated_packet_number << 8) |
                    std::to_integer<std::uint8_t>(wire[offset + index]);
            impl_->receiving_application_path_id = 0U;
            auto& application_path = impl_->receiving_application_path();
            const auto packet_number = packet_number_decode(
                static_cast<std::uint32_t>(truncated_packet_number),
                static_cast<std::uint32_t>(*pn_length * 8),
                application_path.largest_received_packet_number.value_or(0));
            auto plaintext = open_payload(*read_keys,
                std::span<const std::byte>{wire}.subspan(offset + *pn_length),
                std::span<const std::byte>{wire}.first(offset + *pn_length), packet_number,
                application_path.id);
            if (!plaintext)
                co_return {};

            auto& largest = application_path.largest_received_packet_number;
            if (!largest || packet_number > *largest)
                largest = packet_number;
            impl_->receiving_level = encryption_level::early_data;
            bool ack_eliciting = false;
            for (std::size_t frame_offset = 0; frame_offset < plaintext->size();)
            {
                auto frame = decode_frame(std::span<const std::byte>{*plaintext}.subspan(frame_offset));
                if (!frame || frame->second == 0)
                    co_return {};
                // 0-RTT permits application frames but not ACK/CRYPTO or
                // connection-management frames.  Restricting this path to
                // STREAM/RESET/STOP keeps pre-handshake state bounded.
                if (!std::holds_alternative<stream_frame>(frame->first) &&
                    !std::holds_alternative<reset_stream_frame>(frame->first) &&
                    !std::holds_alternative<stop_sending_frame>(frame->first))
                    co_return {};
                ack_eliciting = ack_eliciting || is_ack_eliciting(frame->first);
                co_await process_frames(frame->first);
                frame_offset += frame->second;
            }
            if (ack_eliciting)
                record_ack_eliciting_packet(
                    application_path.received_ack_eliciting_packet_numbers, packet_number);
        }
        co_return co_await handle_long_header_packet(*header);
    }

    const auto cid_size = impl_->local_connection_id
        ? impl_->local_connection_id->size()
        : impl_->config.cid_length;
    auto header = decode_short_header(packet, cid_size);
    if (!header)
        co_return std::unexpected(header.error());
    const auto cid_path = impl_->local_cid_path_ids.find(header->dcid);
    if (cid_path == impl_->local_cid_path_ids.end() ||
        !impl_->application_paths.contains(cid_path->second))
    {
        // An in-flight datagram can target a CID that was retired between the
        // listener route lookup and this connection's serialized processing.
        // It is unrelated UDP input, not a connection-fatal parse error.
        co_return {};
    }
    const auto read_candidates = impl_->tls->application_read_key_candidates();
    if (read_candidates.empty())
    {
        // UDP reordering can deliver a short-header 0-RTT/1-RTT packet while
        // an early-data rejection is resetting the TLS application-key
        // lifecycle. It is unauthenticated at this point, so it must be
        // discarded rather than turning a replay-safe fallback into a fatal
        // connection error. A valid peer retransmission after keys are ready
        // will be processed normally.
        co_return {};
    }
    // The short-header destination CID selects the Path ID before truncated
    // packet-number reconstruction and AEAD authentication.
    impl_->receiving_application_path_id = cid_path->second;
    auto& application_path = impl_->receiving_application_path();
    const auto pn_offset = 1 + cid_size;
    std::optional<std::vector<std::byte>> plaintext;
    std::uint64_t packet_number{};
    application_read_key_kind accepted_key_kind = application_read_key_kind::current;
    for (const auto& candidate : read_candidates)
    {
        std::vector<std::byte> wire(packet.begin(), packet.end());
        auto pn_length = unprotect_header(*candidate.keys, wire, pn_offset, false);
        if (!pn_length || pn_offset + *pn_length > wire.size())
            continue;
        const bool wire_key_phase = (std::to_integer<std::uint8_t>(wire.front()) & 0x04U) != 0;
        if (wire_key_phase != candidate.key_phase)
            continue;
        std::uint64_t truncated_packet_number{};
        for (std::size_t i = 0; i < *pn_length; ++i)
            truncated_packet_number = (truncated_packet_number << 8) |
                std::to_integer<std::uint8_t>(wire[pn_offset + i]);
        const auto candidate_packet_number = packet_number_decode(
            static_cast<std::uint32_t>(truncated_packet_number),
            static_cast<std::uint32_t>(*pn_length * 8),
            application_path.largest_received_packet_number.value_or(0));
        auto candidate_plaintext = open_payload(*candidate.keys,
            std::span<const std::byte>{wire}.subspan(pn_offset + *pn_length),
            std::span<const std::byte>{wire}.first(pn_offset + *pn_length), candidate_packet_number,
            application_path.id);
        if (!candidate_plaintext)
            continue;
        plaintext = std::move(*candidate_plaintext);
        packet_number = candidate_packet_number;
        accepted_key_kind = candidate.kind;
        break;
    }
    // QUIC packet-protection failure is a datagram-level discard, not a
    // connection error (RFC 9001 5.4.1).  A peer may legitimately have sent a
    // packet from a newer/older key phase while the receive key candidates are
    // being advanced, and a corrupted UDP datagram must not tear down the
    // connection either.  Keep receiving so a retransmission can make
    // progress; authenticated frame/protocol errors are still handled below.
    if (!plaintext)
    {

        co_return {};
    }
    impl_->tls->confirm_application_read_key(accepted_key_kind);
    impl_->tls->discard_expired_application_read_keys(
        std::chrono::steady_clock::now(), application_path.recovery.pto_duration() * 3);
    auto& largest = application_path.largest_received_packet_number;
    if (!largest || packet_number > *largest)
        largest = packet_number;
    application_path.bytes_received += packet.size();
    // A non-zero DCID proves which multipath state owns this packet, but it
    // does not validate the new 4-tuple.  Retain the candidate endpoint only
    // for PATH_RESPONSE/PATH_CHALLENGE traffic; the scheduler keeps data off
    // this path until async_probe_path (or the peer validation response)
    // marks it validated.
    if (application_path.id != 0U &&
        sender.to_string() != application_path.peer.to_string())
    {
        application_path.peer = sender;
        application_path.validated = false;
    }
    // A server is allowed to receive a client-initiated path before it has
    // validated the client's tuple.  Replying to the client's challenge is
    // not sufficient to validate the reverse direction: issue an independent
    // challenge on the same Path ID so server response data can subsequently
    // be scheduled there only after the reverse validation boundary holds.
    if (impl_->connection_role == quic_role::server && application_path.id != 0U &&
        !application_path.validated)
    {
        const auto key = sender.to_string();
        if (!impl_->pending_path_validations.contains(key) &&
            impl_->pending_path_validations.size() < std::max<std::size_t>(
                                                         1U, impl_->config.max_pending_path_validations))
        {
            std::array<std::byte, 8> challenge{};
            if (RAND_bytes(reinterpret_cast<unsigned char*>(challenge.data()),
                    static_cast<int>(challenge.size())) == 1)
            {
                impl_->pending_path_validations.emplace(key,
                    quic_connection_impl::path_validation{sender, challenge,
                        std::chrono::steady_clock::now(), application_path.id});
                const auto previous_path = impl_->sending_application_path_id;
                impl_->sending_application_path_id = application_path.id;
                auto probe = pack_path_validation_packet(
                    encode_frame(path_challenge_frame{challenge}));
                impl_->sending_application_path_id = previous_path;
                if (!probe.empty())
                    (void)co_await send_datagram(probe, sender);
            }
        }
    }
    impl_->receiving_level = encryption_level::application;
    impl_->current_packet_sender = sender;
    bool ack_eliciting = false;
    for (std::size_t frame_offset = 0; frame_offset < plaintext->size();)
    {
        while (frame_offset < plaintext->size() &&
            (*plaintext)[frame_offset] == std::byte{})
            ++frame_offset;
        if (frame_offset == plaintext->size())
            break;
        auto frame = decode_frame(std::span<const std::byte>{*plaintext}.subspan(frame_offset));
        if (!frame || frame->second == 0)
            co_return std::unexpected(frame ? std::make_error_code(std::errc::bad_message)
                                            : frame.error());
        ack_eliciting = ack_eliciting || is_ack_eliciting(frame->first);
        if (const auto* acknowledgment = std::get_if<ack_frame>(&frame->first))
            (void)process_ack_frame_state(*acknowledgment);
        else if (const auto* stream = std::get_if<stream_frame>(&frame->first))
            process_stream_frame_state(*stream);
        else
            co_await process_frames(frame->first);

        frame_offset += frame->second;
    }
    if (ack_eliciting)
        record_ack_eliciting_packet(
            application_path.received_ack_eliciting_packet_numbers, packet_number);
    auto post_handshake = impl_->tls->process_post_handshake();
    if (!post_handshake)
        co_return std::unexpected(post_handshake.error());
    const auto now = std::chrono::steady_clock::now();
    const auto validation_timeout = impl_->path_validation_timeout(application_path);
    std::erase_if(impl_->pending_path_validations,
        [now, validation_timeout](const auto& candidate)
        {
            return now - candidate.second.started >= validation_timeout;
        });
    if (impl_->tls->is_handshake_complete() &&
        !impl_->tls->received_transport_params().disable_active_migration &&
        sender.to_string() != impl_->peer.to_string())
    {
        const auto key = sender.to_string();
        if (!impl_->pending_path_validations.contains(key) &&
            impl_->pending_path_validations.size() < std::max<std::size_t>(
                                                         1U, impl_->config.max_pending_path_validations))
        {
            std::array<std::byte, 8> challenge{};
            if (RAND_bytes(reinterpret_cast<unsigned char*>(challenge.data()),
                    static_cast<int>(challenge.size())) == 1)
            {
                impl_->pending_path_validations.emplace(key,
                    quic_connection_impl::path_validation{sender, challenge, now, 0U});
                const auto encoded = encode_frame(path_challenge_frame{challenge});
                impl_->sending_application_path_id = 0U;
                auto probe = pack_path_validation_packet(encoded);
                if (!probe.empty())
                    (void)co_await send_datagram(probe, sender);
            }
        }
    }
    impl_->current_packet_sender.reset();
    co_return co_await handle_short_header_packet(*header);
}

auto quic_connection::handle_long_header_packet(long_header header)
    -> task<std::expected<void, std::error_code>>
{
    if (header.type == packet_type::version_negotiation)
        co_return std::unexpected(make_error_code(quic_errc::protocol_violation));
    if (impl_->tls->is_handshake_complete() &&
        (impl_->connection_state == connection_state::idle ||
            impl_->connection_state == connection_state::handshaking ||
            impl_->connection_state == connection_state::connected))
    {
        auto settled = impl_->settle_early_data_outcome();
        if (!settled)
            co_return std::unexpected(settled.error());
        auto parameters_valid = validate_peer_transport_parameters();
        if (!parameters_valid)
            co_return std::unexpected(parameters_valid.error());
        if (!impl_->peer_transport_parameters_applied)
        {
            const auto& params = impl_->tls->received_transport_params();
            impl_->apply_initial_peer_send_limits();
            if (impl_->config.multipath_initial_max_path_id && params.initial_max_path_id)
            {
                impl_->multipath_negotiated = true;
                impl_->peer_max_path_id = *params.initial_max_path_id;
                impl_->application_paths.at(0U)->peer_connection_id =
                    impl_->peer_connection_id;
            }
            impl_->peer_transport_parameters_applied = true;
            auto issued = issue_parallel_local_connection_ids();
            if (!issued)
            {
                // Parameter application is a one-time transition. A failed
                // CID setup must not let a later packet skip confirmation.
                impl_->queue_protocol_close(quic_errc::internal_error, frame_type::crypto);
                co_return std::unexpected(issued.error());
            }
            if (impl_->connection_role == quic_role::server)
            {
                // RFC 9001 section 4.1.2: TLS completion confirms the server's
                // handshake. The client needs this reliable 1-RTT frame to
                // confirm its handshake and enable application-data PTO.
                impl_->encoded_send_frames.push_back(encode_frame(handshake_done_frame{}));
                impl_->send_flush_requested.store(true, std::memory_order_release);
                impl_->handshake_confirmed = true;
                impl_->tls->discard_keys(encryption_level::handshake);
                impl_->retransmit_crypto_frames[level_index(encryption_level::handshake)].clear();
                impl_->recovery.discard_packet_number_space(pn_space::handshake);
            }
        }
        impl_->connection_state = connection_state::connected;
        impl_->arm_path_mtu_discovery(std::chrono::steady_clock::now());
        if (impl_->tls->early_data_status() == early_data_state::accepted)
            impl_->early_data_streams.clear();
    }
    co_return {};
}

auto quic_connection::handle_short_header_packet(short_header)
    -> task<std::expected<void, std::error_code>>
{
    if (impl_->tls->is_handshake_complete() &&
        (impl_->connection_state == connection_state::idle ||
            impl_->connection_state == connection_state::handshaking ||
            impl_->connection_state == connection_state::connected))
    {
        auto settled = impl_->settle_early_data_outcome();
        if (!settled)
            co_return std::unexpected(settled.error());
        auto parameters_valid = validate_peer_transport_parameters();
        if (!parameters_valid)
            co_return std::unexpected(parameters_valid.error());
        if (!impl_->peer_transport_parameters_applied)
        {
            const auto& params = impl_->tls->received_transport_params();
            impl_->apply_initial_peer_send_limits();
            if (impl_->config.multipath_initial_max_path_id && params.initial_max_path_id)
            {
                impl_->multipath_negotiated = true;
                impl_->peer_max_path_id = *params.initial_max_path_id;
                impl_->application_paths.at(0U)->peer_connection_id =
                    impl_->peer_connection_id;
            }
            impl_->peer_transport_parameters_applied = true;
            auto issued = issue_parallel_local_connection_ids();
            if (!issued)
            {
                // Parameter application is a one-time transition. A failed
                // CID setup must not let a later packet skip confirmation.
                impl_->queue_protocol_close(quic_errc::internal_error, frame_type::crypto);
                co_return std::unexpected(issued.error());
            }
            if (impl_->connection_role == quic_role::server)
            {
                // RFC 9001 section 4.1.2: TLS completion confirms the server's
                // handshake. The client needs this reliable 1-RTT frame to
                // confirm its handshake and enable application-data PTO.
                impl_->encoded_send_frames.push_back(encode_frame(handshake_done_frame{}));
                impl_->send_flush_requested.store(true, std::memory_order_release);
                impl_->handshake_confirmed = true;
                impl_->tls->discard_keys(encryption_level::handshake);
                impl_->retransmit_crypto_frames[level_index(encryption_level::handshake)].clear();
                impl_->recovery.discard_packet_number_space(pn_space::handshake);
            }
        }
        impl_->connection_state = connection_state::connected;
        impl_->arm_path_mtu_discovery(std::chrono::steady_clock::now());
        if (impl_->tls->early_data_status() == early_data_state::accepted)
            impl_->early_data_streams.clear();
    }
    co_return {};
}

auto quic_connection::validate_peer_transport_parameters()
    -> std::expected<void, std::error_code>
{
    if (impl_->connection_role != quic_role::client || impl_->peer_transport_parameters_applied)
        return {};
    const auto& parameters = impl_->tls->received_transport_params();
    // RFC 9000 §7.3: every server supplies the CID it selected as its Initial
    // source CID.  It binds the authenticated TLS parameters to the long
    // header observed by the client.
    if (!impl_->peer_connection_id || !parameters.initial_source_connection_id ||
        *parameters.initial_source_connection_id != *impl_->peer_connection_id)
        return std::unexpected(make_error_code(quic_errc::transport_parameter_error));
    if (impl_->retry_received &&
        (!impl_->original_destination_id || !impl_->initial_destination_id ||
            !parameters.original_destination_connection_id ||
            !parameters.retry_source_connection_id ||
            *parameters.original_destination_connection_id != *impl_->original_destination_id ||
            *parameters.retry_source_connection_id != *impl_->initial_destination_id))
        return std::unexpected(make_error_code(quic_errc::transport_parameter_error));
    return {};
}

auto quic_connection::issue_parallel_local_connection_ids()
    -> std::expected<void, std::error_code>
{
    if (!impl_->peer_transport_parameters_applied || !impl_->local_connection_id)
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));

    const auto peer_limit = impl_->tls->received_transport_params().active_connection_id_limit;
    const auto target = std::min(impl_->config.active_connection_id_limit, peer_limit);
    if (target < 2U || impl_->config.cid_length == 0U ||
        impl_->config.cid_length > max_cid_length)
        return std::unexpected(make_error_code(quic_errc::transport_parameter_error));

    // In single-path QUIC, retain the usual pool of path-zero CIDs.  In
    // multipath each additional Path ID needs a CID of its own, and the
    // active-CID limit is connection-wide: spending the complete allowance
    // on path zero first would make opening another path impossible.
    while (!impl_->multipath_negotiated && impl_->local_connection_ids.size() < target)
    {
        std::array<std::byte, max_cid_length> cid_bytes{};
        std::array<std::byte, 16> reset_token{};
        if (RAND_bytes(reinterpret_cast<unsigned char*>(cid_bytes.data()), impl_->config.cid_length) != 1)
            return std::unexpected(std::make_error_code(std::errc::io_error));

        connection_id cid{cid_bytes.data(), impl_->config.cid_length};
        if (impl_->cids.contains(cid))
            continue;
        auto generated = make_stateless_reset_token(impl_->config, cid);
        if (!generated)
            return std::unexpected(generated.error());
        reset_token = *generated;
        const auto sequence = impl_->next_local_cid_sequence++;
        impl_->cids.emplace(cid, this);
        impl_->local_cid_path_ids.emplace(cid, 0U);
        impl_->local_connection_ids.emplace(sequence,
            quic_connection_impl::local_cid_info{cid, reset_token});
        impl_->local_cid_route_generation.fetch_add(1U, std::memory_order_release);
        impl_->encoded_send_frames.push_back(encode_frame(new_connection_id_frame{
            sequence, 0U, cid, reset_token}));
    }

    if (!impl_->multipath_negotiated)
        return {};

    const auto path_limit = std::min(*impl_->config.multipath_initial_max_path_id,
        impl_->peer_max_path_id);
    const auto available_path_slots = target > impl_->local_connection_ids.size()
        ? target - impl_->local_connection_ids.size()
        : 0U;
    const auto advertised_path_count = std::min<std::uint64_t>(path_limit,
        available_path_slots);
    for (std::uint32_t path_id = 1U; path_id <= advertised_path_count; ++path_id)
    {
        if (impl_->retired_path_ids.contains(path_id) ||
            impl_->local_path_connection_ids.contains(path_id))
            continue;
        std::array<std::byte, max_cid_length> cid_bytes{};
        if (RAND_bytes(reinterpret_cast<unsigned char*>(cid_bytes.data()),
                impl_->config.cid_length) != 1)
            return std::unexpected(std::make_error_code(std::errc::io_error));
        connection_id cid{cid_bytes.data(), impl_->config.cid_length};
        if (impl_->cids.contains(cid))
            continue;
        auto reset_token = make_stateless_reset_token(impl_->config, cid);
        if (!reset_token)
            return std::unexpected(reset_token.error());

        constexpr std::uint64_t sequence = 0U;
        impl_->cids.emplace(cid, this);
        impl_->local_cid_path_ids.emplace(cid, path_id);
        impl_->local_path_connection_ids[path_id].emplace(sequence,
            quic_connection_impl::local_cid_info{cid, *reset_token, path_id});
        // A locally advertised Path ID must have receive state before the
        // peer can use its CID. Otherwise a perfectly valid first
        // PATH_CHALLENGE is discarded by short-header demultiplexing while we
        // are still waiting for the peer's matching PATH_NEW_CONNECTION_ID.
        auto [path, inserted] = impl_->application_paths.try_emplace(path_id,
            std::make_unique<quic_connection_impl::application_path_state>(
                path_id, impl_->peer, impl_->config));
        path->second->local_connection_id = cid;
        impl_->local_cid_route_generation.fetch_add(1U, std::memory_order_release);
        impl_->encoded_send_frames.push_back(encode_frame(path_new_connection_id_frame{
            path_id, new_connection_id_frame{sequence, 0U, cid, *reset_token}}));
    }
    return {};
}

auto quic_connection::process_frames(const quic_frame_variant& frame) -> task<void>
{
    auto dispatch = std::visit([this](const auto& value) -> task<void>
        {
            using frame_t = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<frame_t, ack_frame>)
                co_await process_ack_frame(value);
            else if constexpr (std::is_same_v<frame_t, stream_frame>)
                co_await process_stream_frame(value);
            else if constexpr (std::is_same_v<frame_t, reset_stream_frame>)
                co_await process_reset_stream_frame(value);
            else if constexpr (std::is_same_v<frame_t, stop_sending_frame>)
                co_await process_stop_sending_frame(value);
            else if constexpr (std::is_same_v<frame_t, crypto_frame>)
                co_await process_crypto_frame(value);
            else if constexpr (std::is_same_v<frame_t, connection_close_frame>)
                co_await process_connection_close_frame(value);
            else if constexpr (std::is_same_v<frame_t, ping_frame>)
                co_await process_ping_frame(value);
            else if constexpr (std::is_same_v<frame_t, handshake_done_frame>)
            {
                // TLS completion alone does not confirm a client's handshake.
                // Duplicate HANDSHAKE_DONE frames are valid retransmissions.
                if (impl_->connection_role != quic_role::client ||
                    impl_->receiving_level != encryption_level::application ||
                    !impl_->tls->is_handshake_complete())
                {
                    impl_->queue_protocol_close(quic_errc::protocol_violation,
                        frame_type::handshake_done);
                }
                else if (!impl_->handshake_confirmed)
                {
                    impl_->handshake_confirmed = true;
                    impl_->tls->discard_keys(encryption_level::handshake);
                    impl_->retransmit_crypto_frames[level_index(encryption_level::handshake)].clear();
                    impl_->recovery.discard_packet_number_space(pn_space::handshake);
                }
            }
            else if constexpr (std::is_same_v<frame_t, path_challenge_frame>)
                co_await process_path_challenge_frame(value);
            else if constexpr (std::is_same_v<frame_t, path_response_frame>)
                co_await process_path_response_frame(value);
            else if constexpr (std::is_same_v<frame_t, new_connection_id_frame>)
                co_await process_new_connection_id_frame(value);
            else if constexpr (std::is_same_v<frame_t, retire_connection_id_frame>)
                co_await process_retire_connection_id_frame(value);
            else if constexpr (std::is_same_v<frame_t, datagram_frame>)
                co_await process_datagram_frame(value);
            else if constexpr (std::is_same_v<frame_t, path_ack_frame> ||
                std::is_same_v<frame_t, path_abandon_frame> ||
                std::is_same_v<frame_t, path_status_frame> ||
                std::is_same_v<frame_t, path_new_connection_id_frame> ||
                std::is_same_v<frame_t, path_retire_connection_id_frame> ||
                std::is_same_v<frame_t, max_path_id_frame> ||
                std::is_same_v<frame_t, paths_blocked_frame> ||
                std::is_same_v<frame_t, path_cids_blocked_frame>)
            {
                // draft-ietf-quic-multipath-12 requires both peers to
                // advertise initial_max_path_id before accepting any of its
                // frames. The connection deliberately does not advertise it
                // until independent per-path state is active, therefore a
                // received draft frame is a protocol violation rather than
                // an ignorable unknown extension.
                if (!impl_->multipath_negotiated ||
                    impl_->receiving_level != encryption_level::application)
                {
                    impl_->connection_state = connection_state::closing;
                    co_return;
                }
                if constexpr (std::is_same_v<frame_t, path_ack_frame>)
                {
                    const auto path = impl_->application_paths.find(value.path_id);
                    if (path == impl_->application_paths.end())
                    {
                        // PATH_ACK can arrive after the three-PTO abandonment
                        // grace period. The draft requires it to be silently
                        // discarded once the path state is gone.
                        co_return;
                    }
                    const auto previous_path = impl_->receiving_application_path_id;
                    impl_->receiving_application_path_id = value.path_id;
                    co_await process_ack_frame(value.acknowledgment);
                    impl_->receiving_application_path_id = previous_path;
                }
                else if constexpr (std::is_same_v<frame_t, path_new_connection_id_frame>)
                {
                    const auto local_limit = *impl_->config.multipath_initial_max_path_id;
                    if (value.path_id == 0U || value.path_id > local_limit ||
                        value.connection_id.cid.empty() ||
                        value.connection_id.retire_prior_to > value.connection_id.sequence_number)
                    {
                        impl_->connection_state = connection_state::closing;
                        co_return;
                    }
                    if (impl_->retired_path_ids.contains(value.path_id))
                    {
                        impl_->encoded_send_frames.push_back(encode_frame(
                            path_retire_connection_id_frame{value.path_id,
                                value.connection_id.sequence_number}));
                        co_return;
                    }
                    auto [path, inserted] = impl_->application_paths.try_emplace(value.path_id,
                        std::make_unique<quic_connection_impl::application_path_state>(
                            value.path_id, impl_->peer, impl_->config));
                    auto& state = *path->second;
                    // PATH_ABANDON can legitimately overtake this frame.
                    // The CID is then immediately retired and must not
                    // resurrect an already consumed Path ID.
                    if (state.locally_abandoned || state.peer_abandoned)
                    {
                        impl_->encoded_send_frames.push_back(encode_frame(
                            path_retire_connection_id_frame{value.path_id,
                                value.connection_id.sequence_number}));
                        co_return;
                    }
                    if (value.connection_id.retire_prior_to < state.peer_retire_prior_to)
                    {
                        impl_->connection_state = connection_state::closing;
                        co_return;
                    }
                    auto& cids = impl_->peer_path_connection_ids[value.path_id];
                    const auto [it, cid_inserted] = cids.emplace(
                        value.connection_id.sequence_number,
                        quic_connection_impl::peer_cid_info{value.connection_id.cid,
                            value.connection_id.stateless_reset_token, value.path_id});
                    if (!cid_inserted && (it->second.cid != value.connection_id.cid || it->second.stateless_reset_token != value.connection_id.stateless_reset_token))
                    {
                        impl_->connection_state = connection_state::closing;
                        co_return;
                    }
                    for (auto cid = cids.begin(); cid != cids.end() &&
                        cid->first < value.connection_id.retire_prior_to;)
                    {
                        impl_->encoded_send_frames.push_back(encode_frame(
                            path_retire_connection_id_frame{value.path_id, cid->first}));
                        const auto active = state.active_peer_cid_sequence &&
                            *state.active_peer_cid_sequence == cid->first;
                        cid = cids.erase(cid);
                        if (active)
                        {
                            state.active_peer_cid_sequence.reset();
                            state.peer_connection_id.reset();
                        }
                    }
                    state.peer_retire_prior_to = value.connection_id.retire_prior_to;
                    if (!state.peer_connection_id)
                    {
                        const auto active = cids.lower_bound(state.peer_retire_prior_to);
                        if (active != cids.end())
                        {
                            state.peer_connection_id = active->second.cid;
                            state.active_peer_cid_sequence = active->first;
                        }
                    }
                }
                else if constexpr (std::is_same_v<frame_t, path_status_frame>)
                {
                    const auto path = impl_->application_paths.find(value.path_id);
                    if (path == impl_->application_paths.end() ||
                        value.sequence_number < path->second->last_path_status_sequence)
                        co_return;
                    path->second->last_path_status_sequence = value.sequence_number;
                    path->second->peer_marks_backup = value.backup;
                }
                else if constexpr (std::is_same_v<frame_t, path_abandon_frame>)
                {
                    const auto local_limit = *impl_->config.multipath_initial_max_path_id;
                    if (value.path_id > local_limit)
                    {
                        impl_->connection_state = connection_state::closing;
                        co_return;
                    }
                    impl_->retired_path_ids.insert(value.path_id);
                    // draft-ietf-quic-multipath-12 section 3.3.4 permits a
                    // peer to close an unused Path ID. Materialize a
                    // tombstone so delayed CID/control frames cannot reopen
                    // it, then acknowledge the close on a surviving path.
                    auto [path, inserted] = impl_->application_paths.try_emplace(value.path_id,
                        std::make_unique<quic_connection_impl::application_path_state>(
                            value.path_id, impl_->peer, impl_->config));
                    auto& state = *path->second;
                    if (!state.locally_abandoned)
                    {
                        state.locally_abandoned = true;
                        impl_->encoded_send_frames.push_back(encode_frame(
                            path_abandon_frame{value.path_id, value.error_code}));
                    }
                    state.peer_abandoned = true;
                    state.peer_connection_id.reset();
                    state.active_peer_cid_sequence.reset();
                    impl_->peer_path_connection_ids.erase(value.path_id);
                    state.abandonment_deadline = std::chrono::steady_clock::now() +
                        state.recovery.pto_duration() * 3;
                    const auto live_paths = std::ranges::count_if(impl_->application_paths,
                        [](const auto& entry)
                        {
                            return !entry.second->locally_abandoned &&
                                !entry.second->peer_abandoned;
                        });
                    if (live_paths == 0)
                        impl_->connection_state = connection_state::closing;
                }
                else if constexpr (std::is_same_v<frame_t, max_path_id_frame>)
                {
                    if (value.maximum_path_id < *impl_->config.multipath_initial_max_path_id)
                    {
                        impl_->connection_state = connection_state::closing;
                        co_return;
                    }
                    impl_->peer_max_path_id = std::max(impl_->peer_max_path_id,
                        value.maximum_path_id);
                }
                else if constexpr (std::is_same_v<frame_t, path_retire_connection_id_frame>)
                {
                    const auto paths = impl_->local_path_connection_ids.find(value.path_id);
                    if (value.path_id == 0U || paths == impl_->local_path_connection_ids.end())
                        impl_->connection_state = connection_state::closing;
                    else
                    {
                        const auto retired = paths->second.find(value.sequence_number);
                        if (retired == paths->second.end())
                        {
                            impl_->connection_state = connection_state::closing;
                            co_return;
                        }
                        const auto retired_cid = retired->second.cid;
                        paths->second.erase(retired);
                        impl_->cids.erase(retired_cid);
                        impl_->local_cid_path_ids.erase(retired_cid);
                        impl_->local_cid_route_generation.fetch_add(1U, std::memory_order_release);
                        const auto path = impl_->application_paths.find(value.path_id);
                        if (path != impl_->application_paths.end() &&
                            !path->second->locally_abandoned && !path->second->peer_abandoned)
                        {
                            std::array<std::byte, max_cid_length> bytes{};
                            if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()),
                                    impl_->config.cid_length) != 1)
                            {
                                impl_->connection_state = connection_state::closing;
                                co_return;
                            }
                            connection_id replacement{bytes.data(), impl_->config.cid_length};
                            auto token = make_stateless_reset_token(impl_->config, replacement);
                            if (!token || impl_->cids.contains(replacement))
                            {
                                impl_->connection_state = connection_state::closing;
                                co_return;
                            }
                            const auto sequence = path->second->next_local_cid_sequence++;
                            paths->second.emplace(sequence,
                                quic_connection_impl::local_cid_info{replacement, *token, value.path_id});
                            impl_->cids.emplace(replacement, this);
                            impl_->local_cid_path_ids.emplace(replacement, value.path_id);
                            impl_->encoded_send_frames.push_back(encode_frame(
                                path_new_connection_id_frame{value.path_id,
                                    new_connection_id_frame{sequence, 0U, replacement, *token}}));
                        }
                    }
                }
                else if constexpr (std::is_same_v<frame_t, paths_blocked_frame> ||
                    std::is_same_v<frame_t, path_cids_blocked_frame>)
                {
                    // Informational frames. Their bounds are nevertheless
                    // authenticated and checked to prevent path-state abuse.
                    const auto path_id = []<typename T>(const T& item)
                    {
                        if constexpr (std::is_same_v<T, paths_blocked_frame>)
                            return item.maximum_path_id;
                        else
                            return item.path_id;
                    }(value);
                    if (path_id > *impl_->config.multipath_initial_max_path_id)
                        impl_->connection_state = connection_state::closing;
                }
            }
            else if constexpr (std::is_same_v<frame_t, max_data_frame>)
            {
                impl_->peer_max_data_from_frames = std::max(
                    impl_->peer_max_data_from_frames, value.maximum);
                impl_->peer_max_data = std::max(impl_->peer_max_data, value.maximum);
            }
            else if constexpr (std::is_same_v<frame_t, max_stream_data_frame>)
            {
                const auto stream = impl_->streams.find(value.stream_id);
                if (stream != impl_->streams.end())
                    stream->second.value->update_send_limit(value.maximum);
            }
            else if constexpr (std::is_same_v<frame_t, max_streams_frame>)
            {
                auto& limit = value.bidirectional ? impl_->peer_max_streams_bidi
                                                  : impl_->peer_max_streams_uni;
                auto& from_frames = value.bidirectional
                    ? impl_->peer_max_streams_bidi_from_frames
                    : impl_->peer_max_streams_uni_from_frames;
                from_frames = std::max(from_frames, value.maximum);
                limit = std::max(limit, value.maximum);
                if (impl_->has_deferred_stream_cancellations)
                    impl_->send_flush_requested.store(true, std::memory_order_release);
            }
            else if constexpr (std::is_same_v<frame_t, streams_blocked_frame>)
            {
                const auto limit = value.bidirectional
                    ? impl_->local_max_streams_bidi
                    : impl_->local_max_streams_uni;

                // STREAMS_BLOCKED is commonly sent after the MAX_STREAMS
                // update that replenished credit was lost.  Re-advertise the
                // current absolute limit even when the peer reports an older
                // value; MAX_STREAMS is idempotent and monotonic (RFC 9000
                // sections 4.6 and 19.14).
                impl_->encoded_send_frames.push_back(encode_frame(
                    max_streams_frame{limit, value.bidirectional}));
            }
            co_return;
        },
        frame);
    co_await dispatch;
    co_return;
}

auto quic_connection::process_ack_frame_state(const ack_frame& frame)
    -> encryption_level
{
    const auto level = impl_->receiving_level;
    const auto space = packet_number_space_for(level);
    const bool path_scoped = space == pn_space::application;
    auto& application_path = impl_->receiving_application_path();
    auto& recovery = path_scoped ? application_path.recovery : impl_->recovery;
    const auto now = std::chrono::steady_clock::now();
    auto update = recovery.on_ack_received(frame, now, space);
    if (!update)
    {
        impl_->connection_state = connection_state::closing;
        return level;
    }

    if (path_scoped)
    {
        for (const auto target : update->acknowledged_path_mtu_probes)
        {
            if (!application_path.path_mtu_probe_in_flight ||
                *application_path.path_mtu_probe_in_flight != target)
                continue;
            if (target > application_path.discovered_max_datagram_payload)
            {
                application_path.discovered_max_datagram_payload = target;
                application_path.recovery.on_max_datagram_size_validated(target);
            }
            application_path.path_mtu_probe_in_flight.reset();
            if (application_path.discovered_max_datagram_payload <
                impl_->path_mtu_probe_limit())
            {
                application_path.next_path_mtu_probe_at = now +
                    impl_->config.path_mtu_probe_interval;
            }
        }
        for (const auto target : update->lost_path_mtu_probes)
        {
            if (!application_path.path_mtu_probe_in_flight ||
                *application_path.path_mtu_probe_in_flight != target)
                continue;
            application_path.path_mtu_fast_probe = false;
            application_path.path_mtu_probe_in_flight.reset();
            application_path.next_path_mtu_probe_at = now +
                impl_->config.path_mtu_probe_interval;
        }
    }

    // One ACK permits one retry; pending target alone is not a now timer.
    if (path_scoped && application_path.path_mtu_probe_target)
        impl_->send_flush_requested.store(true, std::memory_order_release);

    for (auto& frame_to_retry : update->lost_frames)
    {
        if (path_scoped)
            impl_->requeue_application_frame(std::move(frame_to_retry));
        else
            impl_->retransmit_crypto_frames[level_index(level)].push_front(
                frame_to_retry.release_owned_bytes());
    }
    return level;
}

auto quic_connection::process_ack_frame(const ack_frame& frame) -> task<void>
{
    const auto level = process_ack_frame_state(frame);
    if (level == encryption_level::initial)
        (void)co_await send_initial_packets();
    else if (level == encryption_level::handshake)
        (void)co_await send_handshake_packets();
    co_return;
}

auto quic_connection::process_crypto_frame(const crypto_frame& frame) -> task<void>
{
    // RFC 9000 CRYPTO offsets are byte offsets, not record boundaries.  Keep
    // out-of-order fragments and feed BoringSSL only the contiguous prefix.
    if (frame.data.empty())
        co_return;
    const auto level_index_value = level_index(impl_->receiving_level);
    auto& fragments = impl_->crypto_fragments[level_index_value];
    auto& next_offset = impl_->next_crypto_offset[level_index_value];
    auto [it, inserted] = fragments.try_emplace(
        frame.offset, frame.data.begin(), frame.data.end());
    if (!inserted && it->second.size() < frame.data.size())
        it->second.assign(frame.data.begin(), frame.data.end());
    for (;;)
    {
        const auto contiguous = fragments.find(next_offset);
        if (contiguous == fragments.end())
            break;
        auto provided = impl_->tls->provide_quic_data(impl_->receiving_level,
            contiguous->second);
        if (!provided)
        {
            impl_->connection_state = connection_state::closing;
            co_return;
        }
        next_offset += contiguous->second.size();
        fragments.erase(contiguous);
    }
    co_return;
}

auto quic_connection::process_ping_frame(const ping_frame&) -> task<void>
{
    co_return;
}

auto quic_connection::process_datagram_frame(const datagram_frame& frame) -> task<void>
{
    // RFC 9221: receiving DATAGRAM without advertising support is a transport
    // protocol error. Do not let an extension frame bypass negotiated limits.
    if (impl_->config.max_datagram_frame_size == 0U ||
        frame.data.size() > impl_->config.max_datagram_frame_size)
    {
        impl_->connection_state = connection_state::closing;
        co_return;
    }
    std::vector<std::byte> copy(frame.data.begin(), frame.data.end());
    // The bounded queue is intentionally lossy: applying backpressure here
    // would turn an unreliable transport primitive into a receive-path stall.
    (void)impl_->received_application_datagrams.try_send(std::move(copy));
    co_return;
}

auto quic_connection::process_path_challenge_frame(const path_challenge_frame& frame) -> task<void>
{
    if (!impl_->current_packet_sender)
        co_return;
    const auto encoded = encode_frame(path_response_frame{frame.data});
    const auto previous_path = impl_->sending_application_path_id;
    impl_->sending_application_path_id = impl_->receiving_application_path_id;
    auto* path_socket = impl_->sending_application_path().path_socket;
    auto packet = pack_path_validation_packet(encoded);
    impl_->sending_application_path_id = previous_path;
    if (!packet.empty())
        (void)co_await send_datagram(packet, *impl_->current_packet_sender, path_socket);
    co_return;
}

auto quic_connection::process_path_response_frame(const path_response_frame& frame) -> task<void>
{
    if (impl_->current_packet_sender)
    {
        // A NAT or UDP relay can change the observed peer tuple between the
        // PATH_CHALLENGE submission and its authenticated PATH_RESPONSE. The
        // response is already bound to a Path ID by the destination CID and
        // AEAD nonce, so correlate it with the unpredictable challenge rather
        // than requiring the pre-validation endpoint string to remain stable.
        const auto candidate = std::ranges::find_if(
            impl_->pending_path_validations,
            [this, &frame](const auto& pending)
            {
                return pending.second.path_id == impl_->receiving_application_path_id &&
                    pending.second.challenge == frame.data;
            });
        if (candidate == impl_->pending_path_validations.end())
            co_return;
        const auto path_id = candidate->second.path_id;
        const auto path = impl_->application_paths.find(path_id);
        if (path == impl_->application_paths.end())
            co_return;
        // RFC 9000 §9.5: use a fresh peer-issued CID for the new path where
        // available, and retire the previous peer CID only after validation.
        // The Initial CID has no advertised sequence number and is therefore
        // deliberately left unretired here.
        if (path_id == 0U && !impl_->peer_connection_ids.empty())
        {
            const auto replacement = impl_->peer_connection_ids.lower_bound(
                impl_->peer_retire_prior_to);
            if (replacement != impl_->peer_connection_ids.end())
            {
                if (impl_->active_peer_cid_sequence &&
                    *impl_->active_peer_cid_sequence != replacement->first)
                    impl_->encoded_send_frames.push_back(encode_frame(
                        retire_connection_id_frame{*impl_->active_peer_cid_sequence}));
                impl_->peer_connection_id = replacement->second.cid;
                impl_->application_paths.at(0U)->peer_connection_id =
                    impl_->peer_connection_id;
                impl_->active_peer_cid_sequence = replacement->first;
            }
        }
        path->second->peer = *impl_->current_packet_sender;
        path->second->validated = true;
        impl_->arm_path_mtu_discovery(std::chrono::steady_clock::now());
        if (path_id == 0U)
            impl_->peer = *impl_->current_packet_sender;
        impl_->pending_path_validations.erase(candidate);
    }
    co_return;
}

auto quic_connection::process_new_connection_id_frame(const new_connection_id_frame& frame)
    -> task<void>
{
    // RFC 9000 §5.1.1: every sequence number identifies exactly one CID and
    // reset token; retire_prior_to is monotonic and cannot exceed the frame's
    // own sequence number.
    if (frame.cid.empty() || frame.retire_prior_to > frame.sequence_number ||
        frame.retire_prior_to < impl_->peer_retire_prior_to)
    {
        impl_->connection_state = connection_state::closing;
        co_return;
    }
    if (const auto existing = impl_->peer_connection_ids.find(frame.sequence_number);
        existing != impl_->peer_connection_ids.end())
    {
        if (existing->second.cid != frame.cid ||
            !std::equal(existing->second.stateless_reset_token.begin(),
                existing->second.stateless_reset_token.end(), frame.stateless_reset_token.begin()))
            impl_->connection_state = connection_state::closing;
        co_return;
    }

    impl_->peer_connection_ids.emplace(frame.sequence_number,
        quic_connection_impl::peer_cid_info{frame.cid, frame.stateless_reset_token});
    if (frame.retire_prior_to == impl_->peer_retire_prior_to)
        co_return;

    // The Initial source CID implicitly has sequence number zero but has no
    // reset token on the wire.  It is therefore not necessarily present in
    // peer_connection_ids yet.  A later retire_prior_to retires it as well.
    if (frame.retire_prior_to > 0U && impl_->peer_connection_id)
    {
        const bool active_is_announced = std::ranges::any_of(impl_->peer_connection_ids,
            [this](const auto& entry)
            {
                return entry.second.cid == *impl_->peer_connection_id;
            });
        if (!active_is_announced)
        {
            impl_->encoded_send_frames.push_back(encode_frame(retire_connection_id_frame{0}));
            impl_->peer_connection_id.reset();
            impl_->application_paths.at(0U)->peer_connection_id.reset();
        }
    }

    for (auto it = impl_->peer_connection_ids.begin();
        it != impl_->peer_connection_ids.end() && it->first < frame.retire_prior_to;)
    {
        // Retiring a peer-issued CID is explicit on the wire.  Keep this as a
        // normal retransmittable application frame; the send path records it
        // in packet metadata just like STREAM/control frames.
        impl_->encoded_send_frames.push_back(encode_frame(retire_connection_id_frame{it->first}));
        const bool was_active = impl_->peer_connection_id && it->second.cid == *impl_->peer_connection_id;
        it = impl_->peer_connection_ids.erase(it);
        if (was_active)
            impl_->peer_connection_id.reset();
        impl_->application_paths.at(0U)->peer_connection_id.reset();
    }
    impl_->peer_retire_prior_to = frame.retire_prior_to;
    if (!impl_->peer_connection_id)
    {
        const auto replacement = impl_->peer_connection_ids.lower_bound(frame.retire_prior_to);
        if (replacement == impl_->peer_connection_ids.end())
        {
            impl_->connection_state = connection_state::closing;
            co_return;
        }
        impl_->peer_connection_id = replacement->second.cid;
        impl_->application_paths.at(0U)->peer_connection_id =
            impl_->peer_connection_id;
    }
    co_return;
}

auto quic_connection::process_retire_connection_id_frame(const retire_connection_id_frame& frame)
    -> task<void>
{
    const auto retired = impl_->local_connection_ids.find(frame.sequence_number);
    if (retired == impl_->local_connection_ids.end())
    {
        impl_->connection_state = connection_state::closing;
        co_return;
    }
    const bool was_active = impl_->local_connection_id &&
        retired->second.cid == *impl_->local_connection_id;
    const auto retired_path_id = retired->second.path_id;
    impl_->cids.erase(retired->second.cid);
    impl_->local_cid_path_ids.erase(retired->second.cid);
    impl_->retired_local_connection_ids.push_back(retired->second);
    impl_->local_connection_ids.erase(retired);
    impl_->local_cid_route_generation.fetch_add(1U, std::memory_order_release);

    // RFC 9000 §5.1.2 requires replacement of a retired active CID.  Generate
    // both the routing CID and its stateless-reset token from the CSPRNG.
    if (was_active)
    {
        std::array<std::byte, max_cid_length> cid_bytes{};
        std::array<std::byte, 16> reset_token{};
        const auto cid_length = impl_->config.cid_length;
        if (cid_length == 0U || cid_length > max_cid_length ||
            RAND_bytes(reinterpret_cast<unsigned char*>(cid_bytes.data()), cid_length) != 1)
        {
            impl_->connection_state = connection_state::closing;
            co_return;
        }
        connection_id replacement{cid_bytes.data(), cid_length};
        auto generated = make_stateless_reset_token(impl_->config, replacement);
        if (!generated)
        {
            impl_->connection_state = connection_state::closing;
            co_return;
        }
        reset_token = *generated;
        const auto sequence = impl_->next_local_cid_sequence++;
        impl_->local_connection_ids.emplace(sequence,
            quic_connection_impl::local_cid_info{replacement, reset_token});
        impl_->local_cid_route_generation.fetch_add(1U, std::memory_order_release);
        impl_->cids.emplace(replacement, this);
        impl_->local_cid_path_ids.emplace(replacement, retired_path_id);
        impl_->local_connection_id = replacement;
        impl_->encoded_send_frames.push_back(encode_frame(new_connection_id_frame{
            sequence, 0U, replacement, reset_token}));
    }
    if (impl_->peer_transport_parameters_applied)
    {
        auto issued = issue_parallel_local_connection_ids();
        if (!issued)
            impl_->connection_state = connection_state::closing;
    }
    co_return;
}

auto quic_connection::process_stream_frame(const stream_frame& frame) -> task<void>
{
    process_stream_frame_state(frame);
    co_return;
}

auto quic_connection::process_stream_frame_state(const stream_frame& frame) -> void
{
    const bool locally_initiated = is_client_initiated(frame.stream_id) ==
        (impl_->connection_role == quic_role::client);
    if (locally_initiated && is_unidirectional(frame.stream_id))
    {
        impl_->queue_protocol_close(quic_errc::stream_state_error, frame_type::stream);
        return;
    }
    if (impl_->retired_streams.contains(frame.stream_id))
    {
        const auto sizes = impl_->retired_streams.find_final_sizes(frame.stream_id);
        if (!sizes)
            return;
        const auto final_size = sizes->received_final_size;
        if (frame.offset > final_size || frame.data.size() > final_size - frame.offset ||
            (frame.fin && frame.offset + frame.data.size() != final_size))
            impl_->queue_protocol_close(quic_errc::final_size_error, frame_type::stream);
        return;
    }
    auto [it, inserted] = impl_->streams.try_emplace(frame.stream_id);
    impl_->pending_priority_streams.erase(frame.stream_id);
    if (inserted)
    {
        const auto peer_initiated = is_client_initiated(frame.stream_id) !=
            (impl_->connection_role == quic_role::client);
        const auto peer_limit = is_bidirectional(frame.stream_id)
            ? impl_->local_max_streams_bidi
            : impl_->local_max_streams_uni;
        if (!peer_initiated || frame.stream_id / 4 + 1 > peer_limit)
        {
            impl_->streams.erase(it);
            impl_->queue_protocol_close(!peer_initiated ? quic_errc::stream_state_error
                : quic_errc::stream_limit_error, frame_type::stream);
            return;
        }
        it->second.value = std::make_unique<quic_stream>(frame.stream_id,
            impl_->connection_role, is_bidirectional(frame.stream_id));
        // A peer-opened bidirectional stream uses the peer's bidi_local
        // credit for our replies. The local receive window is independent.
        // Retaining the stream's fallback cap stranded responses larger than
        // 256 KiB even when the authenticated peer advertised more credit.
        it->second.value->set_initial_send_limit(is_bidirectional(frame.stream_id)
                ? impl_->tls->received_transport_params().initial_max_stream_data_bidi_local
                : 0U);
        it->second.value->set_initial_receive_limit(impl_->config.max_stream_data);
        it->second.value->init();
        it->second.readiness = std::make_shared<channel<std::monostate>>(1);
        if (!impl_->accepted_streams.try_send(frame.stream_id))
        {
            impl_->streams.erase(it);
            impl_->queue_protocol_close(quic_errc::internal_error, frame_type::stream);
            return;
        }
    }
    const auto highest = it->second.value->bytes_received();
    if (frame.offset > std::numeric_limits<std::uint64_t>::max() - frame.data.size())
    {
        impl_->queue_protocol_close(quic_errc::final_size_error, frame_type::stream);
        return;
    }
    const auto end = frame.offset + frame.data.size();
    if (impl_->received_stream_data > impl_->local_advertised_max_data ||
        (end > highest && end - highest >
            impl_->local_advertised_max_data - impl_->received_stream_data))
    {
        impl_->queue_protocol_close(quic_errc::flow_control_error, frame_type::stream);
        return;
    }
    const auto received_before = it->second.value->bytes_received();
    auto delivered = it->second.value->push_received(frame.offset, frame.data, frame.fin);
    if (!delivered)
    {
        impl_->queue_protocol_close(static_cast<quic_errc>(delivered.error().value()),
            frame_type::stream);
    }
    else
    {
        impl_->received_stream_data +=
            it->second.value->bytes_received() - received_before;
        auto readiness = it->second.readiness;
        const bool abandoned = it->second.value->receive_abandoned();
        if (abandoned && it->second.value->state() == stream_state::closed)
        {
            const auto final_size = it->second.value->bytes_received();
            const auto discarded = final_size - it->second.value->bytes_consumed();
            (void)it->second.value->reset_remote(final_size);
            impl_->return_receive_credit(discarded);
            (void)retire_stream(frame.stream_id);
        }
        (void)readiness->try_send({});
    }
    return;
}

auto quic_connection::process_reset_stream_frame(const reset_stream_frame& frame) -> task<void>
{
    const bool locally_initiated = is_client_initiated(frame.stream_id) ==
        (impl_->connection_role == quic_role::client);
    if (locally_initiated && is_unidirectional(frame.stream_id))
    {
        impl_->queue_protocol_close(quic_errc::stream_state_error, frame_type::reset_stream);
        co_return;
    }
    if (impl_->retired_streams.contains(frame.stream_id))
    {
        if (const auto sizes = impl_->retired_streams.find_final_sizes(frame.stream_id);
            sizes && frame.final_size != sizes->received_final_size)
            impl_->queue_protocol_close(quic_errc::final_size_error, frame_type::reset_stream);
        co_return;
    }
    auto [it, inserted] = impl_->streams.try_emplace(frame.stream_id);
    impl_->pending_priority_streams.erase(frame.stream_id);
    if (inserted)
    {
        const auto limit = is_bidirectional(frame.stream_id)
            ? impl_->local_max_streams_bidi : impl_->local_max_streams_uni;
        if (locally_initiated || frame.stream_id / 4U + 1U > limit)
        {
            impl_->streams.erase(it);
            impl_->queue_protocol_close(locally_initiated ? quic_errc::stream_state_error
                : quic_errc::stream_limit_error, frame_type::reset_stream);
            co_return;
        }
        it->second.value = std::make_unique<quic_stream>(frame.stream_id,
            impl_->connection_role, is_bidirectional(frame.stream_id));
        it->second.value->set_initial_send_limit(is_bidirectional(frame.stream_id)
                ? impl_->tls->received_transport_params().initial_max_stream_data_bidi_local
                : 0U);
        it->second.value->set_initial_receive_limit(impl_->config.max_stream_data);
        it->second.value->init();
        it->second.readiness = std::make_shared<channel<std::monostate>>(1);
    }
    const auto received_before = it->second.value->bytes_received();
    const auto consumed_before = it->second.value->bytes_consumed();
    if (impl_->received_stream_data > impl_->local_advertised_max_data ||
        (frame.final_size > received_before && frame.final_size - received_before >
            impl_->local_advertised_max_data - impl_->received_stream_data))
    {
        impl_->queue_protocol_close(quic_errc::flow_control_error, frame_type::reset_stream);
        co_return;
    }
    const auto reset = it->second.value->reset_remote(frame.final_size);
    if (!reset)
        impl_->queue_protocol_close(static_cast<quic_errc>(reset.error().value()),
            frame_type::reset_stream);
    else
    {
        impl_->received_stream_data += frame.final_size - received_before;
        impl_->return_receive_credit(frame.final_size - consumed_before);
        impl_->remotely_cancelled_streams.insert(frame.stream_id);
        auto readiness = it->second.readiness;
        // Detach the observer before cancellation: its owner can unregister
        // itself as soon as cancel() resumes a pending QPACK wait.
        if (const auto observer = impl_->stream_cancellation_observers.find(frame.stream_id);
            observer != impl_->stream_cancellation_observers.end())
        {
            auto* token = observer->second;
            impl_->stream_cancellation_observers.erase(observer);
            token->cancel();
        }
        if (inserted && !impl_->accepted_streams.try_send(frame.stream_id))
            impl_->queue_protocol_close(quic_errc::internal_error, frame_type::reset_stream);
        (void)readiness->try_send({});
        const auto current = impl_->streams.find(frame.stream_id);
        if (current != impl_->streams.end() && current->second.value->receive_abandoned() &&
            current->second.value->state() == stream_state::closed)
            (void)retire_stream(frame.stream_id);
    }
    co_return;
}

auto quic_connection::process_stop_sending_frame(const stop_sending_frame& frame) -> task<void>
{
    const bool local = is_client_initiated(frame.stream_id) ==
        (impl_->connection_role == quic_role::client);
    if (is_unidirectional(frame.stream_id) && !local)
    {
        impl_->queue_protocol_close(quic_errc::stream_state_error, frame_type::stop_sending);
        co_return;
    }

    if (!impl_->retired_streams.contains(frame.stream_id))
        impl_->remotely_cancelled_streams.insert(frame.stream_id);
    if (const auto observer = impl_->stream_cancellation_observers.find(frame.stream_id);
        observer != impl_->stream_cancellation_observers.end())
    {
        auto* token = observer->second;
        impl_->stream_cancellation_observers.erase(observer);
        token->cancel();
    }
    const auto it = impl_->streams.find(frame.stream_id);
    if (it == impl_->streams.end())
    {
        if (impl_->retired_streams.contains(frame.stream_id))
        {
            if (const auto sizes = impl_->retired_streams.find_final_sizes(frame.stream_id))
                impl_->encoded_send_frames.push_back(encode_frame(reset_stream_frame{
                    frame.stream_id, frame.application_error_code,
                    sizes->sent_final_size}));
        }
        else
            impl_->queue_protocol_close(quic_errc::stream_state_error, frame_type::stop_sending);
        co_return;
    }
    if (it->second.has_deferred_reset)
        co_return; // The count-gated writer already owns this cancellation intent.
    if (impl_->can_defer_local_zero_send(frame.stream_id, it->second))
    {
        // STOP_SENDING cancels only our send direction. Keep the receive side
        // open and defer RESET0 until this local SID is actually authorized.
        it->second.value->stop_local();
        it->second.deferred_reset_error = frame.application_error_code;
        it->second.has_deferred_reset = true;
        it->second.deferred_reset_stop_peer = false;
        impl_->has_deferred_stream_cancellations = true;
        impl_->send_flush_requested.store(true, std::memory_order_release);
        (void)it->second.readiness->try_send({});
        co_return;
    }
    // The peer no longer accepts this send direction.  Stop queuing new
    // STREAM data and acknowledge the cancellation with RESET_STREAM.
    const auto final_size = it->second.value->bytes_sent();
    it->second.value->stop_local();
    impl_->encoded_send_frames.push_back(encode_frame(reset_stream_frame{
        frame.stream_id, frame.application_error_code, final_size}));
    co_return;
}

auto quic_connection::process_connection_close_frame(const connection_close_frame&) -> task<void>
{

    impl_->connection_state = connection_state::draining;
    impl_->draining_deadline = std::chrono::steady_clock::now() +
        std::max(impl_->recovery.pto_duration(),
            impl_->sending_application_path().recovery.pto_duration()) *
            3;
    close_transport(std::make_error_code(std::errc::not_connected),
        connection_state::draining);
    co_return;
}

auto quic_connection::handle_pto()
    -> task<std::expected<void, std::error_code>>
{
    struct pending_probe
    {
        std::vector<std::byte> packet;
        endpoint peer;
        udp::udp_socket* path_socket{};
    };

    std::vector<pending_probe> probes;
    // Build probe packets in the connection's serial state domain, but send
    // them after releasing it. Awaiting UDP writability here previously let a
    // timer stall the server listener's receive path.
    co_await impl_->receive_mutex.lock();
    quic_connection_impl::listener_state_guard receive_guard{*this, *impl_};
    const auto handshake_due = impl_->recovery.next_pto_deadline();
    const auto application_due = impl_->earliest_application_pto();
    const bool application_is_due = application_due && (!handshake_due || std::get<1>(*application_due) < handshake_due->first);
    const auto due = application_is_due
        ? std::optional{std::pair{std::get<1>(*application_due),
              std::get<2>(*application_due)}}
        : handshake_due;
    if (!due || due->first > std::chrono::steady_clock::now())
        co_return std::expected<void, std::error_code>{};

    const auto level = encryption_level_for(due->second);
    const bool path_scoped = level == encryption_level::application;
    if (path_scoped)
        impl_->sending_application_path_id = std::get<0>(*application_due);
    auto& application_path = impl_->sending_application_path();
    auto& recovery = path_scoped ? application_path.recovery : impl_->recovery;
    // This selected application-space deadline is based on the latest
    // ACK-eliciting send, at or after this still-live PMTU probe's send.
    // Cover reliable-data PTOs too; their original probe record stays live.
    if (path_scoped && application_path.path_mtu_probe_in_flight)
        application_path.path_mtu_fast_probe = false;

    // RFC 9002 §6.2.4: PTO sends probes and retains the original packets as
    // in-flight.  Do not erase their metadata here; a later ACK may still
    // acknowledge either original or probe packet.
    // Prefer the newest outstanding retransmittable state.  Flow-control
    // frames carry absolute monotonic limits; probing the oldest MAX_STREAMS
    // value can leave a peer blocked even though a newer limit is already in
    // flight.  Packets whose frames were delivered through another PTO copy
    // remain in the map only for congestion accounting and are skipped.
    const auto probe_frames = recovery.newest_probe_frames(due->second);
    if (!probe_frames)
    {
        // Packets containing only PMTU PING/padding or RFC 9221 DATAGRAM
        // data have no retransmittable application frame. Retire them at
        // PTO instead of leaving the same already-due deadline armed forever;
        // that would spin the connection driver and starve unrelated work on
        // the executor. A failed PMTU probe is retried only after its bounded
        // per-path interval, while unreliable DATAGRAM data is discarded.
        if (path_scoped)
        {
            const auto now = std::chrono::steady_clock::now();
            for (const auto target : recovery.discard_non_retransmittable(
                     pn_space::application))
            {
                if (application_path.path_mtu_probe_in_flight &&
                    *application_path.path_mtu_probe_in_flight == target)
                {
                    application_path.path_mtu_probe_in_flight.reset();
                    application_path.next_path_mtu_probe_at = now +
                        impl_->config.path_mtu_probe_interval;
                }
            }
        }
        co_return std::expected<void, std::error_code>{};
    }
    recovery.on_pto_expired(due->second);

    constexpr std::size_t probe_count = 2;
    for (std::size_t probe = 0; probe < probe_count; ++probe)
    {
        if (level == encryption_level::application)
        {
            probe_frames->for_each_reverse(
                [this](const auto& frame)
                {
                    impl_->encoded_send_frames.push_front(frame);
                });
            // MAX_STREAMS carries absolute monotonic state and a peer is not
            // required to emit STREAMS_BLOCKED when the newest update is
            // lost.  Include the current limits in every application PTO so
            // stream creation cannot deadlock behind a lost credit packet.
            impl_->encoded_send_frames.push_front(encode_frame(max_streams_frame{
                impl_->local_max_streams_uni, false}));
            impl_->encoded_send_frames.push_front(encode_frame(max_streams_frame{
                impl_->local_max_streams_bidi, true}));
            impl_->encoded_send_frames.push_front(encode_frame(ping_frame{}));
            auto packet = pack_one_rtt_packet(true);
            if (!packet)
                co_return std::unexpected(packet.error());
            if (!packet->empty())
                probes.push_back(
                    {std::move(*packet), application_path.peer, application_path.path_socket});
        }
        else
        {
            auto& pending = impl_->retransmit_crypto_frames[level_index(level)];
            probe_frames->for_each_reverse(
                [&pending](const auto& frame)
                {
                    pending.push_front(frame.owned_bytes());
                });
            auto packet = level == encryption_level::initial
                ? pack_initial_packet()
                : pack_handshake_packet();
            if (!packet.empty())
                probes.push_back({std::move(packet), impl_->peer, nullptr});
        }
    }
    receive_guard.unlock_and_notify();
    for (const auto& probe : probes)
    {
        const auto sent = co_await send_datagram(probe.packet, probe.peer, probe.path_socket);
        if (!sent && !probe.path_socket && is_terminal_udp_send_error(sent.error()))
        {
            co_return std::unexpected(sent.error());
        }
    }
    co_return std::expected<void, std::error_code>{};
}

auto quic_connection::pack_and_send_packet()
    -> task<std::expected<void, std::error_code>>
{
    std::vector<std::byte> packet;
    endpoint peer;
    udp::udp_socket* path_socket{};
    std::optional<time_point> timer_deadline;
    {
        co_await impl_->receive_mutex.lock();
        async_lock_guard guard{impl_->receive_mutex, std::adopt_lock};
        // Prefer the ACK's own path while it is usable.  Once that path has
        // been abandoned, draft multipath QUIC requires its PATH_ACK to be
        // carried by another live path; select the normal scheduler path in
        // that case rather than dropping the acknowledgement.
        const auto pending_ack_path = impl_->path_with_pending_ack();
        const auto preferred_control_path = pending_ack_path
            ? pending_ack_path
            : impl_->path_with_pending_mtu_probe();
        const auto preferred_path = preferred_control_path &&
                impl_->path_can_carry_application_packet(*preferred_control_path)
            ? *preferred_control_path
            : impl_->select_application_path_for_send();
        std::vector<std::uint32_t> candidates{preferred_path};
        // A full cwnd on one path must not strand frames that another
        // validated path can send immediately.  `pack_one_rtt_packet()` puts
        // a rejected frame back in its original priority queue, so trying the
        // remaining paths here is lossless and does not require a second
        // producer wakeup.
        for (const auto& [path_id, state] : impl_->application_paths)
        {
            if (path_id == preferred_path || state->locally_abandoned ||
                state->peer_abandoned || !state->validated ||
                !impl_->path_peer_connection_id(*state))
                continue;
            candidates.push_back(path_id);
        }
        for (const auto path_id : candidates)
        {
            impl_->sending_application_path_id = path_id;
            packet = pack_zero_rtt_packet();
            if (packet.empty())
            {
                auto built = pack_one_rtt_packet();
                if (!built)
                    co_return std::unexpected(built.error());
                packet = std::move(*built);
            }
            if (!packet.empty())
            {
                peer = impl_->sending_application_path().peer;
                path_socket = impl_->sending_application_path().path_socket;
                break;
            }
        }
        if (!packet.empty() && impl_->timer_deadline_observer)
            timer_deadline = next_timer_deadline();
    }
    if (timer_deadline)
        impl_->timer_deadline_observer(timer_deadline);
    if (packet.empty())
        co_return std::expected<void, std::error_code>{};
    co_await await_application_pacing(packet.size());
    const auto sent = co_await send_datagram(packet, peer, path_socket);
    if (!sent && !path_socket && is_terminal_udp_send_error(sent.error()))
        co_return std::unexpected(sent.error());
    co_return std::expected<void, std::error_code>{};
}

auto quic_connection::pack_and_send_packets()
    -> task<std::expected<void, std::error_code>>
{
    struct pending_datagram
    {
        std::vector<std::byte> packet;
        endpoint peer;
    };

    std::vector<pending_datagram> pending;
    pending.reserve(quic_connection_impl::max_send_batch_datagrams);
    bool primary_socket_only{};
    std::optional<time_point> timer_deadline;
    {
        co_await impl_->receive_mutex.lock();
        async_lock_guard guard{impl_->receive_mutex, std::adopt_lock};

        // The batch API operates on one socket/context. The normal HTTP/3
        // path has exactly path 0 on the connection socket; Multipath keeps
        // the established packet-at-a-time path, including its per-path
        // scheduler and application-owned socket support.
        if (impl_->application_paths.size() != 1U ||
            !impl_->application_paths.contains(0U))
        {
            primary_socket_only = false;
        }
        else
        {
            primary_socket_only = true;
            impl_->sending_application_path_id = 0U;
            for (std::size_t index{};
                index < quic_connection_impl::max_send_batch_datagrams; ++index)
            {
                auto packet = pack_zero_rtt_packet();
                if (packet.empty())
                {
                    auto built = pack_one_rtt_packet(false,
                        std::move(impl_->application_packet_storage[index]),
                        std::addressof(impl_->application_payload_storage[index]));
                    // No datagram in this batch has been submitted yet. Abort
                    // the entire local prefix before pacing, observers, or UDP;
                    // the caller closes recovery after this lock is released.
                    if (!built)
                        co_return std::unexpected(built.error());
                    packet = std::move(*built);
                }
                if (packet.empty())
                {
                    impl_->application_packet_storage[index] = std::move(packet);
                    break;
                }
                pending.push_back({std::move(packet),
                    impl_->sending_application_path().peer});
            }
        }
        if (!pending.empty() && impl_->timer_deadline_observer)
            timer_deadline = next_timer_deadline();
    }
    if (timer_deadline)
        impl_->timer_deadline_observer(timer_deadline);
    if (!primary_socket_only)
    {
        co_return co_await pack_and_send_packet();
    }
    if (pending.empty())
        co_return std::expected<void, std::error_code>{};

    // The packer has already limited this burst to the path's available
    // congestion window. Reserve its aggregate pacing credit with one timer,
    // then submit the admitted packets together. Sleeping once for the total
    // byte budget preserves the controller's average rate without creating a
    // timer and an io_uring wakeup for every UDP packet.
    const auto paced_bytes = std::accumulate(pending.begin(), pending.end(),
        std::size_t{}, [](std::size_t total, const pending_datagram& item)
        {
            return total + item.packet.size();
        });
    co_await await_application_pacing(paced_bytes);

    std::vector<udp_send_datagram> datagrams;
    datagrams.reserve(pending.size());
    for (const auto& item : pending)
    {
        datagrams.push_back({const_buffer{item.packet.data(), item.packet.size()},
            item.peer});
    }
    // async_sendto_batch preserves partial submission and retries the unsent
    // suffix after readiness. Packet metadata was already marked in-flight by
    // the packer, exactly as on the pre-existing single-send path.
    const auto sent = co_await send_datagram_batch(datagrams);
    if (!sent && is_terminal_udp_send_error(sent.error()))
        co_return std::unexpected(sent.error());
    for (std::size_t index{}; index < pending.size(); ++index)
    {
        pending[index].packet.clear();
        impl_->application_packet_storage[index] =
            std::move(pending[index].packet);
    }
    co_return std::expected<void, std::error_code>{};
}

auto quic_connection::send_initial_packets()
    -> task<std::expected<std::size_t, std::error_code>>
{
    std::size_t packet_count{};
    while (true)
    {
        auto packet = pack_initial_packet();
        if (packet.empty())
            break;
        auto sent = co_await send_datagram(packet, impl_->peer);
        if (!sent)
            co_return std::unexpected(sent.error());
        ++packet_count;
    }
    co_return packet_count;
}

auto quic_connection::send_handshake_packets()
    -> task<std::expected<std::size_t, std::error_code>>
{
    std::size_t packet_count{};
    while (true)
    {
        auto packet = pack_handshake_packet();
        if (packet.empty())
            break;
        auto sent = co_await send_datagram(packet, impl_->peer);
        if (!sent)
            co_return std::unexpected(sent.error());
        ++packet_count;
    }
    co_return packet_count;
}

auto quic_connection::pack_zero_rtt_packet() -> std::vector<std::byte>
{
    if (impl_->connection_role != quic_role::client || !impl_->local_connection_id ||
        !impl_->tls || impl_->early_data_send_frames.empty() ||
        impl_->tls->early_data_status() != early_data_state::pending ||
        impl_->tls->write_keys(encryption_level::application))
        return {};

    const auto* destination_cid = impl_->peer_connection_id
        ? std::addressof(*impl_->peer_connection_id)
        : impl_->initial_destination_id ? std::addressof(*impl_->initial_destination_id)
                                        : nullptr;
    if (!destination_cid)
        return {};

    const auto* keys = impl_->tls->write_keys(encryption_level::early_data);
    if (!keys)
        return {};

    auto& path = impl_->sending_application_path();

    std::vector<std::byte> payload;
    std::vector<std::vector<std::byte>> frames;
    // Reserve the largest CID/length encoding so a Retry with a longer CID
    // never invalidates already-fragmented early STREAM frames.
    const auto overhead = 1U + 4U + 1U + 20U + 1U + 20U + 8U + 4U + keys->tag_len;
    const auto payload_budget = max_udp_payload - overhead;
    while (!impl_->early_data_send_frames.empty() &&
        impl_->early_data_send_frames.front().size() <= payload_budget - payload.size())
    {
        auto frame = std::move(impl_->early_data_send_frames.front());
        impl_->early_data_send_frames.pop_front();
        payload.insert(payload.end(), frame.begin(), frame.end());
        frames.push_back(std::move(frame));
    }
    if (payload.empty())
        return {};
    if (!path.recovery.can_send_datagram(payload.size() + overhead))
    {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            impl_->early_data_send_frames.push_front(std::move(*it));
        return {};
    }


    constexpr std::size_t pn_length = 4;
    const auto packet_number = path.next_send_packet_number++;
    const auto length = encode_varint(pn_length + payload.size() + keys->tag_len);
    if (!length)
        return {};

    std::vector<std::byte> packet;
    packet.reserve(1 + 4 + 2 + destination_cid->size() +
        impl_->local_connection_id->size() + length->second + pn_length +
        payload.size() + keys->tag_len);
    const auto type_bits = impl_->version == quic_version::v2 ? 0x20U : 0x10U;
    packet.push_back(static_cast<std::byte>(0xc0U | type_bits | 0x03U));
    const auto version = static_cast<std::uint32_t>(impl_->version);
    for (int shift = 24; shift >= 0; shift -= 8)
        packet.push_back(static_cast<std::byte>((version >> shift) & 0xffU));
    packet.push_back(static_cast<std::byte>(destination_cid->size()));
    packet.insert(packet.end(), destination_cid->data(),
        destination_cid->data() + destination_cid->size());
    packet.push_back(static_cast<std::byte>(impl_->local_connection_id->size()));
    packet.insert(packet.end(), impl_->local_connection_id->data(),
        impl_->local_connection_id->data() + impl_->local_connection_id->size());
    packet.insert(packet.end(), length->first.begin(), length->first.begin() + length->second);
    for (int shift = 24; shift >= 0; shift -= 8)
        packet.push_back(static_cast<std::byte>((packet_number >> shift) & 0xffU));
    const auto pn_offset = packet.size() - pn_length;
    if (!append_sealed_payload(*keys, payload, packet, packet_number, path.id))
    {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            impl_->early_data_send_frames.push_front(std::move(*it));
        return {};
    }
    if (!protect_header(*keys, packet, pn_offset, true))
    {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            impl_->early_data_send_frames.push_front(std::move(*it));
        return {};
    }
    retransmittable_frame_storage recovery_frames;
    for (auto& frame : frames)
        recovery_frames.push_back(quic_connection_impl::early_frame(std::move(frame)));
    path.recovery.on_packet_sent(packet_number, packet.size(),
        std::chrono::steady_clock::now(), true, pn_space::application,
        std::move(recovery_frames));
    return packet;
}

auto quic_connection::await_application_pacing(std::size_t packet_size) -> task<void>
{
    auto& path = impl_->sending_application_path();
    const auto& peer_address = path.peer.address();
    // Loopback has no physical bottleneck queue to protect with timer-based
    // pacing. Congestion-window admission still bounds each burst, while
    // avoiding sub-millisecond timer wakeups that only delay local IPC.
    if ((peer_address.is_v4() && peer_address.to_v4().is_loopback()) ||
        (peer_address.is_v6() && peer_address.to_v6().is_loopback()))
        co_return;
    const auto rate = path.recovery.pacing_rate();
    if (!rate || !std::isfinite(*rate) || *rate <= 0.0)
        co_return;

    const auto now = std::chrono::steady_clock::now();
    const auto burst_capacity = static_cast<double>(path.recovery.congestion_window());
    if (!path.pacing_credit_updated_at)
    {
        path.pacing_credit_updated_at = now;
        path.pacing_credit_bytes = burst_capacity;
    }
    else if (*path.pacing_credit_updated_at < now)
    {
        const auto elapsed = std::chrono::duration<double>(
            now - *path.pacing_credit_updated_at)
                                 .count();
        path.pacing_credit_bytes = std::min(
            burst_capacity, path.pacing_credit_bytes + elapsed * *rate);
        path.pacing_credit_updated_at = now;
    }

    const auto bytes = static_cast<double>(packet_size);
    if (path.pacing_credit_bytes >= bytes)
    {
        path.pacing_credit_bytes -= bytes;
        co_return;
    }

    const auto reservation_time = std::max(now, *path.pacing_credit_updated_at);
    const auto wait = std::chrono::duration<double>(
        (bytes - path.pacing_credit_bytes) / *rate);
    const auto deadline = reservation_time +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(wait);
    path.pacing_credit_bytes = 0.0;
    path.pacing_credit_updated_at = deadline;
    if (deadline > now)
    {
        co_await async_sleep(impl_->ctx, deadline - now);
    }
    co_return;
}

auto quic_connection::try_reserve_application_pacing(std::size_t packet_size)
    -> bool
{
    auto& path = impl_->sending_application_path();
    const auto& peer_address = path.peer.address();
    if ((peer_address.is_v4() && peer_address.to_v4().is_loopback()) ||
        (peer_address.is_v6() && peer_address.to_v6().is_loopback()))
        return true;
    const auto rate = path.recovery.pacing_rate();
    if (!rate || !std::isfinite(*rate) || *rate <= 0.0)
        return true;

    const auto now = std::chrono::steady_clock::now();
    const auto burst_capacity = static_cast<double>(path.recovery.congestion_window());
    if (!path.pacing_credit_updated_at)
    {
        path.pacing_credit_updated_at = now;
        path.pacing_credit_bytes = burst_capacity;
    }
    else if (*path.pacing_credit_updated_at < now)
    {
        const auto elapsed = std::chrono::duration<double>(
            now - *path.pacing_credit_updated_at)
                                 .count();
        path.pacing_credit_bytes = std::min(
            burst_capacity, path.pacing_credit_bytes + elapsed * *rate);
        path.pacing_credit_updated_at = now;
    }

    const auto bytes = static_cast<double>(packet_size);
    if (path.pacing_credit_bytes < bytes)
        return false;
    path.pacing_credit_bytes -= bytes;
    return true;
}

auto quic_connection::pack_initial_packet() -> std::vector<std::byte>
{
    if (!impl_->initial_keys || !impl_->local_connection_id || !impl_->tls ||
        (impl_->connection_role == quic_role::server && !impl_->peer_connection_id))
        return {};
    const auto& destination_id = impl_->connection_role == quic_role::client
        ? *impl_->initial_destination_id
        : *impl_->peer_connection_id;
    constexpr std::size_t pn_length = 4;
    constexpr std::size_t tag_length = 16;
    const auto retry_token_length = encode_varint(impl_->retry_token.size());
    if (!retry_token_length)
        return {};
    const std::size_t fixed_header = 1 + 4 + 1 + destination_id.size() +
        1 + impl_->local_connection_id->size() + retry_token_length->second +
        impl_->retry_token.size();
    constexpr std::size_t length_field_size = 2;
    if (fixed_header + length_field_size + pn_length + tag_length >= max_udp_payload)
        return {};
    const auto maximum_payload = max_udp_payload - fixed_header -
        length_field_size - pn_length - tag_length;

    std::vector<std::byte> payload;
    retransmittable_frame_storage retransmittable_frames;
    auto& retransmit = impl_->retransmit_crypto_frames[level_index(encryption_level::initial)];
    if (retransmit.empty())
    {
        const auto crypto = impl_->tls->take_handshake_data(encryption_level::initial);
        if (!crypto.empty() && !queue_crypto_frames(crypto, impl_->next_send_crypto_offset[level_index(encryption_level::initial)], maximum_payload, retransmit))
            return {};
    }
    if (!retransmit.empty())
    {
        if (!fit_front_crypto_frame(retransmit, maximum_payload))
            return {};
        auto frame = std::move(retransmit.front());
        retransmit.pop_front();
        payload.insert(payload.end(), frame.begin(), frame.end());
        retransmittable_frames.push_back(std::move(frame));
    }
    auto& received =
        impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::initial)];
    if (auto ack = take_ack_frame(received))
    {
        auto encoded_ack = encode_frame(*ack);
        if (payload.size() + encoded_ack.size() <= maximum_payload)
        {
            payload.insert(payload.end(), encoded_ack.begin(), encoded_ack.end());
            received.clear();
        }
    }
    if (payload.empty())
        return {};
    // Initial datagrams must be at least 1200 octets (RFC 9000 §14.1).  The
    // padding is authenticated and therefore added before sealing.
    const auto minimum_payload = min_initial_pkt_size > fixed_header +
                length_field_size + pn_length + tag_length
        ? min_initial_pkt_size - fixed_header - length_field_size - pn_length - tag_length
        : 0;
    if (payload.size() < minimum_payload)
        payload.resize(minimum_payload, std::byte{0});
    const auto payload_length = payload.size() + pn_length + tag_length;
    auto length = encode_varint(payload_length);
    if (!length)
        return {};
    std::vector<std::byte> header;
    header.reserve(fixed_header + length->second + pn_length);
    const auto is_v2 = impl_->version == quic_version::v2;
    header.push_back(is_v2 ? std::byte{0xd3} : std::byte{0xc3});
    const auto version = static_cast<std::uint32_t>(impl_->version);
    for (int shift = 24; shift >= 0; shift -= 8)
        header.push_back(static_cast<std::byte>((version >> shift) & 0xffU));
    header.push_back(static_cast<std::byte>(destination_id.size()));
    header.insert(header.end(), destination_id.data(), destination_id.data() + destination_id.size());
    header.push_back(static_cast<std::byte>(impl_->local_connection_id->size()));
    header.insert(header.end(), impl_->local_connection_id->data(),
        impl_->local_connection_id->data() + impl_->local_connection_id->size());
    header.insert(header.end(), retry_token_length->first.begin(),
        retry_token_length->first.begin() + retry_token_length->second);
    header.insert(header.end(), impl_->retry_token.begin(), impl_->retry_token.end());
    header.insert(header.end(), length->first.begin(), length->first.begin() + length->second);
    const auto packet_number =
        impl_->next_send_packet_number[level_index(encryption_level::initial)]++;
    for (int shift = 24; shift >= 0; shift -= 8)
        header.push_back(static_cast<std::byte>((packet_number >> shift) & 0xffU));
    const auto& write_keys = impl_->connection_role == quic_role::client
        ? impl_->initial_keys->client
        : impl_->initial_keys->server;
    const auto pn_offset = header.size() - pn_length;
    if (!append_sealed_payload(write_keys, payload, header, packet_number))
        return {};
    if (!protect_header(write_keys, header, pn_offset, true))
        return {};
    const auto ack_eliciting = !retransmittable_frames.empty();
    impl_->recovery.on_packet_sent(packet_number, header.size(),
        std::chrono::steady_clock::now(), ack_eliciting, pn_space::initial,
        std::move(retransmittable_frames));
    return header;
}

auto quic_connection::pack_handshake_packet() -> std::vector<std::byte>
{
    if (!impl_->local_connection_id || !impl_->peer_connection_id || !impl_->tls)
        return {};
    const auto* keys = impl_->tls->write_keys(encryption_level::handshake);
    if (!keys)
        return {};
    constexpr std::size_t pn_length = 4;
    constexpr std::size_t tag_length = 16;
    constexpr std::size_t length_field_size = 2;
    const std::size_t fixed_header = 1 + 4 + 1 +
        impl_->peer_connection_id->size() + 1 + impl_->local_connection_id->size();
    if (fixed_header + length_field_size + pn_length + tag_length >= max_udp_payload)
        return {};
    const auto maximum_payload = max_udp_payload - fixed_header -
        length_field_size - pn_length - tag_length;

    std::vector<std::byte> payload;
    retransmittable_frame_storage retransmittable_frames;
    auto& retransmit = impl_->retransmit_crypto_frames[level_index(encryption_level::handshake)];
    if (retransmit.empty())
    {
        const auto crypto = impl_->tls->take_handshake_data(encryption_level::handshake);
        if (!crypto.empty() && !queue_crypto_frames(crypto, impl_->next_send_crypto_offset[level_index(encryption_level::handshake)], maximum_payload, retransmit))
            return {};
    }
    if (!retransmit.empty())
    {
        if (!fit_front_crypto_frame(retransmit, maximum_payload))
            return {};
        auto frame = std::move(retransmit.front());
        retransmit.pop_front();
        payload.insert(payload.end(), frame.begin(), frame.end());
        retransmittable_frames.push_back(std::move(frame));
    }
    auto& received =
        impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::handshake)];
    if (auto ack = take_ack_frame(received))
    {
        auto encoded_ack = encode_frame(*ack);
        if (payload.size() + encoded_ack.size() <= maximum_payload)
        {
            payload.insert(payload.end(), encoded_ack.begin(), encoded_ack.end());
            received.clear();
        }
    }
    if (payload.empty())
        return {};
    auto length = encode_varint(payload.size() + pn_length + tag_length);
    if (!length)
        return {};
    std::vector<std::byte> header;
    header.reserve(1 + 4 + 1 + impl_->peer_connection_id->size() + 1 +
        impl_->local_connection_id->size() + length->second + pn_length +
        payload.size() + tag_length);
    header.push_back(impl_->version == quic_version::v2 ? std::byte{0xf3} : std::byte{0xe3});
    const auto version = static_cast<std::uint32_t>(impl_->version);
    for (int shift = 24; shift >= 0; shift -= 8)
        header.push_back(static_cast<std::byte>((version >> shift) & 0xffU));
    header.push_back(static_cast<std::byte>(impl_->peer_connection_id->size()));
    header.insert(header.end(), impl_->peer_connection_id->data(),
        impl_->peer_connection_id->data() + impl_->peer_connection_id->size());
    header.push_back(static_cast<std::byte>(impl_->local_connection_id->size()));
    header.insert(header.end(), impl_->local_connection_id->data(),
        impl_->local_connection_id->data() + impl_->local_connection_id->size());
    header.insert(header.end(), length->first.begin(), length->first.begin() + length->second);
    const auto packet_number =
        impl_->next_send_packet_number[level_index(encryption_level::handshake)]++;
    for (int shift = 24; shift >= 0; shift -= 8)
        header.push_back(static_cast<std::byte>((packet_number >> shift) & 0xffU));
    const auto pn_offset = header.size() - pn_length;
    if (!append_sealed_payload(*keys, payload, header, packet_number))
        return {};
    if (!protect_header(*keys, header, pn_offset, true))
        return {};
    const auto ack_eliciting = !retransmittable_frames.empty();
    impl_->recovery.on_packet_sent(packet_number, header.size(),
        std::chrono::steady_clock::now(), ack_eliciting, pn_space::handshake,
        std::move(retransmittable_frames));
    return header;
}

auto quic_connection::pack_one_rtt_packet(bool pto_probe,
    std::vector<std::byte> packet_storage,
    std::vector<std::byte>* payload_storage)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (!impl_->tls)
        return {};
    const auto* keys = impl_->tls->write_keys(encryption_level::application);
    if (!keys)
        return {};

    auto& path = impl_->sending_application_path();
    const auto* destination_cid = impl_->path_peer_connection_id(path);
    if (!destination_cid || path.locally_abandoned || path.peer_abandoned)
        return {};

    // Assemble plaintext after the reserved header and seal that same
    // storage in place. The private scratch parameter stays for compatibility.
    (void)payload_storage;
    constexpr std::size_t pn_length = 4U;
    const auto header_size = 1U + destination_cid->size() + pn_length;
    const auto application_level = level_index(encryption_level::application);
    const auto payload_budget = impl_->packet_payload_budget(path);
    auto& payload = packet_storage;
    payload.clear();
    payload.reserve(header_size + payload_budget + keys->tag_len);
    payload.resize(header_size);
    // PATH_ACK explicitly identifies the source packet-number space.  This
    // matters during PATH_ABANDON: the source can be in its three-PTO grace
    // period while this packet is sent on a different validated path.
    auto* ack_source = std::addressof(path);
    if (impl_->multipath_negotiated)
    {
        if (const auto pending = impl_->path_with_pending_ack())
            ack_source = impl_->application_paths.at(*pending).get();
    }
    const auto ack = take_ack_frame(ack_source->received_ack_eliciting_packet_numbers);
    if (ack)
    {
        const auto encoded_ack = impl_->multipath_negotiated
            ? encode_frame(path_ack_frame{ack_source->id, *ack})
            : encode_frame(*ack);
        payload.insert(payload.end(), encoded_ack.begin(), encoded_ack.end());
    }
    retransmittable_frame_storage retransmittable_frames;

    // Keep the priority metadata separate from the retransmission bytes. It
    // lets a packet rejected by congestion control return a STREAM frame to
    // its original priority class instead of silently demoting it into the
    // generic FIFO queue.
    struct scheduled_stream_frame_metadata
    {
        stream_id stream{};
        std::uint8_t urgency{};
        bool default_fifo{};
        bool selection_reserved{};
    };

    using scheduled_metadata_entry =
        std::pair<std::size_t, scheduled_stream_frame_metadata>;
    std::optional<scheduled_metadata_entry> first_scheduled_frame_metadata;
    std::vector<scheduled_metadata_entry> additional_scheduled_frame_metadata;
    const auto remember_scheduled_metadata =
        [&first_scheduled_frame_metadata, &additional_scheduled_frame_metadata](
            std::size_t index, scheduled_stream_frame_metadata metadata)
    {
        if (!first_scheduled_frame_metadata)
            first_scheduled_frame_metadata.emplace(index, metadata);
        else
            additional_scheduled_frame_metadata.emplace_back(index, metadata);
    };
    const auto scheduled_metadata_for =
        [&first_scheduled_frame_metadata, &additional_scheduled_frame_metadata](
            std::size_t index) -> const scheduled_stream_frame_metadata*
    {
        if (first_scheduled_frame_metadata &&
            first_scheduled_frame_metadata->first == index)
            return std::addressof(first_scheduled_frame_metadata->second);
        const auto found = std::ranges::find(additional_scheduled_frame_metadata,
            index, &scheduled_metadata_entry::first);
        return found == additional_scheduled_frame_metadata.end()
            ? nullptr
            : std::addressof(found->second);
    };
    // Preserve the existing PTO exception; defer optional PMTU enlargement
    // when an ordinary packet lacks credit, leaving validated-size work free.
    std::optional<std::size_t> path_mtu_probe_target;
    if (path.path_mtu_probe_target &&
        (pto_probe || path.recovery.can_send_datagram(*path.path_mtu_probe_target)))
        path_mtu_probe_target = path.path_mtu_probe_target;
    const bool contains_path_mtu_probe = path_mtu_probe_target.has_value();
    if (contains_path_mtu_probe)
    {
        // A padded PING is an RFC 9000 ack-eliciting PMTU probe. It is never
        // retransmitted as application work: its one purpose is to establish
        // that this exact UDP payload reached the peer.
        const auto ping = encode_frame(ping_frame{});
        payload.insert(payload.end(), ping.begin(), ping.end());
    }
    if (impl_->connection_role == quic_role::server)
    {
        auto& retransmit_crypto = impl_->retransmit_crypto_frames[application_level];
        if (!retransmit_crypto.empty())
        {
            auto frame = std::move(retransmit_crypto.front());
            retransmit_crypto.pop_front();
            payload.insert(payload.end(), frame.begin(), frame.end());
            retransmittable_frames.push_back(
                quic_send_frame{std::move(frame)});
        }
        else
        {
            const auto crypto = impl_->tls->take_handshake_data(encryption_level::application);
            if (!crypto.empty())
            {
                auto frame = encode_frame(crypto_frame{
                    impl_->next_send_crypto_offset[application_level], crypto});
                impl_->next_send_crypto_offset[application_level] += crypto.size();
                payload.insert(payload.end(), frame.begin(), frame.end());
                retransmittable_frames.push_back(
                    quic_send_frame{std::move(frame)});
            }
        }
    }
    const bool selection_reserved = impl_->stream_scheduler.has_pending_writes();
    if (selection_reserved)
        impl_->stream_scheduler.begin_packet_selection();
    const bool credit_sized_ranges = selection_reserved && !pto_probe &&
        !contains_path_mtu_probe && impl_->encoded_send_frames.empty() &&
        retransmittable_frames.empty() && impl_->unreliable_send_frames.empty() &&
        impl_->connection_state == connection_state::connected;
    const auto range_credit = credit_sized_ranges ? path.recovery.remaining_send_credit() : 0U;
    // Select materialization once; eager packets carry no virtual/defer state.
    struct deferred_materialization_state
    {
        std::size_t virtual_payload_size;
        std::optional<std::size_t> deferred_reliable_begin;
    };
    bool contains_connection_close = false;
    bool contains_unreliable_datagram = false;
    auto materialize = [&]<bool Defer>(auto state) -> std::size_t
    {
        const auto selected_size = [&]() -> std::size_t
        {
            if constexpr (Defer)
                return state.virtual_payload_size;
            else
                return payload.size();
        };
        while ((!impl_->encoded_send_frames.empty() || impl_->has_prioritized_stream_frames()) &&
            (selected_size() - header_size) < payload_budget)
        {
            quic_send_frame frame;
            std::optional<scheduled_stream_frame_metadata> frame_metadata;
            if (!impl_->encoded_send_frames.empty())
            {
                frame = std::move(impl_->encoded_send_frames.front());
                impl_->encoded_send_frames.pop_front();
            }
            else
            {
                std::optional<std::size_t> pending_credit;
                if (credit_sized_ranges)
                {
                    const auto occupied = selected_size() + keys->tag_len;
                    pending_credit = range_credit > occupied ? range_credit - occupied : 0U;
                }
                auto scheduled = selection_reserved
                    ? impl_->stream_scheduler.take_selected_frame(
                          payload_budget - (selected_size() - header_size), pending_credit)
                    : impl_->take_prioritized_stream_frame();
                if (!scheduled)
                    break;
                frame = std::move(scheduled->frame);
                frame_metadata.emplace(scheduled_stream_frame_metadata{
                    scheduled->stream, scheduled->urgency, scheduled->default_fifo, selection_reserved});
            }
            // The next frame must fit as a whole. Checking only the current
            // payload length can coalesce two full STREAM fragments into an
            // oversized UDP datagram and strand a multi-packet response.
            if (frame.size() > payload_budget - (selected_size() - header_size))
            {
                if (frame_metadata)
                {
                    auto restored = scheduled_quic_frame{
                        frame_metadata->stream, frame_metadata->urgency,
                        frame_metadata->default_fifo, std::move(frame)};
                    if (frame_metadata->selection_reserved)
                        impl_->stream_scheduler.rollback_selected_frame(std::move(restored));
                    else
                        impl_->requeue_prioritized_stream_frame(std::move(restored));
                }
                else
                {
                    impl_->encoded_send_frames.push_front(std::move(frame));
                }
                break;
            }
            if constexpr (Defer)
            {
                state.virtual_payload_size += frame.size();
                if (!state.deferred_reliable_begin &&
                    !path.recovery.can_send_datagram(selected_size() + keys->tag_len))
                    state.deferred_reliable_begin = retransmittable_frames.size();
                if (!state.deferred_reliable_begin)
                    frame.append_to(payload);
            }
            else
            {
                frame.append_to(payload);
            }
            if (frame_metadata)
                remember_scheduled_metadata(retransmittable_frames.size(), *frame_metadata);
            retransmittable_frames.push_back(std::move(frame));
        }
        contains_connection_close = retransmittable_frames.any_of(
            [](const auto& frame)
            {
                return frame.is_connection_close();
            });
        // Only CLOSE can exempt these reliable frames from the final CC rejection.
        // It is fully known before DATAGRAM selection; replay only that suffix.
        if constexpr (Defer)
        {
            if (contains_connection_close && state.deferred_reliable_begin)
            {
                for (auto index = *state.deferred_reliable_begin;
                     index < retransmittable_frames.size(); ++index)
                    retransmittable_frames[index].append_to(payload);
            }
        }
        while (!impl_->unreliable_send_frames.empty())
        {
            const auto& frame = impl_->unreliable_send_frames.front();
            if ((selected_size() - header_size) + frame.size() > payload_budget)
                break;
            if constexpr (Defer)
            {
                state.virtual_payload_size += frame.size();
                if (contains_connection_close ||
                    path.recovery.can_send_datagram(selected_size() + keys->tag_len))
                    payload.insert(payload.end(), frame.begin(), frame.end());
            }
            else
            {
                payload.insert(payload.end(), frame.begin(), frame.end());
            }
            impl_->unreliable_send_frames.pop_front();
            contains_unreliable_datagram = true;
        }
        if (path_mtu_probe_target)
        {
            constexpr std::size_t fixed_packet_overhead = 1U + 4U + 16U;
            const auto required_payload = *path_mtu_probe_target >
                    destination_cid->size() + fixed_packet_overhead
                ? *path_mtu_probe_target - destination_cid->size() - fixed_packet_overhead
                : (selected_size() - header_size);
            if ((selected_size() - header_size) < required_payload)
            {
                if constexpr (Defer)
                {
                    state.virtual_payload_size = header_size + required_payload;
                    if (contains_connection_close ||
                        path.recovery.can_send_datagram(selected_size() + keys->tag_len))
                        payload.resize(selected_size(), std::byte{0});
                }
                else
                {
                    payload.resize(header_size + required_payload, std::byte{0});
                }
            }
        }
        return selected_size();
    };
    const auto selected_payload_size = pto_probe ||
        path.recovery.can_send_datagram(header_size + payload_budget + keys->tag_len)
        ? materialize.template operator()<false>(nullptr)
        : materialize.template operator()<true>(
            deferred_materialization_state{payload.size(), std::nullopt});
    if (selected_payload_size == header_size)
    {
        if (selection_reserved)
            impl_->stream_scheduler.finish_packet_selection(false);
        packet_storage.clear();
        return packet_storage;
    }
    const auto estimated_packet_size = 1 + destination_cid->size() + 4 +
        (selected_payload_size - header_size) + keys->tag_len;
    if (!impl_->can_send_on_path(path, estimated_packet_size))
    {
        for (std::size_t index = retransmittable_frames.size(); index != 0U; --index)
        {
            auto& frame = retransmittable_frames[index - 1U];
            const auto* metadata = scheduled_metadata_for(index - 1U);
            if (metadata)
            {
                auto restored = scheduled_quic_frame{
                    metadata->stream, metadata->urgency, metadata->default_fifo, std::move(frame)};
                if (metadata->selection_reserved)
                    impl_->stream_scheduler.rollback_selected_frame(std::move(restored));
                else
                    impl_->requeue_prioritized_stream_frame(std::move(restored));
            }
            else
            {
                impl_->encoded_send_frames.push_front(std::move(frame));
            }
        }
        if (selection_reserved)
            impl_->stream_scheduler.finish_packet_selection(false);
        packet_storage.clear();
        return packet_storage;
    }
    if (!pto_probe &&
        (!retransmittable_frames.empty() || contains_unreliable_datagram ||
            contains_path_mtu_probe) &&
        !contains_connection_close &&
        !path.recovery.can_send_datagram(estimated_packet_size))
    {
        // DATAGRAM frames were consumed above. Drop them under congestion
        // instead of transmitting beyond cwnd (RFC 9221 section 5.4).

        for (std::size_t index = retransmittable_frames.size(); index != 0U; --index)
        {
            auto& frame = retransmittable_frames[index - 1U];
            const auto* metadata = scheduled_metadata_for(index - 1U);
            if (metadata)
            {
                auto restored = scheduled_quic_frame{
                    metadata->stream, metadata->urgency, metadata->default_fifo, std::move(frame)};
                if (metadata->selection_reserved)
                    impl_->stream_scheduler.rollback_selected_frame(std::move(restored));
                else
                    impl_->requeue_prioritized_stream_frame(std::move(restored));
            }
            else
            {
                impl_->encoded_send_frames.push_front(std::move(frame));
            }
        }
        if (selection_reserved)
            impl_->stream_scheduler.finish_packet_selection(false);
        packet_storage.clear();
        return packet_storage;
    }
    const auto packet_number = path.next_send_packet_number++;
    auto packet = std::move(packet_storage);
    packet[0] = impl_->tls->application_write_key_phase()
        ? std::byte{0x47}
        : std::byte{0x43};
    std::ranges::copy(std::span{destination_cid->data(), destination_cid->size()},
        packet.begin() + 1U);
    const auto pn_offset = header_size - pn_length;
    for (std::size_t index{}; index < pn_length; ++index)
        packet[pn_offset + index] = static_cast<std::byte>(
            (packet_number >> ((pn_length - 1U - index) * 8U)) & 0xffU);
    // A crypto failure is terminal work, not an empty/congestion-blocked
    // packet. Some crypto backends fail without pushing an error-queue entry.
    if (const auto sealed = seal_packet_payload_in_place(*keys, packet, header_size, packet_number, path.id);
        !sealed)
    {
        if (selection_reserved) impl_->stream_scheduler.finish_packet_selection(true);
        return std::unexpected(sealed.error() ? sealed.error()
            : std::make_error_code(std::errc::io_error));
    }
    if (const auto protected_header = protect_header(*keys, packet, pn_offset, false);
        !protected_header)
    {
        if (selection_reserved) impl_->stream_scheduler.finish_packet_selection(true);
        return std::unexpected(protected_header.error() ? protected_header.error()
            : std::make_error_code(std::errc::io_error));
    }
    if (selection_reserved) impl_->stream_scheduler.finish_packet_selection(true);
    impl_->account_path_send(path, packet.size());
    // ACK state is cleared only once an authenticated packet containing the
    // frame has been constructed.  The UDP submission path awaits writability
    // on EAGAIN, so this packet remains live until it has either been handed
    // to the socket or the connection observes a terminal send error.
    if (ack)
        ack_source->received_ack_eliciting_packet_numbers.clear();
    if (path_mtu_probe_target)
    {
        path.path_mtu_probe_in_flight = *path_mtu_probe_target;
        path.path_mtu_probe_target.reset();
    }
    const auto recovery_tracked = (!retransmittable_frames.empty() || contains_unreliable_datagram ||
                                      contains_path_mtu_probe) &&
        !contains_connection_close;
    path.recovery.on_packet_sent(packet_number, packet.size(),
        std::chrono::steady_clock::now(), recovery_tracked,
        pn_space::application, std::move(retransmittable_frames),
        path_mtu_probe_target, pto_probe);
    return packet;
}

auto quic_connection::pack_path_validation_packet(std::span<const std::byte> frame)
    -> std::vector<std::byte>
{
    if (!impl_->tls || frame.empty())
        return {};
    const auto* keys = impl_->tls->write_keys(encryption_level::application);
    if (!keys)
        return {};
    auto& path = impl_->sending_application_path();
    const auto* destination_cid = impl_->path_peer_connection_id(path);
    if (!destination_cid || path.locally_abandoned || path.peer_abandoned)
        return {};
    constexpr std::size_t pn_length = 4;
    const auto estimated_packet_size = 1 + destination_cid->size() + pn_length +
        frame.size() + keys->tag_len;
    if (!impl_->can_send_on_path(path, estimated_packet_size))
        return {};
    const auto packet_number = path.next_send_packet_number++;
    std::vector<std::byte> packet;
    packet.reserve(estimated_packet_size);
    packet.push_back(impl_->tls->application_write_key_phase()
            ? std::byte{0x47}
            : std::byte{0x43});
    packet.insert(packet.end(), destination_cid->data(),
        destination_cid->data() + destination_cid->size());
    for (int shift = 24; shift >= 0; shift -= 8)
        packet.push_back(static_cast<std::byte>((packet_number >> shift) & 0xffU));
    const auto pn_offset = packet.size() - pn_length;
    if (!append_sealed_payload(*keys, frame, packet, packet_number, path.id))
        return {};
    if (!protect_header(*keys, packet, pn_offset, false))
        return {};
    impl_->account_path_send(path, packet.size());
    path.recovery.on_packet_sent(packet_number, packet.size(),
        std::chrono::steady_clock::now(), true, pn_space::application,
        retransmittable_frame_storage{quic_send_frame{
            std::vector<std::byte>(frame.begin(), frame.end())}});
    return packet;
}

void quic_connection::publish_deferred_stream_cancellations()
{
    if (impl_->connection_state == connection_state::closing ||
        impl_->connection_state == connection_state::draining || is_closed() ||
        !impl_->tls->write_keys(encryption_level::application))
        return;
    // Keep the retry edge true until the entire scan succeeds. Each queued
    // intent is independently recoverable if a later stream or retirement throws.
    std::vector<stream_id> pending;
    for (const auto& [sid, entry] : impl_->streams)
        if (entry.has_deferred_reset)
            pending.push_back(sid);
    bool remaining{};
    for (const auto sid : pending)
    {
        const auto current = impl_->streams.find(sid);
        if (current == impl_->streams.end() || !current->second.has_deferred_reset)
            continue;
        auto& entry = current->second;
        const auto limit = impl_->peer_transport_parameters_applied
            ? (is_bidirectional(sid) ? impl_->peer_max_streams_bidi : impl_->peer_max_streams_uni)
            : (is_bidirectional(sid) ? impl_->peer_max_streams_bidi_from_frames
                                    : impl_->peer_max_streams_uni_from_frames);
        if (!entry.deferred_reset_queued)
        {
            if (sid / 4U + 1U > limit)
            {
                remaining = true;
                continue;
            }
            const auto error = entry.deferred_reset_error;
            auto reset = encode_frame(reset_stream_frame{sid, error, 0U});
            auto stop = entry.deferred_reset_stop_peer
                ? std::optional{encode_frame(stop_sending_frame{sid, error})} : std::nullopt;
            const auto before = impl_->encoded_send_frames.size();
            try
            {
                impl_->encoded_send_frames.push_back(std::move(reset));
                if (stop)
                    impl_->encoded_send_frames.push_back(std::move(*stop));
            }
            catch (...)
            {
                while (impl_->encoded_send_frames.size() > before)
                    impl_->encoded_send_frames.pop_back();
                throw;
            }
            // Queue ownership is durable even if retirement or a later stream
            // throws. A retry must retire this entry without queueing it twice.
            entry.local_send_accepted = true;
            entry.deferred_reset_queued = true;
            entry.deferred_stop_queued = entry.deferred_reset_stop_peer;
        }
        // Full cancellation may upgrade a STOP-only intent after RESET already
        // reached the reliable queue but an exception postponed retirement.
        if (entry.deferred_reset_stop_peer && !entry.deferred_stop_queued)
        {
            impl_->encoded_send_frames.push_back(encode_frame(stop_sending_frame{
                sid, entry.deferred_reset_error}));
            entry.deferred_stop_queued = true;
        }
        if (entry.value->state() == stream_state::closed)
        {
            // Snapshot IDs, not iterators: retirement may notify application code.
            // A failed/throwing retirement leaves its queued intent retryable.
            if (const auto retired = retire_stream(sid); !retired)
                remaining = true;
        }
        else
        {
            // Both STOP-only and full cancellation retain an unfinished receive
            // half. Only a real peer FIN/RESET supplies its final size.
            entry.has_deferred_reset = false;
            entry.deferred_reset_queued = false;
            entry.deferred_reset_stop_peer = false;
            entry.deferred_stop_queued = false;
        }
    }
    impl_->has_deferred_stream_cancellations = remaining;
}

auto quic_connection::flush_send_queue() -> task<void>
{
    // Producers may request a flush from packet receive, a HTTP/3 response
    // coroutine, cancellation, or a timer. The request bit is lock-free;
    // packet construction itself has exactly one owner. This replaces the
    // former unsynchronised bool/spin loop, which could strand the receive
    // path behind a timer on Windows.
    impl_->send_flush_requested.store(true, std::memory_order_release);
    // Only the coroutine that wins this non-blocking claim becomes packet
    // owner.  A producer that loses has already published both its command
    // and the request bit, so queueing it behind the owner would merely cause
    // an empty lock hand-off per response.  The owner below rechecks after
    // releasing the token, which closes the publish/release race without
    // making producers contend on a coroutine waiter list.
    if (!impl_->flush_mutex.try_lock())
        co_return;
    async_lock_guard flush_guard{impl_->flush_mutex, std::adopt_lock};
    bool coalescing_checked{};
    // A single application write can be fragmented into multiple STREAM
    // frames.  Drain all packets that the congestion window currently admits;
    // every packet passes through await_application_pacing() and async_sendto
    // parks on platform writability, so this neither bursts past pacing nor
    // spins when the UDP queue is full.  If pack_one_rtt_packet() restores the
    // frames because cwnd is full, the queue size does not decrease and the
    // loop yields to ACK/loss/PTO processing instead of busy-looping.
    for (;;)
    {
        impl_->send_flush_requested.store(false, std::memory_order_release);
        // The packet owner is the only code allowed to turn an application
        // write into stream state and QUIC frames.  Keep the state critical
        // section bounded to command application; socket/pacing awaits below
        // stay outside it so receive processing remains responsive.
        co_await impl_->receive_mutex.lock();
        async_lock_guard state_guard{impl_->receive_mutex, std::adopt_lock};
        if (impl_->has_deferred_stream_cancellations)
            publish_deferred_stream_cancellations();
        if (!std::exchange(coalescing_checked, true) &&
            impl_->connection_state == connection_state::connected &&
            !impl_->queued_protocol_error &&
            impl_->write_commands.approximate_size() != 0U)
        {
            // This is a bounded scheduling hint, independent of MAX_STREAMS
            // or stream-ID gaps. Avoid a reactor turn for a single active
            // stream; at most sixteen real entries are examined under the
            // existing state lock, and control-only/many-uni connections may
            // simply miss the optimization rather than pay an unbounded scan.
            std::size_t inspected{};
            std::size_t active_bidirectional{};
            for (const auto& [id, stream] : impl_->streams)
            {
                if (is_bidirectional(id) && stream.value &&
                    stream.value->state() != stream_state::closed &&
                    ++active_bidirectional == 2U)
                    break;
                if (++inspected == 16U)
                    break;
            }
            if (active_bidirectional == 2U)
            {
                // Only the writer owner posts. Already-ready stream services
                // can publish while its token stays held, without a timer or
                // dependence on another incoming datagram.
                state_guard.release();
                impl_->receive_mutex.unlock();
                co_await post_awaitable{impl_->ctx};
                // close_transport completes queued producers. A local CLOSE
                // instead remains queued and is drained on the next pass.
                if (is_closed() || impl_->connection_state == connection_state::draining)
                    co_return;
                continue;
            }
        }
        using completion_result = std::pair<quic_write_completion*,
            std::expected<void, std::error_code>>;
        std::optional<completion_result> first_completion;
        std::vector<completion_result> additional_completions;
        while (auto command = impl_->write_commands.try_dequeue())
        {
            if (command->type == quic_write_command::operation::set_priority)
            {
                impl_->set_stream_priority(command->stream, command->urgency,
                    command->incremental);
                continue;
            }
            auto result = command->type ==
                    quic_write_command::operation::datagram
                ? co_await apply_application_datagram(std::move(command->bytes))
                : command->shared_bytes
                ? apply_stream_write(command->stream,
                      command->bytes, command->shared_bytes, command->fin)
                : apply_stream_write(command->stream,
                      command->bytes, command->fin);
            if (!result && result.error() == make_error_code(quic_errc::flow_control_error) &&
                command->completion)
            {
                command->completion->rejected_bytes = std::move(command->bytes);
                command->completion->rejected_shared_bytes =
                    std::move(command->shared_bytes);
            }
            if (command->completion)
            {
                if (!first_completion)
                    first_completion.emplace(command->completion, std::move(result));
                else
                    additional_completions.emplace_back(command->completion, std::move(result));
            }
        }
        state_guard.release();
        impl_->receive_mutex.unlock();
        // Completion resumes producers synchronously; never run them while
        // holding the connection state mutex.
        if (first_completion)
            first_completion->first->complete(std::move(first_completion->second));
        for (auto& [completion, result] : additional_completions)
            completion->complete(std::move(result));
        bool send_ack = impl_->path_with_pending_ack().has_value() ||
            impl_->path_with_pending_mtu_probe().has_value();
        const auto has_early_data = [&]() noexcept
        {
            return !impl_->early_data_send_frames.empty() &&
                impl_->connection_role == quic_role::client &&
                (impl_->connection_state == connection_state::idle ||
                    impl_->connection_state == connection_state::handshaking);
        };
        while (send_ack || !impl_->encoded_send_frames.empty() ||
            impl_->has_prioritized_stream_frames() ||
            !impl_->unreliable_send_frames.empty() ||
            (impl_->connection_role == quic_role::server &&
                impl_->tls->has_pending_handshake_data(encryption_level::application)) ||
            has_early_data())
        {
            const auto pending_progress_before = impl_->stream_scheduler.pending_progress();
            const auto queued_before = impl_->encoded_send_frames.size() +
                impl_->prioritized_stream_frame_count() +
                impl_->unreliable_send_frames.size() +
                impl_->early_data_send_frames.size();
            const auto sent = co_await pack_and_send_packets();
            if (!sent)
            {
                // The receive lock and packet/path references are gone. Keep
                // the writer claim until queued producers have been failed;
                // async_close joins it before reclaiming connection storage.
                // Existing accepted completions retain their queue semantics.
                close_transport(sent.error());
                co_return;
            }
            send_ack = false;
            if (impl_->encoded_send_frames.size() + impl_->prioritized_stream_frame_count() +
                    impl_->unreliable_send_frames.size() +
                    impl_->early_data_send_frames.size() >=
                queued_before &&
                impl_->stream_scheduler.pending_progress() == pending_progress_before)
                break;
        }
        if (impl_->queued_protocol_error && impl_->encoded_send_frames.empty())
        {
            close_transport(impl_->queued_protocol_error);
        }
        // A newly published command must be applied even when there were no
        // previously encoded frames.  The old compound condition required
        // both a command and an existing frame, which could leave a producer
        // waiting until a later timer tick.
        if (impl_->send_flush_requested.load(std::memory_order_acquire) ||
            impl_->write_commands.approximate_size() != 0U)
            continue;

        // Do not transfer the owner token through a long coroutine waiter
        // chain.  Release it, then recheck: a producer that published before
        // this release is observed here; one that publishes afterwards wins
        // try_lock() in its own flush request.
        flush_guard.release();
        impl_->flush_mutex.unlock();
        if (!impl_->send_flush_requested.load(std::memory_order_acquire) &&
            impl_->write_commands.approximate_size() == 0U)
            co_return;
        if (!impl_->flush_mutex.try_lock())
            co_return;
        flush_guard = async_lock_guard{impl_->flush_mutex, std::adopt_lock};
    }
}

auto quic_connection::apply_stream_write(stream_id sid, std::vector<std::byte>& data,
    bool fin) -> std::expected<void, std::error_code>
{
    return apply_stream_write(sid, data, {}, fin);
}

auto quic_connection::apply_stream_write(stream_id sid,
    std::vector<std::byte>& prefix,
    const std::shared_ptr<const std::string>& shared_data, bool fin)
    -> std::expected<void, std::error_code>
{
    const std::span<const std::byte> prefix_payload{prefix.data(), prefix.size()};
    const auto shared_payload = shared_data
        ? std::as_bytes(std::span{shared_data->data(), shared_data->size()})
        : std::span<const std::byte>{};
    const auto payload_size = prefix_payload.size() + shared_payload.size();
    if (is_closed() || impl_->connection_state == connection_state::closing ||
        impl_->connection_state == connection_state::draining)
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    if (impl_->retired_streams.contains(sid))
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    if (sid > ((std::uint64_t{1} << 62U) - 1U))
        return std::unexpected(make_error_code(quic_errc::stream_limit_error));
    const bool local_stream = is_client_initiated(sid) ==
        (impl_->connection_role == quic_role::client);
    if (local_stream)
    {
        const bool provisional_early = impl_->connection_role == quic_role::client &&
            impl_->tls->early_data_status() == early_data_state::pending;
        const auto count_limit = impl_->peer_transport_parameters_applied || provisional_early
            ? (is_bidirectional(sid) ? impl_->peer_max_streams_bidi : impl_->peer_max_streams_uni)
            : (is_bidirectional(sid) ? impl_->peer_max_streams_bidi_from_frames
                                    : impl_->peer_max_streams_uni_from_frames);
        if (sid / 4U + 1U > count_limit)
            return std::unexpected(make_error_code(quic_errc::stream_limit_error));
    }
    auto it = impl_->streams.find(sid);
    if (it == impl_->streams.end())
    {
        if (!local_stream)
            return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
        (void)impl_->ensure_local_stream(sid);
        it = impl_->streams.find(sid);
    }
    if (payload_size > impl_->peer_max_data -
            std::min(impl_->peer_max_data, impl_->sent_stream_data))
        return std::unexpected(make_error_code(quic_errc::flow_control_error));
    // Reserve the whole logical write before mutating stream accounting. A
    // shared response is submitted as a small prefix followed by its cached
    // body; accepting the prefix and then rejecting the body would otherwise
    // leave a partial HTTP/3 frame on the stream.
    if (payload_size > it->second.value->remaining_send_window())
        return std::unexpected(make_error_code(quic_errc::flow_control_error));
    if (!prefix_payload.empty())
    {
        auto sent = it->second.value->apply_send_state(prefix_payload);
        if (!sent)
            return std::unexpected(sent.error());
    }
    if (!shared_payload.empty())
    {
        auto sent = it->second.value->apply_send_state(shared_payload);
        if (!sent)
            return std::unexpected(sent.error());
    }
    if (payload_size == 0U)
    {
        auto sent = it->second.value->apply_send_state({});
        if (!sent)
            return std::unexpected(sent.error());
    }
    if (local_stream)
        it->second.local_send_accepted = true;
    if (fin)
        it->second.value->apply_close_local_state();
    impl_->sent_stream_data += payload_size;

    // `quic_stream::send` accounts bytes before this point.  The wire offset
    // must therefore be the beginning of this write, not zero for every
    // STREAM frame.  Fragment the write so one application datagram never
    // exceeds the UDP payload budget.
    const auto first_offset = it->second.value->bytes_sent() - payload_size;
    auto send_packet_payload = max_udp_payload;
    for (const auto& [_, path] : impl_->application_paths)
    {
        if (!path->locally_abandoned && !path->peer_abandoned && path->validated)
            send_packet_payload = std::min(send_packet_payload == max_udp_payload
                    ? impl_->packet_payload_budget(*path)
                    : send_packet_payload,
                impl_->packet_payload_budget(*path));
    }
    const auto use_early_data = impl_->connection_role == quic_role::client &&
        (impl_->connection_state == connection_state::idle ||
            impl_->connection_state == connection_state::handshaking) &&
        impl_->tls->early_data_status() == early_data_state::pending &&
        !impl_->tls->write_keys(encryption_level::application);
    if (use_early_data)
    {
        impl_->early_data_streams.insert(sid);
        const auto* keys = impl_->tls->write_keys(encryption_level::early_data);
        const auto overhead = 1U + 4U + 1U + 20U + 1U + 20U + 8U + 4U +
            (keys ? keys->tag_len : 16U);
        send_packet_payload = std::min(send_packet_payload, max_udp_payload - overhead);
    }
    const bool pending_shared_write = !use_early_data && shared_data &&
        (payload_size != 0U || fin) && impl_->connection_state == connection_state::connected &&
        impl_->config.max_datagram_frame_size == 0U && impl_->application_paths.size() == 1U &&
        impl_->application_paths.contains(0U) && !impl_->multipath_negotiated &&
        send_packet_payload > 2U + static_cast<std::size_t>(varint_size(sid)) +
            static_cast<std::size_t>(varint_size(first_offset + (payload_size != 0U ? payload_size - 1U : 0U)));
    if (pending_shared_write)
    {
        impl_->stream_scheduler.enqueue_shared_write(sid, std::move(prefix), shared_data,
            first_offset, fin, send_packet_payload, pending_stream_header);
    }
    else
    {
        for (std::size_t sent_offset = 0; sent_offset < payload_size;)
        {
            const auto stream_offset = first_offset + sent_offset;
            const auto fixed_frame_size = 1U +
                static_cast<std::size_t>(varint_size(sid)) +
                static_cast<std::size_t>(varint_size(stream_offset));
            if (send_packet_payload <= fixed_frame_size + 1U)
                return std::unexpected(std::make_error_code(std::errc::message_size));
            auto chunk = std::min(payload_size - sent_offset,
                send_packet_payload - fixed_frame_size - 1U);
            while (fixed_frame_size + static_cast<std::size_t>(varint_size(chunk)) +
                    chunk >
                send_packet_payload)
                --chunk;
            const auto frame_fin = fin && sent_offset + chunk == payload_size;
            if (!use_early_data && shared_data &&
                sent_offset + chunk > prefix_payload.size())
            {
                std::array<std::byte, 32> header{};
                std::size_t header_size{};
                const auto append_varint = [&header, &header_size](std::uint64_t value)
                {
                    const auto encoded = encode_varint_to(value,
                        std::span{header}.subspan(header_size));
                    if (!encoded)
                        std::terminate();
                    header_size += *encoded;
                };
                std::uint64_t frame_type = 0x0aU;
                if (stream_offset != 0U)
                    frame_type |= 0x04U;
                if (frame_fin)
                    frame_type |= 0x01U;
                append_varint(frame_type);
                append_varint(sid);
                if (stream_offset != 0U)
                    append_varint(stream_offset);
                append_varint(chunk);
                if (sent_offset >= prefix_payload.size())
                {
                    impl_->enqueue_stream_frame(sid,
                        quic_send_frame::shared_stream(
                            std::span{header}.first(header_size), shared_data,
                            sent_offset - prefix_payload.size(), chunk, sid));
                }
                else
                {
                    const auto prefix_count = prefix_payload.size() - sent_offset;
                    impl_->enqueue_stream_frame(sid,
                        quic_send_frame::shared_stream_with_prefix(
                            std::span{header}.first(header_size),
                            prefix_payload.subspan(sent_offset, prefix_count), shared_data,
                            0U, chunk - prefix_count, sid));
                }
                sent_offset += chunk;
                continue;
            }
            std::vector<std::byte> boundary_chunk;
            std::span<const std::byte> chunk_payload;
            if (sent_offset + chunk <= prefix_payload.size())
            {
                chunk_payload = prefix_payload.subspan(sent_offset, chunk);
            }
            else if (sent_offset >= prefix_payload.size())
            {
                chunk_payload = shared_payload.subspan(
                    sent_offset - prefix_payload.size(), chunk);
            }
            else
            {
                const auto prefix_count = prefix_payload.size() - sent_offset;
                boundary_chunk.reserve(chunk);
                boundary_chunk.insert(boundary_chunk.end(),
                    prefix_payload.begin() + sent_offset, prefix_payload.end());
                boundary_chunk.insert(boundary_chunk.end(), shared_payload.begin(),
                    shared_payload.begin() + (chunk - prefix_count));
                chunk_payload = boundary_chunk;
            }
            auto frame = encode_frame(stream_frame{
                sid, stream_offset, chunk_payload,
                frame_fin});
            if (use_early_data)
                impl_->early_data_send_frames.push_back(std::move(frame));
            else
                impl_->enqueue_stream_frame(sid, std::move(frame));
            sent_offset += chunk;
        }
        if (payload_size == 0U && fin)
        {
            auto frame = encode_frame(stream_frame{sid, first_offset, {}, true});
            if (use_early_data)
                impl_->early_data_send_frames.push_back(std::move(frame));
            else
                impl_->enqueue_stream_frame(sid, std::move(frame));
        }
    }
    // If the driver entered recv_datagram() while the connection had no
    // in-flight packets, its receive has no PTO deadline. Cancel that wait so
    // it immediately re-arms with the deadline for this newly queued STREAM
    // frame. This is edge-triggered and does not serialize or block writers.
    impl_->receive_rearm_requested.store(true, std::memory_order_release);
    if (impl_->receive_wait_token && impl_->receive_wait_token->pending_.load(std::memory_order_acquire))
    {
        impl_->receive_wait_token->cancel();
    }
    return {};
}

auto quic_connection::async_send(stream_id sid, std::span<const std::byte> data,
    bool fin) -> task<std::expected<void, std::error_code>>
{
    std::vector<std::byte> owned_data{data.begin(), data.end()};
    co_return co_await async_send(sid, std::move(owned_data), fin);
}

auto quic_connection::await_local_stream_send_authority(stream_id sid)
    -> task<std::expected<void, std::error_code>>
{
    if (sid > ((std::uint64_t{1} << 62U) - 1U))
        co_return std::unexpected(make_error_code(quic_errc::stream_limit_error));
    for (;;)
    {
        {
            co_await impl_->receive_mutex.lock();
            async_lock_guard guard{impl_->receive_mutex, std::adopt_lock};
            if (impl_->terminal_send_error)
                co_return std::unexpected(impl_->terminal_send_error);
            if (is_closed() || impl_->connection_state == connection_state::closing ||
                impl_->connection_state == connection_state::draining)
                co_return std::unexpected(std::make_error_code(std::errc::not_connected));
            if (impl_->retired_streams.contains(sid))
                co_return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
            if (const auto stream = impl_->streams.find(sid); stream != impl_->streams.end() &&
                (stream->second.value->state() == stream_state::closed ||
                    stream->second.value->state() == stream_state::half_closed_local))
                co_return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
            const bool pending_early = impl_->connection_role == quic_role::client &&
                impl_->tls->early_data_status() == early_data_state::pending;
            const auto frame_count = is_bidirectional(sid)
                ? impl_->peer_max_streams_bidi_from_frames : impl_->peer_max_streams_uni_from_frames;
            if (impl_->peer_transport_parameters_applied || pending_early ||
                sid / 4U + 1U <= frame_count)
                co_return {};
            // The old producer path created this zero-authority stream before
            // returning a flow-control rejection. Keep that cancellation target.
            (void)impl_->ensure_local_stream(sid);
        }
        // No queue slot or state lock is retained while negotiation is pending.
        co_await async_sleep(impl_->ctx, std::chrono::milliseconds{1});
    }
}

auto quic_connection::async_send_fragments(stream_id sid,
    std::vector<std::byte> owned_data, bool fin)
    -> task<std::expected<void, std::error_code>>
{
    constexpr std::size_t fragment_size = max_udp_payload - 96U;
    // A complete application write need not fit the peer's current
    // stream/connection credit. Submit bounded fragments, allowing the
    // receive driver to process MAX_DATA/MAX_STREAM_DATA between them.
    // Keep the small-write path below unchanged.
    std::size_t offset{};
    while (offset < owned_data.size())
    {
        std::uint64_t credit{};
        {
            co_await impl_->receive_mutex.lock();
            async_lock_guard guard{impl_->receive_mutex, std::adopt_lock};
            if (is_closed() || impl_->connection_state == connection_state::draining)
                co_return std::unexpected(std::make_error_code(std::errc::not_connected));
            const auto stream = impl_->streams.find(sid);
            if (stream == impl_->streams.end())
                co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
            if (stream->second.value->state() == stream_state::closed ||
                stream->second.value->state() == stream_state::half_closed_local)
                co_return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
            const auto connection_credit = impl_->peer_max_data > impl_->sent_stream_data
                ? impl_->peer_max_data - impl_->sent_stream_data
                : 0U;
            credit = std::min(connection_credit,
                stream->second.value->remaining_send_window());
        }
        if (credit == 0U)
        {
            co_await async_sleep(impl_->ctx, std::chrono::milliseconds{1});
            continue;
        }
        const auto count = std::min<std::size_t>({fragment_size,
            owned_data.size() - offset, static_cast<std::size_t>(credit)});
        std::vector<std::byte> fragment(owned_data.begin() + offset,
            owned_data.begin() + offset + count);
        const auto sent = co_await async_send(sid, std::move(fragment),
            fin && offset + count == owned_data.size());
        if (!sent)
        {
            // Another producer may consume credit after the snapshot.
            // Retry only a rejected write; no bytes have been committed.
            if (sent.error() != make_error_code(quic_errc::flow_control_error))
                co_return std::unexpected(sent.error());
            co_await async_sleep(impl_->ctx, std::chrono::milliseconds{1});
            continue;
        }
        offset += count;
    }
    co_return {};
}

auto quic_connection::async_send(stream_id sid, std::vector<std::byte>&& owned_data,
    bool fin) -> task<std::expected<void, std::error_code>>
{
    // Stream initiator and connection role are immutable; peer responses bypass
    // this coroutine and its state lock entirely.
    if (is_client_initiated(sid) == (impl_->connection_role == quic_role::client))
    {
        const auto authority = co_await await_local_stream_send_authority(sid);
        if (!authority)
            co_return std::unexpected(authority.error());
    }
    for (;;)
    {
        auto* const completion = impl_->write_commands.try_acquire();
        if (!completion)
        {
            if (is_closed() || impl_->connection_state == connection_state::draining)
                co_return std::unexpected(std::make_error_code(std::errc::not_connected));
            co_await async_sleep(impl_->ctx, std::chrono::milliseconds{1});
            continue;
        }
        if (!impl_->write_commands.try_enqueue({quic_write_command::operation::write,
                sid, std::move(owned_data), fin, 0U, false, completion, {}}))
        {
            impl_->write_commands.release(*completion);
            co_return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
        }

        impl_->send_flush_requested.store(true, std::memory_order_release);
        impl_->receive_rearm_requested.store(true, std::memory_order_release);
        if (impl_->receive_wait_token && impl_->receive_wait_token->pending_.load(std::memory_order_acquire))
            impl_->receive_wait_token->cancel();

        // Submit the complete application write first. apply_stream_write()
        // fragments it into UDP-sized STREAM frames under one state lock and
        // one completion. Only a real flow-control rejection needs the slower
        // credit-aware fragment loop below.
        co_await flush_send_queue();
        const auto result = co_await completion->receive();
        owned_data = std::exchange(completion->rejected_bytes, {});
        impl_->write_commands.release(*completion);
        if (impl_->terminal_send_error)
            co_return std::unexpected(impl_->terminal_send_error);
        if (!result && result.error() == make_error_code(quic_errc::flow_control_error))
        {
            // Short HEADERS/DATA writes need the same backpressure as large
            // responses. A transient credit shortage must not abandon a response
            // with no FIN (which previously left the client waiting indefinitely).
            co_return co_await async_send_fragments(sid, std::move(owned_data), fin);
        }
        if (!result)
            co_return std::unexpected(result.error());
        co_return {};
    }
}

auto quic_connection::async_send_shared(stream_id sid,
    std::vector<std::byte>&& prefix,
    std::shared_ptr<const std::string> shared_data, bool fin)
    -> task<std::expected<void, std::error_code>>
{
    if (!shared_data)
        co_return co_await async_send(sid, std::move(prefix), fin);
    // Stream initiator and connection role are immutable; peer responses bypass
    // this coroutine and its state lock entirely.
    if (is_client_initiated(sid) == (impl_->connection_role == quic_role::client))
    {
        const auto authority = co_await await_local_stream_send_authority(sid);
        if (!authority)
            co_return std::unexpected(authority.error());
    }
    for (;;)
    {
        auto* const completion = impl_->write_commands.try_acquire();
        if (!completion)
        {
            if (is_closed() || impl_->connection_state == connection_state::draining)
                co_return std::unexpected(std::make_error_code(std::errc::not_connected));
            co_await async_sleep(impl_->ctx, std::chrono::milliseconds{1});
            continue;
        }
        if (!impl_->write_commands.try_enqueue({quic_write_command::operation::write,
                sid, std::move(prefix), fin, 0U, false, completion,
                std::move(shared_data)}))
        {
            impl_->write_commands.release(*completion);
            co_return std::unexpected(
                std::make_error_code(std::errc::resource_unavailable_try_again));
        }

        impl_->send_flush_requested.store(true, std::memory_order_release);
        impl_->receive_rearm_requested.store(true, std::memory_order_release);
        if (impl_->receive_wait_token && impl_->receive_wait_token->pending_.load(std::memory_order_acquire))
            impl_->receive_wait_token->cancel();
        co_await flush_send_queue();
        const auto result = co_await completion->receive();
        prefix = std::exchange(completion->rejected_bytes, {});
        shared_data = std::exchange(completion->rejected_shared_bytes, {});
        impl_->write_commands.release(*completion);
        if (impl_->terminal_send_error)
            co_return std::unexpected(impl_->terminal_send_error);
        if (!result && result.error() == make_error_code(quic_errc::flow_control_error))
        {
            std::vector<std::byte> owned_data;
            owned_data.reserve(prefix.size() +
                (shared_data ? shared_data->size() : 0U));
            owned_data.insert(owned_data.end(), prefix.begin(), prefix.end());
            if (shared_data)
            {
                const auto bytes = std::as_bytes(
                    std::span{shared_data->data(), shared_data->size()});
                owned_data.insert(owned_data.end(), bytes.begin(), bytes.end());
            }
            co_return co_await async_send_fragments(sid, std::move(owned_data), fin);
        }
        if (!result)
            co_return std::unexpected(result.error());
        co_return {};
    }
}

auto quic_connection::set_stream_priority(stream_id sid, std::uint8_t urgency,
    bool incremental) noexcept -> bool
{
    // Priority updates share the producer path with stream writes.  They are
    // ordered before later writes from the same coroutine and never race the
    // owner while it traverses the RFC 9218 queues.
    if (!impl_->write_commands.try_enqueue({quic_write_command::operation::set_priority,
            sid, {}, false, urgency, incremental, {}, {}}))
        return false;
    impl_->send_flush_requested.store(true, std::memory_order_release);
    impl_->receive_rearm_requested.store(true, std::memory_order_release);
    if (impl_->receive_wait_token && impl_->receive_wait_token->pending_.load(std::memory_order_acquire))
        impl_->receive_wait_token->cancel();
    return true;
}

auto quic_connection::apply_application_datagram(std::vector<std::byte> data)
    -> task<std::expected<void, std::error_code>>
{
    if (is_closed() || impl_->connection_state != connection_state::connected)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    const auto peer_limit = impl_->tls->received_transport_params().max_datagram_frame_size;
    if (impl_->config.max_datagram_frame_size == 0U || peer_limit == 0U)
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    auto frame = encode_frame(datagram_frame{
        std::span<const std::byte>{data.data(), data.size()}, true});
    if (frame.size() > impl_->config.max_datagram_frame_size || frame.size() > peer_limit ||
        frame.size() > max_udp_payload - 64)
        co_return std::unexpected(std::make_error_code(std::errc::message_size));
    // A valid unreliable send may be accepted but dropped locally when its
    // bounded pending queue is full. Keep validation errors above unchanged.
    (void)impl_->unreliable_send_frames.try_push(std::move(frame));
    co_return {};
}

auto quic_connection::async_send_datagram(std::span<const std::byte> data)
    -> task<std::expected<void, std::error_code>>
{
    auto* const completion = impl_->write_commands.try_acquire();
    if (!completion)
        co_return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    std::vector<std::byte> owned_data;
    try
    {
        owned_data.assign(data.begin(), data.end());
    }
    catch (...)
    {
        // No command owns this slot until try_enqueue succeeds.
        impl_->write_commands.release(*completion);
        throw;
    }
    if (!impl_->write_commands.try_enqueue({quic_write_command::operation::datagram,
            {}, std::move(owned_data), false, 0U, false, completion, {}}))
    {
        impl_->write_commands.release(*completion);
        co_return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    }
    impl_->send_flush_requested.store(true, std::memory_order_release);
    impl_->receive_rearm_requested.store(true, std::memory_order_release);
    if (impl_->receive_wait_token && impl_->receive_wait_token->pending_.load(std::memory_order_acquire))
        impl_->receive_wait_token->cancel();
    co_await flush_send_queue();
    const auto result = co_await completion->receive();
    impl_->write_commands.release(*completion);
    if (impl_->terminal_send_error)
        co_return std::unexpected(impl_->terminal_send_error);
    if (!result)
        co_return std::unexpected(result.error());
    co_return {};
}

auto quic_connection::async_receive_datagram()
    -> task<std::expected<std::vector<std::byte>, std::error_code>>
{
    auto value = co_await impl_->received_application_datagrams.receive();
    if (!value)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return std::move(*value);
}

auto quic_connection::async_recv(stream_id sid, mutable_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_await impl_->receive_mutex.lock();
    async_lock_guard guard{impl_->receive_mutex, std::adopt_lock};
    const auto it = impl_->streams.find(sid);
    if (it == impl_->streams.end())
        co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    auto received = it->second.value->apply_receive_state(buffer);
    if (!received || *received == 0)
        co_return received;

    impl_->return_receive_credit(*received);
    const auto consumed = it->second.value->bytes_consumed();
    if (it->second.value->remaining_receive_window() <= impl_->config.max_stream_data / 2)
    {
        const auto new_limit = consumed + impl_->config.max_stream_data;
        it->second.value->extend_receive_limit(new_limit);
        impl_->encoded_send_frames.push_back(encode_frame(max_stream_data_frame{
            sid, new_limit}));
    }
    guard.release();
    impl_->receive_mutex.unlock();
    co_await flush_send_queue();
    co_return received;
}

auto quic_connection::async_wait_readable(stream_id sid)
    -> task<std::expected<void, std::error_code>>
{
    if (is_closed())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));

    const auto stream = impl_->streams.find(sid);
    if (stream == impl_->streams.end())
        co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));

    // A stream may have become readable between async_recv() returning
    // would_block and this call.  Checking before awaiting avoids a lost
    // notification and leaves the hot path allocation-free.
    if (stream->second.value->is_readable())
        co_return {};

    auto readiness = stream->second.readiness;
    const auto notification = co_await readiness->receive();
    if (!notification)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return {};
}

auto quic_connection::async_wait_readable(stream_id sid, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (is_closed())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));

    const auto stream = impl_->streams.find(sid);
    if (stream == impl_->streams.end())
        co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    if (stream->second.value->is_readable())
        co_return {};

    auto readiness = stream->second.readiness;
    stream_wait_cancellation cancellation{impl_->ctx, token, readiness};
    const auto notification = co_await readiness->receive();
    co_await cancellation;
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (!notification)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return {};
}

auto quic_connection::async_cancel_stream(stream_id sid,
    std::uint64_t application_error_code)
    -> task<std::expected<void, std::error_code>>
{
    co_await impl_->receive_mutex.lock();
    async_lock_guard guard{impl_->receive_mutex, std::adopt_lock};
    if (is_closed())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    const auto stream = impl_->streams.find(sid);
    if (stream == impl_->streams.end())
        co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));

    if (stream->second.has_deferred_reset || impl_->can_defer_local_zero_send(sid, stream->second))
    {
        // Keep only the stream's zero-final-size cancellation intent. Waiting
        // producers retain their own payload and observe the closed state.
        const bool already_abandoned = stream->second.value->receive_abandoned();
        stream->second.value->stop_local();
        stream->second.value->abandon_receive();
        // Cancellation abandons delivery; it cannot invent a remote final size.
        // Abandonment closes receive only when FIN/RESET already established it.
        // Otherwise the normal receive path accounts and retires on peer final.
        if (!already_abandoned && stream->second.value->state() == stream_state::closed)
        {
            const auto received = stream->second.value->bytes_received();
            const auto discarded = received - stream->second.value->bytes_consumed();
            (void)stream->second.value->reset_remote(received);
            impl_->return_receive_credit(discarded);
        }
        if (!stream->second.has_deferred_reset)
            stream->second.deferred_reset_error = application_error_code;
        stream->second.has_deferred_reset = true;
        stream->second.deferred_reset_stop_peer = is_bidirectional(sid);
        impl_->has_deferred_stream_cancellations = true;
        impl_->send_flush_requested.store(true, std::memory_order_release);
        auto readiness = stream->second.readiness;
        (void)readiness->try_send({});
        guard.release();
        impl_->receive_mutex.unlock();
        co_await flush_send_queue();
        co_return {};
    }

    const auto final_size = stream->second.value->bytes_sent();
    stream->second.value->stop_local();
    stream->second.value->abandon_receive();
    const bool fully_closed = stream->second.value->state() == stream_state::closed;
    if (fully_closed)
    {
        const auto final_received = stream->second.value->bytes_received();
        const auto discarded = final_received - stream->second.value->bytes_consumed();
        (void)stream->second.value->reset_remote(final_received);
        impl_->return_receive_credit(discarded);
    }
    const bool local = is_client_initiated(sid) ==
        (impl_->connection_role == quic_role::client);
    if (is_bidirectional(sid) || local)
        impl_->encoded_send_frames.push_back(encode_frame(reset_stream_frame{
            sid, application_error_code, final_size}));
    if (is_bidirectional(sid) || !local)
        impl_->encoded_send_frames.push_back(encode_frame(stop_sending_frame{
            sid, application_error_code}));
    auto readiness = stream->second.readiness;
    if (fully_closed)
        (void)retire_stream(sid);
    (void)readiness->try_send({});
    guard.release();
    impl_->receive_mutex.unlock();
    co_await flush_send_queue();
    co_return {};
}

auto quic_connection::is_stream_cancelled(stream_id sid) const noexcept -> bool
{
    return impl_->remotely_cancelled_streams.contains(sid) ||
        impl_->connection_state == connection_state::closing ||
        impl_->connection_state == connection_state::draining ||
        impl_->connection_state == connection_state::closed;
}

void quic_connection::register_stream_cancellation(stream_id sid,
    cancel_token& token) noexcept
{
    if (impl_->remotely_cancelled_streams.contains(sid) ||
        impl_->connection_state == connection_state::closing ||
        impl_->connection_state == connection_state::draining ||
        impl_->connection_state == connection_state::closed)
    {
        token.cancel();
        return;
    }
    impl_->stream_cancellation_observers.insert_or_assign(sid, std::addressof(token));
}

void quic_connection::unregister_stream_cancellation(stream_id sid) noexcept
{
    impl_->stream_cancellation_observers.erase(sid);
}

auto quic_connection::async_open_stream(bool bidirectional)
    -> task<std::expected<stream_id, std::error_code>>
{
    co_await impl_->receive_mutex.lock();
    async_lock_guard guard{impl_->receive_mutex, std::adopt_lock};
    if (is_closed() || impl_->connection_state == connection_state::closing ||
        impl_->connection_state == connection_state::draining)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    auto& next = bidirectional ? impl_->next_bidi_stream : impl_->next_uni_stream;
    const auto id = next;
    if (id > ((std::uint64_t{1} << 62U) - 1U))
        co_return std::unexpected(make_error_code(quic_errc::stream_limit_error));
    const auto opened_count = id / 4 + 1;
    const auto limit = bidirectional ? impl_->peer_max_streams_bidi : impl_->peer_max_streams_uni;
    if (opened_count > limit)
        co_return std::unexpected(make_error_code(quic_errc::stream_limit_error));
    (void)impl_->ensure_local_stream(id);
    impl_->pending_priority_streams.erase(id);
    co_return id;
}

auto quic_connection::async_accept_stream()
    -> task<std::expected<stream_id, std::error_code>>
{
    if (is_closed() || impl_->connection_state == connection_state::draining)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    auto stream = co_await impl_->accepted_streams.receive();
    if (!stream)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return *stream;
}

auto quic_connection::retire_stream(stream_id sid)
    -> std::expected<void, std::error_code>
{
    const auto stream = impl_->streams.find(sid);
    if (stream == impl_->streams.end())
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    if ((stream->second.has_deferred_reset &&
            (!stream->second.deferred_reset_queued ||
                (stream->second.deferred_reset_stop_peer && !stream->second.deferred_stop_queued))) ||
        stream->second.value->state() != stream_state::closed)
        return std::unexpected(std::make_error_code(std::errc::operation_in_progress));

    bool credit_updated{};
    if (is_client_initiated(sid) != (impl_->connection_role == quic_role::client))
    {
        const auto bidirectional = is_bidirectional(sid);
        auto& limit = bidirectional ? impl_->local_max_streams_bidi
                                    : impl_->local_max_streams_uni;
        const auto configured_window = bidirectional
            ? impl_->config.max_streams_bidi
            : impl_->config.max_streams_uni;
        auto& retired = bidirectional ? impl_->retired_peer_streams_bidi
                                     : impl_->retired_peer_streams_uni;
        constexpr auto maximum_streams = std::uint64_t{1} << 60U;
        if (retired < maximum_streams)
            ++retired;
        const auto window = std::min(configured_window, maximum_streams);
        const auto available = window + std::min(retired, maximum_streams - window);
        const auto replenish_threshold = std::max<std::uint64_t>(1U,
            window / 2U);
        // MAX_STREAMS is an absolute, monotonic credit limit.  Updating it for
        // every completed request creates one retransmittable control frame
        // per request and makes a long connection walk old credit updates on
        // PTO. Batch actual retirements; a high stream ID does not release
        // the credit still occupied by unfinished lower-numbered streams.
        if (available > limit && available - limit >= replenish_threshold)
        {
            limit = available;
            credit_updated = true;
            impl_->encoded_send_frames.push_back(encode_frame(max_streams_frame{limit,
                bidirectional}));
        }
    }
    impl_->retired_streams.insert(sid,
        retired_stream_history::final_sizes{
            stream->second.value->bytes_received(), stream->second.value->bytes_sent()});
    impl_->remotely_cancelled_streams.erase(sid);
    impl_->stream_scheduler.retire_stream(sid);
    impl_->pending_priority_streams.erase(sid);
    auto readiness = stream->second.readiness;
    impl_->streams.erase(stream);
    auto* const context = std::addressof(impl_->ctx);
    std::unique_ptr<std::function<void()>> notification;
    if (credit_updated)
    {
        impl_->send_flush_requested.store(true, std::memory_order_release);
        if (impl_->timer_deadline_observer)
        {
            // Retirement also runs inside packet parsing. Carry only the
            // immutable weak observer across the deferred control-plane edge.
            notification = std::make_unique<std::function<void()>>(
                [observer = impl_->timer_deadline_observer,
                    deadline = std::chrono::steady_clock::now()] { observer(deadline); });
        }
        else if (const auto token = impl_->receive_wait_token;
            token && token->pending_.load(std::memory_order_acquire))
        {
            impl_->receive_rearm_requested.store(true, std::memory_order_release);
            token->request_cancel();
            notification = std::make_unique<std::function<void()>>(
                [token] { token->dispatch_cancel(); });
        }
    }
    readiness->close();
    // Completion can resume application code synchronously. No connection
    // storage is accessed after notifying readiness.
    if (notification)
    {
        context->post([](void* value)
            {
                // post's cleanup owns only callbacks discarded before dispatch.
                // A dispatched callback must release its notification itself.
                std::unique_ptr<std::function<void()>> owned{
                    static_cast<std::function<void()>*>(value)};
                (*owned)();
            },
            notification.get(), [](void* value) { delete static_cast<std::function<void()>*>(value); });
        (void)notification.release();
    }
    return {};
}

auto quic_connection::context() noexcept -> io_context&
{
    return impl_->ctx;
}

auto quic_connection::async_close(std::error_code error, std::string_view reason,
    bool application_error) -> task<void>
{
    co_await impl_->receive_mutex.lock();
    async_lock_guard guard{impl_->receive_mutex, std::adopt_lock};
    if (is_closed() || impl_->connection_state == connection_state::draining)
        co_return;

    // A close frame must not remain behind application data or a saturated
    // congestion window. It is not retransmittable application data and the
    // peer needs it promptly to release listener-owned connection state.
    impl_->stream_scheduler = quic_stream_scheduler{};
    impl_->pending_priority_streams.clear();
    impl_->send_queue.clear();
    impl_->encoded_send_frames.clear();
    impl_->unreliable_send_frames.clear();
    impl_->early_data_send_frames.clear();
    impl_->write_commands.fail_all(std::make_error_code(std::errc::operation_canceled));
    impl_->encoded_send_frames.push_back(encode_frame(connection_close_frame{
        static_cast<std::uint64_t>(error.value()), 0, std::string(reason), application_error}));
    impl_->connection_state = connection_state::closing;
    guard.release();
    impl_->receive_mutex.unlock();
    co_await flush_send_queue();
    // flush_send_queue() can yield ownership to an existing writer. Join it
    // before reclaiming the queued CLOSE or its packet/recovery storage.
    co_await impl_->flush_mutex.lock();
    async_lock_guard writer_join{impl_->flush_mutex, std::adopt_lock};
    if (!is_closed())
        close_transport(error ? error : std::make_error_code(std::errc::operation_canceled));
}

auto quic_connection::async_probe_path(std::uint32_t path_id, endpoint peer)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_probe_path_impl(path_id, std::move(peer), nullptr);
}

auto quic_connection::async_probe_path(std::uint32_t path_id, endpoint peer,
    udp::udp_socket& local_socket) -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_probe_path_impl(path_id, std::move(peer),
        std::addressof(local_socket));
}

auto quic_connection::async_probe_path_impl(std::uint32_t path_id, endpoint peer,
    udp::udp_socket* path_socket) -> task<std::expected<void, std::error_code>>
{
    std::vector<std::byte> probe;
    std::array<std::byte, 8> challenge{};
    const auto key = peer.to_string();
    udp::udp_socket* selected_path_socket = path_socket;
    auto retry_interval = std::chrono::milliseconds{20};
    auto validation_deadline = std::chrono::steady_clock::now();
    {
        co_await impl_->receive_mutex.lock();
        quic_connection_impl::listener_state_guard guard{*this, *impl_};
        if (!impl_->multipath_negotiated || path_id == 0U ||
            !impl_->config.multipath_initial_max_path_id ||
            path_id > *impl_->config.multipath_initial_max_path_id)
            co_return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
        const auto path = impl_->application_paths.find(path_id);
        if (path == impl_->application_paths.end() || path->second->locally_abandoned ||
            path->second->peer_abandoned || !impl_->path_peer_connection_id(*path->second))
            co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        if (path_socket && (!path_socket->is_open() || std::addressof(path_socket->context()) != std::addressof(impl_->socket_context)))
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (path_socket)
            path->second->path_socket = path_socket;
        else
            selected_path_socket = path->second->path_socket;
        bool inserted_validation{};
        if (const auto pending = impl_->pending_path_validations.find(key);
            pending != impl_->pending_path_validations.end())
        {
            // Path CID control frames and the first PATH_CHALLENGE can cross
            // on the wire. Re-send the same opaque challenge once the peer
            // CID becomes available; this is idempotent and avoids turning
            // that legal reordering into a permanently unvalidated path.
            if (pending->second.path_id != path_id)
                co_return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
            challenge = pending->second.challenge;
        }
        else
        {
            if (impl_->pending_path_validations.size() >= std::max<std::size_t>(
                                                              1U, impl_->config.max_pending_path_validations))
                co_return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
            if (RAND_bytes(reinterpret_cast<unsigned char*>(challenge.data()),
                    static_cast<int>(challenge.size())) != 1)
                co_return std::unexpected(std::make_error_code(std::errc::io_error));
            impl_->pending_path_validations.emplace(key,
                quic_connection_impl::path_validation{peer, challenge,
                    std::chrono::steady_clock::now(), path_id});
            inserted_validation = true;
        }
        path->second->validated = false;
        // Validation is a control-plane exchange, not merely a successful
        // UDP submission. Bound the wait by three PTOs with the initial-RTT
        // floor, while retrying often enough to survive a lost datagram.
        const auto pto = path->second->recovery.pto_duration();
        validation_deadline = std::chrono::steady_clock::now() +
            impl_->path_validation_timeout(*path->second);
        retry_interval = std::clamp(
            std::chrono::duration_cast<std::chrono::milliseconds>(pto / 2),
            std::chrono::milliseconds{20}, std::chrono::milliseconds{200});
        const auto previous_path = impl_->sending_application_path_id;
        impl_->sending_application_path_id = path_id;
        probe = pack_path_validation_packet(encode_frame(path_challenge_frame{challenge}));
        impl_->sending_application_path_id = previous_path;
        if (probe.empty())
        {
            if (inserted_validation)
                impl_->pending_path_validations.erase(key);
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
    }
    auto sent = co_await send_datagram(probe, peer, selected_path_socket);
    if (!sent && !is_nonfatal_udp_path_error(sent.error()))
        co_return std::unexpected(sent.error());

    for (;;)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= validation_deadline)
        {
            co_await impl_->receive_mutex.lock();
            quic_connection_impl::listener_state_guard guard{*this, *impl_};
            if (const auto pending = impl_->pending_path_validations.find(key);
                pending != impl_->pending_path_validations.end() &&
                pending->second.path_id == path_id && pending->second.challenge == challenge)
                impl_->pending_path_validations.erase(pending);
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        }

        const auto wait = std::min(
            retry_interval,
            std::chrono::duration_cast<std::chrono::milliseconds>(validation_deadline - now));
        const auto waited = co_await async_timer_wait(impl_->ctx, wait);
        if (!waited)
            co_return std::unexpected(waited.error());

        std::vector<std::byte> retry;
        {
            co_await impl_->receive_mutex.lock();
            quic_connection_impl::listener_state_guard guard{*this, *impl_};
            if (impl_->connection_state == connection_state::closed)
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            const auto path = impl_->application_paths.find(path_id);
            if (path == impl_->application_paths.end() || path->second->locally_abandoned ||
                path->second->peer_abandoned)
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            if (path->second->validated)
                co_return {};
            const auto pending = impl_->pending_path_validations.find(key);
            if (pending == impl_->pending_path_validations.end() ||
                pending->second.path_id != path_id || pending->second.challenge != challenge)
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));

            const auto previous_path = impl_->sending_application_path_id;
            impl_->sending_application_path_id = path_id;
            retry = pack_path_validation_packet(
                encode_frame(path_challenge_frame{challenge}));
            impl_->sending_application_path_id = previous_path;
            if (retry.empty())
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        sent = co_await send_datagram(retry, peer, selected_path_socket);
        if (!sent && !is_nonfatal_udp_path_error(sent.error()))
            co_return std::unexpected(sent.error());
    }
}

auto quic_connection::set_path_backup(std::uint32_t path_id, bool backup)
    -> std::expected<void, std::error_code>
{
    if (!impl_->multipath_negotiated)
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    const auto path = impl_->application_paths.find(path_id);
    if (path == impl_->application_paths.end() || path->second->locally_abandoned ||
        path->second->peer_abandoned)
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    path->second->locally_marks_backup = backup;
    ++path->second->next_local_path_status_sequence;
    impl_->encoded_send_frames.push_back(encode_frame(path_status_frame{
        path_id, path->second->next_local_path_status_sequence, backup}));
    impl_->send_flush_requested.store(true, std::memory_order_release);
    if (impl_->timer_deadline_observer)
        impl_->timer_deadline_observer(next_timer_deadline());
    else if (impl_->receive_wait_token && impl_->receive_wait_token->pending_.load(std::memory_order_acquire))
        impl_->receive_wait_token->cancel();
    return {};
}

auto quic_connection::async_abandon_path(std::uint32_t path_id,
    std::uint64_t error_code) -> task<std::expected<void, std::error_code>>
{
    co_await impl_->receive_mutex.lock();
    quic_connection_impl::listener_state_guard guard{*this, *impl_};
    if (!impl_->multipath_negotiated || path_id == 0U)
        co_return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    const auto path = impl_->application_paths.find(path_id);
    if (path == impl_->application_paths.end() || path->second->locally_abandoned ||
        path->second->peer_abandoned)
        co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));

    // The peer's CIDs for this Path ID are immediately retired locally.  Keep
    // our local CID routes and recovery state until the PTO grace deadline so
    // in-flight peer packets are discarded safely rather than misrouted.
    path->second->locally_abandoned = true;
    impl_->retired_path_ids.insert(path_id);
    path->second->peer_connection_id.reset();
    path->second->active_peer_cid_sequence.reset();
    impl_->peer_path_connection_ids.erase(path_id);
    path->second->abandonment_deadline = std::chrono::steady_clock::now() +
        path->second->recovery.pto_duration() * 3;
    impl_->encoded_send_frames.push_back(encode_frame(path_abandon_frame{
        path_id, error_code}));
    impl_->send_flush_requested.store(true, std::memory_order_release);
    guard.unlock_and_notify();
    if (!impl_->timer_deadline_observer && impl_->receive_wait_token &&
        impl_->receive_wait_token->pending_.load(std::memory_order_acquire))
        impl_->receive_wait_token->cancel();
    co_return {};
}

auto quic_connection::discovered_path_mtu(std::uint32_t path_id) const
    -> std::optional<std::size_t>
{
    const auto path = impl_->application_paths.find(path_id);
    if (path == impl_->application_paths.end())
        return std::nullopt;
    return path->second->discovered_max_datagram_payload;
}

auto quic_connection::path_is_validated(std::uint32_t path_id) const noexcept -> bool
{
    const auto path = impl_->application_paths.find(path_id);
    return path != impl_->application_paths.end() && path->second->validated &&
        !path->second->locally_abandoned && !path->second->peer_abandoned;
}

auto quic_connection::local_path_endpoint(std::uint32_t path_id)
    -> std::expected<endpoint, std::error_code>
{
    const auto path = impl_->application_paths.find(path_id);
    if (path == impl_->application_paths.end())
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    auto* socket = path->second->path_socket ? path->second->path_socket : impl_->socket;
    return socket->native_socket().local_endpoint();
}

auto quic_connection::register_cid(connection_id cid) -> std::expected<void, std::error_code>
{
    if (cid.empty() || cid.size() > max_cid_length)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto [_, inserted] = impl_->cids.emplace(cid, this);
    if (!inserted)
        return std::unexpected(std::make_error_code(std::errc::file_exists));
    impl_->local_connection_id = std::move(cid);
    impl_->local_cid_path_ids.emplace(*impl_->local_connection_id, 0U);
    if (impl_->local_connection_ids.empty())
    {
        std::array<std::byte, 16> reset_token{};
        if (impl_->connection_role == quic_role::server)
        {
            auto generated = make_stateless_reset_token(impl_->config, *impl_->local_connection_id);
            if (!generated)
            {
                impl_->cids.erase(*impl_->local_connection_id);
                impl_->local_connection_id.reset();
                return std::unexpected(generated.error());
            }
            reset_token = *generated;
        }
        impl_->local_connection_ids.emplace(0U,
            quic_connection_impl::local_cid_info{*impl_->local_connection_id, reset_token});
        impl_->local_cid_route_generation.fetch_add(1U, std::memory_order_release);
    }
    return {};
}

auto quic_connection::unregister_cid(connection_id cid) -> task<void>
{
    impl_->cids.erase(cid);
    impl_->local_cid_path_ids.erase(cid);
    const auto erased = std::erase_if(impl_->local_connection_ids, [&cid](const auto& entry)
        {
            return entry.second.cid == cid;
        });
    if (erased != 0U)
        impl_->local_cid_route_generation.fetch_add(1U, std::memory_order_release);
    if (impl_->local_connection_id && *impl_->local_connection_id == cid)
        impl_->local_connection_id.reset();
    co_return;
}

auto quic_connection::close_stream_readiness() noexcept -> void
{
    std::vector<std::shared_ptr<channel<std::monostate>>> readiness;
    readiness.reserve(impl_->streams.size());
    for (const auto& [_, stream] : impl_->streams)
        readiness.push_back(stream.readiness);
    // Pop only the observer being called. A synchronous completion can
    // unregister another token; it must remove that still-pending observer
    // from this live map before its owner is destroyed.
    while (!impl_->stream_cancellation_observers.empty())
    {
        const auto observer = impl_->stream_cancellation_observers.begin();
        auto* token = observer->second;
        impl_->stream_cancellation_observers.erase(observer);
        token->cancel();
    }
    for (const auto& latch : readiness)
        latch->close();
    impl_->received_application_datagrams.close();
}

auto quic_connection::close_transport(std::error_code error, connection_state state) noexcept -> void
{
    impl_->connection_state = state;
    impl_->terminal_send_error = error;
    impl_->send_queue.clear();
    impl_->encoded_send_frames.clear();
    impl_->unreliable_send_frames.clear();
    impl_->early_data_send_frames.clear();
    impl_->stream_scheduler = quic_stream_scheduler{};
    impl_->pending_priority_streams.clear();
    for (const auto space : {pn_space::initial, pn_space::handshake, pn_space::application})
        impl_->recovery.discard_packet_number_space(space);
    for (auto& [_, path] : impl_->application_paths)
        path->recovery.discard_packet_number_space(pn_space::application);
    impl_->write_commands.fail_all(error);
    impl_->accepted_streams.close();
    close_stream_readiness();
    impl_->streams.clear();
    impl_->has_deferred_stream_cancellations = false;
    impl_->remotely_cancelled_streams.clear();
    if (state == connection_state::draining)
        return;
    impl_->receive_rearm_requested.store(false, std::memory_order_release);
    if (impl_->receive_wait_token)
        impl_->receive_wait_token->cancel();
    if (impl_->owned_socket)
        impl_->socket->close();
}

auto quic_connection::is_closed() const noexcept -> bool
{
    return impl_->connection_state == connection_state::closed;
}

auto quic_connection::state() const noexcept -> connection_state
{
    return impl_->connection_state;
}

auto quic_connection::datagrams_configured() const noexcept -> bool
{
    return impl_->config.max_datagram_frame_size != 0U;
}

auto quic_connection::native_socket() -> udp::udp_socket&
{
    return *impl_->socket;
}

auto quic_connection::peer_endpoint() const noexcept -> const endpoint&
{
    return impl_->peer;
}

auto quic_connection::role() const noexcept -> quic_role
{
    return impl_->connection_role;
}

auto quic_connection::local_cid() const noexcept -> const connection_id*
{
    return impl_->local_connection_id ? std::addressof(*impl_->local_connection_id) : nullptr;
}

auto quic_connection::local_cids() const -> std::vector<connection_id>
{
    std::vector<connection_id> result;
    std::size_t path_count{};
    for (const auto& [_, path_cids] : impl_->local_path_connection_ids)
        path_count += path_cids.size();
    result.reserve(impl_->local_connection_ids.size() + path_count);
    for (const auto& [_, info] : impl_->local_connection_ids)
        result.push_back(info.cid);
    for (const auto& [_, path_cids] : impl_->local_path_connection_ids)
        for (const auto& [_, info] : path_cids)
            result.push_back(info.cid);
    return result;
}

auto quic_connection::local_cid_routes() const -> std::vector<local_cid_route>
{
    std::vector<local_cid_route> result;
    std::size_t path_count{};
    for (const auto& [_, path_cids] : impl_->local_path_connection_ids)
        path_count += path_cids.size();
    result.reserve(impl_->local_connection_ids.size() + path_count);
    for (const auto& [_, info] : impl_->local_connection_ids)
        result.push_back({info.cid, info.stateless_reset_token});
    // The shared HTTP/3 listener routes solely by DCID. Non-zero Path IDs
    // have their own CID sequence spaces, so omitting them here makes every
    // valid first multipath packet look like an unknown connection.
    for (const auto& [_, path_cids] : impl_->local_path_connection_ids)
        for (const auto& [_, info] : path_cids)
            result.push_back({info.cid, info.stateless_reset_token});
    return result;
}

auto quic_connection::local_cid_route_generation() const noexcept -> std::uint64_t
{
    return impl_->local_cid_route_generation.load(std::memory_order_acquire);
}

auto quic_connection::take_retired_local_cid_routes() -> std::vector<local_cid_route>
{
    std::vector<local_cid_route> result;
    result.reserve(impl_->retired_local_connection_ids.size());
    for (const auto& info : impl_->retired_local_connection_ids)
        result.push_back({info.cid, info.stateless_reset_token});
    impl_->retired_local_connection_ids.clear();
    return result;
}

auto quic_connection::set_original_destination_connection_id(connection_id cid)
    -> std::expected<void, std::error_code>
{
    if (impl_->connection_role != quic_role::server || impl_->connection_state != connection_state::idle ||
        cid.empty() || cid.size() > max_cid_length)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!impl_->local_connection_id)
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    auto configured = impl_->tls->configure_retry_transport_parameters(cid,
        *impl_->local_connection_id);
    if (!configured)
        return std::unexpected(configured.error());
    impl_->original_destination_id = std::move(cid);
    return {};
}

void quic_connection::schedule_idle_timeout()
{
    if (impl_->config.idle_timeout <= std::chrono::milliseconds::zero())
    {
        impl_->idle_deadline.reset();
        return;
    }
    impl_->idle_deadline = std::chrono::steady_clock::now() + impl_->config.idle_timeout;
}

void quic_connection::handle_idle_timeout()
{
    // RFC 9000 §10.1: an idle timeout closes the connection without sending
    // a CONNECTION_CLOSE frame, because the peer may already be unreachable.

    close_transport(std::make_error_code(std::errc::timed_out));
}

auto quic_connection::can_write_to_stream(stream_id sid) noexcept -> bool
{
    return impl_->streams.contains(sid) && !is_closed();
}

} // namespace cnetmod::quic

    #endif
#endif
