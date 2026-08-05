/// cnetmod HTTP/3 end-to-end performance benchmark.
///
/// This benchmark uses the public HTTP/3 client over a real QUIC connection.
/// It never emits successful zero-filled placeholder results: connection or
/// request failures make the process fail and are recorded in the JSON output.

#include <cnetmod/config.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
    #include <time.h>
#endif

import std;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.coro.channel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.session;

namespace {

using clock_type = std::chrono::steady_clock;
using microseconds = std::chrono::microseconds;

// On IOCP, beginning hundreds of UDP handshakes in the same event-loop turn
// can exhaust the receive-posting burst before any of the connections has
// installed its HTTP/3 control streams.  This is deliberately a setup-only
// ramp: every configured connection still completes its warmup, and timed
// measurement does not start until all of them are ready.
constexpr std::size_t connection_start_batch = 4U;
constexpr auto connection_start_interval = std::chrono::milliseconds{2};

struct benchmark_config
{
    std::string host{"127.0.0.1"};
    std::uint16_t port{4433};
    std::string path{"/health"};
    std::size_t connections{1};
    std::size_t client_workers{};
    std::size_t concurrency{32};
    std::size_t requests{1000};
    std::size_t warmup{100};
    std::size_t runs{5};
    std::chrono::milliseconds timeout{5000};
    std::string output{"h3-benchmark-results.json"};
};

struct request_sample
{
    microseconds latency{};
    std::size_t response_bytes{};
    bool successful{};
    std::string error;
};

struct run_result
{
    std::size_t index{};
    std::size_t requested{};
    std::size_t successful{};
    std::size_t failed{};
    std::size_t response_bytes{};
    microseconds elapsed{};
    microseconds p50{};
    microseconds p95{};
    microseconds p99{};
    microseconds minimum{};
    microseconds maximum{};
    double client_cpu_seconds{};
    std::size_t rss_kib{};
    std::vector<std::string> errors;
    std::vector<microseconds> latencies;

    [[nodiscard]] auto qps() const noexcept -> double
    {
        const auto seconds = static_cast<double>(elapsed.count()) / 1'000'000.0;
        return seconds == 0.0 ? 0.0 : static_cast<double>(successful) / seconds;
    }

    [[nodiscard]] auto response_mib_per_second() const noexcept -> double
    {
        const auto seconds = static_cast<double>(elapsed.count()) / 1'000'000.0;
        return seconds == 0.0 ? 0.0
                              : static_cast<double>(response_bytes) / seconds / 1024.0 / 1024.0;
    }
};

[[nodiscard]] auto process_cpu_seconds() noexcept -> double
{
#ifdef _WIN32
    return static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC);
#else
    ::timespec value{};
    if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0)
        return 0.0;
    return static_cast<double>(value.tv_sec) +
        static_cast<double>(value.tv_nsec) / 1'000'000'000.0;
#endif
}

[[nodiscard]] auto resident_memory_kib() noexcept -> std::size_t
{
#ifdef _WIN32
    return 0;
#else
    std::ifstream status{"/proc/self/status"};
    std::string key;
    while (status >> key)
    {
        if (key == "VmRSS:")
        {
            std::size_t value{};
            status >> value;
            return value;
        }
        std::string rest;
        std::getline(status, rest);
    }
    return 0;
#endif
}

[[nodiscard]] auto percentile(const std::vector<microseconds>& sorted, double p)
    -> microseconds
{
    if (sorted.empty())
        return {};
    const auto rank = static_cast<std::size_t>(
        std::ceil(p * static_cast<double>(sorted.size())));
    return sorted[std::min(sorted.size() - 1U, std::max<std::size_t>(1U, rank) - 1U)];
}

auto compute_latency(std::vector<microseconds>& values, run_result& result) -> void
{
    if (values.empty())
        return;
    std::ranges::sort(values);
    result.minimum = values.front();
    result.maximum = values.back();
    result.p50 = percentile(values, 0.50);
    result.p95 = percentile(values, 0.95);
    result.p99 = percentile(values, 0.99);
}

[[nodiscard]] auto make_request(const benchmark_config& config)
    -> cnetmod::http::v3::http3_request
{
    cnetmod::http::v3::http3_request request;
    request.path = config.path;
    request.host = config.host;
    request.port = config.port;
    return request;
}

