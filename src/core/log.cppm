/**
 * @file log.cppm
 * @brief cnetmod logging module - Pure C++23 implementation using std::format
 * 
 * Usage Example:
 *   import cnetmod.core.log;
 *   
 *   // Initialize (optional, no need to call if using default config)
 *   logger::init("myapp", logger::level::debug);
 *   
 *   // Basic logging (automatically captures file name and line number at call site)
 *   logger::info("Hello {}", "world");
 *   logger::debug("value = {}", 42);
 *   logger::warn("Warning message");
 *   logger::error("Error: {}", err_code);
 *   // Output format: [timestamp] [level] [thread_id] [file:line] message
 *   
 *   // With file output
 *   logger::init_with_file("myapp", "logs/app.log");
 */
module;

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

export module cnetmod.core.log;

import std;

export namespace logger {

    // ============================================================================
    // Log Level
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
    // Internal Implementation
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

        // Whether ANSI colors are supported (Windows needs explicit VT processing enabled)
        inline bool& ansi_enabled() {
            static bool enabled = false;
            return enabled;
        }

        /// Try to enable Windows VT color support, returns whether successful
        inline bool try_enable_ansi() {
#ifdef _WIN32
            HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD mode = 0;
            if (!GetConsoleMode(h, &mode)) return false;
            return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
            return true;  // POSIX terminals support ANSI by default
#endif
        }

        inline std::string level_colored(level lv) {
            if (!ansi_enabled()) {
                // No color: keep consistent with level_to_string
                return level_to_string(lv);
            }
            switch (lv) {
                case level::trace:    return "\033[37mtrace\033[0m";
                case level::debug:    return "\033[36mdebug\033[0m";
                case level::info:     return "\033[32minfo\033[0m";
                case level::warn:     return "\033[33mwarn\033[0m";
                case level::error:    return "\033[31merror\033[0m";
                case level::critical: return "\033[35mcritical\033[0m";
                default:              return "unknown";
            }
        }

