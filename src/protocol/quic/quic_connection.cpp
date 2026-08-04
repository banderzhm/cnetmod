module;

#include <cnetmod/config.hpp>

#include <cstdio>
#include <openssl/rand.h>

module cnetmod.protocol.quic;

import std;

#ifdef CNETMOD_HAS_SSL
    #ifdef CNETMOD_ENABLE_QUIC

import :connection;
import :stream;

namespace cnetmod::quic {

namespace {

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
        params.initial_max_data = config.max_data;
        params.initial_max_stream_data_bidi_local = config.max_stream_data;
        params.initial_max_stream_data_bidi_remote = config.max_stream_data;
        params.initial_max_stream_data_uni = config.max_stream_data;
        params.initial_max_streams_bidi = config.max_streams_bidi;
        params.initial_max_streams_uni = config.max_streams_uni;
        params.active_connection_id_limit = config.active_connection_id_limit;
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
    };

    std::map<std::uint64_t, local_cid_info> local_connection_ids;
    std::vector<local_cid_info> retired_local_connection_ids;
    std::uint64_t next_local_cid_sequence{1};

    struct peer_cid_info
    {
        connection_id cid;
        std::array<std::byte, 16> stateless_reset_token{};
    };

    std::map<std::uint64_t, peer_cid_info> peer_connection_ids;
    std::uint64_t peer_retire_prior_to{};
    std::optional<std::uint64_t> active_peer_cid_sequence;
    std::optional<std::array<std::byte, 8>> outstanding_path_challenge;
    std::optional<endpoint> candidate_path;
    std::optional<time_point> candidate_path_started;
    std::optional<endpoint> current_packet_sender;
    std::map<stream_id, std::unique_ptr<quic_stream>> streams;
    // A one-token channel per stream is an edge-triggered readiness latch.
    // It is intentionally bounded: a fast peer cannot accumulate unbounded
    // wake notifications while an application is processing a previous read.
    std::map<stream_id, std::unique_ptr<channel<std::monostate>>> readable_streams;

    struct retired_stream_info
    {
        std::uint64_t received_final_size{};
        std::uint64_t sent_final_size{};
    };

    std::unordered_map<stream_id, retired_stream_info> retired_streams;
    channel<stream_id> accepted_streams;
    std::uint64_t peer_max_data;
    std::uint64_t sent_stream_data{};
    std::uint64_t local_advertised_max_data;
    std::uint64_t received_stream_data{};
    std::uint64_t locally_consumed_data{};
    std::uint64_t peer_max_streams_bidi;
    std::uint64_t peer_max_streams_uni;
    std::uint64_t local_max_streams_bidi;
    std::uint64_t local_max_streams_uni;
    bool peer_transport_parameters_applied{};
    std::uint64_t next_bidi_stream;
    std::uint64_t next_uni_stream;
    std::deque<quic_frame_variant> send_queue;
    std::deque<std::vector<std::byte>> encoded_send_frames;
    // Packet parsing and socket writes are separate phases.  A connection
    // may have many concurrent stream producers, but packet number,
    // congestion and pacing state have exactly one writer.
    bool send_flush_active{};
    bool send_flush_requested{};
    // 0-RTT bytes are deliberately isolated from normal application frames.
    // A rejected offer is never replayed by the transport: only the HTTP
    // layer knows whether a request is idempotent and may retry it at 1-RTT.
    std::deque<std::vector<std::byte>> early_data_send_frames;

    struct sent_packet_metadata
    {
        std::vector<std::vector<std::byte>> retransmittable_frames;
        std::size_t bytes{};
    };

    std::array<std::map<std::uint64_t, sent_packet_metadata>,
        encryption_level_count>
        sent_packets{};
    loss_detector recovery;
    new_reno_congestion_controller congestion;
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
        : ctx(context), socket_context(context), owned_socket(std::move(udp_socket)), receive_storage(max_udp_receive_payload), socket(std::addressof(*owned_socket)), peer(std::move(remote)), connection_role(role), config(options), accepted_streams(std::max<std::uint64_t>(1U, std::min(options.max_streams_bidi, std::uint64_t{1024}) + std::min(options.max_streams_uni, std::uint64_t{1024}))), peer_max_data(options.max_data), local_advertised_max_data(options.max_data), peer_max_streams_bidi(options.max_streams_bidi), peer_max_streams_uni(options.max_streams_uni), local_max_streams_bidi(options.max_streams_bidi), local_max_streams_uni(options.max_streams_uni), next_bidi_stream(role == quic_role::client ? 0U : 1U), next_uni_stream(role == quic_role::client ? 2U : 3U), recovery(options), congestion(options)
    {
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
        : ctx(context), socket_context(datagram_context), socket(std::addressof(shared_socket)), peer(std::move(remote)), connection_role(role), config(options), accepted_streams(std::max<std::uint64_t>(1U, std::min(options.max_streams_bidi, std::uint64_t{1024}) + std::min(options.max_streams_uni, std::uint64_t{1024}))), peer_max_data(options.max_data), local_advertised_max_data(options.max_data), peer_max_streams_bidi(options.max_streams_bidi), peer_max_streams_uni(options.max_streams_uni), local_max_streams_bidi(options.max_streams_bidi), local_max_streams_uni(options.max_streams_uni), next_bidi_stream(role == quic_role::client ? 0U : 1U), next_uni_stream(role == quic_role::client ? 2U : 3U), recovery(options), congestion(options)
    {
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
    const endpoint& destination)
    -> task<std::expected<std::size_t, std::error_code>>
{
    const bool switch_context =
        std::addressof(impl_->ctx) != std::addressof(impl_->socket_context);
        #ifdef CNETMOD_HAS_IOCP
    if (switch_context)
    {
        co_return co_await async_sendto_on(impl_->socket_context, impl_->ctx,
            impl_->socket->native_socket(),
            const_buffer{datagram.data(), datagram.size()}, destination);
    }
        #endif
    if (switch_context)
        co_await post_awaitable{impl_->socket_context};

    auto sent = co_await async_sendto(impl_->socket_context,
        impl_->socket->native_socket(),
        const_buffer{datagram.data(), datagram.size()}, destination);

    if (switch_context)
        co_await post_awaitable{impl_->ctx};
    co_return sent;
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
    return impl_->tls->early_data_status();
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
    {
        impl_->connection_state = connection_state::closed;
        impl_->accepted_streams.close();
        close_stream_readiness();
    }
    co_return result;
}

auto quic_connection::process_datagram(std::span<const std::byte> datagram,
    const endpoint& sender) -> task<std::expected<void, std::error_code>>
{
    if (impl_->connection_state == connection_state::draining)
    {
        if (impl_->draining_deadline && std::chrono::steady_clock::now() >= *impl_->draining_deadline)
        {
            impl_->connection_state = connection_state::closed;
            impl_->accepted_streams.close();
            close_stream_readiness();
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
    if (!impl_->encoded_send_frames.empty() ||
        !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::application)].empty())
        impl_->send_flush_requested = true;
    co_return {};
}

auto quic_connection::async_poll_timers() -> task<void>
{
    if (is_closed())
        co_return;

    const auto now = std::chrono::steady_clock::now();
    if (std::getenv("CNETMOD_QUIC_DIAG") != nullptr &&
        (!impl_->next_diagnostic_snapshot || now >= *impl_->next_diagnostic_snapshot))
    {
        impl_->next_diagnostic_snapshot = now + std::chrono::seconds{1};
        const auto application_level = level_index(encryption_level::application);
        std::fprintf(stderr,
            "QUIC state=%u streams=%zu retired=%zu queued=%zu sent=%zu " "recovery_packets=%zu recovery_bytes=%llu congestion_bytes=%llu " "cwnd=%llu pending_acks=%zu next_send_pn=%llu largest_recv_pn=%llu\n",
            static_cast<unsigned>(impl_->connection_state), impl_->streams.size(),
            impl_->retired_streams.size(), impl_->encoded_send_frames.size(),
            impl_->sent_packets[application_level].size(),
            impl_->recovery.in_flight_packet_count(pn_space::application),
            static_cast<unsigned long long>(
                impl_->recovery.bytes_in_flight(pn_space::application)),
            static_cast<unsigned long long>(impl_->congestion.bytes_in_flight()),
            static_cast<unsigned long long>(impl_->congestion.congestion_window()),
            impl_->received_ack_eliciting_packet_numbers[application_level].size(),
            static_cast<unsigned long long>(
                impl_->next_send_packet_number[application_level]),
            static_cast<unsigned long long>(
                impl_->largest_received_packet_number[application_level].value_or(0U)));
        if (!impl_->streams.empty())
        {
            std::fprintf(stderr, "QUIC open streams:");
            for (const auto& [sid, stream] : impl_->streams)
                std::fprintf(stderr, " %llu:%u",
                    static_cast<unsigned long long>(sid),
                    static_cast<unsigned>(stream->state()));
            std::fputc('\n', stderr);
        }
    }
    if (impl_->connection_state == connection_state::draining)
    {
        if (impl_->draining_deadline && now >= *impl_->draining_deadline)
        {
            impl_->connection_state = connection_state::closed;
            impl_->accepted_streams.close();
            close_stream_readiness();
        }
        co_return;
    }
    if (impl_->idle_deadline && now >= *impl_->idle_deadline)
    {
        handle_idle_timeout();
        co_return;
    }

    co_await handle_pto();
    if (impl_->send_flush_requested || !impl_->encoded_send_frames.empty() ||
        !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::application)].empty())
        co_await flush_send_queue();
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
            auto initial = pack_initial_packet();
            if (initial.empty())
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            auto sent = co_await send_datagram(initial, impl_->peer);
            if (!sent)
                co_return std::unexpected(sent.error());
            // BoringSSL installs the client early-write secret while creating
            // the Initial flight.  Send queued replay-safe application bytes
            // only after that secret exists, in their own 0-RTT long packet.
            co_await pack_and_send_packet();
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
            if (datagram.error() == std::make_error_code(std::errc::timed_out))
            {
                co_await handle_pto();
                continue;
            }
            co_return std::unexpected(datagram.error());
        }

        auto processed = co_await process_packet(datagram->bytes, datagram->sender);
        if (!processed)
            co_return std::unexpected(processed.error());
        schedule_idle_timeout();

        if (!impl_->encoded_send_frames.empty() ||
            !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::application)].empty())
            co_await flush_send_queue();
    }

    co_return {};
}