auto measure_request(cnetmod::http::v3::http3_client& client,
    cnetmod::http::v3::http3_request request, cnetmod::channel<request_sample>& completion)
    -> cnetmod::task<void>
{
    const auto began = clock_type::now();
    auto response = co_await client.send_request(request);
    request_sample sample;
    sample.latency = std::chrono::duration_cast<microseconds>(clock_type::now() - began);
    if (!response)
        sample.error = response.error().message();
    else if (response->status < 200 || response->status >= 300)
        sample.error = std::format("HTTP status {}", response->status);
    else
    {
        sample.successful = true;
        sample.response_bytes = response->body.size();
    }
    (void)co_await completion.send(std::move(sample));
}

auto request_worker(cnetmod::http::v3::http3_client& client,
    const cnetmod::http::v3::http3_request& request,
    cnetmod::channel<request_sample>& completion, std::size_t& next_request,
    std::size_t request_count) -> cnetmod::task<void>
{
    while (next_request < request_count)
    {
        ++next_request;
        co_await measure_request(client, request, completion);
    }
}

auto run_connection(cnetmod::io_context& context, const benchmark_config& config,
    std::atomic<std::size_t>& ready_connections, run_result& result,
    std::atomic<std::size_t>& remaining_connections,
    std::atomic<std::size_t>& connection_start_slots) -> cnetmod::task<void>
{
    result.requested = config.requests;
    const auto finish = [&context, &remaining_connections]
    {
        if (remaining_connections.fetch_sub(1U, std::memory_order_acq_rel) == 1U)
            context.stop();
    };
    const auto start_slot = connection_start_slots.fetch_add(1U, std::memory_order_relaxed);
    const auto start_delay = connection_start_interval *
        static_cast<std::int64_t>(start_slot / connection_start_batch);
    if (start_delay.count() != 0)
        co_await cnetmod::async_sleep(context, start_delay);
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        result.failed = config.requests;
        result.errors.push_back(tls_result.error().message());
        ready_connections.fetch_add(1U, std::memory_order_release);
        finish();
        co_return;
    }
    auto tls = std::move(*tls_result);
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = config.host;
    options.connect_timeout = config.timeout;
    options.request_timeout = config.timeout;
    cnetmod::http::v3::http3_client client{context, tls, std::move(options)};
    auto connected = co_await client.connect(config.host, config.port);
    if (!connected)
    {
        result.failed = config.requests;
        result.errors.push_back(std::format("connect: {}", connected.error().message()));
        ready_connections.fetch_add(1U, std::memory_order_release);
        finish();
        co_return;
    }

    const auto request = make_request(config);
    for (std::size_t warmup{}; warmup < config.warmup; ++warmup)
    {
        auto response = co_await client.send_request(request);
        if (!response || response->status < 200 || response->status >= 300)
        {
            result.failed = config.requests;
            result.errors.push_back(response ? std::format("warmup HTTP status {}", response->status)
                                             : std::format("warmup: {}", response.error().message()));
            ready_connections.fetch_add(1U, std::memory_order_release);
            co_await client.close();
            finish();
            co_return;
        }
    }

    result.latencies.reserve(config.requests);
    ready_connections.fetch_add(1U, std::memory_order_release);
    while (ready_connections.load(std::memory_order_acquire) < config.connections)
        co_await cnetmod::async_sleep(context, std::chrono::microseconds{100});
    const auto began = clock_type::now();
    cnetmod::channel<request_sample> completion{config.concurrency};
    std::size_t next_request{};
    const auto request_workers = std::min(config.concurrency, config.requests);
    for (std::size_t worker{}; worker < request_workers; ++worker)
        cnetmod::spawn(context,
            request_worker(client, request, completion, next_request, config.requests));
    for (std::size_t request_index{}; request_index < config.requests; ++request_index)
    {
        auto sample = co_await completion.receive();
        if (!sample)
        {
            ++result.failed;
            result.errors.emplace_back("request completion channel closed");
            continue;
        }
        if (!sample->successful)
        {
            ++result.failed;
            if (result.errors.size() < 16U)
                result.errors.push_back(std::move(sample->error));
            continue;
        }
        ++result.successful;
        result.response_bytes += sample->response_bytes;
        result.latencies.push_back(sample->latency);
    }
    result.elapsed = std::chrono::duration_cast<microseconds>(clock_type::now() - began);
    compute_latency(result.latencies, result);
    co_await client.close();
    finish();
}