        inline std::string get_timestamp() {
            using namespace std::chrono;
            // Pure C++20 chrono decompose UTC time:
            // - Don't use gmtime_s/gmtime_r (static inline, import std doesn't provide function body)
            // - Don't use {:%Y-%m-%d %H:%M:%S} format (MSVC may trigger timezone database lookup)
            auto now  = system_clock::now();
            auto dp   = floor<days>(now);           // Days since epoch
            year_month_day ymd{dp};                 // Date decomposition
            auto tod  = now - dp;                   // Time elapsed today
            auto h    = floor<hours>(tod);      tod -= h;
            auto mn   = floor<minutes>(tod);    tod -= mn;
            auto s    = floor<seconds>(tod);    tod -= s;
            auto ms   = floor<milliseconds>(tod);
            return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
                static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()),
                static_cast<unsigned>(ymd.day()),
                h.count(), mn.count(), s.count(), ms.count());
        }

        inline std::string get_thread_id() {
            std::ostringstream oss;
            oss << std::this_thread::get_id();
            return oss.str();
        }

        inline std::string_view extract_filename(std::string_view path) {
            if (auto pos = path.find_last_of("/\\"); pos != std::string_view::npos) {
                return path.substr(pos + 1);
            }
            return path;
        }

        inline void write_log(level lv, std::string_view message,
                             const std::source_location& loc = std::source_location::current()) {
            if (lv < current_level()) return;

            auto timestamp = get_timestamp();
            auto thread_id = get_thread_id();
            auto source = std::format("{}:{}", extract_filename(loc.file_name()), loc.line());
            
            std::lock_guard<std::mutex> lock(log_mutex());

            // Console output (with color)
            if (console_enabled()) {
                std::println(std::cerr, "[{}] [{}] [{}] [{}] {}", 
                    timestamp, level_colored(lv), thread_id, source, message);
            }

            // File output (no color)
            if (file_enabled() && log_file().is_open()) {
                log_file() << std::format("[{}] [{}] [{}] [{}] {}\n",
                    timestamp, level_to_string(lv), thread_id, source, message);
                log_file().flush();
            }
        }

        /// Log write without source location
        /// Suitable for access logs, framework internal events, etc. non-debug scenarios, avoids printing meaningless framework internal line numbers
        inline void write_log_no_src(level lv, std::string_view message) {
            if (lv < current_level()) return;

            auto timestamp = get_timestamp();
            auto thread_id = get_thread_id();

            std::lock_guard<std::mutex> lock(log_mutex());

            if (console_enabled()) {
                std::println(std::cerr, "[{}] [{}] [{}] {}",
                    timestamp, level_colored(lv), thread_id, message);
            }

            if (file_enabled() && log_file().is_open()) {
                log_file() << std::format("[{}] [{}] [{}] {}\n",
                    timestamp, level_to_string(lv), thread_id, message);
                log_file().flush();
            }
        }

        /// Embed source_location in format string parameters to avoid MSVC's
        /// deduction defect for "variadic pack + trailing default source_location"
        template<typename... Args>
        struct fmt_loc {
            std::format_string<Args...> fmt;
            std::source_location loc;

            template<typename S>
            consteval fmt_loc(const S& s,
                const std::source_location& l = std::source_location::current())
                : fmt(s), loc(l) {}
        };
    }

    // ============================================================================
    // Initialization Functions
    // ============================================================================

    /**
     * @brief Initialize logging system (console output only)
     * @param name Logger name
     * @param lv Log level
     */
    inline void init(const std::string& name = "cnetmod", level lv = level::info) {
        detail::logger_name() = name;
        detail::current_level() = lv;
        detail::console_enabled() = true;
        detail::file_enabled() = false;
        detail::ansi_enabled() = detail::try_enable_ansi();
    }

    /**
     * @brief Initialize logging system (console + file)
     * @param name Logger name
     * @param filepath Log file path
     * @param lv Log level
     */
    inline void init_with_file(const std::string& name,
                               const std::string& filepath,
                               level lv = level::info) {
        detail::logger_name() = name;
        detail::current_level() = lv;
        detail::console_enabled() = true;
        detail::file_enabled() = true;
        detail::log_file().open(filepath, std::ios::app);
        detail::ansi_enabled() = detail::try_enable_ansi();
    }

    // ============================================================================
    // Configuration Functions
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
    // Log Output Functions (using std::format + std::source_location)
    // Uses struct + template constructor trick to allow source_location default parameter to coexist with variadic templates
    // ============================================================================

    struct trace {
        template<typename... Args>
        trace(detail::fmt_loc<std::type_identity_t<Args>...> fl, Args&&... args) {
            detail::write_log(level::trace, std::format(fl.fmt, std::forward<Args>(args)...), fl.loc);
        }
        trace(std::string_view msg,
              const std::source_location& loc = std::source_location::current()) {
            detail::write_log(level::trace, msg, loc);
        }
    };

    struct debug {
        template<typename... Args>
        debug(detail::fmt_loc<std::type_identity_t<Args>...> fl, Args&&... args) {
            detail::write_log(level::debug, std::format(fl.fmt, std::forward<Args>(args)...), fl.loc);
        }
        debug(std::string_view msg,
              const std::source_location& loc = std::source_location::current()) {
            detail::write_log(level::debug, msg, loc);
        }
    };

    struct info {
        template<typename... Args>
        info(detail::fmt_loc<std::type_identity_t<Args>...> fl, Args&&... args) {
            detail::write_log(level::info, std::format(fl.fmt, std::forward<Args>(args)...), fl.loc);
        }
        info(std::string_view msg,
             const std::source_location& loc = std::source_location::current()) {
            detail::write_log(level::info, msg, loc);
        }
    };

    struct warn {
        template<typename... Args>
        warn(detail::fmt_loc<std::type_identity_t<Args>...> fl, Args&&... args) {
            detail::write_log(level::warn, std::format(fl.fmt, std::forward<Args>(args)...), fl.loc);
        }
        warn(std::string_view msg,
             const std::source_location& loc = std::source_location::current()) {
            detail::write_log(level::warn, msg, loc);
        }
    };

    struct error {
        template<typename... Args>
        error(detail::fmt_loc<std::type_identity_t<Args>...> fl, Args&&... args) {
            detail::write_log(level::error, std::format(fl.fmt, std::forward<Args>(args)...), fl.loc);
        }
        error(std::string_view msg,
              const std::source_location& loc = std::source_location::current()) {
            detail::write_log(level::error, msg, loc);
        }
    };

    struct critical {
        template<typename... Args>
        critical(detail::fmt_loc<std::type_identity_t<Args>...> fl, Args&&... args) {
            detail::write_log(level::critical, std::format(fl.fmt, std::forward<Args>(args)...), fl.loc);
        }
        critical(std::string_view msg,
                 const std::source_location& loc = std::source_location::current()) {
            detail::write_log(level::critical, msg, loc);
        }
    };

} // namespace logger