auto quic_connection::recv_datagram()
    -> task<std::expected<received_datagram, std::error_code>>
{
    auto& storage = impl_->receive_storage;
    endpoint sender;
    auto pto = impl_->recovery.next_pto_deadline();
    std::expected<std::size_t, std::error_code> received;
    std::optional<time_point> deadline;
    if (pto)
        deadline = pto->first;
    if (impl_->idle_deadline && (!deadline || *impl_->idle_deadline < *deadline))
        deadline = impl_->idle_deadline;
    if (deadline)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto timeout = *deadline > now ? *deadline - now
                                             : std::chrono::steady_clock::duration::zero();
        cancel_token token;
        received = co_await with_timeout(impl_->ctx, timeout,
            async_recvfrom(impl_->ctx, impl_->socket->native_socket(),
                mutable_buffer{storage.data(), storage.size()}, sender, token),
            token);
        if (!received && token.is_cancelled())
        {
            if (impl_->idle_deadline && std::chrono::steady_clock::now() >= *impl_->idle_deadline)
                handle_idle_timeout();
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        }
    }
    else
    {
        received = co_await async_recvfrom(impl_->ctx, impl_->socket->native_socket(),
            mutable_buffer{storage.data(), storage.size()}, sender);
    }
    if (!received)
        co_return std::unexpected(received.error());

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

    // A UDP datagram may carry several long-header packets.  Process every
    // packet in wire order so a Handshake flight coalesced with an Initial is
    // not silently discarded.
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

    const auto first = std::to_integer<std::uint8_t>(packet.front());
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
            for (auto& [_, sent] : impl_->sent_packets[level_index(encryption_level::initial)])
                for (const auto& frame : sent.retransmittable_frames)
                    pending.push_back(frame);
            impl_->sent_packets[level_index(encryption_level::initial)].clear();
            impl_->next_send_packet_number[level_index(encryption_level::initial)] = 0;
            auto initial = pack_initial_packet();
            if (initial.empty())
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            auto sent = co_await send_datagram(initial, impl_->peer);
            if (!sent)
                co_return std::unexpected(sent.error());
            co_return {};
        }
        if (header->type == packet_type::retry)
        {
            if (impl_->connection_role != quic_role::client ||
                !impl_->original_destination_id || header->scid.empty())
                co_return std::unexpected(make_error_code(quic_errc::protocol_violation));
            auto verified = validate_retry_integrity_tag(impl_->version,
                *impl_->original_destination_id, packet);
            if (!verified)
                co_return std::unexpected(verified.error());

            // RFC 9000 §17.2.5: the Retry SCID becomes the DCID of the next
            // client Initial; its opaque token is copied verbatim.
            impl_->retry_token.assign(header->token.begin(), header->token.end());
            impl_->peer_connection_id = header->scid;
            impl_->initial_destination_id = header->scid;
            auto keys = derive_initial_keys(impl_->version, header->scid);
            if (!keys)
                co_return std::unexpected(keys.error());
            impl_->initial_keys = std::move(*keys);
            impl_->next_send_packet_number[level_index(encryption_level::initial)] = 0;
            impl_->largest_received_packet_number[level_index(encryption_level::initial)].reset();
            auto& pending = impl_->retransmit_crypto_frames[level_index(encryption_level::initial)];
            for (auto& [_, sent] : impl_->sent_packets[level_index(encryption_level::initial)])
                for (const auto& frame : sent.retransmittable_frames)
                    pending.push_back(frame);
            impl_->sent_packets[level_index(encryption_level::initial)].clear();
            impl_->retry_received = true;
            auto retried_initial = pack_initial_packet();
            if (retried_initial.empty())
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            auto sent = co_await send_datagram(retried_initial, impl_->peer);
            if (!sent)
                co_return std::unexpected(sent.error());
            co_return {};
        }
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
                co_return std::unexpected(plaintext.error());
            auto& largest = impl_->largest_received_packet_number[level_index(level)];
            if (!largest || packet_number > *largest)
                largest = packet_number;
            bool ack_eliciting = false;
            for (std::size_t frame_offset = 0; frame_offset < plaintext->size();)
            {
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
                // The transport never guesses whether application data is
                // idempotent.  Discard unsent 0-RTT frames and leave an
                // explicit retry decision to the HTTP/3 client.
                impl_->early_data_send_frames.clear();
                auto reset = impl_->tls->reset_after_early_data_rejection();
                if (!reset)
                    co_return std::unexpected(reset.error());
            }
            if (impl_->tls->has_pending_handshake_data() ||
                !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::initial)].empty())
            {
                auto response = pack_initial_packet();
                auto handshake_response = pack_handshake_packet();
                response.insert(response.end(), handshake_response.begin(), handshake_response.end());
                if (response.empty())
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                auto sent = co_await send_datagram(response, impl_->peer);
                if (!sent)
                    co_return std::unexpected(sent.error());
            }
        }
        else if (header->type == packet_type::handshake)
        {
            // Handshake long headers omit the Initial token field.  Keys are
            // installed by BoringSSL after it processes the Initial flight.
            const auto* read_keys = impl_->tls->read_keys(encryption_level::handshake);
            if (!read_keys)
            {
                // RFC 9001 §4.9 permits Handshake keys to be discarded once
                // TLS completed. A delayed duplicate of an authenticated
                // Handshake packet is consequently not a new protocol error;
                // silently ignore it instead of aborting an already usable
                // connection. Before completion, however, missing keys still
                // means the peer sent this packet at an invalid time.
                if (impl_->tls->is_handshake_complete())
                    co_return {};
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
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
                co_return std::unexpected(plaintext.error());
            // RFC 9001 §4.9: an authenticated Handshake packet proves both
            // peers have Handshake keys, so Initial keys and their CRYPTO/PN
            // state must no longer be retained.
            impl_->initial_keys.reset();
            impl_->crypto_fragments[level_index(encryption_level::initial)].clear();
            impl_->retransmit_crypto_frames[level_index(encryption_level::initial)].clear();
            impl_->sent_packets[level_index(encryption_level::initial)].clear();
            auto& largest = impl_->largest_received_packet_number[level_index(level)];
            if (!largest || packet_number > *largest)
                largest = packet_number;
            impl_->receiving_level = encryption_level::handshake;
            bool ack_eliciting = false;
            for (std::size_t frame_offset = 0; frame_offset < plaintext->size();)
            {
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
                impl_->early_data_send_frames.clear();
                auto reset = impl_->tls->reset_after_early_data_rejection();
                if (!reset)
                    co_return std::unexpected(reset.error());
            }
            if (impl_->tls->has_pending_handshake_data() ||
                !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::handshake)].empty())
            {
                auto response = pack_handshake_packet();
                if (!response.empty())
                {
                    auto sent = co_await send_datagram(response, impl_->peer);
                    if (!sent)
                        co_return std::unexpected(sent.error());
                }
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
                co_return {};
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
            const auto level = encryption_level::application;
            const auto packet_number = packet_number_decode(
                static_cast<std::uint32_t>(truncated_packet_number),
                static_cast<std::uint32_t>(*pn_length * 8),
                impl_->largest_received_packet_number[level_index(level)].value_or(0));
            auto plaintext = open_payload(*read_keys,
                std::span<const std::byte>{wire}.subspan(offset + *pn_length),
                std::span<const std::byte>{wire}.first(offset + *pn_length), packet_number);
            if (!plaintext)
                co_return {};

            auto& largest = impl_->largest_received_packet_number[level_index(level)];
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
                    impl_->received_ack_eliciting_packet_numbers[level_index(level)], packet_number);
        }
        co_return co_await handle_long_header_packet(*header);
    }

    const auto cid_size = impl_->local_connection_id
        ? impl_->local_connection_id->size()
        : impl_->config.cid_length;
    auto header = decode_short_header(packet, cid_size);
    if (!header)
        co_return std::unexpected(header.error());
    const auto read_candidates = impl_->tls->application_read_key_candidates();
    if (read_candidates.empty())
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
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
            impl_->largest_received_packet_number[level_index(encryption_level::application)].value_or(0));
        auto candidate_plaintext = open_payload(*candidate.keys,
            std::span<const std::byte>{wire}.subspan(pn_offset + *pn_length),
            std::span<const std::byte>{wire}.first(pn_offset + *pn_length), candidate_packet_number);
        if (!candidate_plaintext)
            continue;
        plaintext = std::move(*candidate_plaintext);
        packet_number = candidate_packet_number;
        accepted_key_kind = candidate.kind;
        break;
    }
    if (!plaintext)
        co_return std::unexpected(std::make_error_code(std::errc::bad_message));
    impl_->tls->confirm_application_read_key(accepted_key_kind);
    impl_->tls->discard_expired_application_read_keys(
        std::chrono::steady_clock::now(), impl_->recovery.pto_duration() * 3);
    auto& largest = impl_->largest_received_packet_number[level_index(encryption_level::application)];
    if (!largest || packet_number > *largest)
        largest = packet_number;
    impl_->receiving_level = encryption_level::application;
    impl_->current_packet_sender = sender;
    bool ack_eliciting = false;
    for (std::size_t frame_offset = 0; frame_offset < plaintext->size();)
    {
        auto frame = decode_frame(std::span<const std::byte>{*plaintext}.subspan(frame_offset));
        if (!frame || frame->second == 0)
            co_return std::unexpected(frame ? std::make_error_code(std::errc::bad_message)
                                            : frame.error());
        ack_eliciting = ack_eliciting || is_ack_eliciting(frame->first);
        co_await process_frames(frame->first);
        frame_offset += frame->second;
    }
    if (ack_eliciting)
        record_ack_eliciting_packet(impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::application)], packet_number);
    auto post_handshake = impl_->tls->process_post_handshake();
    if (!post_handshake)
        co_return std::unexpected(post_handshake.error());
    const auto candidate_expired = impl_->candidate_path_started &&
        std::chrono::steady_clock::now() - *impl_->candidate_path_started >=
            impl_->recovery.pto_duration() * 3;
    if (impl_->tls->is_handshake_complete() &&
        !impl_->tls->received_transport_params().disable_active_migration &&
        sender.to_string() != impl_->peer.to_string() &&
        (!impl_->candidate_path || candidate_expired ||
            sender.to_string() != impl_->candidate_path->to_string()))
    {
        std::array<std::byte, 8> challenge{};
        std::random_device random;
        for (auto& byte : challenge)
            byte = static_cast<std::byte>(random() & 0xffU);
        impl_->candidate_path = sender;
        impl_->candidate_path_started = std::chrono::steady_clock::now();
        impl_->outstanding_path_challenge = challenge;
        const auto encoded = encode_frame(path_challenge_frame{challenge});
        auto probe = pack_path_validation_packet(encoded);
        if (!probe.empty())
            (void)co_await send_datagram(probe, sender);
    }
    impl_->current_packet_sender.reset();
    co_return co_await handle_short_header_packet(*header);
}

