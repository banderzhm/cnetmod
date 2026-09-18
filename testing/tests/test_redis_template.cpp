#include "test_framework.hpp"

import std;
import cnetmod.protocol.redis;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.instrumentation.tracing;
import cnetmod.io.io_context;
import cnetmod.json;

namespace {

struct scripted_step
{
    cnetmod::redis::request expected;
    std::vector<std::string> reply_fragments;
};

[[nodiscard]] auto command(std::initializer_list<std::string_view> arguments)
    -> cnetmod::redis::request
{
    cnetmod::redis::request result;
    std::vector<std::string> copied;
    copied.reserve(arguments.size());
    for (const auto argument : arguments)
        copied.emplace_back(argument);
    (void)result.push(copied);
    return result;
}

[[nodiscard]] auto pipeline(
    std::initializer_list<std::initializer_list<std::string_view>> commands)
    -> cnetmod::redis::request
{
    cnetmod::redis::request result;
    for (const auto arguments : commands)
    {
        std::vector<std::string> copied;
        copied.reserve(arguments.size());
        for (const auto argument : arguments)
            copied.emplace_back(argument);
        (void)result.push(copied);
    }
    return result;
}

auto read_request(cnetmod::io_context& io, cnetmod::socket& peer,
    std::string_view expected) -> cnetmod::task<bool>
{
    std::string received(expected.size(), '\0');
    std::size_t offset = 0;
    while (offset < received.size())
    {
        auto result = co_await cnetmod::async_read(io, peer,
            cnetmod::mutable_buffer{
                received.data() + offset, received.size() - offset});
        if (!result || *result == 0U)
            co_return false;
        offset += *result;
    }
    co_return received == expected;
}

auto write_fragments(cnetmod::io_context& io, cnetmod::socket& peer,
    const std::vector<std::string>& fragments) -> cnetmod::task<bool>
{
    for (std::size_t index = 0; index < fragments.size(); ++index)
    {
        const auto& fragment = fragments[index];
        auto written = co_await cnetmod::async_write_all(io, peer,
            cnetmod::const_buffer{fragment.data(), fragment.size()});
        if (!written)
            co_return false;
        if (index + 1U < fragments.size())
            co_await cnetmod::async_sleep(io, std::chrono::milliseconds{1});
    }
    co_return true;
}

[[nodiscard]] auto pool_parameters(std::uint16_t port)
    -> cnetmod::redis::pool_params
{
    return {.host = "127.0.0.1",
        .port = port,
        .resp3 = false,
        .initial_size = 1,
        .max_size = 1,
        .connect_timeout = std::chrono::seconds{1},
        .pool_timeout = std::chrono::seconds{1},
        .retry_interval = std::chrono::milliseconds{10},
        .ping_interval = std::chrono::hours{1}};
}

} // namespace

