export module cnetmod.orm.interceptor_chain;

import std;
import cnetmod.orm.sql_parameters;

export namespace cnetmod::orm {

/**
 * @brief SQL operation category supplied to ORM interceptors.
 */
enum class sql_operation : std::uint8_t
{
    query,
    insert,
    update,
    remove,
    execute,
};

/**
 * @brief Owned SQL and bound arguments passed between interceptors.
 */
struct intercepted_statement
{
    std::string sql;
    std::vector<param_value> parameters;
};

/**
 * @brief Per-statement controls that cannot disable isolation or SQL safety.
 */
struct statement_interceptor_options
{
    bool logical_delete = true;
};

using sql_interceptor_function = std::function<
    std::expected<intercepted_statement, std::string>(
        sql_operation, intercepted_statement)>;

/**
 * @brief Ordered, freezeable SQL interception pipeline.
 *
 * Interceptors execute in ascending priority. A chain rejects duplicate names
 * and registration after `freeze()`. Each callback owns its input and must
 * return either a transformed parameterized statement or an explanatory
 * error.
 */
class interceptor_chain
{
public:
    interceptor_chain();
    ~interceptor_chain();

    interceptor_chain(interceptor_chain&&) noexcept;
    auto operator=(interceptor_chain&&) noexcept -> interceptor_chain&;
    interceptor_chain(const interceptor_chain&) = delete;
    auto operator=(const interceptor_chain&) -> interceptor_chain& = delete;

    auto add(std::string name, int priority, sql_interceptor_function function)
        -> std::expected<void, std::string>;
    auto freeze() -> std::expected<void, std::string>;
    [[nodiscard]] auto frozen() const noexcept -> bool;
    [[nodiscard]] auto empty() const noexcept -> bool;

    auto apply(sql_operation operation, intercepted_statement statement) const
        -> std::expected<intercepted_statement, std::string>;
    auto apply(sql_operation operation, intercepted_statement statement,
        statement_interceptor_options options) const
        -> std::expected<intercepted_statement, std::string>;

private:
    struct implementation;
    std::unique_ptr<implementation> implementation_;
};

} // namespace cnetmod::orm
