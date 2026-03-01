module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mysql;

import :transaction;
import :client;
import :types;
import cnetmod.coro.task;

namespace cnetmod::mysql {

// =============================================================================
// transaction_guard implementation
// =============================================================================

auto transaction_guard::commit() -> task<result_set> {
    if (!cli_ || committed_) co_return result_set{};
    committed_ = true;
    co_return co_await cli_->execute("COMMIT");
}

auto transaction_guard::rollback() -> task<result_set> {
    if (!cli_ || committed_) co_return result_set{};
    committed_ = true;
    co_return co_await cli_->execute("ROLLBACK");
}

// =============================================================================
// transaction implementation
// =============================================================================

auto transaction::begin(client& cli) -> task<std::expected<transaction_guard, std::string>> {
    auto rs = co_await cli.execute("START TRANSACTION");
    if (rs.is_err()) {
        co_return std::unexpected(rs.error_msg);
    }
    co_return transaction_guard{cli};
}

auto transaction::begin(client& cli, isolation_level level)
    -> task<std::expected<transaction_guard, std::string>>
{
    // Set isolation level for this transaction
    std::string sql = "SET TRANSACTION ISOLATION LEVEL ";
    sql.append(isolation_level_to_str(level));
    
    auto set_rs = co_await cli.execute(sql);
    if (set_rs.is_err()) {
        co_return std::unexpected(set_rs.error_msg);
    }

    auto start_rs = co_await cli.execute("START TRANSACTION");
    if (start_rs.is_err()) {
        co_return std::unexpected(start_rs.error_msg);
    }

    co_return transaction_guard{cli};
}

} // namespace cnetmod::mysql
