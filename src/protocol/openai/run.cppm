/// Provider-neutral lifecycle observation shared by OpenAI integrations.
export module cnetmod.protocol.openai:run;

import std;
import cnetmod.coro.cancel;
import cnetmod.protocol.http.middleware.tracing;
import :foundation;

namespace cnetmod::openai {

export enum class run_event_type
{
    model_start,
    model_end,
    model_error,
    model_retry,
    model_rejected,
    tool_start,
    tool_end,
    tool_error,
    retriever_start,
    retriever_end,
    retriever_error,
    agent_start,
    agent_end,
    agent_error
};

export struct run_event
{
    run_event_type type = run_event_type::model_start;
    std::string run_id;
    std::string name;
    std::string detail;
    std::size_t attempt = 0;
    std::chrono::system_clock::time_point timestamp =
        std::chrono::system_clock::now();
    json attributes = {};
    std::optional<http::tracing::trace_context> trace_parent;
    std::string parent_operation_id;
    std::string operation_id;
};

export using run_callback = std::function<void(const run_event&)>;

export class run_listener
{
public:
    virtual ~run_listener() = default;
    virtual void on_event(const run_event& event) = 0;
};

export class functional_run_listener final : public run_listener
{
public:
    explicit functional_run_listener(run_callback callback);
    void on_event(const run_event& event) override;

private:
    run_callback callback_;
};

export struct run_config
{
    std::string run_id;
    std::vector<std::string> tags;
    std::map<std::string, std::string> metadata;
    run_callback callback;
    std::vector<run_listener*> listeners;
    cancel_token* cancellation = nullptr;
    std::optional<http::tracing::trace_context> trace_parent;
    std::string parent_operation_id;

    [[nodiscard]] auto is_cancelled() const noexcept -> bool;
    [[nodiscard]] auto has_observers() const noexcept -> bool;
    void notify(const run_event& event) const;

    template <typename Factory>
    void notify_lazy(Factory&& factory) const noexcept
    {
        if (!has_observers())
            return;
        try
        {
            notify(std::invoke(std::forward<Factory>(factory)));
        }
        catch (...)
        {
        }
    }
};

export class run_scope
{
public:
    run_scope(const run_config& config, run_event_type start_type,
        run_event_type success_type, run_event_type error_type,
        std::string_view name, std::string_view detail = {},
        json attributes = {}) noexcept;
    ~run_scope();
    run_scope(const run_scope&) = delete;
    auto operator=(const run_scope&) -> run_scope& = delete;
    run_scope(run_scope&& other) noexcept;
    auto operator=(run_scope&& other) noexcept -> run_scope&;

    template <typename Factory>
    [[nodiscard]] static auto start_lazy(const run_config& config,
        run_event_type start_type, run_event_type success_type,
        run_event_type error_type, std::string_view name, Factory&& factory,
        std::string_view detail = {}) noexcept -> run_scope
    {
        if (config.has_observers())
        {
            try
            {
                return run_scope{config, start_type, success_type, error_type,
                    name, detail, std::invoke(std::forward<Factory>(factory))};
            }
            catch (...)
            {
            }
        }
        return run_scope{config, start_type, success_type, error_type, name, detail};
    }

    void succeed(std::string_view detail = {}, std::size_t attempt = 0,
        json attributes = {});
    void fail(std::string_view detail = {}, std::size_t attempt = 0,
        json attributes = {});

    template <typename Factory>
    void succeed_lazy(Factory&& factory, std::string_view detail = {},
        std::size_t attempt = 0) noexcept
    {
        finish_lazy(success_type_, std::forward<Factory>(factory), detail, attempt);
    }

    template <typename Factory>
    void fail_lazy(Factory&& factory, std::string_view detail = {},
        std::size_t attempt = 0) noexcept
    {
        finish_lazy(error_type_, std::forward<Factory>(factory), detail, attempt);
    }

    [[nodiscard]] auto operation_id() const noexcept -> std::string_view;
    [[nodiscard]] auto child_config() const -> run_config;

private:
    template <typename Factory>
    void finish_lazy(run_event_type type, Factory&& factory,
        std::string_view detail, std::size_t attempt) noexcept
    {
        if (!config_)
            return;
        try
        {
            finish(type, detail, attempt, std::invoke(std::forward<Factory>(factory)));
        }
        catch (...)
        {
            finish(type, detail, attempt, {});
        }
    }

    void finish(run_event_type type, std::string_view detail,
        std::size_t attempt, json attributes) noexcept;

    const run_config* config_ = nullptr;
    const run_config* source_config_ = nullptr;
    run_event_type success_type_ = run_event_type::model_end;
    run_event_type error_type_ = run_event_type::model_error;
    std::string name_;
    std::string operation_id_;
};

} // namespace cnetmod::openai
