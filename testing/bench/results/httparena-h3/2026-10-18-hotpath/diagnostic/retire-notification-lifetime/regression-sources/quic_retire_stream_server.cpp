// Single-connection raw QUIC fixture for delayed MAX_STREAMS publication.
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
    bool observer{true};
    bool private_pto{};
    std::string certificate;
    std::string key;
    std::string listening;
    std::string ready;
    std::string trigger;
    std::string report;
    std::string notification_report;
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
    bool response_queued{};
    bool failed{};
    std::weak_ptr<int> observer_lifetime;

    void launch(cn::task<void> operation)
    {
        tasks.push_back(std::move(operation));
        tasks.back().handle().resume();
    }
};

auto fail(fixture& state, std::string message) -> cn::task<void>
{
    state.failed = true;
    logger::error{"retire-stream fixture: {}", message};
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
        // periodic flush. Once the proxy gates ingress only observer edges run.
        schedule(state, state.connection->next_timer_deadline());
    }
}

auto private_receive(fixture& state) -> cn::task<void>
{
    const auto result = co_await state.connection->run();
    if (!result)
        co_await fail(state, "private run: " + result.error().message());
}

auto respond(fixture& state) -> cn::task<void>
{
    const auto stream = co_await state.connection->async_accept_stream();
    if (!stream || *stream != 0U)
    {
        co_await fail(state, "expected stream zero");
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
                co_await fail(state, "stream read: " + read.error().message());
                co_return;
            }
            const auto readable = co_await state.connection->async_wait_readable(*stream);
            if (!readable)
            {
                co_await fail(state, "stream readiness: " + readable.error().message());
                co_return;
            }
            continue;
        }
        if (*read == 0U)
            break;
        request.append(bytes.data(), *read);
    }
    if (request != "retire request\n")
    {
        co_await fail(state, "incorrect request payload/FIN");
        co_return;
    }
    constexpr std::string_view response{"retire response\n"};
    const auto sent = co_await state.connection->async_send(*stream,
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(response.data()), response.size()}, true);
    if (!sent)
    {
        co_await fail(state, "stream send: " + sent.error().message());
        co_return;
    }
    state.response_queued = true;
}

