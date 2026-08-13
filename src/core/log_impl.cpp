module;

#include <cstdio>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

module cnetmod.core.log;
import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import cnetmod.utils.concurrent_containers.queue;

namespace logger {
namespace concurrent_containers = cnetmod::concurrent_containers;
}

namespace logger::detail {

struct log_event
{
    level severity{};
    output_format format{output_format::text};
    bool has_source{};
    std::string timestamp;
    std::string thread_id;
    std::string source;
    std::string message;
};

struct logger_state
{
    // Configuration and sink lifetime need short synchronous transactions;
    // the hot producer/consumer path itself is CAS-based MPMC.
    concurrent_containers::atomic_rw_latch config_latch;
    concurrent_containers::atomic_rw_latch lifecycle_latch;
    concurrent_containers::bounded_mpmc_queue<log_event> queue{65536};
    std::jthread worker;
    std::ofstream file;
    std::string name{"cnetmod"};
    std::atomic<std::size_t> queue_limit{65536};
    std::atomic<std::size_t> queued{};
    std::atomic<std::size_t> active_writes{};
    std::atomic<std::uint64_t> dropped{};
    std::atomic<std::uint64_t> work_epoch{};
    std::atomic<std::uint64_t> drain_epoch{};
    level threshold{level::info};
    output_format format{output_format::text};
    bool console{true};
    std::atomic_bool stopping{};
    bool ansi{};
};

auto state() -> logger_state&
{
    static logger_state instance;
    return instance;
}

auto level_name(level value) -> std::string_view
{
    switch (value)
    {
    case level::trace:
        return "trace";
    case level::debug:
        return "debug";
    case level::info:
        return "info";
    case level::warn:
        return "warn";
    case level::error:
        return "error";
    case level::critical:
        return "critical";
    case level::off:
        return "off";
    }
    return "unknown";
}

auto timestamp() -> std::string
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto day = floor<days>(now);
    const year_month_day ymd{day};
    auto time = now - day;
    const auto hours_part = floor<hours>(time);
    time -= hours_part;
    const auto minutes_part = floor<minutes>(time);
    time -= minutes_part;
    const auto seconds_part = floor<seconds>(time);
    time -= seconds_part;
    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
        static_cast<int>(ymd.year()),
        static_cast<unsigned>(ymd.month()),
        static_cast<unsigned>(ymd.day()), hours_part.count(),
        minutes_part.count(), seconds_part.count(),
        floor<milliseconds>(time).count());
}

auto thread_id() -> std::string
{
    thread_local const auto cached =
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return cached;
}

auto filename(std::string_view path) -> std::string_view
{
    const auto pos = path.find_last_of("/\\");
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

auto json_escape(std::string_view text) -> std::string
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text)
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
            else
                out += c;
        }
    return out;
}

auto can_enable_ansi() -> bool
{
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode{};
    return handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) &&
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    return true;
#endif
}

void sink(logger_state& s, const log_event& event)
{
    std::string line;
    if (event.format == output_format::json)
    {
        line =
            event.has_source
            ? std::format(
                  R"({{"timestamp":"{}","level":"{}","thread":"{}","source":"{}","message":"{}"}})",
                  event.timestamp, level_name(event.severity), event.thread_id,
                  event.source, json_escape(event.message))
            : std::format(
                  R"({{"timestamp":"{}","level":"{}","thread":"{}","message":"{}"}})",
                  event.timestamp, level_name(event.severity), event.thread_id,
                  json_escape(event.message));
    }
    else
    {
        line = event.has_source
            ? std::format("[{}] [{}] [{}] [{}] {}", event.timestamp,
                  level_name(event.severity), event.thread_id,
                  event.source, event.message)
            : std::format("[{}] [{}] [{}] {}", event.timestamp,
                  level_name(event.severity), event.thread_id,
                  event.message);
    }
    if (s.console)
    {
        std::fwrite(line.data(), 1U, line.size(), stderr);
        std::fputc('\n', stderr);
    }
    if (s.file.is_open())
        s.file << line << '\n';
}

