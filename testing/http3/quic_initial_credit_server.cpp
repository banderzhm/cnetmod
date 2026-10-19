// Public-API raw QUIC initial-credit and cancellation regression fixture.
// The peer driver stops this dedicated shared-socket listener after its report.
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
    std::string scenario{"credit"};
    std::string certificate;
    std::string key;
    std::string listening;
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
    std::array<bool, 4> completed{};
    bool failed{};
    bool local_close_requested{};

    void launch(cn::task<void> operation)
    {
        tasks.push_back(std::move(operation));
        tasks.back().handle().resume();
    }
};

auto fail(fixture& state, std::string message) -> cn::task<void>
{
    state.failed = true;
    logger::error{"initial-credit fixture: {}", message};
    (void)co_await cn::async_file_write_all(state.context, state.arguments.report + ".failed",
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
        const auto processed = co_await state.connection->process_datagram(
            std::span<const std::byte>{storage.data(), *received}, peer);
        if (!processed)
        {
            if (processed.error() == std::make_error_code(std::errc::not_connected) &&
                state.local_close_requested && state.connection->is_closed())
                co_return;

            co_await fail(state, "process: " + processed.error().message());
            co_return;
        }
        // Refresh the public timer deadline after actual peer ingress.
        schedule(state, state.connection->next_timer_deadline());
    }
}

auto send_probe(fixture& state, quic::stream_id sid, std::size_t size,
    bool shared, std::size_t index) -> cn::task<void>
{
    std::expected<void, std::error_code> result;
    if (shared)
        result = co_await state.connection->async_send_shared(sid, {},
            std::make_shared<const std::string>(size, 'p'), true);
    else
        result = co_await state.connection->async_send(sid,
            std::vector<std::byte>(size, std::byte{'p'}), true);
    const bool cancelled = state.arguments.scenario != "credit";
    if ((cancelled && (result || result.error() != std::make_error_code(std::errc::operation_not_permitted))) ||
        (!cancelled && !result))
    {
        co_await fail(state, "public producer completed with unexpected result");
        co_return;
    }
    state.completed[index] = true;
    logger::info{"CREDIT_RECORD producer_done {}", sid};
}

auto read_request(fixture& state, quic::stream_id sid)
    -> cn::task<std::expected<std::string, std::error_code>>
{
    std::string result;
    std::array<std::byte, 128> storage{};
    for (;;)
    {
        const auto read = co_await state.connection->async_recv(sid, cn::buffer(storage));
        if (read && *read == 0U)
        {
            co_return result;
        }
        if (read)
        {
            result.append(reinterpret_cast<const char*>(storage.data()), *read);
        }
        else if (read.error() == std::make_error_code(std::errc::operation_would_block))
        {
            const auto ready = co_await state.connection->async_wait_readable(sid);
            if (!ready)
            {
                co_return std::unexpected(ready.error());
            }
        }
        else
        {
            co_return std::unexpected(read.error());
        }
    }
}

