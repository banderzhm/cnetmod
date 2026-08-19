export module cnetmod.orm.schema_migration_runner;

import std;
import cnetmod.coro.task;
import cnetmod.orm.semantic_schema_migration;

export namespace cnetmod::orm {

// Driver-neutral orchestration for semantic schema migrations.  Database
// providers supply only the six infrastructure callbacks; migration ordering,
// checksum validation, SQL rendering and failure semantics stay in cnetmod.
class schema_migration_runner
{
public:
    struct callbacks
    {
        std::function<task<std::expected<std::vector<applied_schema_migration>, std::string>>()> load_applied;
        std::function<task<std::expected<void, std::string>>()> acquire_lock;
        std::function<task<void>()> release_lock;
        std::function<task<std::expected<void, std::string>>(std::string_view)> execute_sql;
        std::function<task<std::expected<void, std::string>>(const schema_migration&, const schema_migration_checksum&)> record_started;
        std::function<task<std::expected<void, std::string>>(const schema_migration&, const schema_migration_checksum&)> record_completed;
    };

    schema_migration_runner(relational_dialect dialect, callbacks operations)
        : dialect_(dialect), operations_(std::move(operations))
    {
    }

    [[nodiscard]] auto run(std::span<const schema_migration> declared)
        -> task<std::expected<std::size_t, std::string>>
    {
        if (!operations_.load_applied || !operations_.acquire_lock
            || !operations_.release_lock || !operations_.execute_sql
            || !operations_.record_started || !operations_.record_completed)
            co_return std::unexpected("schema migration runner callbacks are incomplete");

        auto locked = co_await operations_.acquire_lock();
        if (!locked)
            co_return std::unexpected(locked.error());

        auto finish = [this]() -> task<void>
        {
            co_await operations_.release_lock();
        };

        auto applied = co_await operations_.load_applied();
        if (!applied)
        {
            co_await finish();
            co_return std::unexpected(applied.error());
        }
        auto plan = plan_forward_schema_migrations(declared, *applied);
        if (!plan)
        {
            co_await finish();
            co_return std::unexpected(plan.error().message);
        }

        std::size_t count{};
        for (const auto* migration : plan->pending)
        {
            auto checksum = canonical_schema_migration_checksum(*migration);
            if (!checksum)
            {
                co_await finish();
                co_return std::unexpected(checksum.error().message);
            }
            auto started = co_await operations_.record_started(*migration, *checksum);
            if (!started)
            {
                co_await finish();
                co_return std::unexpected(started.error());
            }
            auto statements = render_schema_migration(*migration, dialect_);
            if (!statements)
            {
                co_await finish();
                co_return std::unexpected(statements.error().message);
            }
            for (const auto& sql : *statements)
            {
                auto executed = co_await operations_.execute_sql(sql);
                if (!executed)
                {
                    co_await finish();
                    co_return std::unexpected(executed.error());
                }
            }
            auto completed = co_await operations_.record_completed(*migration, *checksum);
            if (!completed)
            {
                co_await finish();
                co_return std::unexpected(completed.error());
            }
            ++count;
        }
        co_await finish();
        co_return count;
    }

private:
    relational_dialect dialect_;
    callbacks operations_;
};

} // namespace cnetmod::orm