TEST(redis_template_normalizes_values_collections_scans_pipeline_and_spans)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                   cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!listener || !endpoint)
        return;

    const std::string large(13000, 'L');
    const auto large_wire = "$13000\r\n" + large + "\r\n";
    const auto mget_wire = "*2\r\n" + large_wire + "$-1\r\n";
    std::vector<scripted_step> steps;
    steps.push_back({command({"GET", "app:missing"}), {"$-1\r\n"}});
    steps.push_back({command({"GET", "app:large"}),
        {large_wire.substr(0, 6300), large_wire.substr(6300)}});
    steps.push_back({command({"SET", "app:value", "saved", "EX", "9"}),
        {"+OK\r\n"}});
    steps.push_back({command({"DEL", "app:value"}), {":1\r\n"}});
    steps.push_back({command({"EXISTS", "app:value"}), {":0\r\n"}});
    steps.push_back({command({"INCRBY", "app:counter", "4"}), {":5\r\n"}});
    steps.push_back({command({"EXPIRE", "app:counter", "7"}), {":1\r\n"}});
    steps.push_back({command({"PTTL", "app:counter"}), {":-1\r\n"}});
    steps.push_back({command({"MGET", "app:large", "app:missing"}),
        {mget_wire.substr(0, 6400), mget_wire.substr(6400)}});
    steps.push_back({pipeline({{"HSET", "app:hash", "field", "value"},
                         {"EXPIRE", "app:hash", "5"}}),
        {":1\r\n:1\r\n"}});
    steps.push_back(
        {command({"HGET", "app:hash", "missing"}), {"$-1\r\n"}});
    steps.push_back({command({"HGETALL", "app:hash"}),
        {"%2\r\n$1\r\na\r\n$1\r\n1\r\n$1\r\nb\r\n$1\r\n2\r\n"}});
    steps.push_back({command({"HDEL", "app:hash", "field"}), {":1\r\n"}});
    steps.push_back({pipeline({{"SADD", "app:set", "a"},
                         {"EXPIRE", "app:set", "9"}}),
        {":1\r\n:1\r\n"}});
    steps.push_back({command({"SREM", "app:set", "a"}), {":1\r\n"}});
    steps.push_back({command({"SISMEMBER", "app:set", "a"}), {":0\r\n"}});
    steps.push_back({command({"SSCAN", "app:set", "0", "COUNT", "2"}),
        {"*2\r\n$1\r\n7\r\n*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\na\r\n"}});
    steps.push_back({command({"SSCAN", "app:set", "7", "COUNT", "2"}),
        {"*2\r\n$1\r\n0\r\n*2\r\n$1\r\nb\r\n$1\r\nc\r\n"}});
    steps.push_back({pipeline({{"GET", "app:a"}, {"GET", "app:b"},
                         {"INCRBY", "app:c", "1"},
                         {"private-secret", "app:key"}}),
        {"$1\r\nx\r\n-WRONGTYPE hidden-detail\r\n:2\r\n+OK\r\n"}});

    cnetmod::redis::connection_pool pool{*io,
        pool_parameters(endpoint->port())};
    std::vector<cnetmod::instrumentation::completed_span> spans;
    cnetmod::instrumentation::span_exporter exporter =
        [&](const cnetmod::instrumentation::completed_span& span)
    {
        spans.push_back(span);
    };
    cnetmod::redis::redis_template redis{pool,
        {.ns = {.prefix = "app:"},
            .default_ttl = std::chrono::seconds{9},
            .scan_page = 2,
            .scan_limit = 10},
        cnetmod::instrumentation::new_root_context(), exporter};

    bool server_ok = false;
    bool exercise_ok = false;
    bool run_finished = false;
    bool server_finished = false;
    std::size_t completed_steps = 0;

    auto server = [&]() -> cnetmod::task<void>
    {
        auto peer = co_await cnetmod::async_accept(*io, *listener);
        if (!peer)
        {
            server_finished = true;
            co_return;
        }
        for (const auto& step : steps)
        {
            if (!(co_await read_request(*io, *peer, step.expected.payload())) ||
                !(co_await write_fragments(*io, *peer, step.reply_fragments)))
            {
                pool.request_stop();
                server_finished = true;
                co_return;
            }
            ++completed_steps;
        }
        server_ok = true;
        server_finished = true;
    };
    auto runner = [&]() -> cnetmod::task<void>
    {
        co_await pool.async_run();
        run_finished = true;
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto missing = co_await redis.get("missing");
        auto loaded = co_await redis.get("large");
        auto saved = co_await redis.set("value", "saved");
        auto deleted = co_await redis.del("value");
        auto present = co_await redis.exists("value");
        auto incremented = co_await redis.incr("counter", 4);
        auto expired = co_await redis.expire("counter", std::chrono::seconds{7});
        auto remaining = co_await redis.pttl("counter");
        const std::vector<std::string> keys{"large", "missing"};
        auto values = co_await redis.mget(keys);
        auto inserted = co_await redis.hset(
            "hash", "field", "value", std::chrono::seconds{5});
        auto hash_missing = co_await redis.hget("hash", "missing");
        auto hash = co_await redis.hgetall("hash");
        auto hash_deleted = co_await redis.hdel("hash", "field");
        auto member_added = co_await redis.sadd("set", "a");
        auto member_removed = co_await redis.srem("set", "a");
        auto member_present = co_await redis.sismember("set", "a");
        auto members = co_await redis.sscan_all("set");
        auto batch = redis.pipeline();
        batch.get("a").get("b").incr("c");
        std::vector<std::string> advanced{"private-secret", "app:key"};
        batch.raw_command(advanced);
        auto piped = co_await redis.execute(batch);

        exercise_ok = missing && !*missing && loaded && *loaded &&
            **loaded == large && saved && deleted && *deleted && present &&
            !*present && incremented && *incremented == 5 && expired &&
            *expired && remaining && *remaining &&
            **remaining == std::chrono::milliseconds{-1} && values &&
            values->size() == 2U && (*values)[0] && *(*values)[0] == large &&
            !(*values)[1] && inserted && *inserted && hash_missing &&
            !*hash_missing && hash && hash->size() == 2U &&
            *hash == std::vector<std::pair<std::string, std::string>>({{"a", "1"}, {"b", "2"}}) &&
            hash_deleted && *hash_deleted && member_added && *member_added &&
            member_removed && *member_removed && member_present &&
            !*member_present && members &&
            *members == std::vector<std::string>({"a", "b", "c"}) && piped &&
            piped->size() == 4U && (*piped)[0] && !(*piped)[1] &&
            (*piped)[1].error() == cnetmod::redis::make_error_code(cnetmod::redis::redis_errc::resp3_simple_error) &&
            (*piped)[2] && (*piped)[3];

        co_await pool.cancel();
        while (!run_finished || !server_finished)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        io->stop();
    };

    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, runner());
    cnetmod::spawn(*io, exercise());
    io->run();

    ASSERT_TRUE(server_ok);
    ASSERT_EQ(completed_steps, steps.size());
    ASSERT_TRUE(exercise_ok);
    ASSERT_TRUE(spans.size() >= steps.size());
    bool saw_get = false;
    bool saw_failed_pipeline_item = false;
    for (const auto& span : spans)
    {
        for (const auto& [name, value] : span.attributes)
        {
            ASSERT_FALSE(value.contains("app:"));
            ASSERT_FALSE(value.contains("hidden-detail"));
            if (name == "db.system.name")
                ASSERT_EQ(value, std::string{"redis"});
            if (name == "db.operation.name" && value == "GET")
                saw_get = true;
        }
        saw_failed_pipeline_item = saw_failed_pipeline_item || span.failed;
    }
    ASSERT_TRUE(saw_get);
    ASSERT_TRUE(saw_failed_pipeline_item);
}