void worker_loop(std::stop_token token)
{
    auto& s = state();
    std::stop_callback wake_on_stop{token, [&s]
        {
            s.work_epoch.fetch_add(1U, std::memory_order_release);
            s.work_epoch.notify_all();
        }};
    for (;;)
    {
        while (auto event = s.queue.try_dequeue())
        {
            s.queued.fetch_sub(1U, std::memory_order_release);
            s.active_writes.fetch_add(1U, std::memory_order_acq_rel);
            {
                concurrent_containers::exclusive_latch_guard lock{
                    s.config_latch};
                sink(s, *event);
            }
            s.active_writes.fetch_sub(1U, std::memory_order_release);
        }
        if (s.queued.load(std::memory_order_acquire) == 0U &&
            s.active_writes.load(std::memory_order_acquire) == 0U)
        {
            s.drain_epoch.fetch_add(1U, std::memory_order_release);
            s.drain_epoch.notify_all();
        }
        if ((token.stop_requested() || s.stopping.load(std::memory_order_acquire)) &&
            s.queued.load(std::memory_order_acquire) == 0U)
            break;

        const auto observed = s.work_epoch.load(std::memory_order_acquire);
        if (s.queued.load(std::memory_order_acquire) == 0U)
            s.work_epoch.wait(observed, std::memory_order_relaxed);
    }
    s.drain_epoch.fetch_add(1U, std::memory_order_release);
    s.drain_epoch.notify_all();
}

void ensure_worker()
{
    auto& s = state();
    concurrent_containers::exclusive_latch_guard lock{s.lifecycle_latch};
    if (s.worker.joinable())
        return;
    s.stopping.store(false, std::memory_order_release);
    s.worker = std::jthread([](std::stop_token token)
        {
            worker_loop(token);
        });
}

void stop_worker()
{
    auto& s = state();
    concurrent_containers::exclusive_latch_guard lock{s.lifecycle_latch};
    if (!s.worker.joinable())
        return;
    s.stopping.store(true, std::memory_order_release);
    s.work_epoch.fetch_add(1U, std::memory_order_release);
    s.work_epoch.notify_all();
    s.worker.request_stop();
    s.worker.join();
}

