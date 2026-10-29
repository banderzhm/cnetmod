#include "test_framework.hpp"

import std;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.protocol.openai;

namespace {
namespace cn = cnetmod;
enum class mode { complete, aggregate, incomplete, failed, eof, cancel, callback_stop,
    connection_close, unknown_length,
    error_length, error_coalesced, error_chunked };
struct observation
{
    bool seen = false, incremental = false, request_valid = false;
    bool done = false, connection_closed = false, server_closed = false, early_return = false;
    std::string deltas, error;
    std::optional<cn::openai::response_result> result;
};

auto write_fragmented(cn::io_context& io, cn::socket& socket, std::string_view data,
    std::size_t width) -> cn::task<bool>
{
    for (std::size_t position = 0; position < data.size(); position += width) {
        const auto part = data.substr(position, std::min(width, data.size() - position));
        auto written = co_await cn::async_write_all(io, socket,
            cn::const_buffer{part.data(), part.size()});
        if (!written) co_return false;
        co_await cn::async_sleep(io, std::chrono::milliseconds{1});
    }
    co_return true;
}

auto chunk(std::string_view data) -> std::string
{
    return std::format("{:x}\r\n{}\r\n", data.size(), data);
}

auto mock_http_error(cn::io_context& io, cn::socket& peer, mode selected,
    observation& observed) -> cn::task<void>
{
    constexpr std::string_view body = R"({"error":{"message":"bad key"}})";
    auto wire = selected != mode::error_chunked
        ? std::format("HTTP/1.1 401 Unauthorized\r\nContent-Length: {}\r\nConnection: keep-alive\r\n\r\n{}", body.size(), body)
        : std::format("HTTP/1.1 429 Too Many Requests\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n{}{}0\r\n\r\n", chunk(body.substr(0, 7)), chunk(body.substr(7)));
    const auto width = selected == mode::error_coalesced ? wire.size() : 3U;
    if (!(co_await write_fragmented(io, peer, wire, width))) co_return;
    co_await cn::async_sleep(io, std::chrono::seconds{2});
    observed.server_closed = true;
    peer.close();
}

auto tail_events(mode selected) -> std::string
{
    if (selected == mode::failed)
        return "data: {\"type\":\"response.failed\"}\n\n";
    if (selected == mode::incomplete)
        return "data: {\"type\":\"response.incomplete\",\"response\":{\"id\":\"r1\",\"status\":\"incomplete\",\"usage\":{\"input_tokens\":3,\"output_tokens\":2,\"total_tokens\":5}}}\n\n";
    if (selected == mode::aggregate)
        return R"(data: {"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","call_id":"call-1","name":"lookup","arguments":""}}

data: {"type":"response.function_call_arguments.delta","output_index":0,"delta":"{\"q\":"}

data: {"type":"response.function_call_arguments.delta","output_index":0,"delta":"1}"}

data: {"type":"response.completed","response":{"id":"r1","status":"completed","output":[],"usage":{"input_tokens":3,"output_tokens":2,"total_tokens":5}}}

)";
    return R"(data: {"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","call_id":"call-1","name":"lookup","arguments":""}}

data: {"type":"response.function_call_arguments.delta","output_index":0,"delta":"{\"q\":"}

data: {"type":"response.function_call_arguments.delta","output_index":0,"delta":"1}"}

data: {"type":"response.function_call_arguments.done","output_index":0,"arguments":"{\"q\":1}"}

data: {"type":"response.completed","response":{"id":"r1","model":"fixture","status":"completed","output":[{"type":"function_call","call_id":"call-1","name":"lookup","arguments":"{\"q\":1}"},{"type":"message","content":[{"type":"output_text","text":"你"}]}],"usage":{"input_tokens":3,"output_tokens":2,"total_tokens":5}}}

)";
}