[[nodiscard]] auto run_once(const benchmark_config& config, std::size_t index)
    -> run_result
{
    run_result result;
    result.index = index;
    result.requested = config.requests * config.connections;
    cnetmod::net_init network;
    std::atomic<std::size_t> ready_connections{};
    std::atomic<std::size_t> connection_start_slots{};
    std::vector<run_result> connections(config.connections);
    const auto worker_count = std::min(config.client_workers, config.connections);
    std::vector<std::unique_ptr<cnetmod::io_context>> contexts;
    std::vector<std::shared_ptr<std::atomic<std::size_t>>> remaining;
    contexts.reserve(worker_count);
    remaining.reserve(worker_count);
    for (std::size_t worker{}; worker < worker_count; ++worker)
    {
        contexts.push_back(cnetmod::make_io_context());
        const auto assigned = config.connections / worker_count +
            (worker < config.connections % worker_count ? 1U : 0U);
        remaining.push_back(std::make_shared<std::atomic<std::size_t>>(assigned));
    }
    for (std::size_t connection{}; connection < config.connections; ++connection)
    {
        const auto worker = connection % worker_count;
        cnetmod::spawn(*contexts[worker], run_connection(*contexts[worker], config,
            ready_connections, connections[connection], *remaining[worker],
            connection_start_slots));
    }
    std::vector<std::jthread> threads;
    threads.reserve(worker_count);
    const auto cpu_began = process_cpu_seconds();
    for (auto& context : contexts)
    {
        threads.emplace_back([context = context.get()]
            {
                context->run();
            });
    }
    threads.clear();
    result.client_cpu_seconds = std::max(0.0, process_cpu_seconds() - cpu_began);
    result.rss_kib = resident_memory_kib();

    for (auto& connection : connections)
    {
        result.successful += connection.successful;
        result.failed += connection.failed;
        result.response_bytes += connection.response_bytes;
        result.elapsed = std::max(result.elapsed, connection.elapsed);
        result.latencies.insert(result.latencies.end(),
            std::make_move_iterator(connection.latencies.begin()),
            std::make_move_iterator(connection.latencies.end()));
        for (auto& error : connection.errors)
        {
            if (result.errors.size() < 16U)
                result.errors.push_back(std::move(error));
        }
    }
    compute_latency(result.latencies, result);
    return result;
}

