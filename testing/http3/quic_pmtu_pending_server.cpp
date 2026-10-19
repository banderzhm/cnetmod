// Real public DATAGRAM flight / pending PMTU / ACK scheduling fixture.
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
    std::uint64_t initial_delay_ms{200U};
    std::uint64_t interval_ms{10U};
    std::string report;
    std::string burst_report;
    std::string attempt_report;
    std::string state_report;
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
    bool burst_started{};
    bool burst_done{};
    bool attempt_reported{};
    std::optional<clock_type::time_point> observed_public_deadline;
    std::optional<clock_type::time_point> first_mds_observed_at;

    void launch(cn::task<void> operation)
    {
        tasks.push_back(std::move(operation));
        tasks.back().handle().resume();
    }
};

auto fail(fixture& state, std::string message) -> cn::task<void>
{
    state.failed = true;
    logger::error{"PMTU pending fixture: {}", message};
    (void)co_await cn::async_file_write_all(state.context, state.arguments.report,
        "failed: " + message + "\n");
}

auto write_status(fixture& state, const std::string& path) -> cn::task<void>
{
    const auto now = clock_type::now();
    const auto next = state.connection->next_timer_deadline();
    const auto record = std::format(
        "{{\"at_us\":{},\"next_deadline_us\":{},\"mds\":{},\"ingress\":{}}}\n",
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count(),
        next ? std::chrono::duration_cast<std::chrono::microseconds>(next->time_since_epoch()).count() : -1,
        state.connection->discovered_path_mtu(0U).value_or(0U), state.ingress);
    const auto written = co_await cn::async_file_write_all(state.context, path, record);
    if (!written)
        co_await fail(state, "status report: " + written.error().message());
}

auto fill_datagram_flight(fixture& state) -> cn::task<void>
{
    // The real probe ACK has been processed. Yield only application dispatch;
    // recovery and observer timers continue to run normally.
    co_await cn::post_awaitable{state.context};
    const auto started = clock_type::now();
    std::array<std::byte, 1100U> payload;
    payload.fill(std::byte{'x'});
    payload[0U] = std::byte{'D'};
    payload[1U] = std::byte{'G'};
    payload[2U] = std::byte{'C'};
    payload[3U] = std::byte{'C'};
    for (std::uint32_t sequence{}; sequence < 64U; ++sequence)
    {
        for (std::size_t index{}; index < 4U; ++index)
            payload[4U + index] = static_cast<std::byte>(sequence >> ((3U - index) * 8U));
        const auto sent = co_await state.connection->async_send_datagram(payload);
        if (!sent)
        {
            co_await fail(state, "public DATAGRAM acceptance: " + sent.error().message());
            co_return;
        }
    }
    co_await cn::post_awaitable{state.context};
    const auto finished = clock_type::now();
    if (!state.observed_public_deadline || finished >= *state.observed_public_deadline)
    {
        co_await fail(state, "invalid setup: DATAGRAM burst did not finish before observed public timer checkpoint");
        co_return;
    }
    state.burst_done = true;
    const auto record = std::format(
        "{{\"accepted\":64,\"elapsed_us\":{},\"finished_us\":{},\"observed_deadline_us\":{},\"mds_observed_us\":{},\"configured_interval_us\":{}}}\n",
        std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(finished.time_since_epoch()).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(state.observed_public_deadline->time_since_epoch()).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(state.first_mds_observed_at->time_since_epoch()).count(),
        state.arguments.interval_ms * 1000U);
    const auto written = co_await cn::async_file_write_all(state.context, state.arguments.burst_report, record);
    if (!written)
        co_await fail(state, "burst report: " + written.error().message());
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
    const auto before_wait = clock_type::now();
    if (deadline > before_wait)
        (void)co_await cn::async_timer_wait(state.context, deadline - before_wait);
    if (generation != state.timer_generation || state.failed)
        co_return;
    state.scheduled.reset();
    co_await state.connection->async_poll_timers();
    schedule(state, state.connection->next_timer_deadline());
    if (state.burst_done && !state.attempt_reported && state.observed_public_deadline &&
        deadline >= *state.observed_public_deadline)
    {
        // Observe only a normal timer poll. No private transport state is
        // modified and no extra flush or timer poll is injected.
        state.attempt_reported = true;
        co_await write_status(state, state.arguments.attempt_report);
    }
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
        if (!state.burst_started && state.connection->discovered_path_mtu(0U) == 1328U)
        {
            state.first_mds_observed_at = clock_type::now();
            state.observed_public_deadline = state.connection->next_timer_deadline();
            const auto now = clock_type::now();
            if (!state.observed_public_deadline || *state.observed_public_deadline <= now ||
                *state.observed_public_deadline > now + std::chrono::milliseconds{100})
            {
                co_await fail(state, "invalid setup: first ACK did not expose a future public timer checkpoint");
                co_return;
            }
            state.burst_started = true;
            state.launch(fill_datagram_flight(state));
        }
        if (state.burst_done && state.attempt_reported)
            co_await write_status(state, state.arguments.state_report);
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
        config.enable_path_mtu_discovery = true;
        config.max_path_mtu = 1452U;
        config.path_mtu_initial_probe_delay = std::chrono::milliseconds{state.arguments.initial_delay_ms};
        config.path_mtu_probe_interval = std::chrono::milliseconds{state.arguments.interval_ms};
        config.max_udp_payload_size = 1452U;
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
        co_return;
    }
}
} // namespace

auto main(int argc, char** argv) -> int
{
    cn::net_init network;
    logger::init("quic-pmtu-pending", logger::level::info);
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
        else if (name == "--initial-delay-ms") arguments.initial_delay_ms = std::stoull(value);
        else if (name == "--interval-ms") arguments.interval_ms = std::stoull(value);
        else if (name == "--result-report") arguments.report = value;
        else if (name == "--burst-report") arguments.burst_report = value;
        else if (name == "--attempt-report") arguments.attempt_report = value;
        else if (name == "--state-report") arguments.state_report = value;
        else return 2;
    }
    if (arguments.listening.empty() || arguments.report.empty() || arguments.burst_report.empty() || arguments.attempt_report.empty() || arguments.state_report.empty() || arguments.interval_ms == 0U || arguments.initial_delay_ms > 10000U || arguments.interval_ms > 10000U)
        return 2;
    auto tls_result = cn::ssl_context::quic_server();
    if (!tls_result)
        return 1;
    auto tls = std::move(*tls_result);
    tls.configure_alpn_server({"quic-pmtu-pending-test"});
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
