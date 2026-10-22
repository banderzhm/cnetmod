// Single-path raw QUIC fixture for real PMTU discovery and ordinary-loss epochs.
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
    std::string host{"127.0.0.1"};
    std::uint16_t port{4433};
    std::uint64_t ceiling{4096U};
    std::string certificate;
    std::string key;
    std::string listening;
    std::string ready;
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
    logger::error{"PMTU fixture: {}", message};
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
        const bool final = request == "final\n";
        const bool epoch = request.starts_with("epoch/");
        const bool confirmation = request.starts_with("confirm/");
        if (!final && !epoch && !confirmation)
        {
            co_await fail(state, "unknown fixture request");
            co_return;
        }
        const auto mds = state.connection->discovered_path_mtu(0U).value_or(0U);
        const std::string body(final ? 16384U : epoch ? 256U : 1U,
            final ? 'F' : epoch ? 'e' : 'c');
        logger::info{"PMTU_FIXTURE_ENQUEUE stream={} command={} mds={} bytes={}",
            *stream, request.substr(0U, request.size() - 1U), mds, body.size()};
        const auto sent = co_await state.connection->async_send(*stream,
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(body.data()), body.size()}, true);
        if (!sent)
        {
            co_await fail(state, "send: " + sent.error().message());
            co_return;
        }
        // Do not manually flush/poll timers: production outbound notifications
        // and the listener's real timer driver must make this response progress.
    }
}

auto monitor_discovery(fixture& state) -> cn::task<void>
{
    const auto limit = clock_type::now() + std::chrono::seconds{8};
    for (;;)
    {
        const auto mds = state.connection->discovered_path_mtu(0U).value_or(0U);
        if (mds == state.arguments.ceiling)
        {
            (void)co_await cn::async_file_write_all(state.context, state.arguments.ready,
                std::format("mds {}\n", mds));
            co_return;
        }
        if (state.failed || clock_type::now() >= limit)
        {
            co_await fail(state, "ACK-validated discovery did not reach configured ceiling");
            co_return;
        }
        (void)co_await cn::async_timer_wait(state.context, std::chrono::milliseconds{5});
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
        config.max_path_mtu = state.arguments.ceiling;
        config.max_udp_payload_size = 65527U;
        config.path_mtu_probe_interval = std::chrono::milliseconds{1};
        config.path_mtu_initial_probe_delay = std::chrono::milliseconds{50};
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
        state.launch(monitor_discovery(state));
        co_return;
    }
}
} // namespace

auto main(int argc, char** argv) -> int
{
    cn::net_init network;
    logger::init("quic-pmtu-cc", logger::level::info);
    options arguments;
    if ((argc - 1) % 2 != 0)
        return 2;
    for (int index = 1; index + 1 < argc; index += 2)
    {
        const std::string_view name{argv[index]};
        const std::string value{argv[index + 1]};
        if (name == "--host")
            arguments.host = value;
        else if (name == "--port")
            arguments.port = static_cast<std::uint16_t>(std::stoul(value));
        else if (name == "--ceiling")
            arguments.ceiling = std::stoull(value);
        else if (name == "--cert")
            arguments.certificate = value;
        else if (name == "--key")
            arguments.key = value;
        else if (name == "--listening-report")
            arguments.listening = value;
        else if (name == "--ready-report")
            arguments.ready = value;
        else if (name == "--result-report")
            arguments.report = value;
        else
            return 2;
    }
    if (arguments.ceiling < 1200U || arguments.ceiling > 65507U ||
        arguments.listening.empty() || arguments.ready.empty() || arguments.report.empty())
        return 2;
    auto tls_result = cn::ssl_context::quic_server();
    if (!tls_result)
        return 1;
    auto tls = std::move(*tls_result);
    tls.configure_alpn_server({"quic-pmtu-cc-test"});
    if (!tls.load_cert_file(arguments.certificate) || !tls.load_key_file(arguments.key))
        return 1;
    auto context = cn::make_io_context();
    fixture state{*context, tls, std::move(arguments)};
    const auto address = cn::ip_address::from_string(state.arguments.host);
    if (!address || !state.socket.open(cn::endpoint{*address, state.arguments.port}))
        return 1;
    auto start = [&state]() -> cn::task<void>
    {
        (void)co_await cn::async_file_write_all(state.context, state.arguments.listening, "listening\n");
        co_await serve(state);
    };
    auto running = start();
    running.handle().resume();
    context->run();
    return state.failed ? 1 : 0;
}
