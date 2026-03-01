export module cnetmod.protocol.mysql:orm_page;

import std;
import :types;
import :client;
import :orm_meta;
import :orm_mapper;
import :orm_wrapper;
import :format_sql;
import cnetmod.coro.task;

namespace cnetmod::mysql::orm {

// =============================================================================
// page — Pagination result container
// =============================================================================

export template <Model T>
struct page {
    std::vector<T> records;           // Current page records
    std::int64_t   total = 0;         // Total record count
    std::int64_t   current_page = 1;  // Current page number (1-based)
    std::int64_t   page_size = 10;    // Records per page
    std::int64_t   total_pages = 0;   // Total page count

    auto has_next() const noexcept -> bool {
        return current_page < total_pages;
    }

    auto has_previous() const noexcept -> bool {
        return current_page > 1;
    }

    auto is_first() const noexcept -> bool {
        return current_page == 1;
    }

    auto is_last() const noexcept -> bool {
        return current_page == total_pages;
    }
};

// =============================================================================
// page_helper — Pagination helper for executing paginated queries
// =============================================================================

export class page_helper {
public:
    /// Execute paginated query with wrapper
    template <Model T>
    static auto select_page(client& cli,
                           std::int64_t page_num,
                           std::int64_t page_size,
                           const query_wrapper<T>& wrapper = {}) -> task<page<T>> {
        page<T> result;
        result.current_page = std::max<std::int64_t>(1, page_num);
        result.page_size = std::max<std::int64_t>(1, page_size);

        // Step 1: Get total count
        auto [count_sql, count_params] = wrapper.build_count_sql();
        auto final_count_sql = format_sql(cli.current_format_opts(), count_sql, count_params);
        if (!final_count_sql) co_return result;

        auto count_rs = co_await cli.execute(*final_count_sql);
        if (count_rs.is_err() || count_rs.rows.empty()) co_return result;

        auto& count_row = count_rs.rows[0];
        if (!count_row.empty()) {
            auto& count_field = count_row[0];
            if (!count_field.is_null()) {
                result.total = count_field.get_int64();
            }
        }

        // Calculate total pages
        result.total_pages = (result.total + result.page_size - 1) / result.page_size;

        // If no records or page out of range, return empty
        if (result.total == 0 || result.current_page > result.total_pages) {
            co_return result;
        }

        // Step 2: Get page records
        auto page_wrapper = wrapper;
        std::int64_t offset = (result.current_page - 1) * result.page_size;
        page_wrapper.limit(result.page_size).offset(offset);

        auto [select_sql, select_params] = page_wrapper.build_select_sql();
        auto final_select_sql = format_sql(cli.current_format_opts(), select_sql, select_params);
        if (!final_select_sql) co_return result;

        auto select_rs = co_await cli.execute(*final_select_sql);
        if (select_rs.is_err()) co_return result;

        result.records = from_result_set<T>(select_rs);
        co_return result;
    }

    /// Execute paginated query without wrapper (all records)
    template <Model T>
    static auto select_page(client& cli,
                           std::int64_t page_num,
                           std::int64_t page_size) -> task<page<T>> {
        co_return co_await select_page<T>(cli, page_num, page_size, query_wrapper<T>{});
    }
};

// =============================================================================
// Pagination extension for base_mapper
// =============================================================================

/// Add this to base_mapper via inheritance or composition
export template <Model T>
class pageable_mapper {
public:
    explicit pageable_mapper(client& cli) noexcept : cli_(cli) {}

    /// Select page with wrapper
    auto select_page(std::int64_t page_num,
                    std::int64_t page_size,
                    const query_wrapper<T>& wrapper = {}) -> task<page<T>> {
        co_return co_await page_helper::select_page<T>(cli_, page_num, page_size, wrapper);
    }

    /// Select page without wrapper
    auto select_page(std::int64_t page_num, std::int64_t page_size) -> task<page<T>> {
        co_return co_await page_helper::select_page<T>(cli_, page_num, page_size);
    }

private:
    client& cli_;
};

} // namespace cnetmod::mysql::orm
