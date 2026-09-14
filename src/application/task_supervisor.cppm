/**
 * @brief Structured ownership and bounded restart policy for long-running tasks.
 */
export module cnetmod.application.task_supervisor;

import std;
import cnetmod.application.recovery_policy;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;

namespace cnetmod::application {

/**
 * @brief Observable state of a supervised background task.
 */
export enum class supervised_task_state
{
    starting,
    running,
    recovering,
    failed,
    stopped
};

export using supervised_task =
    std::function<task<std::expected<void, std::error_code>>(cancel_token&)>;
export using recovery_exhausted_handler =
    std::function<void(std::string_view, std::error_code)>;

/**
 * @brief Owns cancellable background tasks and bounded restart loops.
 *
 * Required tasks notify the application when their recovery budget is
 * exhausted. join() propagates terminal errors after cooperative cancellation.
 */
export class task_supervisor
{
public:
    /**
     * @brief Creates a supervisor bound to an application I/O context.
     */
    explicit task_supervisor(io_context& context);
    ~task_supervisor();

    task_supervisor(const task_supervisor&) = delete;
    auto operator=(const task_supervisor&) -> task_supervisor& = delete;

    /**
     * @brief Starts ownership of a uniquely named background operation.
     * @param stop_request Optional nonblocking, thread-safe cancellation adapter.
     * It must not throw and its captures must remain alive until join completes.
     * @param recovery_deadline Optional absolute limit including the first attempt.
     * Operations must enforce this limit cooperatively during their own I/O.
     */
    [[nodiscard]] auto supervise(std::string name, supervised_task operation,
        recovery_policy recovery = {}, bool required = true,
        std::function<void()> stop_request = {}, deadline recovery_deadline = {})
        -> std::expected<void, std::error_code>;
    /**
     * @brief Returns the current state using an allocation-free borrowed-name lookup.
     */
    [[nodiscard]] auto state(std::string_view name) const noexcept
        -> std::optional<supervised_task_state>;
    /**
     * @brief Returns the last error using an allocation-free borrowed-name lookup.
     */
    [[nodiscard]] auto last_error(std::string_view name) const noexcept
        -> std::error_code;
    /**
     * @brief Installs the callback invoked when required recovery is exhausted.
     */
    void on_recovery_exhausted(recovery_exhausted_handler handler);

    /**
     * @brief Requests cooperative cancellation of every owned task.
     */
    void request_stop() noexcept;

    /**
     * @brief Waits for all owned tasks and propagates terminal failures.
     *
     * Optional failures do not fail the join. Multiple required failures are
     * selected by task name for deterministic results; per-task errors remain
     * available through last_error(). Recovered failures are not terminal.
     */
    [[nodiscard]] auto join() -> task<std::expected<void, std::error_code>>;

private:
    class implementation;
    std::shared_ptr<implementation> implementation_;
};

} // namespace cnetmod::application
