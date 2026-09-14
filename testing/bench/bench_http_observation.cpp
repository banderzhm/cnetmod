#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
#ifndef CNETMOD_BENCH_RAW_ONLY
import cnetmod.observability.http;
import cnetmod.protocol.http.middleware.tracing;
#endif

namespace {

auto measure(cnetmod::io_context& io, std::string url, std::size_t count, bool& success)
    -> cnetmod::task<void>
{
    cnetmod::http::client raw{io};
#ifndef CNETMOD_BENCH_RAW_ONLY
    cnetmod::observability::instrumented_http_client disabled{raw, {}, {}};
    cnetmod::http::tracing::trace_context parent;
#endif
    const cnetmod::http::request request{cnetmod::http::http_method::GET, url};
    std::vector<double> samples(count);
    std::string expected_body;
    for (std::size_t warmup = 0; warmup < 100; ++warmup)
    {
        auto response = co_await raw.send(request);
        if (!response || response->status_code() != 200)
        {
            co_return;
        }
        expected_body = response->body();
    }
    for (unsigned round = 0; round < 8; ++round)
    {
        constexpr unsigned modes =
#if defined(CNETMOD_BENCH_RAW_ONLY) || defined(CNETMOD_BENCH_DISABLED_ONLY)
            1;
#else
            2;
#endif
        for (unsigned order = 0; order < modes; ++order)
        {
#ifdef CNETMOD_BENCH_RAW_ONLY
            const bool observed = false;
#elif defined(CNETMOD_BENCH_DISABLED_ONLY)
            const bool observed = true;
#else
            const bool observed = ((round + order) % 2) != 0;
#endif
            const auto began = std::chrono::steady_clock::now();
            for (auto& elapsed : samples)
            {
                const auto started = std::chrono::steady_clock::now();
#ifdef CNETMOD_BENCH_RAW_ONLY
                auto response = co_await raw.send(request);
#else
                auto response = observed ? co_await disabled.send(request, parent)
                                         : co_await raw.send(request);
#endif
                elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
                if (!response || response->status_code() != 200 || response->body() != expected_body)
                {
                    co_return;
                }
            }
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            std::ranges::sort(samples);
            logger::info{"round={} mode={} requests={} rps={} p50_us={} p99_us={}",
                round, observed ? "disabled" : "raw", count, count / seconds,
                samples[count / 2], samples[(count - 1) * 99 / 100]};
        }
    }
    success = true;
    raw.close();
}

auto run_measurement(cnetmod::io_context& io, std::string url, bool& success)
    -> cnetmod::task<void>
{
    try
    {
        co_await measure(io, std::move(url), 1000, success);
    }
    catch (...)
    {
        success = false;
    }
    io.stop();
}

} // namespace

auto main(int argc, char** argv) -> int
{
    cnetmod::net_init network;
    logger::init("http-observation-benchmark", logger::level::info);
    if ((argc != 2 && argc != 3) || !std::string_view{argv[1]}.starts_with("http://127.0.0.1:"))
    {
        logger::error{"usage: bench_http_observation http://127.0.0.1:PORT/path [cpu]"};
        logger::shutdown();
        return 2;
    }
    if (argc == 3)
    {
        const std::string_view text{argv[2]};
        unsigned processor{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), processor);
        bool bound = false;
#if defined(CNETMOD_PLATFORM_WINDOWS) || defined(CNETMOD_PLATFORM_LINUX)
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && processor < 65536)
            bound = cnetmod::set_current_thread_affinity(processor).has_value();
#endif
        if (!bound)
        {
            logger::error{"CPU affinity unavailable or invalid; measurement refused"};
            logger::shutdown();
            return 2;
        }
        logger::info{"benchmark_cpu={}", processor};
    }
    auto io = cnetmod::make_io_context();
    bool success{};
    auto work = run_measurement(*io, argv[1], success);
    work.handle().resume();
    if (!io->stopped())
        io->run();
    if (work.handle().done())
        work.handle().promise().result();
    if (!success)
        logger::error{"benchmark failed; incomplete measurements are not valid results"};
    logger::shutdown();
    return success ? 0 : 1;
}
