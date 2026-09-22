/// cnetmod.protocol.openai:agentic — implementations

module;

#include <cnetmod/config.hpp>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.coro.task_group;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.json;
import :model;
import :prompt;
import :checkpoint;
import :agentic;
import cnetmod.json;

namespace cnetmod::openai {

namespace {
    auto workflow_hash(std::string_view value) noexcept -> std::uint64_t
    {
        std::uint64_t result = 14695981039346656037ULL;
        for (const auto character : value)
        {
            result ^= static_cast<unsigned char>(character);
            result *= 1099511628211ULL;
        }
        return result;
    }

    auto checkpoint_path(const std::filesystem::path& directory,
        std::string_view workflow_id) -> std::filesystem::path
    {
        return directory /
            std::format("workflow-{:016x}.json", workflow_hash(workflow_id));
    }

    auto replace_checkpoint_file(const std::filesystem::path& temporary,
        const std::filesystem::path& target) -> std::expected<void, std::string>
    {
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return std::unexpected(std::format(
                "cannot replace checkpoint file '{}': Windows error {}",
                target.string(), GetLastError()));
#else
        std::error_code error;
        std::filesystem::rename(temporary, target, error);
        if (error)
            return std::unexpected(std::format(
                "cannot replace checkpoint file '{}': {}",
                target.string(), error.message()));
#endif
        return {};
    }

    auto encode_human_input(const human_input_request& request) -> json
    {
        return {{"id", request.id}, {"prompt", request.prompt},
            {"response_key", request.response_key},
            {"response_schema", request.response_schema}};
    }

    auto decode_human_input(const json& value)
        -> std::expected<human_input_request, std::string>
    {
        if (!value.is_object() || !value.contains("id") ||
            !value["id"].is_string() || !value.contains("prompt") ||
            !value["prompt"].is_string())
            return std::unexpected("checkpoint human input is invalid");
        auto response_key = cnetmod::json::value_or(
            value, "response_key", std::string{"human_input"});
        auto response_schema = cnetmod::json::value_or(
            value, "response_schema", cnetmod::json::object());
        if (response_key.empty() || !response_schema.is_object())
            return std::unexpected("checkpoint human input is invalid");
        return human_input_request{.id = value["id"].get<std::string>(),
            .prompt = value["prompt"].get<std::string>(),
            .response_key = std::move(response_key),
            .response_schema = std::move(response_schema)};
    }

    auto read_checkpoint(const std::filesystem::path& directory,
        std::string_view workflow_id, std::size_t max_bytes)
        -> std::expected<std::optional<agentic_checkpoint>, std::string>
    {
        const auto path = checkpoint_path(directory, workflow_id);
        std::error_code error;
        if (!std::filesystem::exists(path, error))
            return std::optional<agentic_checkpoint>{};
        if (error)
            return std::unexpected(
                "cannot inspect checkpoint file: " + error.message());
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::unexpected(
                "cannot inspect checkpoint size: " + error.message());
        if (size > max_bytes)
            return std::unexpected(std::format(
                "workflow checkpoint exceeds {} bytes", max_bytes));
        std::ifstream input{path, std::ios::binary};
        if (!input)
            return std::unexpected(
                "cannot open checkpoint file: " + path.string());
        const std::string content{std::istreambuf_iterator<char>{input}, {}};
        auto parsed = cnetmod::json::parse_document(content);
        if (!parsed)
            return std::unexpected("checkpoint file contains invalid JSON");
        auto& payload = *parsed;
        if (!payload.is_object() ||
            cnetmod::json::value_or(payload, "version", std::uint64_t{0}) != 1 ||
            cnetmod::json::value_or(
                payload, "workflow_id", std::string{}) != workflow_id ||
            !payload.contains("checkpoint") ||
            !payload["checkpoint"].is_object())
            return std::unexpected("checkpoint file has an invalid envelope");
        const auto& value = payload["checkpoint"];
        if (!value.contains("scope") || !value["scope"].is_object() ||
            !value.contains("planner") || !value["planner"].is_object() ||
            !value.contains("completed_steps") ||
            !value["completed_steps"].is_uint64())
            return std::unexpected("checkpoint payload is invalid");
        agentic_checkpoint checkpoint{.scope = value["scope"],
            .planner = value["planner"],
            .completed_steps = value["completed_steps"].as<std::size_t>()};
        if (value.contains("pending_human_input") &&
            !value["pending_human_input"].is_null())
        {
            auto pending = decode_human_input(value["pending_human_input"]);
            if (!pending)
                return std::unexpected(pending.error());
            checkpoint.pending_human_input = std::move(*pending);
        }
        return std::optional<agentic_checkpoint>{std::move(checkpoint)};
    }