auto quic_connection::handle_long_header_packet(long_header header)
    -> task<std::expected<void, std::error_code>>
{
    if (header.type == packet_type::version_negotiation)
        co_return std::unexpected(make_error_code(quic_errc::protocol_violation));
    if (impl_->tls->is_handshake_complete())
    {
        // RFC 9001 §4.9: once 1-RTT keys are available, Handshake protection
        // is no longer permitted.  Keeping it would accept obsolete packets.
        // Initial and Handshake packets may be coalesced. TLS can report
        // completion after the Initial CRYPTO frames while the following
        // Handshake packet still needs these keys. An authenticated 1-RTT
        // packet is the safe discard point (handle_short_header_packet).
        auto parameters_valid = validate_peer_transport_parameters();
        if (!parameters_valid)
            co_return std::unexpected(parameters_valid.error());
        if (!impl_->peer_transport_parameters_applied)
        {
            const auto& params = impl_->tls->received_transport_params();
            impl_->peer_max_data = params.initial_max_data;
            impl_->peer_max_streams_bidi = params.initial_max_streams_bidi;
            impl_->peer_max_streams_uni = params.initial_max_streams_uni;
            for (auto& [id, stream] : impl_->streams)
            {
                if (is_client_initiated(id) !=
                    (impl_->connection_role == quic_role::client))
                    continue;
                const auto limit = is_unidirectional(id)
                    ? params.initial_max_stream_data_uni
                    : params.initial_max_stream_data_bidi_remote;
                stream->update_send_limit(limit);
            }
            impl_->peer_transport_parameters_applied = true;
            auto issued = issue_parallel_local_connection_ids();
            if (!issued)
                co_return std::unexpected(issued.error());
        }
        impl_->connection_state = connection_state::connected;
    }
    co_return {};
}

