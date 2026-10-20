// Real public DATAGRAM API admission, ACK recovery and reliable-data PTO fixture.
// The Python harness terminates this dedicated process after collecting proof.
import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.coro.awaitable;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.protocol.quic;
import cnetmod.protocol.udp;

namespace {
namespace cn = cnetmod;
namespace quic = cn::quic;
using clock_type = std::chrono::steady_clock;

struct options
{
    std::uint16_t port{4433};
    std::string certificate;
    std::string key;
    std::string listening;
    std::string ready;
    std::string burst;
    std::string report;
};

struct fixture
{
    cn::io_context& context;
    cn::ssl_context& tls;
    options arguments;
    cn::udp::udp_socket socket{context};
    std::unique_ptr<quic::quic_connection> connection;
    std::vector<cn::task<void>> tasks;
    std::optional<clock_type::time_point> scheduled;
    std::uint64_t timer_generation{};
    std::uint64_t observer_calls{};
    std::uint64_t ingress{};
    bool failed{};

    void launch(cn::task<void> operation)
    {
        tasks.push_back(std::move(operation));
        tasks.back().handle().resume();
    }
};

auto fail(fixture& state, std::string message) -> cn::task<void>
{
    state.failed = true;
    logger::error{"DATAGRAM CC fixture: {}", message};
    (void)co_await cn::async_file_write_all(state.context, state.arguments.report,
        "failed: " + message + "\n");
}

auto timer_at(fixture& state, std::uint64_t generation,
    clock_type::time_point deadline) -> cn::task<void>;

void schedule(fixture& state, std::optional<clock_type::time_point> deadline)
{
    if (state.scheduled == deadline)
        return;
    state.scheduled = deadline;
    const auto generation = ++state.timer_generation;
    if (deadline)
        state.launch(timer_at(state, generation, *deadline));
}

auto timer_at(fixture& state, std::uint64_t generation,
    clock_type::time_point deadline) -> cn::task<void>
{
    // Even an immediate observer edge runs after packet parsing releases its
    // serial domain. Never poll QUIC from inside the observer callback.
    co_await cn::post_awaitable{state.context};
    if (deadline > clock_type::now())
        (void)co_await cn::async_timer_wait(state.context, deadline - clock_type::now());
    if (generation != state.timer_generation || state.failed)
        co_return;
    state.scheduled.reset();
    co_await state.connection->async_poll_timers();
    schedule(state, state.connection->next_timer_deadline());
}

auto observer_receive(fixture& state) -> cn::task<void>
{
    std::array<std::byte, 65536> storage{};
    while (!state.connection->is_closed())
    {
        cn::endpoint peer;
        const auto received = co_await cn::async_recvfrom(state.context,
            state.socket.native_socket(), cn::mutable_buffer{storage.data(), storage.size()}, peer);
        if (!received)
        {
            co_await fail(state, "receive: " + received.error().message());
            co_return;
        }
        ++state.ingress;
        const auto processed = co_await state.connection->process_datagram(
            std::span<const std::byte>{storage.data(), *received}, peer);
        if (!processed)
        {
            co_await fail(state, "process: " + processed.error().message());
            co_return;
        }
        // This is a normal listener refresh after actual ingress, never a
        // periodic flush. Loss recovery must use actual ACKs and timer edges.
        schedule(state, state.connection->next_timer_deadline());
    }
}

auto respond(fixture& state) -> cn::task<void>
{
    for (;;)
    {
        const auto stream = co_await state.connection->async_accept_stream();
        if (!stream)
        {
            co_await fail(state, "accept stream: " + stream.error().message());
            co_return;
        }
        std::array<char, 128> bytes{};
        std::string request;
        for (;;)
        {
            const auto read = co_await state.connection->async_recv(*stream,
                cn::mutable_buffer{bytes.data(), bytes.size()});
            if (!read)
            {
                if (read.error() != std::make_error_code(std::errc::operation_would_block))
                {
                    co_await fail(state, "read: " + read.error().message());
                    co_return;
                }
                const auto ready = co_await state.connection->async_wait_readable(*stream);
                if (!ready)
                {
                    co_await fail(state, "read readiness: " + ready.error().message());
                    co_return;
                }
                continue;
            }
            if (*read == 0U)
                break;
            request.append(bytes.data(), *read);
            if (request.size() > 128U)
            {
                co_await fail(state, "oversized fixture request");
                co_return;
            }
        }
        // Channel readiness may resume this handler inline in packet parsing.
        // Run application work on the following owner-context turn, after
        // ingress and any synchronous receive/flush owner have unwound.
        co_await cn::post_awaitable{state.context};
        if (request == "burst\n")
        {
            constexpr std::uint32_t count{64U};
            std::array<std::byte, 1100U> payload;
            payload.fill(std::byte{'x'});
            payload[0U] = std::byte{'D'};
            payload[1U] = std::byte{'G'};
            payload[2U] = std::byte{'C'};
            payload[3U] = std::byte{'C'};
            const auto started = clock_type::now();
            std::optional<clock_type::time_point> first_deadline;
            auto sampled = started;
            for (std::uint32_t sequence{}; sequence < count; ++sequence)
            {
                for (std::uint32_t index{}; index < 4U; ++index)
                    payload[4U + index] = static_cast<std::byte>(
                        (sequence >> (24U - index * 8U)) & 0xffU);
                const auto sent = co_await state.connection->async_send_datagram(payload);
                if (!sent)
                {
                    co_await fail(state, "burst public DATAGRAM acceptance: " + sent.error().message());
                    co_return;
                }
                if (!first_deadline)
                {
                    first_deadline = state.connection->next_timer_deadline();
                    sampled = clock_type::now();
                }
            }
            const auto finished = clock_type::now();
            if (!first_deadline || *first_deadline <= started || finished >= *first_deadline ||
                *first_deadline > started + std::chrono::seconds{5})
            {
                co_await fail(state, std::format(
                    "invalid setup: elapsed_us={} first_deadline_us={} sampled_us={} first_is_due={}",
                    std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count(),
                    first_deadline ? std::chrono::duration_cast<std::chrono::microseconds>(*first_deadline - started).count() : -1,
                    std::chrono::duration_cast<std::chrono::microseconds>(sampled - started).count(),
                    first_deadline && *first_deadline <= sampled));
                co_return;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count();
            const auto deadline = std::chrono::duration_cast<std::chrono::microseconds>(*first_deadline - started).count();
            // All public API sends finished before the first public recovery deadline.
            // Timer processing remains active; no clock or CC state is changed.
            const auto reported = co_await cn::async_file_write_all(state.context, state.arguments.burst,
                std::format("{{\"accepted\":{},\"payload_bytes\":1100,\"elapsed_us\":{},\"first_recovery_deadline_us\":{}}}\n",
                    count, elapsed, deadline));
            if (!reported)
            {
                co_await fail(state, "burst report: " + reported.error().message());
                co_return;
            }
        }
        else if (request == "marker\n")
        {
            const std::string marker{"DGCMfresh"};
            const auto sent = co_await state.connection->async_send_datagram(
                std::as_bytes(std::span{marker.data(), marker.size()}));
            if (!sent)
            {
                co_await fail(state, "fresh marker DATAGRAM: " + sent.error().message());
                co_return;
            }
        }
        const bool warmup = request == "warmup\n";
        const bool reliable = request == "pto\n";
        if (!warmup && !reliable && request != "burst\n" && request != "marker\n")
        {
            co_await fail(state, "unknown fixture request");
            co_return;
        }
        const std::string body(reliable ? 262144U : 1U, reliable ? 'P' : 'c');
        // Small reliable application writes leave room for the existing
        // PTO's PING/MAX_STREAMS controls. This scenario tests the explicit
        // PTO cwnd exception, not whole-MSS retransmission repacketization.
        for (std::size_t offset{}; offset < body.size();)
        {
            const auto count = std::min(reliable ? std::size_t{512U} : body.size(), body.size() - offset);
            const auto sent = co_await state.connection->async_send(*stream,
                std::as_bytes(std::span{body.data() + offset, count}), offset + count == body.size());
            if (!sent)
            {
                co_await fail(state, "response STREAM acceptance: " + sent.error().message());
                co_return;
            }
            offset += count;
        }
        if (warmup)
        {
            const auto timeout = clock_type::now() + std::chrono::seconds{3};
            for (;;)
            {
                const auto now = clock_type::now();
                const auto deadline = state.connection->next_timer_deadline();
                // The configured idle timeout is 30 s; the initial recovery PTO can be 1.024 s.
                if (deadline && *deadline > now + std::chrono::seconds{10})
                    break;
                if (state.failed || now >= timeout)
                {
                    co_await fail(state, "warmup did not drain real recovery timers");
                    co_return;
                }
                (void)co_await cn::async_timer_wait(state.context, std::chrono::milliseconds{1});
            }
            const auto reported = co_await cn::async_file_write_all(state.context, state.arguments.ready, "ready\n");
            if (!reported)
            {
                co_await fail(state, "warmup report: " + reported.error().message());
                co_return;
            }
        }
    }
}

auto retry_packet(const quic::long_header& initial, const quic::connection_id& retry_cid)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    std::vector<std::byte> packet{std::byte{0xf0}};
    for (int shift = 24; shift >= 0; shift -= 8)
        packet.push_back(static_cast<std::byte>((initial.version >> shift) & 0xffU));
    packet.push_back(static_cast<std::byte>(initial.scid.size()));
    packet.insert(packet.end(), initial.scid.data(), initial.scid.data() + initial.scid.size());
    packet.push_back(static_cast<std::byte>(retry_cid.size()));
    packet.insert(packet.end(), retry_cid.data(), retry_cid.data() + retry_cid.size());
    packet.push_back(std::byte{0x71});
    auto tag = quic::make_retry_integrity_tag(quic::quic_version::v1, initial.dcid, packet);
    if (!tag)
        return std::unexpected(tag.error());
    packet.insert(packet.end(), tag->begin(), tag->end());
    return packet;
}

auto serve(fixture& state) -> cn::task<void>
{
    std::array<std::byte, 65536> storage{};
    cn::endpoint peer;
    quic::connection_id original;
    std::array<std::byte, 8> cid_bytes{};
    std::random_device random;
    for (auto& byte : cid_bytes)
        byte = static_cast<std::byte>(random() & 0xffU);
    const quic::connection_id retry_cid{cid_bytes.data(), static_cast<std::uint8_t>(cid_bytes.size())};
    bool retried{};
    for (;;)
    {
        auto received = co_await cn::async_recvfrom(state.context, state.socket.native_socket(),
            cn::mutable_buffer{storage.data(), storage.size()}, peer);
        if (!received)
        {
            co_await fail(state, "Initial receive: " + received.error().message());
            co_return;
        }
        auto header = quic::decode_long_header(std::span<const std::byte>{storage.data(), *received});
        if (!header || header->type != quic::packet_type::initial || header->version != 1U)
            continue;
        if (header->token.empty())
        {
            if (!retried)
                original = header->dcid;
            auto retry = retry_packet(*header, retry_cid);
            if (!retry)
            {
                co_await fail(state, "Retry encoding");
                co_return;
            }
            (void)co_await cn::async_sendto(state.context, state.socket.native_socket(),
                cn::const_buffer{retry->data(), retry->size()}, peer);
            retried = true;
            continue;
        }
        if (!retried || header->dcid != retry_cid || header->token.size() != 1U ||
            header->token.front() != std::byte{0x71})
            continue;
        quic::quic_config config;
        config.idle_timeout = std::chrono::seconds{30};
        config.enable_path_mtu_discovery = false;
        config.max_udp_payload_size = 1200U;
        config.max_datagram_frame_size = 1200U;
        config.congestion_algorithm = quic::quic_congestion_algorithm::new_reno;
        config.max_data = 1048576U;
        config.max_stream_data = 1048576U;
        config.max_streams_bidi = 128U;
        config.max_streams_uni = 0U;
        config.active_connection_id_limit = 2U;
        state.connection = std::make_unique<quic::quic_connection>(state.context,
            state.socket, peer, quic::quic_role::server, state.tls, config);
        if (!state.connection->register_cid(retry_cid) ||
            !state.connection->set_original_destination_connection_id(original))
        {
            co_await fail(state, "Retry transport parameters");
            co_return;
        }
        state.connection->set_timer_deadline_observer([&state](auto deadline)
        {
            ++state.observer_calls;
            schedule(state, deadline);
        });
        const auto processed = co_await state.connection->process_datagram(
            std::span<const std::byte>{storage.data(), *received}, peer);
        if (!processed)
        {
            co_await fail(state, "validated Initial: " + processed.error().message());
            co_return;
        }
        schedule(state, state.connection->next_timer_deadline());
        state.launch(observer_receive(state));
        state.launch(respond(state));
        co_return;
    }
}
} // namespace

