/**
 * @brief Provider-neutral result and policy contracts shared by ORM facades.
 */
export module cnetmod.orm.repository_contract;

import std;
import cnetmod.orm.sql_query_data;

export namespace cnetmod::orm {

/**
 * @brief Selects the cardinality contract for a single-row query.
 */
enum class single_result_policy
{
    require_unique,
    first
};

/**
 * @brief Explicit authorization for an unbounded DELETE or UPDATE operation.
 */
struct allow_full_table_t
{
    explicit constexpr allow_full_table_t() = default;
};

inline constexpr allow_full_table_t allow_full_table{};

using projection_row = std::map<std::string, field_value, std::less<>>;

/**
 * @brief Carries mapped values and native database diagnostics.
 */
template <class T> struct model_result
{
    std::vector<T> data;
    std::uint64_t affected_rows{};
    std::uint64_t last_insert_id{};
    std::string error_msg;
    std::string sql_state;
    std::uint32_t error_code{};
    std::error_code framework_error;
    std::optional<std::size_t> batch_index;
    std::optional<std::size_t> item_index;
    std::string operation;

    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return error_msg.empty() && !framework_error;
    }

    [[nodiscard]] auto is_err() const noexcept -> bool
    {
        return !ok();
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return data.empty();
    }

    [[nodiscard]] auto first() const -> std::optional<T>
    {
        return data.empty() ? std::nullopt : std::optional<T>{data.front()};
    }
};

/**
 * @brief Carries one page of mapped values and complete query diagnostics.
 */
template <class T> struct page_result
{
    model_result<T> records;
    std::size_t total{};
    std::size_t page = 1;
    std::size_t page_size = 20;
    std::size_t total_pages{};

    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return records.ok();
    }

    [[nodiscard]] auto has_next() const noexcept -> bool
    {
        return page < total_pages;
    }

    [[nodiscard]] auto has_previous() const noexcept -> bool
    {
        return page > 1 && total_pages > 0;
    }
};

/**
 * @brief Bounds cooperative repository streaming.
 */
struct stream_options
{
    std::size_t batch_size = 256;
    std::size_t max_rows = std::numeric_limits<std::size_t>::max();
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

/**
 * @brief Bounds a stateful database cursor.
 */
struct cursor_options
{
    std::size_t batch_size = 256;
    std::size_t max_rows = std::numeric_limits<std::size_t>::max();
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

} // namespace cnetmod::orm