    auto write_checkpoint(const std::filesystem::path& directory,
        std::string_view workflow_id, const agentic_checkpoint& checkpoint,
        std::size_t max_bytes) -> std::expected<void, std::string>
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
            return std::unexpected(
                "cannot create checkpoint directory: " + error.message());
        auto value = cnetmod::json::object();
        value["scope"] = checkpoint.scope;
        value["planner"] = checkpoint.planner;
        value["completed_steps"] = checkpoint.completed_steps;
        value["pending_human_input"] = nullptr;
        if (checkpoint.pending_human_input)
            value["pending_human_input"] =
                encode_human_input(*checkpoint.pending_human_input);
        auto encoded = cnetmod::json::write_document(cnetmod::json::object(
            {{"version", std::uint64_t{1}},
                {"workflow_id", std::string{workflow_id}},
                {"checkpoint", std::move(value)}}));
        if (!encoded)
            return std::unexpected("cannot encode workflow checkpoint");
        const auto& content = *encoded;
        if (content.size() > max_bytes)
            return std::unexpected(std::format(
                "workflow checkpoint exceeds {} bytes", max_bytes));
        const auto target = checkpoint_path(directory, workflow_id);
        static std::atomic<std::uint64_t> sequence{0};
        auto temporary = target;
        temporary += std::format(".tmp-{}",
            sequence.fetch_add(1, std::memory_order_relaxed));
        {
            std::ofstream output{temporary,
                std::ios::binary | std::ios::trunc};
            if (!output)
                return std::unexpected(
                    "cannot create temporary checkpoint file: " +
                    temporary.string());
            output.write(content.data(),
                static_cast<std::streamsize>(content.size()));
            output.flush();
            if (!output)
            {
                std::filesystem::remove(temporary, error);
                return std::unexpected(
                    "cannot write temporary checkpoint file: " +
                    temporary.string());
            }
        }
        auto replaced = replace_checkpoint_file(temporary, target);
        if (!replaced)
            std::filesystem::remove(temporary, error);
        return replaced;
    }
} // namespace

auto agentic_scope::read(std::string key) -> task<std::optional<json>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto* found = cnetmod::json::find(state_, key);
    if (found == nullptr)
        co_return std::nullopt;
    co_return *found;
}

auto agentic_scope::write(std::string key, json value) -> task<void>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    state_[std::move(key)] = std::move(value);
}

auto agentic_scope::erase(std::string key) -> task<void>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    if (state_.is_object())
        state_.get_object().erase(key);
}

auto agentic_scope::contains(std::string key) -> task<bool>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return state_.contains(key);
}

auto agentic_scope::snapshot() -> task<json>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return state_;
}

auto agentic_scope::restore(json state)
    -> task<std::expected<void, std::string>>
{
    if (!state.is_object())
        co_return std::unexpected("agentic scope state must be a JSON object");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    state_ = std::move(state);
    co_return std::expected<void, std::string>{};
}

functional_agent::functional_agent(std::string name, agent_handler handler)
    : name_(std::move(name)), handler_(std::move(handler))
{
    if (name_.empty())
        throw std::invalid_argument("agent name cannot be empty");
    if (!handler_)
        throw std::invalid_argument("agent handler cannot be empty");
}

auto functional_agent::name() const noexcept -> std::string_view
{
    return name_;
}

auto functional_agent::invoke(agentic_scope& scope,
    const run_config& config) -> task<std::expected<void, std::string>>
{
    co_return co_await handler_(scope, config);
}

auto workflow_planner::save_state() const -> json
{
    return cnetmod::json::object();
}

auto workflow_planner::restore_state(const json&)
    -> std::expected<void, std::string>
{
    return {};
}

sequence_planner::sequence_planner(std::vector<workflow_agent*> agents)
    : agents_(std::move(agents))
{
}

auto sequence_planner::next(agentic_scope&, const run_config&)
    -> task<std::expected<planner_directive, std::string>>
{
    if (cursor_ >= agents_.size())
        co_return planner_directive{.status = planner_status::complete};
    auto* selected = agents_[cursor_++];
    if (!selected)
        co_return std::unexpected("sequence planner contains a null agent");
    co_return planner_directive{.status = planner_status::execute,
        .agents = {selected}};
}