auto main(int argc, char** argv) -> int
{
    cn::net_init network;
    logger::init("quic-datagram-cc", logger::level::info);
    options arguments;
    if ((argc - 1) % 2 != 0)
        return 2;
    for (int index = 1; index + 1 < argc; index += 2)
    {
        const std::string_view name{argv[index]};
        const std::string value{argv[index + 1]};
        if (name == "--port") arguments.port = static_cast<std::uint16_t>(std::stoul(value));
        else if (name == "--cert") arguments.certificate = value;
        else if (name == "--key") arguments.key = value;
        else if (name == "--listening-report") arguments.listening = value;
        else if (name == "--ready-report") arguments.ready = value;
        else if (name == "--burst-report") arguments.burst = value;
        else if (name == "--result-report") arguments.report = value;
        else return 2;
    }
    if (arguments.listening.empty() || arguments.ready.empty() || arguments.burst.empty() || arguments.report.empty())
        return 2;
    auto tls_result = cn::ssl_context::quic_server();
    if (!tls_result)
        return 1;
    auto tls = std::move(*tls_result);
    tls.configure_alpn_server({"quic-datagram-cc-test"});
    if (!tls.load_cert_file(arguments.certificate) || !tls.load_key_file(arguments.key))
        return 1;
    auto context = cn::make_io_context();
    fixture state{*context, tls, std::move(arguments)};
    const auto address = cn::ip_address::from_string("127.0.0.1");
    if (!address || !state.socket.open(cn::endpoint{*address, state.arguments.port}))
        return 1;
    auto start = [&state]() -> cn::task<void>
    {
        const auto reported = co_await cn::async_file_write_all(state.context, state.arguments.listening, "listening\n");
        if (!reported)
        {
            co_await fail(state, "listening report: " + reported.error().message());
            co_return;
        }
        co_await serve(state);
    };
    auto running = start();
    running.handle().resume();
    context->run();
    return state.failed ? 1 : 0;
}