auto quic_connection::handle_short_header_packet(short_header)
    -> task<std::expected<void, std::error_code>>
{
    if (impl_->tls->is_handshake_complete())
    {
        impl_->tls->discard_keys(encryption_level::handshake);
        auto parameters_valid = validate_peer_transport_parameters();
        if (!parameters_valid)
            co_return std::unexpected(parameters_valid.error());
        if (!impl_->peer_transport_parameters_applied)
        {
            const auto& params = impl_->tls->received_transport_params();
            impl_->peer_max_data = params.initial_max_data;
            impl_->peer_max_streams_bidi = params.initial_max_streams_bidi;
            impl_->peer_max_streams_uni = params.initial_max_streams_uni;
            impl_->peer_transport_parameters_applied = true;
            auto issued = issue_parallel_local_connection_ids();
            if (!issued)
                co_return std::unexpected(issued.error());
        }
        impl_->connection_state = connection_state::connected;
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

    while (impl_->local_connection_ids.size() < target)
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
        impl_->local_connection_ids.emplace(sequence,
            quic_connection_impl::local_cid_info{cid, reset_token});
        impl_->encoded_send_frames.push_back(encode_frame(new_connection_id_frame{
            sequence, 0U, cid, reset_token}));
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
            else if constexpr (std::is_same_v<frame_t, path_challenge_frame>)
                co_await process_path_challenge_frame(value);
            else if constexpr (std::is_same_v<frame_t, path_response_frame>)
                co_await process_path_response_frame(value);
            else if constexpr (std::is_same_v<frame_t, new_connection_id_frame>)
                co_await process_new_connection_id_frame(value);
            else if constexpr (std::is_same_v<frame_t, retire_connection_id_frame>)
                co_await process_retire_connection_id_frame(value);
            else if constexpr (std::is_same_v<frame_t, max_data_frame>)
                impl_->peer_max_data = std::max(impl_->peer_max_data, value.maximum);
            else if constexpr (std::is_same_v<frame_t, max_stream_data_frame>)
            {
                const auto stream = impl_->streams.find(value.stream_id);
                if (stream != impl_->streams.end())
                    stream->second->update_send_limit(value.maximum);
            }
            else if constexpr (std::is_same_v<frame_t, max_streams_frame>)
            {
                auto& limit = value.bidirectional ? impl_->peer_max_streams_bidi
                                                  : impl_->peer_max_streams_uni;
                limit = std::max(limit, value.maximum);
            }
            else if constexpr (std::is_same_v<frame_t, streams_blocked_frame>)
            {
                const auto limit = value.bidirectional
                    ? impl_->local_max_streams_bidi
                    : impl_->local_max_streams_uni;
                if (std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
                    std::fprintf(stderr,
                        "QUIC streams_blocked direction=%s peer_limit=%llu local_limit=%llu\n",
                        value.bidirectional ? "bidi" : "uni",
                        static_cast<unsigned long long>(value.maximum),
                        static_cast<unsigned long long>(limit));
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

auto quic_connection::process_ack_frame(const ack_frame& frame) -> task<void>
{
    const auto level = impl_->receiving_level;
    const auto space = packet_number_space_for(level);
    auto acknowledged = impl_->recovery.on_ack_received(frame, frame.largest_acked,
        std::chrono::steady_clock::now(), space);
    if (!acknowledged)
    {
        impl_->connection_state = connection_state::closing;
        co_return;
    }

    auto& sent = impl_->sent_packets[level_index(level)];
    for (const auto packet_number : *acknowledged)
    {
        const auto it = sent.find(packet_number);
        if (it == sent.end())
            continue;
        // A PTO can carry copies of frames that are still recorded against
        // their original packets.  Once any copy is acknowledged, those
        // frames have been delivered and must never be queued again when a
        // sibling packet is later declared lost (RFC 9002 section 6.2.4).
        // Keep the sibling packet itself in flight for congestion accounting;
        // only retire its now-obsolete retransmission metadata.
        const auto delivered_frames = it->second.retransmittable_frames;
        for (auto& [other_packet_number, metadata] : sent)
        {
            if (other_packet_number == packet_number)
                continue;
            std::erase_if(metadata.retransmittable_frames,
                [&delivered_frames](const auto& candidate)
                {
                    return std::ranges::find(delivered_frames, candidate) !=
                        delivered_frames.end();
                });
        }
        impl_->congestion.on_packet_acked(it->second.bytes);
        sent.erase(it);
    }
    impl_->congestion.update_rtt(impl_->recovery.rtt_estimate().smoothed_rtt_);

    // RFC 9002 loss detection is packet-number-space scoped.  Requeue only
    // frames that are retransmittable; ACKs are deliberately excluded when a
    // packet is recorded below.
    const auto lost = impl_->recovery.detect_lost_packets(
        std::chrono::steady_clock::now(), space);
    for (const auto packet_number : lost)
    {
        const auto it = sent.find(packet_number);
        if (it == sent.end())
            continue;
        impl_->congestion.on_congestion_event(it->second.bytes);
        for (auto frame = it->second.retransmittable_frames.rbegin();
            frame != it->second.retransmittable_frames.rend(); ++frame)
        {
            if (level == encryption_level::application)
                impl_->encoded_send_frames.push_front(std::move(*frame));
            else
                impl_->retransmit_crypto_frames[level_index(level)].push_front(
                    std::move(*frame));
        }
        sent.erase(it);
    }
    if (level == encryption_level::initial)
    {
        auto retransmission = pack_initial_packet();
        if (!retransmission.empty())
            (void)co_await send_datagram(retransmission, impl_->peer);
    }
    else if (level == encryption_level::handshake)
    {
        auto retransmission = pack_handshake_packet();
        if (!retransmission.empty())
            (void)co_await send_datagram(retransmission, impl_->peer);
    }
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

auto quic_connection::process_path_challenge_frame(const path_challenge_frame& frame) -> task<void>
{
    if (!impl_->current_packet_sender)
        co_return;
    const auto encoded = encode_frame(path_response_frame{frame.data});
    auto packet = pack_path_validation_packet(encoded);
    if (!packet.empty())
        (void)co_await send_datagram(packet, *impl_->current_packet_sender);
    co_return;
}

auto quic_connection::process_path_response_frame(const path_response_frame& frame) -> task<void>
{
    if (impl_->candidate_path && impl_->current_packet_sender &&
        impl_->outstanding_path_challenge && *impl_->outstanding_path_challenge == frame.data &&
        impl_->candidate_path->to_string() == impl_->current_packet_sender->to_string())
    {
        // RFC 9000 §9.5: use a fresh peer-issued CID for the new path where
        // available, and retire the previous peer CID only after validation.
        // The Initial CID has no advertised sequence number and is therefore
        // deliberately left unretired here.
        if (!impl_->peer_connection_ids.empty())
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
                impl_->active_peer_cid_sequence = replacement->first;
            }
        }
        impl_->peer = *impl_->candidate_path;
        impl_->candidate_path.reset();
        impl_->candidate_path_started.reset();
        impl_->outstanding_path_challenge.reset();
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
    impl_->cids.erase(retired->second.cid);
    impl_->retired_local_connection_ids.push_back(retired->second);
    impl_->local_connection_ids.erase(retired);

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
        impl_->cids.emplace(replacement, this);
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
    if (const auto retired = impl_->retired_streams.find(frame.stream_id);
        retired != impl_->retired_streams.end())
    {
        const auto final_size = retired->second.received_final_size;
        if (frame.offset > final_size || frame.data.size() > final_size - frame.offset ||
            (frame.fin && frame.offset + frame.data.size() != final_size))
            impl_->connection_state = connection_state::closing;
        co_return;
    }
    auto [it, inserted] = impl_->streams.try_emplace(frame.stream_id);
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
            impl_->connection_state = connection_state::closing;
            co_return;
        }
        it->second = std::make_unique<quic_stream>(frame.stream_id,
            impl_->connection_role, is_bidirectional(frame.stream_id));
        it->second->set_initial_receive_limit(impl_->config.max_stream_data);
        it->second->init();
        impl_->readable_streams.emplace(frame.stream_id,
            std::make_unique<channel<std::monostate>>(1));
        if (!impl_->accepted_streams.try_send(frame.stream_id))
        {
            impl_->streams.erase(it);
            impl_->connection_state = connection_state::closing;
            co_return;
        }
    }
    const auto received_before = it->second->bytes_received();
    auto delivered = it->second->push_received(frame.offset, frame.data, frame.fin);
    if (!delivered)
        impl_->connection_state = connection_state::closing;
    else
    {
        impl_->received_stream_data +=
            it->second->bytes_received() - received_before;
        const auto readiness = impl_->readable_streams.find(frame.stream_id);
        if (readiness != impl_->readable_streams.end())
            (void)readiness->second->try_send({});
        if (impl_->received_stream_data > impl_->local_advertised_max_data)
            impl_->connection_state = connection_state::closing;
    }
    co_return;
}

auto quic_connection::process_reset_stream_frame(const reset_stream_frame& frame) -> task<void>
{
    if (const auto retired = impl_->retired_streams.find(frame.stream_id);
        retired != impl_->retired_streams.end())
    {
        if (frame.final_size != retired->second.received_final_size)
            impl_->connection_state = connection_state::closing;
        co_return;
    }
    auto [it, inserted] = impl_->streams.try_emplace(frame.stream_id);
    if (inserted)
    {
        it->second = std::make_unique<quic_stream>(frame.stream_id,
            impl_->connection_role, is_bidirectional(frame.stream_id));
        it->second->set_initial_receive_limit(impl_->config.max_stream_data);
        it->second->init();
        impl_->readable_streams.emplace(frame.stream_id,
            std::make_unique<channel<std::monostate>>(1));
    }
    if (!it->second->reset_remote(frame.final_size))
        impl_->connection_state = connection_state::closing;
    else if (const auto readiness = impl_->readable_streams.find(frame.stream_id);
        readiness != impl_->readable_streams.end())
        (void)readiness->second->try_send({});
    co_return;
}

auto quic_connection::process_stop_sending_frame(const stop_sending_frame& frame) -> task<void>
{
    const auto it = impl_->streams.find(frame.stream_id);
    if (it == impl_->streams.end())
    {
        if (const auto retired = impl_->retired_streams.find(frame.stream_id);
            retired != impl_->retired_streams.end())
        {
            impl_->encoded_send_frames.push_back(encode_frame(reset_stream_frame{
                frame.stream_id, frame.application_error_code,
                retired->second.sent_final_size}));
        }
        else
            impl_->connection_state = connection_state::closing;
        co_return;
    }
    // The peer no longer accepts this send direction.  Stop queuing new
    // STREAM data and acknowledge the cancellation with RESET_STREAM.
    const auto final_size = it->second->bytes_sent();
    it->second->stop_local();
    impl_->encoded_send_frames.push_back(encode_frame(reset_stream_frame{
        frame.stream_id, frame.application_error_code, final_size}));
    co_return;
}

auto quic_connection::process_connection_close_frame(const connection_close_frame&) -> task<void>
{
    impl_->connection_state = connection_state::draining;
    impl_->draining_deadline = std::chrono::steady_clock::now() +
        impl_->recovery.pto_duration() * 3;
    impl_->accepted_streams.close();
    close_stream_readiness();
    co_return;
}

auto quic_connection::handle_pto() -> task<void>
{
    const auto due = impl_->recovery.next_pto_deadline();
    if (!due || due->first > std::chrono::steady_clock::now())
        co_return;

    const auto level = encryption_level_for(due->second);
    auto& sent = impl_->sent_packets[level_index(level)];
    if (sent.empty())
        co_return;

    // RFC 9002 §6.2.4: PTO sends probes and retains the original packets as
    // in-flight.  Do not erase their metadata here; a later ACK may still
    // acknowledge either original or probe packet.
    // Prefer the newest outstanding retransmittable state.  Flow-control
    // frames carry absolute monotonic limits; probing the oldest MAX_STREAMS
    // value can leave a peer blocked even though a newer limit is already in
    // flight.  Packets whose frames were delivered through another PTO copy
    // remain in the map only for congestion accounting and are skipped.
    const auto probe_packet = std::ranges::find_if(sent.rbegin(), sent.rend(),
        [](const auto& entry)
        {
            return !entry.second.retransmittable_frames.empty();
        });
    if (probe_packet == sent.rend())
        co_return;
    const auto probe_frames = probe_packet->second.retransmittable_frames;
    impl_->recovery.on_pto_expired(due->second);

    constexpr std::size_t probe_count = 2;
    for (std::size_t probe = 0; probe < probe_count; ++probe)
    {
        if (level == encryption_level::application)
        {
            for (auto frame = probe_frames.rbegin(); frame != probe_frames.rend(); ++frame)
                impl_->encoded_send_frames.push_front(*frame);
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
            if (!packet.empty())
                (void)co_await send_datagram(packet, impl_->peer);
        }
        else
        {
            auto& pending = impl_->retransmit_crypto_frames[level_index(level)];
            for (auto frame = probe_frames.rbegin(); frame != probe_frames.rend(); ++frame)
                pending.push_front(*frame);
            auto packet = level == encryption_level::initial
                ? pack_initial_packet()
                : pack_handshake_packet();
            if (!packet.empty())
                (void)co_await send_datagram(packet, impl_->peer);
        }
    }
    co_return;
}

auto quic_connection::pack_and_send_packet() -> task<void>
{
    auto packet = pack_zero_rtt_packet();
    if (packet.empty())
        packet = pack_one_rtt_packet();
    if (packet.empty())
        co_return;
    co_await await_application_pacing(packet.size());
    (void)co_await send_datagram(packet, impl_->peer);
    co_return;
}

auto quic_connection::pack_zero_rtt_packet() -> std::vector<std::byte>
{
    if (impl_->connection_role != quic_role::client || !impl_->local_connection_id ||
        !impl_->tls || impl_->early_data_send_frames.empty() ||
        impl_->tls->early_data_status() != early_data_state::pending)
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

    std::vector<std::byte> payload;
    std::vector<std::vector<std::byte>> frames;
    while (!impl_->early_data_send_frames.empty() && payload.size() < max_udp_payload - 64)
    {
        auto frame = std::move(impl_->early_data_send_frames.front());
        impl_->early_data_send_frames.pop_front();
        payload.insert(payload.end(), frame.begin(), frame.end());
        frames.push_back(std::move(frame));
    }
    if (payload.empty())
        return {};

    constexpr std::size_t pn_length = 4;
    const auto packet_number =
        impl_->next_send_packet_number[level_index(encryption_level::application)]++;
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
    auto sealed = seal_payload(*keys, payload, packet, packet_number);
    if (!sealed)
    {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            impl_->early_data_send_frames.push_front(std::move(*it));
        return {};
    }
    packet.insert(packet.end(), sealed->begin(), sealed->end());
    if (!protect_header(*keys, packet, pn_offset, true))
        return {};
    return packet;
}

auto quic_connection::await_application_pacing(std::size_t packet_size) -> task<void>
{
    const auto rate = impl_->congestion.pacing_rate();
    if (!rate || !std::isfinite(*rate) || *rate <= 0.0)
        co_return;

    const auto now = std::chrono::steady_clock::now();
    const auto burst_capacity = static_cast<double>(impl_->congestion.congestion_window());
    if (!impl_->pacing_credit_updated_at)
    {
        impl_->pacing_credit_updated_at = now;
        impl_->pacing_credit_bytes = burst_capacity;
    }
    else if (*impl_->pacing_credit_updated_at < now)
    {
        const auto elapsed = std::chrono::duration<double>(
            now - *impl_->pacing_credit_updated_at)
                                 .count();
        impl_->pacing_credit_bytes = std::min(
            burst_capacity, impl_->pacing_credit_bytes + elapsed * *rate);
        impl_->pacing_credit_updated_at = now;
    }

    const auto bytes = static_cast<double>(packet_size);
    if (impl_->pacing_credit_bytes >= bytes)
    {
        impl_->pacing_credit_bytes -= bytes;
        co_return;
    }

    const auto reservation_time = std::max(now, *impl_->pacing_credit_updated_at);
    const auto wait = std::chrono::duration<double>(
        (bytes - impl_->pacing_credit_bytes) / *rate);
    const auto deadline = reservation_time +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(wait);
    impl_->pacing_credit_bytes = 0.0;
    impl_->pacing_credit_updated_at = deadline;
    if (deadline > now)
    {
        if (std::getenv("CNETMOD_QUIC_DIAG") != nullptr &&
            deadline - now >= std::chrono::milliseconds{1})
            std::fprintf(stderr,
                "QUIC pacing wait_us=%lld packet=%zu rate=%.2f\n",
                static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        deadline - now)
                        .count()),
                packet_size, *rate);
        co_await async_sleep(impl_->ctx, deadline - now);
    }
    co_return;
}

