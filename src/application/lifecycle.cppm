/// Ordered asynchronous startup and best-effort shutdown hooks.
export module cnetmod.application.lifecycle;

import std;
import cnetmod.coro.task;

namespace cnetmod::application {

export using lifecycle_action =
    std::function<task<std::expected<void, std::error_code>>()>;

export class application_lifecycle
{
public:
    void on_start(lifecycle_action action);
    void on_stop(lifecycle_action action);

    [[nodiscard]] auto start() -> task<std::expected<void, std::error_code>>;
    /// Runs every shutdown action in reverse registration order and returns
    /// the first failure after cleanup has completed.
    [[nodiscard]] auto stop() -> task<std::expected<void, std::error_code>>;

private:
    std::vector<lifecycle_action> startup_;
    std::vector<lifecycle_action> shutdown_;
};

} // namespace cnetmod::application
