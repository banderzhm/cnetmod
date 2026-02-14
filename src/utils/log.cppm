/**
 * @file log.cppm
 * @brief cnetmod 日志模块 - 使用 std::format 的纯 C++23 实现
 * 
 * 使用示例:
 *   import cnetmod.utils.log;
 *   
 *   // 初始化 (可选, 使用默认配置则无需调用)
 *   logger::init("myapp", logger::level::debug);
 *   
 *   // 基本日志
 *   logger::info("Hello {}", "world");
 *   logger::debug("value = {}", 42);
 *   logger::warn("Warning message");
 *   logger::error("Error: {}", err_code);
 *   
 *   // 带文件输出
 *   logger::init_with_file("myapp", "logs/app.log");
 */
export module cnetmod.utils.log;

import std;

export namespace logger {

    // ============================================================================
    // 日志级别
    // ============================================================================
    enum class level {
        trace    = 0,
        debug    = 1,
        info     = 2,
        warn     = 3,
        error    = 4,
        critical = 5,
        off      = 6
    };

    // ============================================================================
    // 内部实现
    // ============================================================================
    namespace detail {
        inline level& current_level() {
            static level lv = level::info;
            return lv;
        }

        inline std::string& logger_name() {
            static std::string name = "cnetmod";
            return name;
        }

        inline std::mutex& log_mutex() {
            static std::mutex mtx;
            return mtx;
        }

        inline std::ofstream& log_file() {
            static std::ofstream file;
            return file;
        }

        inline bool& file_enabled() {
            static bool enabled = false;
            return enabled;
        }

        inline bool& console_enabled() {
            static bool enabled = true;
            return enabled;
        }

        inline const char* level_to_string(level lv) {
            switch (lv) {
                case level::trace:    return "trace";
                case level::debug:    return "debug";
                case level::info:     return "info";
                case level::warn:     return "warn";
                case level::error:    return "error";
                case level::critical: return "critical";
                default:              return "unknown";
            }
        }

        inline std::string level_colored(level lv) {
            // ANSI color codes
            switch (lv) {
                case level::trace:    return "\033[37mtrace\033[0m";    // white
                case level::debug:    return "\033[36mdebug\033[0m";    // cyan
                case level::info:     return "\033[32minfo\033[0m";     // green
                case level::warn:     return "\033[33mwarn\033[0m";     // yellow
                case level::error:    return "\033[31merror\033[0m";    // red
                case level::critical: return "\033[35mcritical\033[0m"; // magenta
                default:              return "unknown";
            }
        }

        inline std::string get_timestamp() {
            using namespace std::chrono;
            auto now = system_clock::now();
            auto tp_ms = floor<milliseconds>(now);
            auto tp_s  = floor<seconds>(tp_ms);
            auto ms    = duration_cast<milliseconds>(tp_ms - tp_s).count();
            // 使用 UTC 时间，避免依赖时区数据库（跨平台更稳定）
            return std::format("{:%Y-%m-%d %H:%M:%S}.{:03}", tp_s, ms);
        }

        inline std::string get_thread_id() {
            std::ostringstream oss;
            oss << std::this_thread::get_id();
            return oss.str();
        }

        inline void write_log(level lv, std::string_view message) {
            if (lv < current_level()) return;

            auto timestamp = get_timestamp();
            auto thread_id = get_thread_id();
            
            std::lock_guard<std::mutex> lock(log_mutex());

            // 控制台输出 (带颜色)
            if (console_enabled()) {
                std::println(std::cerr, "[{}] [{}] [{}] {}", 
                    timestamp, level_colored(lv), thread_id, message);
            }

            // 文件输出 (无颜色)
            if (file_enabled() && log_file().is_open()) {
                log_file() << std::format("[{}] [{}] [{}] {}\n",
                    timestamp, level_to_string(lv), thread_id, message);
                log_file().flush();
            }
        }
    }

    // ============================================================================
    // 初始化函数
    // ============================================================================

    /**
     * @brief 初始化日志系统 (仅控制台输出)
     * @param name 日志器名称
     * @param lv 日志级别
     */
    inline void init(const std::string& name = "cnetmod", level lv = level::info) {
        detail::logger_name() = name;
        detail::current_level() = lv;
        detail::console_enabled() = true;
        detail::file_enabled() = false;
    }

    /**
     * @brief 初始化日志系统 (控制台 + 文件)
     * @param name 日志器名称
     * @param filepath 日志文件路径
     * @param lv 日志级别
     */
    inline void init_with_file(const std::string& name, 
                               const std::string& filepath,
                               level lv = level::info) {
        detail::logger_name() = name;
        detail::current_level() = lv;
        detail::console_enabled() = true;
        detail::file_enabled() = true;
        detail::log_file().open(filepath, std::ios::app);
    }

    // ============================================================================
    // 配置函数
    // ============================================================================

    inline void set_level(level lv) {
        detail::current_level() = lv;
    }

    inline void flush() {
        std::lock_guard<std::mutex> lock(detail::log_mutex());
        if (detail::file_enabled() && detail::log_file().is_open()) {
            detail::log_file().flush();
        }
    }

    inline void shutdown() {
        std::lock_guard<std::mutex> lock(detail::log_mutex());
        if (detail::log_file().is_open()) {
            detail::log_file().close();
        }
        detail::file_enabled() = false;
    }

    // ============================================================================
    // 日志输出函数 (使用 std::format)
    // ============================================================================

    template<typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args) {
        detail::write_log(level::trace, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        detail::write_log(level::debug, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        detail::write_log(level::info, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        detail::write_log(level::warn, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        detail::write_log(level::error, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void critical(std::format_string<Args...> fmt, Args&&... args) {
        detail::write_log(level::critical, std::format(fmt, std::forward<Args>(args)...));
    }

    // 无格式化参数的重载
    inline void trace(std::string_view msg)    { detail::write_log(level::trace, msg); }
    inline void debug(std::string_view msg)    { detail::write_log(level::debug, msg); }
    inline void info(std::string_view msg)     { detail::write_log(level::info, msg); }
    inline void warn(std::string_view msg)     { detail::write_log(level::warn, msg); }
    inline void error(std::string_view msg)    { detail::write_log(level::error, msg); }
    inline void critical(std::string_view msg) { detail::write_log(level::critical, msg); }

} // namespace logger