auto quic_connection::pack_initial_packet() -> std::vector<std::byte>
{
    if (!impl_->initial_keys || !impl_->local_connection_id || !impl_->tls ||
        (impl_->connection_role == quic_role::server && !impl_->peer_connection_id))
        return {};
    const auto& destination_id = impl_->connection_role == quic_role::client
        ? *impl_->initial_destination_id
        : *impl_->peer_connection_id;
    std::vector<std::byte> payload;
    std::vector<std::vector<std::byte>> retransmittable_frames;
    auto& retransmit = impl_->retransmit_crypto_frames[level_index(encryption_level::initial)];
    if (!retransmit.empty())
    {
        auto frame = std::move(retransmit.front());
        retransmit.pop_front();
        payload.insert(payload.end(), frame.begin(), frame.end());
        retransmittable_frames.push_back(std::move(frame));
    }
    else
    {
        const auto crypto = impl_->tls->take_handshake_data(encryption_level::initial);
        if (!crypto.empty())
        {
            auto frame = encode_frame(crypto_frame{
                impl_->next_send_crypto_offset[level_index(encryption_level::initial)], crypto});
            impl_->next_send_crypto_offset[level_index(encryption_level::initial)] += crypto.size();
            payload.insert(payload.end(), frame.begin(), frame.end());
            retransmittable_frames.push_back(std::move(frame));
        }
    }
    if (auto ack = take_ack_frame(
            impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::initial)]))
    {
        auto encoded_ack = encode_frame(*ack);
        payload.insert(payload.end(), encoded_ack.begin(), encoded_ack.end());
    }
    if (payload.empty())
        return {};
    // Initial datagrams must be at least 1200 octets (RFC 9000 §14.1).  The
    // padding is authenticated and therefore added before sealing.
    constexpr std::size_t pn_length = 4;
    constexpr std::size_t tag_length = 16;
    const auto retry_token_length = encode_varint(impl_->retry_token.size());
    if (!retry_token_length)
        return {};
    const std::size_t fixed_header = 1 + 4 + 1 + destination_id.size() +
        1 + impl_->local_connection_id->size() + retry_token_length->second +
        impl_->retry_token.size();
    const auto minimum_payload = min_initial_pkt_size > fixed_header + 2 + pn_length + tag_length
        ? min_initial_pkt_size - fixed_header - 2 - pn_length - tag_length
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
    auto sealed = seal_payload(write_keys, payload, header, packet_number);
    if (!sealed)
        return {};
    header.insert(header.end(), sealed->begin(), sealed->end());
    const auto pn_offset = header.size() - sealed->size() - pn_length;
    if (!protect_header(write_keys, header, pn_offset, true))
        return {};
    const auto ack_eliciting = !retransmittable_frames.empty();
    impl_->recovery.on_packet_sent(packet_number, header.size(),
        std::chrono::steady_clock::now(), ack_eliciting, pn_space::initial);
    if (ack_eliciting)
    {
        impl_->congestion.on_packet_sent(header.size());
        impl_->sent_packets[level_index(encryption_level::initial)].emplace(
            packet_number, quic_connection_impl::sent_packet_metadata{std::move(retransmittable_frames), header.size()});
    }
    return header;
}