auto sequence_planner::save_state() const -> json
{
    return {{"cursor", cursor_}};
}

auto sequence_planner::restore_state(const json& state)
    -> std::expected<void, std::string>
{
    if (!state.is_object())
        return std::unexpected("sequence planner state must be an object");
    const auto cursor = cnetmod::json::value_or(
        state, "cursor", std::size_t{0});
    if (cursor > agents_.size())
        return std::unexpected("sequence planner cursor is out of range");
    cursor_ = cursor;
    return {};
}

auto in_memory_agentic_scope_store::load(std::string workflow_id)
    -> task<std::expected<std::optional<agentic_checkpoint>, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto found = checkpoints_.find(workflow_id);
    if (found == checkpoints_.end())
        co_return std::optional<agentic_checkpoint>{};
    co_return std::optional<agentic_checkpoint>{found->second};
}

auto in_memory_agentic_scope_store::save(std::string workflow_id,
    agentic_checkpoint checkpoint) -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    checkpoints_.insert_or_assign(
        std::move(workflow_id), std::move(checkpoint));
    co_return std::expected<void, std::string>{};
}

auto in_memory_agentic_scope_store::erase(std::string workflow_id)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    checkpoints_.erase(workflow_id);
    co_return std::expected<void, std::string>{};
}

checkpoint_agentic_scope_store::checkpoint_agentic_scope_store(
    checkpoint_store& store, std::string branch)
    : store_(store), branch_(std::move(branch))
{
    if (branch_.empty())
        throw std::invalid_argument("checkpoint branch cannot be empty");
}

auto checkpoint_agentic_scope_store::load(std::string workflow_id)
    -> task<std::expected<std::optional<agentic_checkpoint>, std::string>>
{
    auto loaded = co_await store_.load_latest(workflow_id, branch_);
    if (!loaded)
        co_return std::unexpected(loaded.error());
    if (!*loaded)
        co_return std::optional<agentic_checkpoint>{};
    const auto& value = (*loaded)->state;
    if (!value.is_object() || !value.contains("scope") ||
        !value["scope"].is_object() || !value.contains("planner") ||
        !value["planner"].is_object() ||
        !value.contains("completed_steps") ||
        !value["completed_steps"].is_uint64())
        co_return std::unexpected("agentic checkpoint state is invalid");
    agentic_checkpoint checkpoint{.scope = value["scope"],
        .planner = value["planner"],
        .completed_steps = value["completed_steps"].as<std::size_t>()};
    if (value.contains("pending_human_input") &&
        !value["pending_human_input"].is_null())
    {
        auto pending = decode_human_input(value["pending_human_input"]);
        if (!pending)
            co_return std::unexpected(pending.error());
        checkpoint.pending_human_input = std::move(*pending);
    }
    co_return std::optional<agentic_checkpoint>{std::move(checkpoint)};
}

auto checkpoint_agentic_scope_store::save(std::string workflow_id,
    agentic_checkpoint checkpoint) -> task<std::expected<void, std::string>>
{
    auto latest = co_await store_.load_latest(workflow_id, branch_);
    if (!latest)
        co_return std::unexpected(latest.error());
    auto state = cnetmod::json::object();
    state["scope"] = std::move(checkpoint.scope);
    state["planner"] = std::move(checkpoint.planner);
    state["completed_steps"] = checkpoint.completed_steps;
    state["pending_human_input"] = nullptr;
    if (checkpoint.pending_human_input)
        state["pending_human_input"] =
            encode_human_input(*checkpoint.pending_human_input);
    auto committed = co_await store_.commit({.thread_id = workflow_id,
        .branch = branch_,
        .state = std::move(state),
        .metadata = cnetmod::json::object({{"kind", "agentic_scope"}}),
        .expected_head_version = *latest
            ? std::optional<std::uint64_t>{(*latest)->version}
            : std::optional<std::uint64_t>{0}});
    if (!committed)
        co_return std::unexpected(committed.error());
    co_return std::expected<void, std::string>{};
}

auto checkpoint_agentic_scope_store::erase(std::string workflow_id)
    -> task<std::expected<void, std::string>>
{
    co_return co_await store_.erase_branch(
        std::move(workflow_id), branch_);
}

