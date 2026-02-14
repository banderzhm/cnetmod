module;

// spdlog header-only includes
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>
#include <spdlog/sinks/msvc_sink.h>

export module spdlog;

export namespace spdlog {
    // 日志级别
    using ::spdlog::level::level_enum;
    namespace level {
        using ::spdlog::level::trace;
        using ::spdlog::level::debug;
        using ::spdlog::level::info;
        using ::spdlog::level::warn;
        using ::spdlog::level::err;
        using ::spdlog::level::critical;
        using ::spdlog::level::off;
        using ::spdlog::level::n_levels;
    }

    // 核心类型
    using ::spdlog::logger;
    using ::spdlog::async_logger;
    using ::spdlog::spdlog_ex;
    using ::spdlog::source_loc;
    
    // 全局函数
    using ::spdlog::set_level;
    using ::spdlog::get_level;
    using ::spdlog::set_pattern;
    using ::spdlog::set_default_logger;
    using ::spdlog::default_logger;
    using ::spdlog::get;
    using ::spdlog::drop;
    using ::spdlog::drop_all;
    using ::spdlog::register_logger;
    using ::spdlog::flush_on;
    using ::spdlog::flush_every;
    using ::spdlog::shutdown;
    
    // 日志宏的函数版本
    using ::spdlog::trace;
    using ::spdlog::debug;
    using ::spdlog::info;
    using ::spdlog::warn;
    using ::spdlog::error;
    using ::spdlog::critical;

    // sink 命名空间
    namespace sinks {
        using ::spdlog::sinks::sink;
        using ::spdlog::sinks::stdout_color_sink_mt;
        using ::spdlog::sinks::stdout_color_sink_st;
        using ::spdlog::sinks::stderr_color_sink_mt;
        using ::spdlog::sinks::stderr_color_sink_st;
        using ::spdlog::sinks::basic_file_sink_mt;
        using ::spdlog::sinks::basic_file_sink_st;
        using ::spdlog::sinks::rotating_file_sink_mt;
        using ::spdlog::sinks::rotating_file_sink_st;
        using ::spdlog::sinks::daily_file_sink_mt;
        using ::spdlog::sinks::daily_file_sink_st;
#ifdef _WIN32
        using ::spdlog::sinks::msvc_sink_mt;
        using ::spdlog::sinks::msvc_sink_st;
#endif
    }

    // 创建 logger 的辅助函数
    using ::spdlog::stdout_color_mt;
    using ::spdlog::stdout_color_st;
    using ::spdlog::stderr_color_mt;
    using ::spdlog::stderr_color_st;
    using ::spdlog::basic_logger_mt;
    using ::spdlog::basic_logger_st;
    using ::spdlog::rotating_logger_mt;
    using ::spdlog::rotating_logger_st;
    using ::spdlog::daily_logger_mt;
    using ::spdlog::daily_logger_st;

    // 异步相关
    using ::spdlog::init_thread_pool;
    using ::spdlog::thread_pool;
    using ::spdlog::create_async;
    using ::spdlog::create_async_nb;  // non-blocking
}
