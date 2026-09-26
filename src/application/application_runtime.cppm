/**
 * @brief Controlled access to application-owned execution and telemetry.
 *
 * The runtime is deliberately integration-neutral: it exposes the host's
 * execution resources, supervised tasks, shared templates and telemetry.
 * Integrations (repositories, Redis, chat models) are injected as components
 * by the modules that own them, so adding an integration never changes this
 * interface.
 */
export module cnetmod.application.runtime;

import std;
import cnetmod.application.async_file_template;
import cnetmod.application.configuration;
import cnetmod.application.rest_template;
import cnetmod.application.json_template;
import cnetmod.application.recovery_policy;
import cnetmod.application.task_supervisor;
import cnetmod.coro.bridge;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.compress;

namespace cnetmod::application {

/**
 * @brief Non-owning handle to the caller/control event loop and CPU pool.
 *
 * Protocol-level components that need an execution context (timers,
 * decorators such as ai::resilient_chat_model, explicit timeouts) receive this
 * handle instead of the host. The referenced resources live as long as the
 * application host. Callers must never run, stop or restart the event loop.
 */
export class execution_context
{
public:
    execution_context(io_context& control_event_loop,
        thread_pool& cpu_pool) noexcept
        : control_event_loop_(&control_event_loop), cpu_pool_(&cpu_pool)
    {
    }

    /**
     * @brief Returns the calling event loop, falling back to the control loop.
     *
     * Request code may pass this reference to an offloaded operation before
     * suspension and will then resume on the socket-owning worker. On a CPU
     * pool or ordinary thread there is no current loop, so the control loop is
     * returned. Capture the loop before leaving an event-loop thread.
     */
    [[nodiscard]] auto event_loop() const noexcept -> io_context&
    {
        if (auto* current = io_context::current())
            return *current;
        return *control_event_loop_;
    }

    /**
     * @brief Returns the host control loop regardless of the calling thread.
     *
     * Managed-service ownership and application lifecycle code use this
     * explicit accessor. Request handlers normally use event_loop().
     */
    [[nodiscard]] auto control_event_loop() const noexcept -> io_context&
    {
        return *control_event_loop_;
    }

    /**
     * @brief The host CPU pool used for offloaded work.
     */
    [[nodiscard]] auto cpu_pool() const noexcept -> thread_pool&
    {
        return *cpu_pool_;
    }

    /**
     * @brief Resumes on the caller loop, or the control loop when none exists.
     */
    [[nodiscard]] auto post() const noexcept -> post_awaitable
    {
        return post_awaitable{event_loop()};
    }

    /**
     * @brief Cancellable sleep on the caller/control event loop.
     */
    [[nodiscard]] auto sleep(std::chrono::steady_clock::duration duration,
        cancel_token& cancellation) const
        -> task<std::expected<void, std::error_code>>
    {
        return async_timer_wait(event_loop(), duration, cancellation);
    }

    /**
     * @brief Bounds a cancellable operation by a relative timeout.
     *
     * On expiry `cancellation` is cancelled with the deadline reason and the
     * result is std::errc::timed_out. Use a dedicated token per operation.
     */
    template <typename T>
    [[nodiscard]] auto with_timeout(std::chrono::steady_clock::duration timeout,
        task<std::expected<T, std::error_code>> operation,
        cancel_token& cancellation) const
        -> task<std::expected<T, std::error_code>>
    {
        return cnetmod::with_timeout(event_loop(), timeout, std::move(operation),
            cancellation);
    }

private:
    io_context* control_event_loop_;
    thread_pool* cpu_pool_;
};

/**
 * @brief Provides supervised background execution without exposing host state.
 *
 * The facade does not own its dependencies and remains valid only while its
 * application host is alive. CPU work is returned to the host event loop after
 * completion. Long-running operations must use spawn_managed() so shutdown can
 * cancel and join them deterministically.
 */
export class application_runtime
{
public:
    /**
     * @brief Binds the facade to application-owned runtime components.
     *
     * Application hosts construct this object after all referenced components.
     * Callers must preserve those components for the facade lifetime.
     */
    application_runtime(io_context& io,
        std::span<io_context* const> event_loops, thread_pool& cpu_pool,
        task_supervisor& supervisor,
        observability::telemetry_hub& telemetry,
        std::stop_token cancellation,
        const application_configuration& configuration);
    application_runtime(io_context& io, thread_pool& cpu_pool,
        task_supervisor& supervisor,
        observability::telemetry_hub& telemetry,
        std::stop_token cancellation,
        const application_configuration& configuration);

    application_runtime(const application_runtime&) = delete;
    auto operator=(const application_runtime&) -> application_runtime& = delete;