auto mock_server(cn::io_context& io, cn::socket& listener, mode selected,
    observation& seen) -> cn::task<void>
{
    auto peer = co_await cn::async_accept(io, listener);
    if (!peer) co_return;
    std::array<char, 8192> request{};
    auto read = co_await cn::async_read(io, *peer,
        cn::mutable_buffer{request.data(), request.size()});
    if (!read) co_return;
    auto wire = std::string_view{request.data(), *read};
    seen.request_valid = wire.contains("POST /v1/responses") && wire.contains("\"stream\":true");
    if (selected == mode::error_length || selected == mode::error_coalesced ||
        selected == mode::error_chunked) {
        co_await mock_http_error(io, *peer, selected, seen);
        co_return;
    }
    const std::string_view header = selected == mode::unknown_length
        ? "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n"
        : selected == mode::connection_close
        ? "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
        : "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\n\r\n";
    if (!(co_await write_fragmented(io, *peer, header, 3))) co_return;
    const std::string raw_delta = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"你\"}\r\n\r\n";
    const auto delta = selected == mode::unknown_length ? raw_delta : chunk(raw_delta);
    if (!(co_await write_fragmented(io, *peer, delta, 1))) co_return;
    for (int i = 0; i < 500 && !seen.seen; ++i)
        co_await cn::async_sleep(io, std::chrono::milliseconds{1});
    seen.incremental = seen.seen;
    if (selected == mode::eof) { peer->close(); co_return; }
    if (selected == mode::cancel || selected == mode::callback_stop) co_return;
    auto tail = selected == mode::unknown_length ? tail_events(selected) : chunk(tail_events(selected));
    if (!(co_await write_fragmented(io, *peer, tail, 7))) co_return;
    if (selected != mode::unknown_length)
        (void)co_await write_fragmented(io, *peer, "0\r\n\r\n", 1);
}

auto run_client(cn::io_context& io, std::uint16_t port, mode selected,
    observation& observed) -> cn::task<void>
{
    cn::openai::client client{io};
    auto connected = co_await client.connect({.api_base = std::format("http://127.0.0.1:{}/v1", port),
        .api_key = "fixture", .timeout_seconds = 5});
    if (!connected) { observed.error = connected.error(); observed.done = true; io.stop(); co_return; }
    cn::cancel_token cancellation;
    cn::openai::response_request request{.model="fixture", .input={cn::openai::message::user("hello")}};
    auto result = co_await client.responses_stream(std::move(request),
        [&](std::string_view text) -> cn::task<bool> {
            observed.deltas.append(text); observed.seen = true;
            if (selected == mode::cancel) cancellation.cancel();
            co_return selected != mode::callback_stop;
        }, cancellation);
    observed.connection_closed = !client.is_connected();
    observed.early_return = !observed.server_closed;
    if (result) observed.result = std::move(*result); else observed.error = result.error();
    observed.done = true;
    io.stop();
}