void write_log(level value, std::string_view message,
    const std::source_location& location)
{
    auto& s = state();
    output_format event_format;
    {
        concurrent_containers::shared_latch_guard lock{s.config_latch};
        if (value < s.threshold || s.threshold == level::off)
            return;
        event_format = s.format;
    }
    ensure_worker();
    const auto queued = s.queued.fetch_add(1U, std::memory_order_acq_rel);
    const auto configured_limit =
        s.queue_limit.load(std::memory_order_acquire);
    const auto limit = configured_limit < s.queue.capacity()
        ? configured_limit
        : s.queue.capacity();
    if (queued >= limit)
    {
        s.queued.fetch_sub(1U, std::memory_order_release);
        s.dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    if (!s.queue.try_enqueue({value, event_format, true, timestamp(), thread_id(),
            std::format("{}:{}", filename(location.file_name()), location.line()),
            std::string(message)}))
    {
        s.queued.fetch_sub(1U, std::memory_order_release);
        s.dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    s.work_epoch.fetch_add(1U, std::memory_order_release);
    s.work_epoch.notify_one();
}

void write_log_no_src(level value, std::string_view message)
{
    auto& s = state();
    output_format event_format;
    {
        concurrent_containers::shared_latch_guard lock{s.config_latch};
        if (value < s.threshold || s.threshold == level::off)
            return;
        event_format = s.format;
    }
    ensure_worker();
    const auto queued = s.queued.fetch_add(1U, std::memory_order_acq_rel);
    const auto configured_limit =
        s.queue_limit.load(std::memory_order_acquire);
    const auto limit = configured_limit < s.queue.capacity()
        ? configured_limit
        : s.queue.capacity();
    if (queued >= limit)
    {
        s.queued.fetch_sub(1U, std::memory_order_release);
        s.dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    if (!s.queue.try_enqueue({value,
            event_format,
            false,
            timestamp(),
            thread_id(),
            {},
            std::string(message)}))
    {
        s.queued.fetch_sub(1U, std::memory_order_release);
        s.dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    s.work_epoch.fetch_add(1U, std::memory_order_release);
    s.work_epoch.notify_one();
}

} // namespace logger::detail

namespace logger {
void init(const std::string& name, level value, output_format format)
{
    auto& s = detail::state();
    concurrent_containers::exclusive_latch_guard lock{s.config_latch};
    s.name = name;
    s.threshold = value;
    s.format = format;
    s.console = true;
    s.ansi = format == output_format::text && detail::can_enable_ansi();
    if (s.file.is_open())
        s.file.close();
    detail::ensure_worker();
}

void init_with_file(const std::string& name, const std::string& path,
    level value, output_format format, bool echo_console)
{
    init(name, value, format);
    auto& s = detail::state();
    concurrent_containers::exclusive_latch_guard lock{s.config_latch};
    s.console = echo_console;
    s.file.open(path, std::ios::app);
}

void set_level(level value)
{
    auto& s = detail::state();
    concurrent_containers::exclusive_latch_guard lock{s.config_latch};
    s.threshold = value;
}

void set_format(output_format value)
{
    auto& s = detail::state();
    concurrent_containers::exclusive_latch_guard lock{s.config_latch};
    s.format = value;
    s.ansi = value == output_format::text && detail::can_enable_ansi();
}

void set_console_enabled(bool enabled)
{
    auto& s = detail::state();
    concurrent_containers::exclusive_latch_guard lock{s.config_latch};
    s.console = enabled;
}

auto set_file_output(const std::string& path, bool append) -> bool
{
    auto& s = detail::state();
    concurrent_containers::exclusive_latch_guard lock{s.config_latch};
    if (s.file.is_open())
        s.file.close();
    s.file.open(path, append ? std::ios::app : std::ios::trunc);
    return s.file.is_open();
}

void disable_file_output()
{
    auto& s = detail::state();
    concurrent_containers::exclusive_latch_guard lock{s.config_latch};
    if (s.file.is_open())
    {
        s.file.flush();
        s.file.close();
    }
}

void set_async_queue_limit(std::size_t limit)
{
    auto& s = detail::state();
    const auto bounded = std::clamp(std::max<std::size_t>(limit, 1024U),
        std::size_t{1024U}, s.queue.capacity());
    s.queue_limit.store(bounded, std::memory_order_release);
}

auto dropped_messages() -> std::uint64_t
{
    return detail::state().dropped.load(std::memory_order_acquire);
}

void flush()
{
    auto& s = detail::state();
    for (;;)
    {
        if (s.queued.load(std::memory_order_acquire) == 0U &&
            s.active_writes.load(std::memory_order_acquire) == 0U)
            break;
        const auto observed = s.drain_epoch.load(std::memory_order_acquire);
        s.work_epoch.fetch_add(1U, std::memory_order_release);
        s.work_epoch.notify_one();
        if (s.queued.load(std::memory_order_acquire) != 0U ||
            s.active_writes.load(std::memory_order_acquire) != 0U)
            s.drain_epoch.wait(observed, std::memory_order_relaxed);
    }
    concurrent_containers::exclusive_latch_guard lock{s.config_latch};
    if (s.file.is_open())
        s.file.flush();
}

void shutdown()
{
    flush();
    detail::stop_worker();
    disable_file_output();
}

trace::trace(std::string_view message, const std::source_location& location)
{
    detail::write_log(level::trace, message, location);
}

debug::debug(std::string_view message, const std::source_location& location)
{
    detail::write_log(level::debug, message, location);
}

info::info(std::string_view message, const std::source_location& location)
{
    detail::write_log(level::info, message, location);
}

warn::warn(std::string_view message, const std::source_location& location)
{
    detail::write_log(level::warn, message, location);
}

error::error(std::string_view message, const std::source_location& location)
{
    detail::write_log(level::error, message, location);
}

critical::critical(std::string_view message,
    const std::source_location& location)
{
    detail::write_log(level::critical, message, location);
}
} // namespace logger
