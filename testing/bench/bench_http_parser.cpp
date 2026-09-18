#include <cnetmod/config.hpp>

#if defined(__AVX2__)
    #include <immintrin.h>
#endif

import std;
import cnetmod.core.log;
import cnetmod.protocol.http;

namespace {
#if defined(_MSC_VER)
    #define CNETMOD_NOINLINE __declspec(noinline)
#else
    #define CNETMOD_NOINLINE __attribute__((noinline))
#endif

CNETMOD_NOINLINE auto scalar_find_crlf(const char* data,
    std::size_t size) noexcept -> std::size_t
{
    for (std::size_t index = 0; index + 1 < size; ++index)
        if (data[index] == '\r' && data[index + 1] == '\n')
            return index;
    return std::string_view::npos;
}

CNETMOD_NOINLINE auto vector_find_crlf(const char* data,
    std::size_t size) noexcept -> std::size_t
{
#if defined(__AVX2__)
    const auto carriage_return = _mm256_set1_epi8('\r');
    std::size_t index = 0;
    for (; index + 32 <= size; index += 32)
    {
        auto bytes = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(data + index));
        auto matches = static_cast<std::uint32_t>(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(bytes, carriage_return)));
        while (matches != 0)
        {
            const auto offset = static_cast<std::size_t>(std::countr_zero(matches));
            const auto candidate = index + offset;
            if (candidate + 1 < size && data[candidate + 1] == '\n')
                return candidate;
            matches &= matches - 1;
        }
    }
    const auto tail = scalar_find_crlf(data + index, size - index);
    return tail == std::string_view::npos ? tail : index + tail;
#else
    return scalar_find_crlf(data, size);
#endif
}

struct measurement
{
    double nanoseconds_per_operation{};
    double operations_per_second{};
    std::size_t checksum{};
};

template <typename Operation>
auto measure(std::size_t iterations, Operation&& operation) -> measurement
{
    const auto started = std::chrono::steady_clock::now();
    std::size_t checksum = 0;
    for (std::size_t index = 0; index < iterations; ++index)
        checksum += operation(index);
    const auto elapsed = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - started)
                             .count();
    return {.nanoseconds_per_operation = elapsed / iterations,
        .operations_per_second = iterations * 1'000'000'000.0 / elapsed,
        .checksum = checksum};
}

auto make_request(std::size_t header_count, std::size_t value_size)
    -> std::string
{
    std::string request = "POST /api/orders/42?expand=items HTTP/1.1\r\n";
    for (std::size_t index = 0; index < header_count; ++index)
    {
        request += "X-Benchmark-" + std::to_string(index) + ": ";
        request.append(value_size, static_cast<char>('a' + index % 26));
        request += "\r\n";
    }
    request += "Content-Length: 16\r\n\r\n0123456789abcdef";
    return request;
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
    logger::init("http-parser-benchmark", logger::level::info);
    const auto typical = make_request(8, 24);
    const auto large = make_request(32, 72);
    constexpr std::size_t scan_iterations = 5'000'000;
    constexpr std::size_t parse_iterations = 400'000;

    const auto scalar = measure(scan_iterations, [&](std::size_t index)
        {
            const auto& input = (index & 1U) == 0 ? typical : large;
            return scalar_find_crlf(input.data(), input.size());
        });
    const auto vectorized = measure(scan_iterations, [&](std::size_t index)
        {
            const auto& input = (index & 1U) == 0 ? typical : large;
            return vector_find_crlf(input.data(), input.size());
        });
    const auto complete = measure(parse_iterations, [&](std::size_t index)
        {
            cnetmod::http::request_parser parser;
            const auto& input = (index & 1U) == 0 ? typical : large;
            auto result = parser.consume(input.data(), input.size());
            return result && parser.ready() ? input.size() : 0U;
        });
    const auto fragmented = measure(parse_iterations, [&](std::size_t index)
        {
            cnetmod::http::request_parser parser;
            const auto& input = (index & 1U) == 0 ? typical : large;
            for (std::size_t offset = 0; offset < input.size(); offset += 31)
            {
                const auto count = std::min<std::size_t>(31, input.size() - offset);
                if (!parser.consume(input.data() + offset, count))
                    return std::size_t{};
            }
            return parser.ready() ? input.size() : 0U;
        });

    report("CRLF scalar", scalar);
    report("CRLF AVX2 candidate", vectorized);
    report("request_parser complete", complete);
    report("request_parser fragmented-31", fragmented);
    logger::info{"isolated CRLF speedup: {:.2f}x",
        scalar.nanoseconds_per_operation / vectorized.nanoseconds_per_operation};
    logger::shutdown();
    return complete.checksum != 0 && fragmented.checksum != 0 ? 0 : 1;
}
