/// cnetmod unit tests — logger text/json format, file output, level filtering

#include "test_framework.hpp"
#include <fstream>
#include <string>
#include <filesystem>

import cnetmod.core.log;

// =============================================================================
// Helpers
// =============================================================================

static std::string read_file_contents(const std::string& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static std::string temp_log_path(const char* name) {
    auto dir = std::filesystem::temp_directory_path();
    return (dir / name).string();
}

// =============================================================================
// Tests
// =============================================================================

TEST(log_text_format_file_output) {
    auto path = temp_log_path("cnetmod_test_text.log");
    std::filesystem::remove(path);

    logger::init_with_file("test", path, logger::level::info, logger::output_format::text);
    logger::info("hello text");
    logger::flush();
    logger::shutdown();

    auto contents = read_file_contents(path);
    // Text format: [timestamp] [info] [thread] [file:line] hello text
    ASSERT_TRUE(contents.find("hello text") != std::string::npos);
    ASSERT_TRUE(contents.find("[info]") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(log_json_format_file_output) {
    auto path = temp_log_path("cnetmod_test_json.log");
    std::filesystem::remove(path);

    logger::init_with_file("test", path, logger::level::info, logger::output_format::json);
    logger::info("hello json");
    logger::flush();
    logger::shutdown();

    auto contents = read_file_contents(path);
    // JSON format: {"timestamp":"...","level":"info",...,"message":"hello json"}
    ASSERT_TRUE(contents.find("\"level\":\"info\"") != std::string::npos);
    ASSERT_TRUE(contents.find("\"message\":\"hello json\"") != std::string::npos);
    ASSERT_TRUE(contents.find("\"timestamp\"") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(log_level_filtering) {
    auto path = temp_log_path("cnetmod_test_filter.log");
    std::filesystem::remove(path);

    logger::init_with_file("test", path, logger::level::warn, logger::output_format::text);
    logger::debug("should not appear");
    logger::info("should not appear");
    logger::warn("warning message");
    logger::error("error message");
    logger::flush();
    logger::shutdown();

    auto contents = read_file_contents(path);
    ASSERT_TRUE(contents.find("should not appear") == std::string::npos);
    ASSERT_TRUE(contents.find("warning message") != std::string::npos);
    ASSERT_TRUE(contents.find("error message") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(log_set_level_dynamic) {
    auto path = temp_log_path("cnetmod_test_setlevel.log");
    std::filesystem::remove(path);

    logger::init_with_file("test", path, logger::level::info, logger::output_format::text);
    logger::info("visible");
    logger::set_level(logger::level::error);
    logger::info("invisible after set_level");
    logger::error("error after set_level");
    logger::flush();
    logger::shutdown();

    auto contents = read_file_contents(path);
    ASSERT_TRUE(contents.find("visible") != std::string::npos);
    ASSERT_TRUE(contents.find("invisible after set_level") == std::string::npos);
    ASSERT_TRUE(contents.find("error after set_level") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(log_set_format_dynamic) {
    auto path = temp_log_path("cnetmod_test_setfmt.log");
    std::filesystem::remove(path);

    logger::init_with_file("test", path, logger::level::info, logger::output_format::text);
    logger::info("text line");
    logger::set_format(logger::output_format::json);
    logger::info("json line");
    logger::flush();
    logger::shutdown();

    auto contents = read_file_contents(path);
    // First line: text format (has [info])
    ASSERT_TRUE(contents.find("[info]") != std::string::npos);
    // Second line: json format (has "level":"info")
    ASSERT_TRUE(contents.find("\"level\":\"info\"") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(log_format_string_args) {
    auto path = temp_log_path("cnetmod_test_fmtargs.log");
    std::filesystem::remove(path);

    logger::init_with_file("test", path, logger::level::info, logger::output_format::text);
    logger::info("value={} name={}", 42, "test");
    logger::flush();
    logger::shutdown();

    auto contents = read_file_contents(path);
    ASSERT_TRUE(contents.find("value=42 name=test") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(log_json_escaping) {
    auto path = temp_log_path("cnetmod_test_escape.log");
    std::filesystem::remove(path);

    logger::init_with_file("test", path, logger::level::info, logger::output_format::json);
    logger::info("line with \"quotes\" and \\backslash");
    logger::flush();
    logger::shutdown();

    auto contents = read_file_contents(path);
    // JSON should have escaped quotes and backslashes
    ASSERT_TRUE(contents.find("\\\"quotes\\\"") != std::string::npos);
    ASSERT_TRUE(contents.find("\\\\backslash") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(log_shutdown_then_write) {
    // After shutdown, logging should not crash (just silently do nothing)
    logger::init("test", logger::level::info);
    logger::shutdown();
    logger::info("after shutdown");  // Should not crash
    ASSERT_TRUE(true);
}

TEST(log_multiple_init) {
    // Re-initializing should work without issues
    logger::init("first", logger::level::debug);
    logger::init("second", logger::level::warn);
    // Should not crash, second init overwrites first
    ASSERT_TRUE(true);
}

RUN_TESTS()