[[nodiscard]] auto json_escape(std::string_view value) -> std::string
{
    std::string escaped;
    for (const auto character : value)
    {
        if (character == '\\' || character == '"')
            escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}

[[nodiscard]] auto platform_name() -> std::string_view
{
#ifdef _WIN32
    return "Windows/IOCP";
#elif defined(__APPLE__)
    return "macOS/kqueue";
#elif defined(CNETMOD_HAS_IO_URING)
    return "Linux/io_uring";
#else
    return "Linux/epoll";
#endif
}

[[nodiscard]] auto compiler_name() -> std::string
{
#ifdef __clang__
    return std::format("Clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
    return std::format("MSVC {}", _MSC_FULL_VER);
#else
    return "unknown";
#endif
}

auto write_results(const benchmark_config& config, const std::vector<run_result>& runs)
    -> bool
{
    std::ofstream output{config.output};
    if (!output)
        return false;
    output << "{\n"
           << "  \"benchmark\": \"cnetmod HTTP/3 end-to-end\",\n"
           << "  \"platform\": \"" << platform_name() << "\",\n"
           << "  \"compiler\": \"" << compiler_name() << "\",\n"
           << "  \"build_type\": \""
#ifdef NDEBUG
           << "Release"
#else
           << "Debug"
#endif
           << "\",\n"
           << "  \"logical_cores\": " << std::thread::hardware_concurrency() << ",\n"
           << "  \"target\": \"" << json_escape(config.host) << ':' << config.port
           << json_escape(config.path) << "\",\n"
           << "  \"connections\": " << config.connections << ",\n"
           << "  \"client_workers\": " << config.client_workers << ",\n"
           << "  \"concurrency\": " << config.concurrency << ",\n"
           << "  \"requests_per_connection\": " << config.requests << ",\n"
           << "  \"warmup_requests_per_connection\": " << config.warmup << ",\n"
           << "  \"runs\": [\n";
    for (std::size_t index{}; index < runs.size(); ++index)
    {
        const auto& run = runs[index];
        output << "    {\"index\": " << run.index
               << ", \"successful\": " << run.successful
               << ", \"failed\": " << run.failed
               << ", \"elapsed_us\": " << run.elapsed.count()
               << ", \"qps\": " << run.qps()
               << ", \"response_mib_per_second\": " << run.response_mib_per_second()
               << ", \"latency_p50_us\": " << run.p50.count()
               << ", \"latency_p95_us\": " << run.p95.count()
               << ", \"latency_p99_us\": " << run.p99.count()
               << ", \"latency_min_us\": " << run.minimum.count()
               << ", \"latency_max_us\": " << run.maximum.count()
               << ", \"client_cpu_seconds\": " << run.client_cpu_seconds
               << ", \"rss_kib\": " << run.rss_kib << ", \"errors\": [";
        for (std::size_t error{}; error < run.errors.size(); ++error)
        {
            if (error != 0U)
                output << ',';
            output << '"' << json_escape(run.errors[error]) << '"';
        }
        output << "]}" << (index + 1U == runs.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    return true;
}

[[nodiscard]] auto parse_arguments(int argc, char** argv) -> benchmark_config
{
    benchmark_config config;
    const auto value = [&](int& index) -> std::string_view
    {
        if (++index >= argc)
            throw std::invalid_argument{"missing option value"};
        return argv[index];
    };
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument == "--host")
            config.host = value(index);
        else if (argument == "--port")
            config.port = static_cast<std::uint16_t>(std::stoul(std::string{value(index)}));
        else if (argument == "--path")
            config.path = value(index);
        else if (argument == "--concurrency")
            config.concurrency = std::stoull(std::string{value(index)});
        else if (argument == "--connections")
            config.connections = std::stoull(std::string{value(index)});
        else if (argument == "--client-workers")
            config.client_workers = std::stoull(std::string{value(index)});
        else if (argument == "--requests")
            config.requests = std::stoull(std::string{value(index)});
        else if (argument == "--warmup")
            config.warmup = std::stoull(std::string{value(index)});
        else if (argument == "--runs")
            config.runs = std::stoull(std::string{value(index)});
        else if (argument == "--timeout")
            config.timeout = std::chrono::milliseconds{std::stoll(std::string{value(index)})};
        else if (argument == "--output")
            config.output = value(index);
        else if (argument == "--help" || argument == "-h")
        {
            std::println("usage: h3_benchmark [--host HOST] [--port PORT] [--path PATH]");
            std::println("                    [--connections N] [--client-workers N]");
            std::println("                    [--concurrency N]");
            std::println("                    [--requests N] [--warmup N]");
            std::println("                    [--runs N] [--timeout MS] [--output FILE]");
            std::exit(0);
        }
        else
            throw std::invalid_argument{std::format("unknown option: {}", argument)};
    }
    if (config.client_workers == 0U)
    {
        const auto hardware = std::max(1U, std::thread::hardware_concurrency());
        config.client_workers = std::min<std::size_t>(
            config.connections, std::max(1U, hardware / 2U));
    }
    if (config.host.empty() || config.path.empty() || config.port == 0U ||
        config.connections == 0U || config.concurrency == 0U ||
        config.requests == 0U || config.runs == 0U)
        throw std::invalid_argument{
            "host, path, port, connections, concurrency, requests and runs must be non-zero"};
    return config;
}

} // namespace

auto main(int argc, char** argv) -> int
{
#if !defined(CNETMOD_HAS_QUIC) || !defined(CNETMOD_HAS_SSL)
    std::cerr << "HTTP/3 benchmark requires QUIC and TLS support\n";
    return 77;
#else
    try
    {
        const auto config = parse_arguments(argc, argv);
        std::vector<run_result> runs;
        runs.reserve(config.runs);
        bool passed = true;
        for (std::size_t index{}; index < config.runs; ++index)
        {
            auto run = run_once(config, index + 1U);
            std::println("run {}: {}/{} succeeded, {:.2f} req/s, p50={}us, p95={}us, " "p99={}us, {:.2f} response MiB/s",
                run.index, run.successful, run.requested, run.qps(), run.p50.count(),
                run.p95.count(), run.p99.count(), run.response_mib_per_second());
            passed = passed && run.failed == 0U && run.successful == run.requested;
            runs.push_back(std::move(run));
        }
        if (!write_results(config, runs))
        {
            std::cerr << "failed to write benchmark results to " << config.output << '\n';
            return 1;
        }
        return passed ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "HTTP/3 benchmark error: " << error.what() << '\n';
        return 2;
    }
#endif
}
