#pragma once

/// cnetmod test framework — Lightweight C++23 test harness
/// Usage:
///   TEST(test_name) { ASSERT_EQ(1, 1); }
///   int main() { return cnetmod::test::run_all(); }

#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <source_location>
#include <sstream>

namespace cnetmod::test {

struct test_case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<test_case>& registry() {
    static std::vector<test_case> cases;
    return cases;
}

inline int& failure_count() {
    static int n = 0;
    return n;
}

inline int& total_count() {
    static int n = 0;
    return n;
}

struct registrar {
    registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

inline int run_all() {
    int passed = 0;
    int failed = 0;

    std::cout << "[==========] Running " << registry().size() << " test(s).\n";

    for (auto& tc : registry()) {
        std::cout << "[ RUN      ] " << tc.name << "\n";
        failure_count() = 0;
        total_count() = 0;

        try {
            tc.fn();
        } catch (const std::exception& e) {
            std::cerr << "  EXCEPTION: " << e.what() << "\n";
            ++failure_count();
        } catch (...) {
            std::cerr << "  UNKNOWN EXCEPTION\n";
            ++failure_count();
        }

        if (failure_count() == 0) {
            std::cout << "[       OK ] " << tc.name << "\n";
            ++passed;
        } else {
            std::cout << "[  FAILED  ] " << tc.name
                      << " (" << failure_count() << " failure(s))\n";
            ++failed;
        }
    }

    std::cout << "[==========] " << (passed + failed) << " test(s) ran.\n";
    std::cout << "[  PASSED  ] " << passed << " test(s).\n";
    if (failed > 0)
        std::cout << "[  FAILED  ] " << failed << " test(s).\n";

    return failed > 0 ? 1 : 0;
}

namespace detail {

inline void assert_fail(const char* expr, const char* file, int line) {
    std::cerr << "  ASSERT FAILED: " << expr
              << " at " << file << ":" << line << "\n";
    ++failure_count();
}

template <typename A, typename B>
void assert_eq_impl(const A& a, const B& b,
                    const char* a_expr, const char* b_expr,
                    const char* file, int line) {
    ++total_count();
    if (!(a == b)) {
        std::ostringstream oss;
        oss << a_expr << " == " << b_expr
            << " (got: " << a << " vs " << b << ")";
        assert_fail(oss.str().c_str(), file, line);
    }
}

template <typename A, typename B>
void assert_ne_impl(const A& a, const B& b,
                    const char* a_expr, const char* b_expr,
                    const char* file, int line) {
    ++total_count();
    if (a == b) {
        std::ostringstream oss;
        oss << a_expr << " != " << b_expr << " (both equal)";
        assert_fail(oss.str().c_str(), file, line);
    }
}

} // namespace detail

} // namespace cnetmod::test

// =============================================================================
// Macros
// =============================================================================

#define TEST(name) \
    void test_##name(); \
    static cnetmod::test::registrar reg_##name(#name, test_##name); \
    void test_##name()

#define ASSERT_TRUE(expr) \
    do { \
        ++cnetmod::test::total_count(); \
        if (!(expr)) \
            cnetmod::test::detail::assert_fail(#expr, __FILE__, __LINE__); \
    } while(0)

#define ASSERT_FALSE(expr) \
    do { \
        ++cnetmod::test::total_count(); \
        if ((expr)) \
            cnetmod::test::detail::assert_fail("!" #expr, __FILE__, __LINE__); \
    } while(0)

#define ASSERT_EQ(a, b) \
    cnetmod::test::detail::assert_eq_impl((a), (b), #a, #b, __FILE__, __LINE__)

#define ASSERT_NE(a, b) \
    cnetmod::test::detail::assert_ne_impl((a), (b), #a, #b, __FILE__, __LINE__)

#define ASSERT_THROWS(expr) \
    do { \
        ++cnetmod::test::total_count(); \
        bool caught = false; \
        try { expr; } catch (...) { caught = true; } \
        if (!caught) \
            cnetmod::test::detail::assert_fail(#expr " should throw", __FILE__, __LINE__); \
    } while(0)

#define RUN_TESTS() \
    int main() { return cnetmod::test::run_all(); }