auto quic_connection::pack_handshake_packet() -> std::vector<std::byte>
{
    if (!impl_->local_connection_id || !impl_->peer_connection_id || !impl_->tls)
        return {};
    const auto* keys = impl_->tls->write_keys(encryption_level::handshake);
    if (!keys)
        return {};
    std::vector<std::byte> payload;
    std::vector<std::vector<std::byte>> retransmittable_frames;
    auto& retransmit = impl_->retransmit_crypto_frames[level_index(encryption_level::handshake)];
    if (!retransmit.empty())
    {
        auto frame = std::move(retransmit.front());
        retransmit.pop_front();
        payload.insert(payload.end(), frame.begin(), frame.end());
        retransmittable_frames.push_back(std::move(frame));
    }
    else
    {
        const auto crypto = impl_->tls->take_handshake_data(encryption_level::handshake);
        if (!crypto.empty())
        {
            auto frame = encode_frame(crypto_frame{
                impl_->next_send_crypto_offset[level_index(encryption_level::handshake)], crypto});
            impl_->next_send_crypto_offset[level_index(encryption_level::handshake)] += crypto.size();
            payload.insert(payload.end(), frame.begin(), frame.end());
            retransmittable_frames.push_back(std::move(frame));
        }
    }
    if (auto ack = take_ack_frame(
            impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::handshake)]))
    {
        auto encoded_ack = encode_frame(*ack);
        payload.insert(payload.end(), encoded_ack.begin(), encoded_ack.end());
    }
    if (payload.empty())
        return {};
    constexpr std::size_t pn_length = 4;
    constexpr std::size_t tag_length = 16;
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
    auto sealed = seal_payload(*keys, payload, header, packet_number);
    if (!sealed)
        return {};
    header.insert(header.end(), sealed->begin(), sealed->end());
    const auto pn_offset = header.size() - sealed->size() - pn_length;
    if (!protect_header(*keys, header, pn_offset, true))
        return {};
    const auto ack_eliciting = !retransmittable_frames.empty();
    impl_->recovery.on_packet_sent(packet_number, header.size(),
        std::chrono::steady_clock::now(), ack_eliciting, pn_space::handshake);
    if (ack_eliciting)
    {
        impl_->congestion.on_packet_sent(header.size());
        impl_->sent_packets[level_index(encryption_level::handshake)].emplace(
            packet_number, quic_connection_impl::sent_packet_metadata{std::move(retransmittable_frames), header.size()});
    }
    return header;
}