TEST(redis_template_cancellation_discards_partial_connection_and_recovers)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                   cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!listener || !endpoint)
        return;

    cnetmod::redis::connection_pool pool{*io,
        pool_parameters(endpoint->port())};
    cnetmod::redis::redis_template redis{pool,
        {.ns = {.prefix = "app:"}}};
    const auto expected = command({"GET", "app:blocked"});
    bool first_closed = false;
    bool replacement_used = false;
    bool timed_out = false;
    bool recovered = false;
    bool run_finished = false;
    bool server_finished = false;

    auto server = [&]() -> cnetmod::task<void>
    {
        auto first = co_await cnetmod::async_accept(*io, *listener);
        if (first && co_await read_request(*io, *first, expected.payload()))
        {
            constexpr std::string_view partial = "$13000\r\npartial";
            (void)co_await cnetmod::async_write_all(*io, *first,
                cnetmod::const_buffer{partial.data(), partial.size()});
            char byte{};
            auto closed = co_await cnetmod::async_read(*io, *first,
                cnetmod::mutable_buffer{&byte, 1});
            first_closed = !closed || *closed == 0U;
        }
        auto replacement = co_await cnetmod::async_accept(*io, *listener);
        if (replacement &&
            co_await read_request(*io, *replacement, expected.payload()))
        {
            constexpr std::string_view response = "$2\r\nok\r\n";
            replacement_used = co_await write_fragments(
                *io, *replacement, {std::string{response}});
        }
        server_finished = true;
    };
    auto runner = [&]() -> cnetmod::task<void>
    {
        co_await pool.async_run();
        run_finished = true;
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token first;
        auto interrupted = co_await cnetmod::with_timeout(*io,
            std::chrono::milliseconds{20}, redis.get("blocked", first), first);
        timed_out = !interrupted &&
            interrupted.error() == std::make_error_code(std::errc::timed_out);
        cnetmod::cancel_token second;
        auto loaded = co_await cnetmod::with_timeout(*io,
            std::chrono::seconds{1}, redis.get("blocked", second), second);
        recovered = loaded && *loaded && **loaded == "ok";
        co_await pool.cancel();
        while (!run_finished || !server_finished)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        io->stop();
    };

    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, runner());
    cnetmod::spawn(*io, exercise());
    io->run();
    ASSERT_TRUE(timed_out);
    ASSERT_TRUE(first_closed);
    ASSERT_TRUE(replacement_used);
    ASSERT_TRUE(recovered);
}