auto await_retirement(fixture& state) -> cn::task<void>
{
    const auto setup_limit = clock_type::now() + std::chrono::seconds{3};
    std::optional<clock_type::time_point> quiet_since;
    while (!state.failed)
    {
        // With PMTU/multipath disabled, this fixture has only PTO/flush/idle
        // deadlines. A >4s minimum during the first 3s of a 6s-idle connection
        // excludes outstanding recovery work (the local PTO starts below 1s).
        const auto deadline = state.connection->next_timer_deadline();
        const auto now = clock_type::now();
        if (state.response_queued && state.connection->state() == quic::connection_state::connected &&
            deadline && *deadline - now > std::chrono::seconds{4})
        {
            if (!quiet_since)
                quiet_since = now;
            if (now - *quiet_since >= std::chrono::milliseconds{50})
                break;
        }
        else
            quiet_since.reset();
        if (now >= setup_limit)
        {
            co_await fail(state, "response ACK/PTO did not become quiescent");
            co_return;
        }
        (void)co_await cn::async_timer_wait(state.context, std::chrono::milliseconds{5});
    }
    if (state.failed)
        co_return;
    (void)co_await cn::async_file_write_all(state.context, state.arguments.ready, "quiescent\n");
    const auto trigger_limit = clock_type::now() + std::chrono::seconds{2};
    while (!(co_await cn::async_file_stat(state.context, state.arguments.trigger)))
    {
        if (clock_type::now() >= trigger_limit || state.failed)
        {
            co_await fail(state, "retire trigger missing");
            co_return;
        }
        (void)co_await cn::async_timer_wait(state.context, std::chrono::milliseconds{5});
    }
    const auto before = state.connection->next_timer_deadline();
    const auto now = clock_type::now();
    if (!before || *before - now <= std::chrono::seconds{3})
    {
        co_await fail(state, "new QUIC work appeared before explicit retirement");
        co_return;
    }
    if (state.arguments.private_pto)
    {
        // Both the quiescence snapshot and this application action execute on
        // the connection owner. No timer polling or peer input drives the send.
        const auto stream = co_await state.connection->async_open_stream();
        if (!stream || *stream != 1U)
        {
            co_await fail(state, "expected server-initiated stream one");
            co_return;
        }
        constexpr std::string_view payload{"private delayed response\n"};
        const auto sent = co_await state.connection->async_send(*stream,
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(payload.data()), payload.size()}, true);
        if (!sent)
        {
            co_await fail(state, "private delayed send: " + sent.error().message());
            co_return;
        }
        (void)co_await cn::async_file_write_all(state.context, state.arguments.report,
            std::format("sent {} {} {}\n",
                std::chrono::duration_cast<std::chrono::milliseconds>(*before - now).count(),
                state.observer_calls, state.ingress));
        co_return;
    }
    const auto calls_before = state.observer_calls;
    const auto notification_owners_before = state.observer_lifetime.use_count();
    const auto retired = state.connection->retire_stream(0U);
    if (!retired)
    {
        co_await fail(state, "retire: " + retired.error().message());
        co_return;
    }
    // No poll_timers/receive/send call follows retire here. Only its own wake
    // edge may deliver MAX_STREAMS before the pre-existing idle deadline.
    (void)co_await cn::async_timer_wait(state.context, std::chrono::milliseconds{100});
    if (!state.arguments.notification_report.empty())
    {
        const auto notification_owners_after = state.observer_lifetime.use_count();
        state.connection->set_timer_deadline_observer({});
        (void)co_await cn::async_file_write_all(state.context, state.arguments.notification_report,
            std::format("{} {} {}\n", notification_owners_before,
                notification_owners_after, state.observer_lifetime.expired()));
    }
    (void)co_await cn::async_file_write_all(state.context, state.arguments.report,
        std::format("retired {} {} {}\n",
            std::chrono::duration_cast<std::chrono::milliseconds>(*before - now).count(),
            state.observer_calls - calls_before, state.ingress));
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
        config.idle_timeout = std::chrono::seconds{6};
        config.max_streams_bidi = 1U;
        config.max_streams_uni = 0U;
        config.active_connection_id_limit = 2U;
        if (state.arguments.observer)
            state.connection = std::make_unique<quic::quic_connection>(state.context,
                state.socket, peer, quic::quic_role::server, state.tls, config);
        else
            state.connection = std::make_unique<quic::quic_connection>(state.context,
                std::move(state.socket), peer, quic::quic_role::server, state.tls, config);
        if (!state.connection->register_cid(retry_cid) ||
            !state.connection->set_original_destination_connection_id(original))
        {
            co_await fail(state, "Retry transport parameters");
            co_return;
        }
        if (state.arguments.observer)
        {
            auto probe = state.arguments.notification_report.empty()
                ? std::shared_ptr<int>{} : std::make_shared<int>(0);
            state.observer_lifetime = probe;
            state.connection->set_timer_deadline_observer([&state, probe](auto deadline)
            {
                ++state.observer_calls;
                schedule(state, deadline);
            });
        }
        else
            state.launch(private_receive(state));
        const auto processed = co_await state.connection->process_datagram(
            std::span<const std::byte>{storage.data(), *received}, peer);
        if (!processed)
        {
            co_await fail(state, "validated Initial: " + processed.error().message());
            co_return;
        }
        if (state.arguments.observer)
        {
            schedule(state, state.connection->next_timer_deadline());
            state.launch(observer_receive(state));
        }
        state.launch(respond(state));
        state.launch(await_retirement(state));
        co_return;
    }
}
} // namespace

auto main(int argc, char** argv) -> int
{
    cn::net_init network;
    logger::init("quic-retire-stream", logger::level::info);
    options arguments;
    for (int index = 1; index + 1 < argc; index += 2)
    {
        const std::string_view name{argv[index]};
        const std::string value{argv[index + 1]};
        if (name == "--port") arguments.port = static_cast<std::uint16_t>(std::stoul(value));
        else if (name == "--mode") arguments.observer = value == "observer";
        else if (name == "--scenario") arguments.private_pto = value == "private-pto";
        else if (name == "--cert") arguments.certificate = value;
        else if (name == "--key") arguments.key = value;
        else if (name == "--listening-report") arguments.listening = value;
        else if (name == "--ready-report") arguments.ready = value;
        else if (name == "--retire-trigger") arguments.trigger = value;
        else if (name == "--result-report") arguments.report = value;
        else if (name == "--notification-report") arguments.notification_report = value;
        else return 2;
    }
    if (arguments.private_pto && arguments.observer)
        return 2;
    auto tls_result = cn::ssl_context::quic_server();
    if (!tls_result)
        return 1;
    auto tls = std::move(*tls_result);
    tls.configure_alpn_server({"quic-retire-test"});
    if (!tls.load_cert_file(arguments.certificate) || !tls.load_key_file(arguments.key))
        return 1;
    auto context = cn::make_io_context();
    fixture state{*context, tls, std::move(arguments)};
    const auto address = cn::ip_address::from_string("127.0.0.1");
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