    /**
     * @brief Registers a uniquely named, cancellable background operation.
     * @return Success when ownership was accepted by the task supervisor.
     */
    [[nodiscard]] auto spawn_managed(std::string name,
        supervised_task operation, recovery_policy recovery = {},
        bool required = true, std::function<void()> stop_request = {},
        deadline recovery_deadline = {})
        -> std::expected<void, std::error_code>;

    /**
     * @brief Executes CPU-bound work on the host pool and resumes on its event loop.
     *
     * The callable must not retain references whose lifetime ends before this
     * operation completes. Exceptions are propagated to the awaiting coroutine.
     */
    template <typename Function>
    requires std::invocable<std::decay_t<Function>>
    auto offload(Function&& function)
        -> task<std::invoke_result_t<std::decay_t<Function>>>
    {
        if (stop_requested())
            throw std::system_error(
                std::make_error_code(std::errc::operation_canceled));
        auto* caller = io_context::current();
        if (caller == nullptr)
            caller = &io_;
        if constexpr (std::is_void_v<
                          std::invoke_result_t<std::decay_t<Function>>>)
        {
            co_await blocking_invoke(cpu_pool_, *caller,
                std::decay_t<Function>(std::forward<Function>(function)));
            co_return;
        }
        else
        {
            co_return co_await blocking_invoke(cpu_pool_, *caller,
                std::decay_t<Function>(std::forward<Function>(function)));
        }
    }

    /**
     * @brief Schedules the awaiting route coroutine on the managed CPU pool.
     *
     * Pair this operation with resume_to_event_loop() before accessing HTTP
     * request or response state. Prefer offload() when the CPU operation can
     * be expressed as a callable because it restores the event loop even when
     * the callable throws.
     */
    [[nodiscard]] auto schedule_on_cpu() noexcept -> pool_post_awaitable;

    /**
     * @brief Resumes the awaiting coroutine on the application event loop.
     */
    [[nodiscard]] auto resume_to_event_loop() noexcept -> post_awaitable;

    /**
     * @brief Resumes on an explicitly captured event loop.
     *
     * Capture `io_context::current()` before leaving an HTTP worker. This
     * overload is required when schedule_on_cpu() is used in multi-loop mode.
     */
    [[nodiscard]] auto resume_to_event_loop(io_context& event_loop) noexcept
        -> post_awaitable;

    /**
     * @brief Returns the host execution handle for protocol-level components.
     */
    [[nodiscard]] auto executor() noexcept -> execution_context&;

    /**
     * @brief Returns application-managed asynchronous file operations.
     */
    [[nodiscard]] auto files() noexcept -> async_file_template&;

    /**
     * @brief Returns pooled and observable outbound HTTP operations.
     */
    [[nodiscard]] auto rest() noexcept -> rest_template&;

    /**
     * @brief Returns typed JSON operations offloaded to the application CPU pool.
     */
    [[nodiscard]] auto json() noexcept -> json_template&;

    /**
     * @brief Creates response compression managed by the application CPU pool.
     *
     * The returned middleware uses bounded concurrency, request cancellation,
     * and the application measurement sink. A compression call already running
     * inside a native codec cannot be preempted; its result is discarded after
     * cancellation and the bounded operation is allowed to finish.
     */
    [[nodiscard]] auto compression(compress_options options = {})
        -> http::middleware_fn;

    /**
     * @brief Reports whether application shutdown has been requested.
     */
    [[nodiscard]] auto stop_requested() const noexcept -> bool;

    /**
     * @brief Returns the broadcast application shutdown signal.
     *
     * The token is safe to copy and supports stop callbacks. Individual I/O
     * operations must continue to use their own cancel_token instances.
     */
    [[nodiscard]] auto cancellation() const noexcept -> std::stop_token;

    /**
     * @brief Returns the task supervisor used by this application.
     */
    [[nodiscard]] auto tasks() noexcept -> task_supervisor&;

    /**
     * @brief Returns the application telemetry composition root.
     */
    [[nodiscard]] auto telemetry() noexcept
        -> observability::telemetry_hub&;

    /**
     * @brief Returns the validated immutable-at-runtime application settings.
     *
     * Restart-only fields, including credentials, remain unchanged during a
     * hot reload. Callers must not copy secrets into logs or telemetry.
     * Application sections are read through options_monitor<T> components.
     */
    [[nodiscard]] auto configuration() const noexcept
        -> const application_configuration&;

private:
    io_context& io_;
    thread_pool& cpu_pool_;
    task_supervisor& supervisor_;
    observability::telemetry_hub& telemetry_;
    std::stop_token cancellation_;
    const application_configuration& configuration_;
    execution_context executor_;
    async_file_template files_;
    rest_template rest_;
    json_template json_;
};

} // namespace cnetmod::application
