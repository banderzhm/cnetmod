#pragma once

/// cnetmod benchmark framework — Lightweight C++23 micro-benchmark harness
///
/// Usage:
///   BENCH("task<int> sync_wait", 1'000'000) {
///       auto result = sync_wait(make_task());
///       do_not_optimize(result);
///   }
///   int main() { return cnetmod::bench::run_all(); }

import std;

namespace cnetmod::bench {

// =============================================================================
// Prevent compiler optimization of benchmark results
// =============================================================================

template <typename T>
inline void do_not_optimize(T const& value) {
    // Use volatile to prevent compiler from optimizing away the value
    volatile auto sink = &value;
    (void)sink;
}

inline void clobber_memory() {
#ifdef _MSC_VER
    _ReadWriteBarrier();
#else
    asm volatile("" ::: "memory");
#endif
}

// =============================================================================
// Benchmark entry
// =============================================================================

struct bench_entry {
    std::string name;
    std::string category;
    std::size_t iterations;
    std::function<void(std::size_t)> fn;  // fn(iterations)
};

inline std::vector<bench_entry>& registry() {
    static std::vector<bench_entry> entries;
    return entries;
}

struct registrar {
    registrar(std::string name, std::string category,
              std::size_t iterations, std::function<void(std::size_t)> fn) {
        registry().push_back({std::move(name), std::move(category), iterations, std::move(fn)});
    }
};

// =============================================================================
// Formatting helpers
// =============================================================================

inline std::string format_number(double n) {
    char buf[64];
    if (n >= 1e9) std::snprintf(buf, sizeof(buf), "%.2f B", n / 1e9);
    else if (n >= 1e6) std::snprintf(buf, sizeof(buf), "%.2f M", n / 1e6);
    else if (n >= 1e3) std::snprintf(buf, sizeof(buf), "%.2f K", n / 1e3);
    else std::snprintf(buf, sizeof(buf), "%.2f", n);
    return buf;
}

inline std::string format_duration(double ns) {
    char buf[64];
    if (ns >= 1e6) std::snprintf(buf, sizeof(buf), "%.2f ms", ns / 1e6);
    else if (ns >= 1e3) std::snprintf(buf, sizeof(buf), "%.2f us", ns / 1e3);
    else std::snprintf(buf, sizeof(buf), "%.1f ns", ns);
    return buf;
}

inline std::string format_throughput(double bytes_per_sec) {
    char buf[64];
    if (bytes_per_sec >= 1e9) std::snprintf(buf, sizeof(buf), "%.2f GB/s", bytes_per_sec / 1e9);
    else if (bytes_per_sec >= 1e6) std::snprintf(buf, sizeof(buf), "%.2f MB/s", bytes_per_sec / 1e6);
    else if (bytes_per_sec >= 1e3) std::snprintf(buf, sizeof(buf), "%.2f KB/s", bytes_per_sec / 1e3);
    else std::snprintf(buf, sizeof(buf), "%.2f B/s", bytes_per_sec);
    return buf;
}

// =============================================================================
// Run benchmarks
// =============================================================================

struct bench_result {
    std::string name;
    std::string category;
    double ops_per_sec;
    double ns_per_op;
    std::size_t iterations;
};

inline int run_all() {
    using clock = std::chrono::steady_clock;

    // Platform info header
    logger::info("================================================================");
    logger::info("  cnetmod Performance Benchmark");
    logger::info("================================================================");
#ifdef _WIN32
    logger::info("  Platform  : Windows IOCP");
#elif defined(__APPLE__)
    logger::info("  Platform  : macOS kqueue");
#else
    logger::info("  Platform  : Linux epoll/io_uring");
#endif
#ifdef _MSC_VER
    logger::info("  Compiler  : MSVC {}", _MSC_VER);
#elif defined(__clang__)
    logger::info("  Compiler  : Clang {}.{}", __clang_major__, __clang_minor__);
#elif defined(__GNUC__)
    logger::info("  Compiler  : GCC {}.{}", __GNUC__, __GNUC_MINOR__);
#endif
#ifdef NDEBUG
    logger::info("  Build     : Release");
#else
    logger::info("  Build     : Debug (results not representative!)");
#endif
    logger::info("================================================================");

    std::vector<bench_result> results;
    std::string current_category;

    for (auto& entry : registry()) {
        // Print category header
        if (entry.category != current_category) {
            current_category = entry.category;
            logger::info("--- {} ---", current_category);
        }

        // Warmup (10% of iterations, minimum 100)
        std::size_t warmup_n = std::max<std::size_t>(entry.iterations / 10, 100);
        entry.fn(warmup_n);

        // Timed run
        auto start = clock::now();
        entry.fn(entry.iterations);
        auto end = clock::now();

        double elapsed_ns = std::chrono::duration<double, std::nano>(end - start).count();
        double ns_per_op = elapsed_ns / static_cast<double>(entry.iterations);
        double ops_per_sec = 1e9 / ns_per_op;

        results.push_back({
            entry.name, entry.category,
            ops_per_sec, ns_per_op, entry.iterations
        });

        // Print result
        logger::info("  {:<38}{:>14} ops/sec  ({}/op)",
            entry.name, format_number(ops_per_sec), format_duration(ns_per_op));
    }

    // Summary table for cross-library comparison
    logger::info("================================================================");
    logger::info("  Summary (copy-paste for comparison)");
    logger::info("================================================================");
    logger::info("{:<40}{:>16}{:>14}", "Benchmark", "ops/sec", "ns/op");
    logger::info("{}", std::string(70, '-'));
    for (auto& r : results) {
        logger::info("{:<40}{:>16.0f}{:>14.1f}", r.name, r.ops_per_sec, r.ns_per_op);
    }
    logger::info("================================================================");

    return 0;
}

} // namespace cnetmod::bench

// =============================================================================
// Macros
// =============================================================================

#define BENCH_CAT(category, name, iters) \
    void bench_fn_##name(std::size_t); \
    static cnetmod::bench::registrar bench_reg_##name( \
        #name, category, iters, bench_fn_##name); \
    void bench_fn_##name(std::size_t _bench_iters_)

#define BENCH(name, iters) BENCH_CAT("General", name, iters)

#define RUN_BENCHMARKS() \
    int main() { return cnetmod::bench::run_all(); }