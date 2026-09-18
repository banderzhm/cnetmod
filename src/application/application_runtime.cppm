/**
 * @brief Controlled access to application-owned execution and telemetry.
 */
export module cnetmod.application.runtime;

import std;
import cnetmod.application.async_file_template;
import cnetmod.application.rest_template;
import cnetmod.application.recovery_policy;
import cnetmod.application.task_supervisor;
import cnetmod.coro.bridge;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.pool;
import cnetmod.io.io_context;
import cnetmod.observability;

namespace cnetmod::application {

/**
 * @brief Provides supervised background execution without exposing raw I/O state.
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
    application_runtime(io_context& io, thread_pool& cpu_pool,
        task_supervisor& supervisor,
        observability::telemetry_hub& telemetry,
        std::stop_token cancellation) noexcept;

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
        if constexpr (std::is_void_v<
                          std::invoke_result_t<std::decay_t<Function>>>)
        {
            co_await blocking_invoke(cpu_pool_, io_,
                std::decay_t<Function>(std::forward<Function>(function)));
            co_return;
        }
        else
        {
            co_return co_await blocking_invoke(cpu_pool_, io_,
                std::decay_t<Function>(std::forward<Function>(function)));
        }
    }

    /**
     * @brief Returns application-managed asynchronous file operations.
     */
    [[nodiscard]] auto files() noexcept -> async_file_template&;

    /**
     * @brief Returns pooled and observable outbound HTTP operations.
     */
    [[nodiscard]] auto rest() noexcept -> rest_template&;

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

private:
    io_context& io_;
    thread_pool& cpu_pool_;
    task_supervisor& supervisor_;
    observability::telemetry_hub& telemetry_;
    std::stop_token cancellation_;
    async_file_template files_;
    rest_template rest_;
};

} // namespace cnetmod::application