auto quic_connection::pack_one_rtt_packet(bool pto_probe) -> std::vector<std::byte>
{
    if (!impl_->peer_connection_id || !impl_->tls)
        return {};
    const auto* keys = impl_->tls->write_keys(encryption_level::application);
    if (!keys)
        return {};

    std::vector<std::byte> payload;
    const auto application_level = level_index(encryption_level::application);
    const auto ack = take_ack_frame(impl_->received_ack_eliciting_packet_numbers[application_level]);
    if (ack)
        payload = encode_frame(*ack);
    std::vector<std::vector<std::byte>> retransmittable_frames;
    while (!impl_->encoded_send_frames.empty() && payload.size() < max_udp_payload - 64)
    {
        auto frame = std::move(impl_->encoded_send_frames.front());
        impl_->encoded_send_frames.pop_front();
        payload.insert(payload.end(), frame.begin(), frame.end());
        retransmittable_frames.push_back(std::move(frame));
    }
    if (payload.empty())
        return {};
    const auto contains_connection_close = std::ranges::any_of(
        retransmittable_frames, [](const auto& frame)
        {
            return !frame.empty() &&
                (frame.front() == std::byte{0x1c} || frame.front() == std::byte{0x1d});
        });
    const auto estimated_packet_size = 1 + impl_->peer_connection_id->size() + 4 +
        payload.size() + keys->tag_len;
    if (!pto_probe && !retransmittable_frames.empty() && !contains_connection_close &&
        !impl_->congestion.can_send_datagram(estimated_packet_size))
    {
        for (auto frame = retransmittable_frames.rbegin();
            frame != retransmittable_frames.rend(); ++frame)
            impl_->encoded_send_frames.push_front(std::move(*frame));
        return {};
    }
    constexpr std::size_t pn_length = 4;
    const auto packet_number =
        impl_->next_send_packet_number[level_index(encryption_level::application)]++;
    std::vector<std::byte> packet;
    packet.reserve(estimated_packet_size);
    packet.push_back(impl_->tls->application_write_key_phase()
            ? std::byte{0x47}
            : std::byte{0x43}); // short header, fixed bit, key phase, four-byte PN
    packet.insert(packet.end(), impl_->peer_connection_id->data(),
        impl_->peer_connection_id->data() + impl_->peer_connection_id->size());
    for (int shift = 24; shift >= 0; shift -= 8)
        packet.push_back(static_cast<std::byte>((packet_number >> shift) & 0xffU));
    const auto pn_offset = packet.size() - pn_length;
    auto sealed = seal_payload(*keys, payload, packet, packet_number);
    if (!sealed)
        return {};
    packet.insert(packet.end(), sealed->begin(), sealed->end());
    if (!protect_header(*keys, packet, pn_offset, false))
        return {};
    // ACK state is cleared only once an authenticated packet containing the
    // frame has been constructed.  The UDP submission path awaits writability
    // on EAGAIN, so this packet remains live until it has either been handed
    // to the socket or the connection observes a terminal send error.
    if (ack)
        impl_->received_ack_eliciting_packet_numbers[application_level].clear();
    const auto recovery_tracked = !retransmittable_frames.empty() &&
        !contains_connection_close;
    impl_->recovery.on_packet_sent(packet_number, packet.size(),
        std::chrono::steady_clock::now(), recovery_tracked,
        pn_space::application);
    if (recovery_tracked)
    {
        impl_->congestion.on_packet_sent(packet.size());
        impl_->sent_packets[level_index(encryption_level::application)].emplace(
            packet_number, quic_connection_impl::sent_packet_metadata{std::move(retransmittable_frames), packet.size()});
    }
    return packet;
}

auto quic_connection::pack_path_validation_packet(std::span<const std::byte> frame)
    -> std::vector<std::byte>
{
    if (!impl_->peer_connection_id || !impl_->tls || frame.empty())
        return {};
    const auto* keys = impl_->tls->write_keys(encryption_level::application);
    if (!keys)
        return {};
    constexpr std::size_t pn_length = 4;
    const auto packet_number =
        impl_->next_send_packet_number[level_index(encryption_level::application)]++;
    std::vector<std::byte> packet;
    packet.reserve(1 + impl_->peer_connection_id->size() + pn_length + frame.size() + keys->tag_len);
    packet.push_back(impl_->tls->application_write_key_phase()
            ? std::byte{0x47}
            : std::byte{0x43});
    packet.insert(packet.end(), impl_->peer_connection_id->data(),
        impl_->peer_connection_id->data() + impl_->peer_connection_id->size());
    for (int shift = 24; shift >= 0; shift -= 8)
        packet.push_back(static_cast<std::byte>((packet_number >> shift) & 0xffU));
    const auto pn_offset = packet.size() - pn_length;
    auto sealed = seal_payload(*keys, frame, packet, packet_number);
    if (!sealed)
        return {};
    packet.insert(packet.end(), sealed->begin(), sealed->end());
    if (!protect_header(*keys, packet, pn_offset, false))
        return {};
    impl_->recovery.on_packet_sent(packet_number, packet.size(),
        std::chrono::steady_clock::now(), true, pn_space::application);
    impl_->congestion.on_packet_sent(packet.size());
    impl_->sent_packets[level_index(encryption_level::application)].emplace(
        packet_number, quic_connection_impl::sent_packet_metadata{{std::vector<std::byte>(frame.begin(), frame.end())}, packet.size()});
    return packet;
}

auto quic_connection::flush_send_queue() -> task<void>
{
    if (impl_->send_flush_active)
    {
        impl_->send_flush_requested = true;
        co_return;
    }

    impl_->send_flush_active = true;
    // A single application write can be fragmented into multiple STREAM
    // frames.  Drain all packets that the congestion window currently admits;
    // every packet passes through await_application_pacing() and async_sendto
    // parks on platform writability, so this neither bursts past pacing nor
    // spins when the UDP queue is full.  If pack_one_rtt_packet() restores the
    // frames because cwnd is full, the queue size does not decrease and the
    // loop yields to ACK/loss/PTO processing instead of busy-looping.
    do
    {
        impl_->send_flush_requested = false;
        bool send_ack = !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::application)]
                             .empty();
        while (send_ack || !impl_->encoded_send_frames.empty())
        {
            const auto queued_before = impl_->encoded_send_frames.size();
            co_await pack_and_send_packet();
            send_ack = false;
            if (impl_->encoded_send_frames.size() >= queued_before)
                break;
        }
    } while (impl_->send_flush_requested &&
        (!impl_->encoded_send_frames.empty() ||
            !impl_->received_ack_eliciting_packet_numbers[level_index(encryption_level::application)].empty()));
    impl_->send_flush_active = false;
}

