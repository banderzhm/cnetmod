export module cnetmod.protocol.mysql:transaction;

import std;
import :types;
import cnetmod.coro.task;

namespace cnetmod::mysql {

// Forward declaration
export class client;

// =============================================================================
// transaction_guard — RAII transaction guard (auto rollback on exception)
// =============================================================================

export class transaction_guard {
public:
    explicit transaction_guard(client& cli) noexcept : cli_(&cli) {}

    transaction_guard(const transaction_guard&) = delete;
    transaction_guard& operator=(const transaction_guard&) = delete;

    transaction_guard(transaction_guard&& other) noexcept
        : cli_(other.cli_), committed_(other.committed_) {
        other.cli_ = nullptr;
    }

    transaction_guard& operator=(transaction_guard&& other) noexcept {
        if (this != &other) {
            cli_ = other.cli_;
            committed_ = other.committed_;
            other.cli_ = nullptr;
        }
        return *this;
    }

    ~transaction_guard() = default;

    auto commit() -> task<result_set>;
    auto rollback() -> task<result_set>;

    auto is_committed() const noexcept -> bool { return committed_; }

private:
    client* cli_ = nullptr;
    bool committed_ = false;
};

// =============================================================================
// transaction — Transaction helper functions
// =============================================================================

export class transaction {
public:
    /// Begin a transaction
    static auto begin(client& cli) -> task<std::expected<transaction_guard, std::string>>;

    /// Begin a transaction with specific isolation level
    static auto begin(client& cli, isolation_level level)
        -> task<std::expected<transaction_guard, std::string>>;

    /// Execute a function within a transaction (auto commit/rollback)
    template <typename Func>
        requires std::invocable<Func> && 
                 requires(Func f) { { f() } -> std::same_as<task<void>>; }
    static auto execute(client& cli, Func&& func) -> task<result_set>;

    /// Execute a function within a transaction with specific isolation level
    template <typename Func>
        requires std::invocable<Func> && 
                 requires(Func f) { { f() } -> std::same_as<task<void>>; }
    static auto execute(client& cli, Func&& func, isolation_level level) -> task<result_set>;
};

} // namespace cnetmod::mysql
