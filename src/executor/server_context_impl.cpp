module;

#include <cnetmod/config.hpp>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(CNETMOD_PLATFORM_LINUX)
    #include <pthread.h>
    #include <sched.h>
#elif defined(CNETMOD_PLATFORM_MACOS)
    #include <mach/mach.h>
    #include <mach/thread_policy.h>
#endif

module cnetmod.executor.pool;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

namespace cnetmod {

namespace {

    struct pool_resume_receiver
    {
        using receiver_concept = stdexec::receiver_t;
        std::coroutine_handle<> coroutine;

        void set_value() noexcept
        {
            coroutine.resume();
        }

        void set_error(std::exception_ptr) noexcept
        {
            coroutine.resume();
        }

        void set_stopped() noexcept
        {
            coroutine.resume();
        }

        struct env
        {
        };

        auto get_env() const noexcept -> env
        {
            return {};
        }
    };

    using native_pool = exec::static_thread_pool;
    using native_scheduler = native_pool::scheduler;
    using native_resume_operation = decltype(stdexec::connect(
        std::declval<native_scheduler>().schedule(),
        std::declval<pool_resume_receiver>()));

    struct resume_operation
    {
        native_resume_operation operation;

        explicit resume_operation(native_scheduler scheduler,
            std::coroutine_handle<> coroutine)
            : operation(stdexec::connect(
                  scheduler.schedule(), pool_resume_receiver{coroutine})) {}
    };

} // namespace

struct thread_pool::impl
{
    explicit impl(unsigned thread_count)
        : native(thread_count == 0 ? 1U : thread_count) {}

    native_pool native;
};

thread_pool::thread_pool(unsigned thread_count)
    : impl_(std::make_unique<impl>(thread_count)) {}

thread_pool::~thread_pool() = default;

void thread_pool::request_stop() noexcept
{
    impl_->native.request_stop();
}

auto thread_pool::prepare_resume(std::coroutine_handle<> coroutine) -> void*
{
    return new resume_operation{impl_->native.get_scheduler(), coroutine};
}

void thread_pool::start_resume(void* operation) noexcept
{
    static_cast<resume_operation*>(operation)->operation.start();
}

void thread_pool::release_resume(void* operation) noexcept
{
    delete static_cast<resume_operation*>(operation);
}

pool_post_awaitable::pool_post_awaitable(thread_pool& value) noexcept
    : pool(value) {}

auto pool_post_awaitable::await_ready() const noexcept -> bool
{
    return false;
}

void pool_post_awaitable::await_suspend(std::coroutine_handle<> coroutine) noexcept
{
    operation_ = pool.prepare_resume(coroutine);
    pool.start_resume(operation_);
}

void pool_post_awaitable::await_resume() noexcept
{
    pool.release_resume(operation_);
    operation_ = nullptr;
}

auto set_current_thread_affinity(unsigned processor) noexcept
    -> std::expected<void, std::error_code>
{
#ifdef CNETMOD_PLATFORM_WINDOWS
    constexpr auto processors_per_group = sizeof(KAFFINITY) * 8U;
    GROUP_AFFINITY affinity{};
    affinity.Group = static_cast<WORD>(processor / processors_per_group);
    affinity.Mask = KAFFINITY{1} << (processor % processors_per_group);
    if (::SetThreadGroupAffinity(::GetCurrentThread(), &affinity, nullptr) == 0)
        return std::unexpected(std::error_code{
            static_cast<int>(::GetLastError()), std::system_category()});
    return {};
#elif defined(CNETMOD_PLATFORM_LINUX)
    if (processor >= CPU_SETSIZE)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    cpu_set_t processors;
    CPU_ZERO(&processors);
    CPU_SET(processor, &processors);
    if (const auto error = ::pthread_setaffinity_np(
            ::pthread_self(), sizeof(processors), &processors);
        error != 0)
        return std::unexpected(std::error_code{error, std::system_category()});
    return {};
#elif defined(CNETMOD_PLATFORM_MACOS)
    thread_affinity_policy_data_t policy{
        static_cast<integer_t>(processor + 1U)};
    const auto thread = mach_thread_self();
    const auto status = ::thread_policy_set(thread,
        THREAD_AFFINITY_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_AFFINITY_POLICY_COUNT);
    ::mach_port_deallocate(mach_task_self(), thread);
    if (status != KERN_SUCCESS)
        return std::unexpected(std::make_error_code(std::errc::not_supported));
    return {};
#else
    (void)processor;
    return std::unexpected(std::make_error_code(std::errc::not_supported));
#endif
}

void spawn_on(io_context& target, task<void> task)
{
    spawn(target, std::move(task));
}

server_context::server_context(unsigned worker_count, unsigned pool_threads,
    thread_affinity_options affinity)
    : pool_(pool_threads == 0 ? 1 : pool_threads),
      affinity_(std::move(affinity))
{
    worker_count = std::max(worker_count, 1U);
    accept_io_ = make_io_context();
    workers_.reserve(worker_count);
    for (unsigned index = 0; index < worker_count; ++index)
        workers_.push_back(make_io_context());
}

server_context::~server_context()
{
    stop();
}

auto server_context::accept_io() noexcept -> io_context&
{
    return *accept_io_;
}

auto server_context::next_worker_io() noexcept -> io_context&
{
    const auto index =
        next_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
    return *workers_[index];
}

auto server_context::worker_count() const noexcept -> unsigned
{
    return static_cast<unsigned>(workers_.size());
}

auto server_context::worker_ios() -> std::vector<io_context*>
{
    std::vector<io_context*> result;
    result.reserve(workers_.size());
    for (const auto& worker : workers_)
        result.push_back(worker.get());
    return result;
}

auto server_context::pool() noexcept -> thread_pool&
{
    return pool_;
}

void server_context::spawn_next(task<void> t)
{
    spawn_on(next_worker_io(), std::move(t));
}

void server_context::run()
{
    threads_.reserve(workers_.size());
    for (std::size_t index{}; index < workers_.size(); ++index)
    {
        auto processor = std::optional<unsigned>{};
        if (affinity_.enabled && !affinity_.worker_processors.empty())
            processor = affinity_.worker_processors[index % affinity_.worker_processors.size()];
        threads_.emplace_back([context = workers_[index].get(), processor]
            {
                if (processor)
                    (void)set_current_thread_affinity(*processor);
                context->run();
            });
    }
    if (affinity_.enabled && affinity_.accept_processor)
        (void)set_current_thread_affinity(*affinity_.accept_processor);
    accept_io_->run();
    for (auto& thread : threads_)
    {
        if (thread.joinable())
            thread.join();
    }
    threads_.clear();
}

void server_context::stop()
{
    if (accept_io_)
        accept_io_->stop();
    for (const auto& worker : workers_)
        worker->stop();
    pool_.request_stop();
}

} // namespace cnetmod