auto quic_connection::async_send(stream_id sid, std::span<const std::byte> data,
    bool fin) -> task<std::expected<void, std::error_code>>
{
    if (is_closed() || impl_->connection_state == connection_state::draining)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    auto [it, inserted] = impl_->streams.try_emplace(sid);
    if (inserted)
    {
        if (is_client_initiated(sid) != (impl_->connection_role == quic_role::client))
            co_return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
        it->second = std::make_unique<quic_stream>(sid, impl_->connection_role,
            is_bidirectional(sid));
        it->second->update_send_limit(impl_->config.max_stream_data);
        it->second->set_initial_receive_limit(impl_->config.max_stream_data);
        it->second->init();
        impl_->readable_streams.emplace(sid,
            std::make_unique<channel<std::monostate>>(1));
    }
    if (data.size() > impl_->peer_max_data - impl_->sent_stream_data)
        co_return std::unexpected(make_error_code(quic_errc::flow_control_error));
    auto sent = co_await it->second->send(data);
    if (!sent)
        co_return std::unexpected(sent.error());
    if (fin)
        co_await it->second->close_local();
    impl_->sent_stream_data += data.size();

    // `quic_stream::send` accounts bytes before this point.  The wire offset
    // must therefore be the beginning of this write, not zero for every
    // STREAM frame.  Fragment the write so one application datagram never
    // exceeds the UDP payload budget.
    const auto first_offset = it->second->bytes_sent() - data.size();
    constexpr std::size_t max_stream_payload = max_udp_payload - 96;
    const auto use_early_data = impl_->connection_role == quic_role::client &&
        (impl_->connection_state == connection_state::idle ||
            impl_->connection_state == connection_state::handshaking) &&
        impl_->tls->early_data_status() == early_data_state::pending;
    auto& target_queue = use_early_data ? impl_->early_data_send_frames
                                        : impl_->encoded_send_frames;
    for (std::size_t sent_offset = 0; sent_offset < data.size();)
    {
        const auto chunk = std::min(max_stream_payload, data.size() - sent_offset);
        target_queue.push_back(encode_frame(stream_frame{
            sid, first_offset + sent_offset, data.subspan(sent_offset, chunk),
            fin && sent_offset + chunk == data.size()}));
        sent_offset += chunk;
    }
    if (data.empty() && fin)
        target_queue.push_back(
            encode_frame(stream_frame{sid, first_offset, {}, true}));
    if (fin && it->second->state() == stream_state::closed &&
        is_client_initiated(sid) != (impl_->connection_role == quic_role::client))
    {
        const auto bidirectional = is_bidirectional(sid);
        auto& limit = bidirectional ? impl_->local_max_streams_bidi
                                    : impl_->local_max_streams_uni;
        const auto configured_window = bidirectional
            ? impl_->config.max_streams_bidi
            : impl_->config.max_streams_uni;
        const auto opened_count = sid / 4U + 1U;
        const auto replenish_threshold = std::max<std::uint64_t>(1U,
            configured_window / 2U);
        // MAX_STREAMS is an absolute, monotonic credit limit.  Updating it for
        // every completed request creates one retransmittable control frame
        // per request and makes a long connection walk old credit updates on
        // PTO.  Replenish a full sliding window only when half is consumed.
        if (limit <= opened_count || limit - opened_count <= replenish_threshold)
        {
            limit = opened_count + std::max<std::uint64_t>(1U, configured_window);
            if (std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
                std::fprintf(stderr,
                    "QUIC advertise MAX_STREAMS direction=%s sid=%llu opened=%llu limit=%llu\n",
                    bidirectional ? "bidi" : "uni",
                    static_cast<unsigned long long>(sid),
                    static_cast<unsigned long long>(opened_count),
                    static_cast<unsigned long long>(limit));
            target_queue.push_back(encode_frame(max_streams_frame{limit,
                bidirectional}));
        }
    }
    co_await flush_send_queue();
    co_return {};
}

auto quic_connection::async_recv(stream_id sid, mutable_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    const auto it = impl_->streams.find(sid);
    if (it == impl_->streams.end())
        co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    auto received = co_await it->second->receive(buffer);
    if (!received || *received == 0)
        co_return received;

    impl_->locally_consumed_data += *received;
    if (impl_->local_advertised_max_data - impl_->locally_consumed_data <=
        impl_->config.max_data / 2)
    {
        impl_->local_advertised_max_data = impl_->locally_consumed_data + impl_->config.max_data;
        impl_->encoded_send_frames.push_back(encode_frame(max_data_frame{
            impl_->local_advertised_max_data}));
    }
    const auto consumed = it->second->bytes_consumed();
    if (it->second->remaining_receive_window() <= impl_->config.max_stream_data / 2)
    {
        const auto new_limit = consumed + impl_->config.max_stream_data;
        it->second->extend_receive_limit(new_limit);
        impl_->encoded_send_frames.push_back(encode_frame(max_stream_data_frame{
            sid, new_limit}));
    }
    co_await flush_send_queue();
    co_return received;
}

auto quic_connection::async_wait_readable(stream_id sid)
    -> task<std::expected<void, std::error_code>>
{
    if (is_closed())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));

    const auto stream = impl_->streams.find(sid);
    const auto readiness = impl_->readable_streams.find(sid);
    if (stream == impl_->streams.end() || readiness == impl_->readable_streams.end())
        co_return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));

    // A stream may have become readable between async_recv() returning
    // would_block and this call.  Checking before awaiting avoids a lost
    // notification and leaves the hot path allocation-free.
    if (stream->second->is_readable())
        co_return {};

    const auto notification = co_await readiness->second->receive();
    if (!notification)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return {};
}

auto quic_connection::async_open_stream(bool bidirectional)
    -> task<std::expected<stream_id, std::error_code>>
{
    if (is_closed() || impl_->connection_state == connection_state::draining)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    auto& next = bidirectional ? impl_->next_bidi_stream : impl_->next_uni_stream;
    const auto id = next;
    const auto opened_count = id / 4 + 1;
    const auto limit = bidirectional ? impl_->peer_max_streams_bidi : impl_->peer_max_streams_uni;
    if (opened_count > limit)
        co_return std::unexpected(make_error_code(quic_errc::stream_limit_error));
    next += 4;
    auto stream = std::make_unique<quic_stream>(id, impl_->connection_role, bidirectional);
    stream->update_send_limit(impl_->config.max_stream_data);
    stream->set_initial_receive_limit(impl_->config.max_stream_data);
    stream->init();
    impl_->streams.emplace(id, std::move(stream));
    impl_->readable_streams.emplace(id,
        std::make_unique<channel<std::monostate>>(1));
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
    if (stream->second->state() != stream_state::closed)
        return std::unexpected(std::make_error_code(std::errc::operation_in_progress));

    impl_->retired_streams.insert_or_assign(sid,
        quic_connection_impl::retired_stream_info{
            stream->second->bytes_received(), stream->second->bytes_sent()});
    if (const auto readiness = impl_->readable_streams.find(sid);
        readiness != impl_->readable_streams.end())
    {
        readiness->second->close();
        impl_->readable_streams.erase(readiness);
    }
    impl_->streams.erase(stream);
    return {};
}

auto quic_connection::context() noexcept -> io_context&
{
    return impl_->ctx;
}

auto quic_connection::async_close(std::error_code error, std::string_view reason) -> task<void>
{
    if (is_closed())
        co_return;
    // A close frame must not remain behind application data or a saturated
    // congestion window. It is not retransmittable application data and the
    // peer needs it promptly to release listener-owned connection state.
    impl_->send_queue.clear();
    impl_->encoded_send_frames.clear();
    impl_->early_data_send_frames.clear();
    impl_->encoded_send_frames.push_back(encode_frame(connection_close_frame{
        static_cast<std::uint64_t>(error.value()), 0, std::string(reason), false}));
    impl_->connection_state = connection_state::closing;
    co_await flush_send_queue();
    impl_->connection_state = connection_state::closed;
    impl_->accepted_streams.close();
    close_stream_readiness();
    if (impl_->owned_socket)
        impl_->socket->close();
}

auto quic_connection::register_cid(connection_id cid) -> std::expected<void, std::error_code>
{
    if (cid.empty() || cid.size() > max_cid_length)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto [_, inserted] = impl_->cids.emplace(cid, this);
    if (!inserted)
        return std::unexpected(std::make_error_code(std::errc::file_exists));
    impl_->local_connection_id = std::move(cid);
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
    }
    return {};
}

auto quic_connection::unregister_cid(connection_id cid) -> task<void>
{
    impl_->cids.erase(cid);
    std::erase_if(impl_->local_connection_ids, [&cid](const auto& entry)
        {
            return entry.second.cid == cid;
        });
    if (impl_->local_connection_id && *impl_->local_connection_id == cid)
        impl_->local_connection_id.reset();
    co_return;
}

auto quic_connection::close_stream_readiness() noexcept -> void
{
    for (auto& [_, readiness] : impl_->readable_streams)
        readiness->close();
}

auto quic_connection::is_closed() const noexcept -> bool
{
    return impl_->connection_state == connection_state::closed;
}

auto quic_connection::state() const noexcept -> connection_state
{
    return impl_->connection_state;
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
    result.reserve(impl_->local_connection_ids.size());
    for (const auto& [_, info] : impl_->local_connection_ids)
        result.push_back(info.cid);
    return result;
}

auto quic_connection::local_cid_routes() const -> std::vector<local_cid_route>
{
    std::vector<local_cid_route> result;
    result.reserve(impl_->local_connection_ids.size());
    for (const auto& [_, info] : impl_->local_connection_ids)
        result.push_back({info.cid, info.stateless_reset_token});
    return result;
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
    impl_->connection_state = connection_state::closed;
    impl_->accepted_streams.close();
    close_stream_readiness();
    if (impl_->owned_socket)
        impl_->socket->close();
}

auto quic_connection::can_write_to_stream(stream_id sid) noexcept -> bool
{
    return impl_->streams.contains(sid) && !is_closed();
}

} // namespace cnetmod::quic

    #endif
#endif