file_agentic_scope_store::file_agentic_scope_store(io_context& context,
    thread_pool& pool, std::filesystem::path directory,
    file_agentic_store_options options)
    : context_(context), pool_(pool), directory_(std::move(directory)), options_(options)
{
    if (directory_.empty())
        throw std::invalid_argument("checkpoint directory cannot be empty");
    if (options_.max_checkpoint_bytes == 0)
        throw std::invalid_argument(
            "checkpoint size limit cannot be zero");
}

auto file_agentic_scope_store::load(std::string workflow_id)
    -> task<std::expected<std::optional<agentic_checkpoint>, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return co_await blocking_invoke(pool_, context_,
        [directory = directory_, workflow_id = std::move(workflow_id),
            max_bytes = options_.max_checkpoint_bytes]
        {
            return read_checkpoint(directory, workflow_id, max_bytes);
        });
}

auto file_agentic_scope_store::save(std::string workflow_id,
    agentic_checkpoint checkpoint) -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return co_await blocking_invoke(pool_, context_,
        [directory = directory_, workflow_id = std::move(workflow_id),
            checkpoint = std::move(checkpoint),
            max_bytes = options_.max_checkpoint_bytes]
        {
            return write_checkpoint(
                directory, workflow_id, checkpoint, max_bytes);
        });
}

auto file_agentic_scope_store::erase(std::string workflow_id)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return co_await blocking_invoke(pool_, context_,
        [directory = directory_, workflow_id = std::move(workflow_id)]
        -> std::expected<void, std::string>
        {
            std::error_code error;
            std::filesystem::remove(
                checkpoint_path(directory, workflow_id), error);
            if (error)
                return std::unexpected(
                    "cannot erase workflow checkpoint: " + error.message());
            return {};
        });
}

agentic_runtime::agentic_runtime(io_context& context,
    agentic_scope_store& store, std::size_t max_steps)
    : context_(context), store_(store), max_steps_(std::max<std::size_t>(1, max_steps))
{
}