TEST(redis_template_scan_limit_fails_before_returning_partial_results)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                   cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!listener || !endpoint)
        return;

    cnetmod::redis::connection_pool pool{*io,
        pool_parameters(endpoint->port())};
    cnetmod::redis::redis_template redis{pool,
        {.ns = {.prefix = "app:"}, .scan_page = 3, .scan_limit = 2}};
    const auto expected = command({"SSCAN", "app:set", "0", "COUNT", "3"});
    bool rejected = false;
    bool run_finished = false;
    bool server_finished = false;

    auto server = [&]() -> cnetmod::task<void>
    {
        auto peer = co_await cnetmod::async_accept(*io, *listener);
        if (peer && co_await read_request(*io, *peer, expected.payload()))
        {
            constexpr std::string_view response =
                "*2\r\n$1\r\n0\r\n*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n";
            (void)co_await cnetmod::async_write_all(*io, *peer,
                cnetmod::const_buffer{response.data(), response.size()});
        }
        server_finished = true;
    };
    auto runner = [&]() -> cnetmod::task<void>
    {
        co_await pool.async_run();
        run_finished = true;
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto members = co_await redis.sscan_all("set");
        rejected = !members && members.error() == std::make_error_code(std::errc::value_too_large);
        co_await pool.cancel();
        while (!run_finished || !server_finished)
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        io->stop();
    };

    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, runner());
    cnetmod::spawn(*io, exercise());
    io->run();
    ASSERT_TRUE(rejected);
}

TEST(redis_template_scan_honors_precancelled_token_without_network_io)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::redis::connection_pool pool{*io,
        {.initial_size = 0, .max_size = 1}};
    cnetmod::redis::redis_template redis{pool};
    cnetmod::cancel_token cancellation;
    cancellation.cancel();
    auto result = cnetmod::sync_wait(
        redis.sscan_all("never-sent", cancellation));
    ASSERT_FALSE(result.has_value());
    if (!result)
        ASSERT_EQ(result.error(),
            std::make_error_code(std::errc::operation_canceled));
}

TEST(redis_template_json_codec_round_trips_and_rejects_invalid_data)
{
    auto encoded = cnetmod::redis::json_codec::encode(
        std::vector<int>{1, 2, 3});
    ASSERT_TRUE(encoded.has_value());
    auto decoded = encoded
        ? cnetmod::redis::json_codec::decode<std::vector<int>>(*encoded)
        : std::expected<std::vector<int>, std::error_code>{
              std::unexpected(std::make_error_code(std::errc::invalid_argument))};
    ASSERT_TRUE(decoded.has_value());
    if (decoded)
        ASSERT_TRUE(*decoded == std::vector<int>({1, 2, 3}));
    auto invalid = cnetmod::redis::json_codec::decode<std::vector<int>>("{");
    ASSERT_FALSE(invalid.has_value());
    if (!invalid)
        ASSERT_EQ(invalid.error(), cnetmod::json::make_error_code(cnetmod::json::errc::parse_failed));
}

RUN_TESTS()