auto exercise(mode selected) -> observation
{
    cn::net_init network;
    auto io = cn::make_io_context();
    auto listener = cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener) return {};
    ASSERT_TRUE(listener->bind({cn::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint) return {};
    observation result;
    auto watchdog = [&]() -> cn::task<void> {
        co_await cn::async_sleep(*io, std::chrono::seconds{10});
        if (!result.done) io->stop();
    };
    cn::spawn(*io, mock_server(*io, *listener, selected, result));
    cn::spawn(*io, run_client(*io, endpoint->port(), selected, result));
    cn::spawn(*io, watchdog());
    io->run();
    ASSERT_TRUE(result.done);
    ASSERT_TRUE(result.request_valid);
    if (selected == mode::complete || selected == mode::aggregate ||
        selected == mode::incomplete) ASSERT_FALSE(result.connection_closed);
    else ASSERT_TRUE(result.connection_closed);
    return result;
}

auto read_request(cn::io_context& io, cn::socket& peer) -> cn::task<bool>
{
    std::string request;
    std::array<char, 8192> bytes{};
    for (;;) {
        auto got = co_await cn::async_read(io, peer, cn::mutable_buffer{bytes.data(), bytes.size()});
        if (!got || *got == 0) co_return false;
        request.append(bytes.data(), *got);
        const auto end = request.find("\r\n\r\n");
        if (end == std::string::npos) continue;
        auto header = request.substr(0, end);
        std::ranges::transform(header, header.begin(), [](unsigned char c) { return std::tolower(c); });
        const auto pos = header.find("content-length:");
        if (pos == std::string::npos) co_return false;
        auto value = std::string_view{header}.substr(pos + 15);
        while (value.starts_with(' ')) value.remove_prefix(1);
        std::size_t length = 0;
        auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
        if (parsed.ec != std::errc{}) co_return false;
        if (request.size() >= end + 4 + length) co_return true;
    }
}

auto keepalive_server(cn::io_context& io, cn::socket& listener, observation& seen)
    -> cn::task<void>
{
    auto peer = co_await cn::async_accept(io, listener);
    if (!peer || !(co_await read_request(io, *peer))) co_return;
    const auto complete = chunk(tail_events(mode::complete));
    const auto header = std::string{"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n"};
    if (!(co_await write_fragmented(io, *peer, header + complete, 512))) co_return;
    co_await cn::async_sleep(io, std::chrono::milliseconds{50});
    const auto tail = chunk("data: [DONE]\n\n") + "0\r\n\r\n";
    if (!(co_await write_fragmented(io, *peer, tail, 3))) co_return;
    if (!(co_await read_request(io, *peer))) co_return;
    seen.request_valid = true;
    (void)co_await write_fragmented(io, *peer, header + complete + tail, 512);
}

auto keepalive_client(cn::io_context& io, std::uint16_t port, observation& seen)
    -> cn::task<void>
{
    cn::openai::client client{io};
    auto connected = co_await client.connect({.api_base = std::format("http://127.0.0.1:{}/v1", port),
        .api_key = "fixture", .timeout_seconds = 5});
    if (!connected) { seen.error = connected.error(); seen.done = true; io.stop(); co_return; }
    for (int i = 0; i < 2; ++i) {
        cn::openai::response_request request{.model="fixture", .input={cn::openai::message::user("hello")}};
        auto result = co_await client.responses_stream(std::move(request), {});
        if (!result || !client.is_connected()) {
            seen.error = result ? "connection unexpectedly closed" : result.error();
            seen.done = true; io.stop(); co_return;
        }
    }
    seen.done = true;
    io.stop();
}

auto reconnect_server(cn::io_context& io, cn::socket& listener, observation& seen)
    -> cn::task<void>
{
    auto first = co_await cn::async_accept(io, listener);
    if (!first || !(co_await read_request(io, *first))) co_return;
    (void)co_await write_fragmented(io, *first, "HTTP/1.1 200 OK\r\nX-Partial: ", 128);
    first->close();
    auto second = co_await cn::async_accept(io, listener);
    if (!second || !(co_await read_request(io, *second))) co_return;
    seen.request_valid = true;
    const auto wire = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" +
        chunk(tail_events(mode::complete)) + "0\r\n\r\n";
    (void)co_await write_fragmented(io, *second, wire, 512);
}

auto reconnect_client(cn::io_context& io, std::uint16_t port, observation& seen)
    -> cn::task<void>
{
    cn::openai::client client{io};
    auto connected = co_await client.connect({.api_base = std::format("http://127.0.0.1:{}/v1", port),
        .api_key = "fixture", .timeout_seconds = 5});
    if (!connected) { seen.error = connected.error(); seen.done = true; io.stop(); co_return; }
    cn::openai::response_request first{.model="fixture", .input={cn::openai::message::user("hello")}};
    auto failed = co_await client.responses_stream(std::move(first), {});
    if (failed || client.is_connected()) {
        seen.error = "failed response remained connected"; seen.done = true; io.stop(); co_return;
    }
    cn::openai::response_request second{.model="fixture", .input={cn::openai::message::user("hello")}};
    auto recovered = co_await client.responses_stream(std::move(second), {});
    if (!recovered) seen.error = recovered.error();
    seen.done = true;
    io.stop();
}
} // namespace

