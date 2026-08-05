/// cnetmod unit tests -- application-level R<T, E>

#include "test_framework.hpp"

import std;
import cnetmod.utils;

using cnetmod::utils::R;

TEST(application_result_success_and_move_access)
{
    auto value = R<std::unique_ptr<int>>::ok(std::make_unique<int>(42));
    auto moved = std::move(value).data();
    ASSERT_TRUE(moved != nullptr);
    ASSERT_EQ(*moved, 42);
}

TEST(application_result_map_and_then_propagate_errors)
{
    auto mapped = R<int>::ok(20).map([](int value) { return value + 22; });
    ASSERT_TRUE(mapped.ok());
    ASSERT_EQ(mapped.data(), 42);

    auto chained = R<int>::ok(21).and_then([](int value) {
        return R<std::string>::ok(std::to_string(value * 2));
    });
    ASSERT_TRUE(chained.ok());
    ASSERT_EQ(chained.data(), std::string("42"));

    auto failed = R<int>::error(404, "missing", "lookup");
    auto preserved = failed.and_then([](int) { return R<std::string>::ok("unused"); });
    ASSERT_TRUE(preserved.error());
    ASSERT_EQ(preserved.failure().code, 404);
    ASSERT_EQ(preserved.failure().diagnostic, std::string("lookup"));
}

TEST(application_result_maps_error_codes)
{
    auto failed = R<int>::error(7, "unavailable");
    auto mapped = failed.map_error([](int code) { return std::to_string(code); });
    ASSERT_TRUE(mapped.error());
    ASSERT_EQ(mapped.failure().code, std::string("7"));

    auto transport = R<int, std::error_code>::from_error_code(
        std::make_error_code(std::errc::timed_out));
    ASSERT_TRUE(transport.error());
    ASSERT_EQ(transport.failure().code, std::make_error_code(std::errc::timed_out));

    auto mapped_transport = R<int>::from_error_code(
        std::make_error_code(std::errc::permission_denied),
        [](const std::error_code&) { return 403; });
    ASSERT_TRUE(mapped_transport.error());
    ASSERT_EQ(mapped_transport.failure().code, 403);
}

TEST(application_result_void_composes)
{
    auto completed = R<void>::success().map([] { return 42; });
    ASSERT_TRUE(completed.ok());
    ASSERT_EQ(completed.data(), 42);

    auto chained = R<void>::success().and_then([] { return R<std::string>::ok("done"); });
    ASSERT_TRUE(chained.ok());
    ASSERT_EQ(chained.data(), std::string("done"));

    auto failed = R<void>::error(9, "cancelled").map_error(
        [](int code) { return std::to_string(code); });
    ASSERT_TRUE(failed.error());
    ASSERT_EQ(failed.failure().code, std::string("9"));

    auto moved_error = std::move(R<std::string>::error(5, "moved"))
                           .map_error([](int&& code) { return code + 1; });
    ASSERT_TRUE(moved_error.error());
    ASSERT_EQ(moved_error.failure().code, 6);
}

RUN_TESTS()
