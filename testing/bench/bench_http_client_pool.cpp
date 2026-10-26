#include <cnetmod/config.hpp>

import std;
import cnetmod.core.log;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.http.client.pool;

namespace {

struct measurement
{
    double nanoseconds_per_operation{};
    double operations_per_second{};
    std::size_t checksum{};
};

auto measure_legacy(cnetmod::http::client_pool& pool,
    std::size_t iterations) -> measurement
{
    const auto started = std::chrono::steady_clock::now();
    std::size_t checksum = 0;
    for (std::size_t index = 0; index < iterations; ++index)
    {
        auto value = pool.acquire();
        checksum += reinterpret_cast<std::uintptr_t>(value.get()) != 0;
        pool.release(std::move(value));
    }
    const auto elapsed = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - started)
                             .count();
    return {.nanoseconds_per_operation = elapsed / iterations,
        .operations_per_second = iterations * 1'000'000'000.0 / elapsed,
        .checksum = checksum};
}

auto measure_bounded(cnetmod::http::bounded_client_pool& pool,
    std::size_t iterations) -> cnetmod::task<measurement>
{
    cnetmod::cancel_token cancellation;
    const auto started = std::chrono::steady_clock::now();
    std::size_t checksum = 0;
    for (std::size_t index = 0; index < iterations; ++index)
    {
        auto value = co_await pool.acquire(cancellation);
        if (!value)
            co_return measurement{};
        checksum += reinterpret_cast<std::uintptr_t>(
                        std::addressof(value->get())) != 0;
    }
    const auto elapsed = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - started)
                             .count();
    co_await pool.close();
    co_return measurement{.nanoseconds_per_operation = elapsed / iterations,
        .operations_per_second = iterations * 1'000'000'000.0 / elapsed,
        .checksum = checksum};
}

void report(std::string_view name, const measurement& value)
{
    logger::info{"{}: {:.2f} ns/op, {:.0f} ops/s, checksum={}",
        name, value.nanoseconds_per_operation, value.operations_per_second,
        value.checksum};
}

} // namespace

auto main() -> int
{
    constexpr std::size_t iterations = 2'000'000;
    auto context = cnetmod::make_io_context();

    cnetmod::http::client_pool legacy{*context, {}, 1};
    auto legacy_result = measure_legacy(legacy, iterations);

    cnetmod::http::bounded_client_pool bounded{
        *context, {.maximum_clients = 1,
                      .maximum_waiters = 1,
                      .acquire_timeout = std::chrono::milliseconds{0}}};
    auto bounded_result = cnetmod::sync_wait(
        measure_bounded(bounded, iterations));

    report("legacy client pool", legacy_result);
    report("bounded client pool", bounded_result);
    if (legacy_result.checksum != iterations ||
        bounded_result.checksum != iterations)
        return 1;
    return 0;
}
