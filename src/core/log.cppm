/**
 * @file log.cppm
 * @brief cnetmod logging module - async logger with console/file sinks
 *
 * Usage Example:
 *   import cnetmod.core.log;
 *
 *   logger::init("myapp", logger::level::debug);
 *   logger::set_console_enabled(true);
 *   logger::set_file_output("logs/app.log");
 *
 *   logger::info("Hello {}", "world");
 *   logger::warn("Warning message");
 *   logger::error("Error: {}", 42);
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

    // =========================================================================
    // Log Level
    // =========================================================================
    enum class level {
        trace    = 0,
        debug    = 1,
        info     = 2,
        warn     = 3,
        error    = 4,
        critical = 5,
        off      = 6
    };

    // =========================================================================
    // Output Format
    // =========================================================================
    enum class output_format {
        text,   // [timestamp] [level] [thread] [source] message
        json,   // {"timestamp":"...","level":"...","thread":"...","source":"...","message":"..."}
    };

    // =========================================================================
    // Internal Implementation
    // =========================================================================
    namespace detail {
        struct log_event {
            std::uint64_t seq = 0;
            level lv = level::info;
            bool has_source = false;
            std::string timestamp;
            std::string thread_id;
            std::string source;
            std::string message;
        };

        inline std::atomic<int>& current_level_raw() {
            static std::atomic<int> lv{static_cast<int>(level::info)};
            return lv;
        }

        inline auto current_level() -> level {
            return static_cast<level>(current_level_raw().load(std::memory_order_acquire));
        }

        inline void set_current_level(level lv) {
            current_level_raw().store(static_cast<int>(lv), std::memory_order_release);
        }

        inline std::atomic<int>& current_format_raw() {
            static std::atomic<int> fmt{static_cast<int>(output_format::text)};
            return fmt;
        }

        inline auto current_format() -> output_format {
            return static_cast<output_format>(current_format_raw().load(std::memory_order_acquire));
        }

        inline void set_current_format(output_format fmt) {
            current_format_raw().store(static_cast<int>(fmt), std::memory_order_release);
        }

        inline std::atomic<bool>& file_enabled_raw() {
            static std::atomic<bool> enabled{false};
            return enabled;
        }

        inline auto file_enabled() -> bool {
            return file_enabled_raw().load(std::memory_order_acquire);
        }

        inline void set_file_enabled(bool enabled) {
            file_enabled_raw().store(enabled, std::memory_order_release);
        }

        inline std::atomic<bool>& console_enabled_raw() {
            static std::atomic<bool> enabled{true};
            return enabled;
        }

        inline auto console_enabled() -> bool {
            return console_enabled_raw().load(std::memory_order_acquire);
        }

        inline void set_console_enabled(bool enabled) {
            console_enabled_raw().store(enabled, std::memory_order_release);
        }

        inline std::atomic<bool>& ansi_enabled_raw() {
            static std::atomic<bool> enabled{false};
            return enabled;
        }

        inline auto ansi_enabled() -> bool {
            return ansi_enabled_raw().load(std::memory_order_acquire);
        }

        inline void set_ansi_enabled(bool enabled) {
            ansi_enabled_raw().store(enabled, std::memory_order_release);
        }

        inline std::string& logger_name() {
            static std::string name = "cnetmod";
            return name;
        }

        inline std::mutex& output_mutex() {
            static std::mutex mtx;
            return mtx;
        }

        inline std::ofstream& log_file() {
            static std::ofstream file;
            return file;
        }

        inline std::string& log_file_path() {
            static std::string path;
            return path;
        }

        struct log_slot {
            std::atomic<std::size_t> seq{0};
            log_event event;
        };

        struct log_ring {
            std::size_t capacity = 0;
            std::size_t mask = 0;
            std::unique_ptr<log_slot[]> slots;
            std::atomic<std::size_t> enqueue_pos{0};
            std::atomic<std::size_t> dequeue_pos{0};
        };

        inline log_ring& ring() {
            static log_ring q;
            return q;
        }

        inline std::mutex& ring_init_mutex() {
            static std::mutex mtx;
            return mtx;
        }

        inline std::once_flag& ring_init_once() {
            static std::once_flag once;
            return once;
        }

        inline std::mutex& wake_mutex() {
            static std::mutex mtx;
            return mtx;
        }

        inline std::condition_variable& wake_cv() {
            static std::condition_variable cv;
            return cv;
        }

        inline std::jthread& worker_thread() {
            static std::jthread t;
            return t;
        }

        inline std::atomic<bool>& worker_running() {
            static std::atomic<bool> running{false};
            return running;
        }

        inline std::atomic<bool>& worker_stop() {
            static std::atomic<bool> stop{false};
            return stop;
        }

        inline std::atomic<std::uint64_t>& next_seq() {
            static std::atomic<std::uint64_t> seq{0};
            return seq;
        }

        inline std::atomic<std::uint64_t>& flushed_seq() {
            static std::atomic<std::uint64_t> seq{0};
            return seq;
        }

        inline std::mutex& flush_mutex() {
            static std::mutex mtx;
            return mtx;
        }

        inline std::condition_variable& flush_cv() {
            static std::condition_variable cv;
            return cv;
        }

        inline std::atomic<std::size_t>& max_queue_size() {
            static std::atomic<std::size_t> maxq{65536};
            return maxq;
        }

        inline std::atomic<std::size_t>& queued_count() {
            static std::atomic<std::size_t> cnt{0};
            return cnt;
        }

        inline std::atomic<std::uint64_t>& dropped_count() {
            static std::atomic<std::uint64_t> dropped{0};
            return dropped;
        }

        inline auto next_pow2(std::size_t n) -> std::size_t {
            if (n <= 2) return 2;
            --n;
            for (std::size_t i = 1; i < sizeof(std::size_t) * 8; i <<= 1) {
                n |= (n >> i);
            }
            return n + 1;
        }

        inline void init_ring_if_needed() {
            auto& q = ring();
            std::call_once(ring_init_once(), [&]() {
                auto cap = next_pow2(max_queue_size().load(std::memory_order_acquire));
                q.capacity = cap;
                q.mask = cap - 1;
                q.slots = std::make_unique<log_slot[]>(cap);
                q.enqueue_pos.store(0, std::memory_order_relaxed);
                q.dequeue_pos.store(0, std::memory_order_relaxed);
                queued_count().store(0, std::memory_order_relaxed);
                for (std::size_t i = 0; i < cap; ++i) {
                    q.slots[i].seq.store(i, std::memory_order_relaxed);
                }
            });
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

        inline bool try_enable_ansi() {
#ifdef _WIN32
            HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD mode = 0;
            if (!GetConsoleMode(h, &mode)) return false;
            return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
            return true;
#endif
        }

        inline std::string level_colored(level lv) {
            if (!ansi_enabled()) {
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
            auto now  = system_clock::now();
            auto dp   = floor<days>(now);
            year_month_day ymd{dp};
            auto tod  = now - dp;
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

        inline std::string json_escape(std::string_view sv) {
            std::string out;
            out.reserve(sv.size() + 8);
            for (char c : sv) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20)
                            out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                        else
                            out += c;
                        break;
                }
            }
            return out;
        }

        inline void sink_one(const log_event& ev) {
            std::lock_guard<std::mutex> lock(output_mutex());

            if (current_format() == output_format::json) {
                std::string line;
                if (ev.has_source) {
                    line = std::format(
                        R"({{"timestamp":"{}","level":"{}","thread":"{}","source":"{}","message":"{}"}})",
                        ev.timestamp, level_to_string(ev.lv), ev.thread_id, ev.source, json_escape(ev.message));
                } else {
                    line = std::format(
                        R"({{"timestamp":"{}","level":"{}","thread":"{}","message":"{}"}})",
                        ev.timestamp, level_to_string(ev.lv), ev.thread_id, json_escape(ev.message));
                }

                if (console_enabled()) {
                    std::println(std::cerr, "{}", line);
                }
                if (file_enabled() && log_file().is_open()) {
                    log_file() << line << '\n';
                }
                return;
            }

            if (console_enabled()) {
                if (ev.has_source) {
                    std::println(std::cerr, "[{}] [{}] [{}] [{}] {}",
                        ev.timestamp, level_colored(ev.lv), ev.thread_id, ev.source, ev.message);
                } else {
                    std::println(std::cerr, "[{}] [{}] [{}] {}",
                        ev.timestamp, level_colored(ev.lv), ev.thread_id, ev.message);
                }
            }

            if (file_enabled() && log_file().is_open()) {
                if (ev.has_source) {
                    log_file() << std::format("[{}] [{}] [{}] [{}] {}\n",
                        ev.timestamp, level_to_string(ev.lv), ev.thread_id, ev.source, ev.message);
                } else {
                    log_file() << std::format("[{}] [{}] [{}] {}\n",
                        ev.timestamp, level_to_string(ev.lv), ev.thread_id, ev.message);
                }
            }
        }

        inline void sink_drop_notice(std::uint64_t dropped) {
            if (dropped == 0) return;
            log_event ev;
            ev.lv = level::warn;
            ev.has_source = false;
            ev.timestamp = get_timestamp();
            ev.thread_id = get_thread_id();
            ev.message = std::format("logger queue is full, dropped {} messages", dropped);
            sink_one(ev);
        }

        inline void worker_loop() {
            init_ring_if_needed();
            std::vector<log_event> batch;
            batch.reserve(256);
            log_event ev;

            while (true) {
                while (batch.size() < 256) {
                    auto& q = ring();
                    auto pos = q.dequeue_pos.load(std::memory_order_relaxed);
                    auto& slot = q.slots[pos & q.mask];
                    auto seq = slot.seq.load(std::memory_order_acquire);
                    auto diff = static_cast<std::intptr_t>(seq) -
                                static_cast<std::intptr_t>(pos + 1);

                    if (diff == 0) {
                        if (q.dequeue_pos.compare_exchange_weak(
                                pos, pos + 1,
                                std::memory_order_relaxed,
                                std::memory_order_relaxed)) {
                            ev = std::move(slot.event);
                            slot.seq.store(pos + q.capacity, std::memory_order_release);
                            queued_count().fetch_sub(1, std::memory_order_relaxed);
                            batch.push_back(std::move(ev));
                        }
                        continue;
                    }
                    break;
                }

                if (batch.empty()) {
                    std::unique_lock<std::mutex> lock(wake_mutex());
                    wake_cv().wait_for(lock, std::chrono::milliseconds(50), [] {
                        return worker_stop().load(std::memory_order_acquire) ||
                               queued_count().load(std::memory_order_acquire) > 0;
                    });
                    if (worker_stop().load(std::memory_order_acquire) &&
                        queued_count().load(std::memory_order_acquire) == 0) {
                        break;
                    }
                    continue;
                }

                sink_drop_notice(dropped_count().exchange(0, std::memory_order_acq_rel));

                for (const auto& ev : batch) {
                    sink_one(ev);
                    flushed_seq().store(ev.seq, std::memory_order_release);
                }
                batch.clear();

                flush_cv().notify_all();
            }

            sink_drop_notice(dropped_count().exchange(0, std::memory_order_acq_rel));

            {
                std::lock_guard<std::mutex> lock(output_mutex());
                if (file_enabled() && log_file().is_open()) {
                    log_file().flush();
                }
            }

            worker_running().store(false, std::memory_order_release);
            flush_cv().notify_all();
        }

        inline void start_worker_if_needed() {
            bool expected = false;
            if (!worker_running().compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return;
            }

            worker_stop().store(false, std::memory_order_release);
            worker_thread() = std::jthread([](std::stop_token) {
                worker_loop();
            });
        }

        inline void stop_worker() {
            if (!worker_running().load(std::memory_order_acquire)) {
                return;
            }

            worker_stop().store(true, std::memory_order_release);
            wake_cv().notify_all();

            if (worker_thread().joinable()) {
                worker_thread().join();
            }
        }

        inline void enqueue(log_event&& ev) {
            init_ring_if_needed();
            start_worker_if_needed();

            auto& q = ring();
            auto pos = q.enqueue_pos.load(std::memory_order_relaxed);
            for (;;) {
                auto& slot = q.slots[pos & q.mask];
                auto seq = slot.seq.load(std::memory_order_acquire);
                auto diff = static_cast<std::intptr_t>(seq) -
                            static_cast<std::intptr_t>(pos);

                if (diff == 0) {
                    if (q.enqueue_pos.compare_exchange_weak(
                            pos, pos + 1,
                            std::memory_order_relaxed,
                            std::memory_order_relaxed)) {
                        ev.seq = next_seq().fetch_add(1, std::memory_order_acq_rel) + 1;
                        slot.event = std::move(ev);
                        slot.seq.store(pos + 1, std::memory_order_release);
                        queued_count().fetch_add(1, std::memory_order_relaxed);
                        wake_cv().notify_one();
                        return;
                    }
                    continue;
                }

                if (diff < 0) {
                    dropped_count().fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                pos = q.enqueue_pos.load(std::memory_order_relaxed);
            }
        }

        inline void write_log(level lv, std::string_view message,
                              const std::source_location& loc = std::source_location::current()) {
            if (lv < current_level()) return;

            log_event ev;
            ev.lv = lv;
            ev.has_source = true;
            ev.timestamp = get_timestamp();
            ev.thread_id = get_thread_id();
            ev.source = std::format("{}:{}", extract_filename(loc.file_name()), loc.line());
            ev.message.assign(message.begin(), message.end());

            enqueue(std::move(ev));
        }

        inline void write_log_no_src(level lv, std::string_view message) {
            if (lv < current_level()) return;

            log_event ev;
            ev.lv = lv;
            ev.has_source = false;
            ev.timestamp = get_timestamp();
            ev.thread_id = get_thread_id();
            ev.message.assign(message.begin(), message.end());

            enqueue(std::move(ev));
        }

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

    // =========================================================================
    // Initialization Functions
    // =========================================================================

    inline void init(const std::string& name = "cnetmod", level lv = level::info,
                     output_format fmt = output_format::text) {
        detail::logger_name() = name;
        detail::set_current_level(lv);
        detail::set_current_format(fmt);
        detail::set_console_enabled(true);
        detail::set_file_enabled(false);
        detail::set_ansi_enabled((fmt == output_format::text) ? detail::try_enable_ansi() : false);

        std::lock_guard<std::mutex> lock(detail::output_mutex());
        if (detail::log_file().is_open()) {
            detail::log_file().flush();
            detail::log_file().close();
        }
        detail::log_file_path().clear();

        detail::init_ring_if_needed();
        detail::start_worker_if_needed();
    }

    inline void init_with_file(const std::string& name,
                               const std::string& filepath,
                               level lv = level::info,
                               output_format fmt = output_format::text,
                               bool echo_console = true) {
        detail::logger_name() = name;
        detail::set_current_level(lv);
        detail::set_current_format(fmt);
        detail::set_console_enabled(echo_console);
        detail::set_ansi_enabled((fmt == output_format::text) ? detail::try_enable_ansi() : false);

        {
            std::lock_guard<std::mutex> lock(detail::output_mutex());
            if (detail::log_file().is_open()) {
                detail::log_file().flush();
                detail::log_file().close();
            }
            detail::log_file().open(filepath, std::ios::app);
            detail::log_file_path() = filepath;
            detail::set_file_enabled(detail::log_file().is_open());
        }

        detail::init_ring_if_needed();
        detail::start_worker_if_needed();
    }

    // =========================================================================
    // Configuration Functions
    // =========================================================================

    inline void set_level(level lv) {
        detail::set_current_level(lv);
    }

    inline void set_format(output_format fmt) {
        detail::set_current_format(fmt);
        if (fmt == output_format::json) {
            detail::set_ansi_enabled(false);
        } else {
            detail::set_ansi_enabled(detail::try_enable_ansi());
        }
    }

    inline void set_console_enabled(bool enabled) {
        detail::set_console_enabled(enabled);
    }

    inline auto set_file_output(const std::string& filepath, bool append = true) -> bool {
        std::lock_guard<std::mutex> lock(detail::output_mutex());
        if (detail::log_file().is_open()) {
            detail::log_file().flush();
            detail::log_file().close();
        }
        auto mode = append ? std::ios::app : std::ios::trunc;
        detail::log_file().open(filepath, mode);
        detail::log_file_path() = filepath;
        detail::set_file_enabled(detail::log_file().is_open());
        return detail::log_file().is_open();
    }

    inline void disable_file_output() {
        std::lock_guard<std::mutex> lock(detail::output_mutex());
        if (detail::log_file().is_open()) {
            detail::log_file().flush();
            detail::log_file().close();
        }
        detail::log_file_path().clear();
        detail::set_file_enabled(false);
    }

    inline void set_async_queue_limit(std::size_t max_queue) {
        auto clamped = std::max<std::size_t>(max_queue, 1024);
        detail::max_queue_size().store(clamped, std::memory_order_release);
    }

    inline auto dropped_messages() -> std::uint64_t {
        return detail::dropped_count().load(std::memory_order_acquire);
    }

    inline void flush() {
        auto target = detail::next_seq().load(std::memory_order_acquire);
        if (target > 0 && detail::worker_running().load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(detail::flush_mutex());
            detail::flush_cv().wait(lock, [target] {
                return detail::flushed_seq().load(std::memory_order_acquire) >= target
                    || !detail::worker_running().load(std::memory_order_acquire);
            });
        }

        std::lock_guard<std::mutex> out_lock(detail::output_mutex());
        if (detail::file_enabled() && detail::log_file().is_open()) {
            detail::log_file().flush();
        }
    }

    inline void shutdown() {
        flush();
        detail::stop_worker();
        disable_file_output();
    }

    // =========================================================================
    // Log Output Functions
    // =========================================================================

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