auto agentic_runtime::execute(std::string workflow_id,
    workflow_planner& planner, json initial_state, const run_config& config)
    -> task<std::expected<workflow_result, std::string>>
{
    if (workflow_id.empty())
        co_return std::unexpected("workflow id cannot be empty");
    if (!initial_state.is_object())
        co_return std::unexpected("initial agentic state must be a JSON object");

    agentic_scope scope;
    std::size_t completed_steps = 0;
    auto checkpoint = co_await store_.load(workflow_id);
    if (!checkpoint)
        co_return std::unexpected("checkpoint load failed: " + checkpoint.error());
    if (*checkpoint)
    {
        if ((*checkpoint)->pending_human_input)
        {
            co_return workflow_result{
                .status = workflow_status::suspended,
                .state = (*checkpoint)->scope,
                .suspension_reason = "waiting for human input",
                .pending_human_input = (*checkpoint)->pending_human_input,
                .completed_steps = (*checkpoint)->completed_steps};
        }
        auto restored = co_await scope.restore((*checkpoint)->scope);
        if (!restored)
            co_return std::unexpected(restored.error());
        auto planner_restored = planner.restore_state((*checkpoint)->planner);
        if (!planner_restored)
            co_return std::unexpected(planner_restored.error());
        completed_steps = (*checkpoint)->completed_steps;
    }
    else
    {
        auto restored = co_await scope.restore(std::move(initial_state));
        if (!restored)
            co_return std::unexpected(restored.error());
    }

    workflow_result result;
    for (; completed_steps < max_steps_;)
    {
        if (config.is_cancelled())
            co_return std::unexpected("agentic workflow cancelled");
        const auto planner_before_step = planner.save_state();
        auto directive = co_await planner.next(scope, config);
        if (!directive)
        {
            (void)planner.restore_state(planner_before_step);
            co_return std::unexpected("planner failed: " + directive.error());
        }
        if (directive->status == planner_status::complete)
        {
            result.status = workflow_status::completed;
            result.state = co_await scope.snapshot();
            result.completed_steps = completed_steps;
            co_return result;
        }
        if (directive->status == planner_status::suspend)
        {
            if (directive->human_input &&
                (directive->human_input->id.empty() ||
                    directive->human_input->response_key.empty()))
            {
                (void)planner.restore_state(planner_before_step);
                co_return std::unexpected(
                    "human input request requires id and response_key");
            }
            auto state = co_await scope.snapshot();
            auto saved = co_await store_.save(workflow_id,
                {.scope = state,
                    .planner = planner.save_state(),
                    .completed_steps = completed_steps,
                    .pending_human_input = directive->human_input});
            if (!saved)
                co_return std::unexpected("checkpoint save failed: " + saved.error());
            result.status = workflow_status::suspended;
            result.state = std::move(state);
            result.suspension_reason = std::move(directive->reason);
            result.pending_human_input = std::move(directive->human_input);
            result.completed_steps = completed_steps;
            co_return result;
        }
        if (directive->agents.empty())
        {
            (void)planner.restore_state(planner_before_step);
            co_return std::unexpected("planner execute directive has no agents");
        }

        struct group_state
        {
            async_mutex mutex;
            std::vector<agent_invocation> invocations;
            std::string first_error;
        } group_state;

        task_group group(context_);
        for (auto* selected : directive->agents)
        {
            if (!selected)
            {
                (void)planner.restore_state(planner_before_step);
                co_return std::unexpected("planner selected a null agent");
            }
            const auto started = group.run([&, selected](cancel_token& token)
                                               -> task<std::expected<void, std::error_code>>
                {
                    auto child_config = config;
                    child_config.cancellation = &token;
                    auto invocation = co_await selected->invoke(scope, child_config);
                    co_await group_state.mutex.lock();
                    async_lock_guard guard(group_state.mutex, std::adopt_lock);
                    group_state.invocations.push_back({.agent = std::string(selected->name()),
                        .successful = invocation.has_value(),
                        .error = invocation ? std::string{} : invocation.error()});
                    if (!invocation)
                    {
                        if (group_state.first_error.empty())
                            group_state.first_error = invocation.error();
                        co_return std::unexpected(
                            std::make_error_code(std::errc::operation_canceled));
                    }
                    co_return std::expected<void, std::error_code>{};
                });
            if (!started)
            {
                (void)planner.restore_state(planner_before_step);
                co_return std::unexpected("failed to schedule workflow agent");
            }
        }
        auto joined = co_await group.join();
        result.invocations.insert(result.invocations.end(),
            std::make_move_iterator(group_state.invocations.begin()),
            std::make_move_iterator(group_state.invocations.end()));
        if (!joined)
        {
            (void)planner.restore_state(planner_before_step);
            co_return std::unexpected(group_state.first_error.empty()
                    ? joined.error().message()
                    : group_state.first_error);
        }

        ++completed_steps;
        auto state = co_await scope.snapshot();
        auto saved = co_await store_.save(workflow_id,
            {.scope = std::move(state), .planner = planner.save_state(), .completed_steps = completed_steps});
        if (!saved)
            co_return std::unexpected("checkpoint save failed: " + saved.error());
    }
    co_return std::unexpected(std::format(
        "agentic workflow exceeded max_steps={}", max_steps_));
}

auto agentic_runtime::resume(std::string workflow_id,
    workflow_planner& planner, human_input_response response,
    const run_config& config)
    -> task<std::expected<workflow_result, std::string>>
{
    auto loaded = co_await store_.load(workflow_id);
    if (!loaded)
        co_return std::unexpected("checkpoint load failed: " + loaded.error());
    if (!*loaded)
        co_return std::unexpected("workflow checkpoint was not found");
    auto checkpoint = std::move(**loaded);
    if (!checkpoint.pending_human_input)
        co_return std::unexpected("workflow is not waiting for human input");
    if (response.request_id != checkpoint.pending_human_input->id)
        co_return std::unexpected("human input request id does not match");
    if (!checkpoint.pending_human_input->response_schema.empty())
    {
        auto valid = validate_json_schema(response.value,
            checkpoint.pending_human_input->response_schema);
        if (!valid)
            co_return std::unexpected("human input validation failed: " +
                valid.error());
    }
    checkpoint.scope[checkpoint.pending_human_input->response_key] =
        std::move(response.value);
    checkpoint.pending_human_input.reset();
    auto saved = co_await store_.save(workflow_id, std::move(checkpoint));
    if (!saved)
        co_return std::unexpected("checkpoint save failed: " + saved.error());
    co_return co_await execute(std::move(workflow_id), planner,
        cnetmod::json::object(), config);
}

auto agentic_runtime::discard(std::string workflow_id)
    -> task<std::expected<void, std::string>>
{
    co_return co_await store_.erase(std::move(workflow_id));
}

} // namespace cnetmod::openai