TEST(responses_stream_fragmented_unicode_tools_and_usage)
{
    auto result = exercise(mode::complete);
    ASSERT_TRUE(result.incremental);
    ASSERT_EQ(result.deltas, std::string("你"));
    ASSERT_TRUE(result.result.has_value());
    if (!result.result) return;
    ASSERT_EQ(result.result->status, std::string("completed"));
    ASSERT_EQ(result.result->output_text, std::string("你"));
    ASSERT_EQ(result.result->token_usage.total_tokens, 5);
    ASSERT_EQ(result.result->tool_calls.size(), std::size_t{1});
    if (!result.result->tool_calls.empty()) {
        ASSERT_EQ(result.result->tool_calls[0].id, std::string("call-1"));
        ASSERT_EQ(result.result->tool_calls[0].function.arguments, std::string("{\"q\":1}"));
    }
}
TEST(responses_stream_incomplete_preserves_status)
{
    auto result = exercise(mode::incomplete);
    ASSERT_TRUE(result.result.has_value());
    if (result.result) ASSERT_EQ(result.result->status, std::string("incomplete"));
}
TEST(responses_stream_aggregates_tool_deltas)
{
    auto result = exercise(mode::aggregate);
    ASSERT_TRUE(result.result.has_value());
    if (!result.result) return;
    ASSERT_EQ(result.result->tool_calls.size(), std::size_t{1});
    if (!result.result->tool_calls.empty()) {
        ASSERT_EQ(result.result->tool_calls[0].function.name, std::string("lookup"));
        ASSERT_EQ(result.result->tool_calls[0].function.arguments, std::string("{\"q\":1}"));
    }
}
TEST(responses_stream_failed_and_early_eof_are_errors)
{
    ASSERT_FALSE(exercise(mode::failed).error.empty());
    ASSERT_FALSE(exercise(mode::eof).error.empty());
}
TEST(responses_stream_cancel_and_callback_abort)
{
    ASSERT_FALSE(exercise(mode::cancel).error.empty());
    ASSERT_FALSE(exercise(mode::callback_stop).error.empty());
}
TEST(responses_stream_http_errors_finish_without_waiting_for_close)
{
    auto length = exercise(mode::error_length);
    ASSERT_TRUE(length.early_return);
    ASSERT_TRUE(length.error.contains("HTTP 401"));
    ASSERT_TRUE(length.error.contains("bad key"));
    auto coalesced = exercise(mode::error_coalesced);
    ASSERT_TRUE(coalesced.early_return);
    ASSERT_TRUE(coalesced.error.contains("HTTP 401"));
    ASSERT_TRUE(coalesced.error.contains("bad key"));
    auto chunked = exercise(mode::error_chunked);
    ASSERT_TRUE(chunked.early_return);
    ASSERT_TRUE(chunked.error.contains("HTTP 429"));
    ASSERT_TRUE(chunked.error.contains("bad key"));
}
TEST(responses_stream_reuses_connection_after_delayed_chunk_trailers)
{
    cn::net_init network;
    auto io = cn::make_io_context();
    auto listener = cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener) return;
    ASSERT_TRUE(listener->bind({cn::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint) return;
    observation seen;
    auto watchdog = [&]() -> cn::task<void> {
        co_await cn::async_sleep(*io, std::chrono::seconds{10});
        if (!seen.done) io->stop();
    };
    cn::spawn(*io, keepalive_server(*io, *listener, seen));
    cn::spawn(*io, keepalive_client(*io, endpoint->port(), seen));
    cn::spawn(*io, watchdog());
    io->run();
    ASSERT_TRUE(seen.done);
    ASSERT_TRUE(seen.request_valid);
    ASSERT_TRUE(seen.error.empty());
}
TEST(responses_stream_closes_unframed_and_server_close_connections)
{
    ASSERT_TRUE(exercise(mode::connection_close).result.has_value());
    ASSERT_TRUE(exercise(mode::unknown_length).result.has_value());
}
TEST(responses_stream_reconnect_discards_partial_header)
{
    cn::net_init network;
    auto io = cn::make_io_context();
    auto listener = cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener) return;
    ASSERT_TRUE(listener->bind({cn::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint) return;
    observation seen;
    auto watchdog = [&]() -> cn::task<void> {
        co_await cn::async_sleep(*io, std::chrono::seconds{10});
        if (!seen.done) io->stop();
    };
    cn::spawn(*io, reconnect_server(*io, *listener, seen));
    cn::spawn(*io, reconnect_client(*io, endpoint->port(), seen));
    cn::spawn(*io, watchdog());
    io->run();
    ASSERT_TRUE(seen.done);
    ASSERT_TRUE(seen.request_valid);
    ASSERT_TRUE(seen.error.empty());
}
RUN_TESTS()