auto control(fixture& state) -> cn::task<void>
{
    for (unsigned stage = 0U; stage != 2U; ++stage)
    {
        const auto sid = co_await state.connection->async_accept_stream();
        if (!sid || *sid != stage * 4U)
        {
            co_await fail(state, "control stream ID/order");
            co_return;
        }
        const auto request = co_await read_request(state, *sid);
        if (!request || *request != (stage == 0U ? "first" : "finish"))
        {
            co_await fail(state, "control request bytes");
            co_return;
        }
        if (state.arguments.scenario != "credit")
        {
            const auto retired = state.connection->retire_stream(1U);
            if ((stage == 0U && (retired || retired.error() != std::make_error_code(std::errc::operation_in_progress))) ||
                (stage == 1U && (retired || retired.error() != std::make_error_code(std::errc::no_such_file_or_directory))))
            {
                co_await fail(state, "unknown/actual remote final retirement boundary");
                co_return;
            }
            // Cancellation wakes producers asynchronously; receiving the control
            // request does not establish that their timer continuations ran.
            const auto deadline = clock_type::now() + std::chrono::seconds(1);
            while ((!state.completed[0] || !state.completed[1]) && clock_type::now() < deadline)
                co_await cn::async_timer_wait(state.context, std::chrono::milliseconds(1));
            if (!state.completed[0] || !state.completed[1])
            {
                co_await fail(state, "cancelled owned/shared producers remained pending");
                co_return;
            }
            if (state.arguments.scenario == "close-count-zero")
            {
                state.local_close_requested = true;
                (void)co_await state.connection->async_close({}, "count-zero regression close");
                if (state.failed)
                    co_return;
                (void)co_await cn::async_file_write_all(state.context, state.arguments.report, "passed\n");
                co_return;
            }
        }
        if (stage == 1U && state.arguments.scenario == "credit" &&
            !std::ranges::all_of(state.completed, [](bool done) { return done; }))
        {
            co_await fail(state, "directional-credit producers incomplete");
            co_return;
        }
        const auto response = std::string(stage == 0U && state.arguments.scenario == "credit" ? 42U : 2U, 'r');
        const auto sent = co_await state.connection->async_send(*sid,
            std::as_bytes(std::span{response.data(), response.size()}), true);
        if (!sent)
        {
            co_await fail(state, "control response send");
            co_return;
        }
        logger::info{"CREDIT_RECORD control_done {}", stage};
    }
    if (!state.failed)
        (void)co_await cn::async_file_write_all(state.context, state.arguments.report, "passed\n");
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
        config.max_streams_bidi = 16U;
        config.max_streams_uni = 16U;
        config.active_connection_id_limit = 2U;
        config.max_stream_data = 1048576U;
        config.max_data = 16777216U;
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
            schedule(state, deadline);
        });
        // Start both public empty-FIN forms before the first authenticated
        // transport parameter can be consumed. No private connection access.
        state.launch(send_probe(state, 1U, 0U, false, 0U));
        state.launch(send_probe(state, 5U, 0U, true, 1U));
        if (state.arguments.scenario == "credit")
        {
            state.launch(send_probe(state, 9U, 18U, false, 2U));
            state.launch(send_probe(state, 3U, 30U, true, 3U));
        }
        if (state.completed[0] || state.completed[1])
        {
            co_await fail(state, "owned/shared empty FIN completed before authenticated TP");
            co_return;
        }
        logger::info{"CREDIT_RECORD preTP_pending"};
        if (state.arguments.scenario != "credit")
        {
            if (!(co_await state.connection->async_cancel_stream(1U, 7U)) ||
                !(co_await state.connection->async_cancel_stream(5U, 11U)))
            {
                co_await fail(state, "preTP public cancellation");
                co_return;
            }
        }
        const auto processed = co_await state.connection->process_datagram(
            std::span<const std::byte>{storage.data(), *received}, peer);
        if (!processed)
        {
            co_await fail(state, "validated Initial: " + processed.error().message());
            co_return;
        }
        schedule(state, state.connection->next_timer_deadline());
        state.launch(observer_receive(state));
        state.launch(control(state));
        co_return;
    }
}
} // namespace

auto main(int argc, char** argv) -> int
{
    cn::net_init network;
    logger::init("quic-initial-credit", logger::level::info);
    options arguments;
    for (int index = 1; index + 1 < argc; index += 2)
    {
        const std::string_view name{argv[index]};
        const std::string value{argv[index + 1]};
        if (name == "--port") arguments.port = static_cast<std::uint16_t>(std::stoul(value));
        else if (name == "--scenario") arguments.scenario = value;
        else if (name == "--cert") arguments.certificate = value;
        else if (name == "--key") arguments.key = value;
        else if (name == "--listening-report") arguments.listening = value;
        else if (name == "--result-report") arguments.report = value;
        else return 2;
    }
    if (arguments.scenario != "credit" && arguments.scenario != "cancel-final" &&
        arguments.scenario != "close-count-zero")
        return 2;
    auto tls_result = cn::ssl_context::quic_server();
    if (!tls_result)
        return 1;
    auto tls = std::move(*tls_result);
    tls.configure_alpn_server({"quic-initial-credit"});
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
