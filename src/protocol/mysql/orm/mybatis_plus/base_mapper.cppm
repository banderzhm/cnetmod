export module cnetmod.protocol.mysql:orm_base_mapper;

import std;
import :types;
import :client;
import :orm_meta;
import :orm_crud;
import :orm_mapper;
import :orm_reflect;
import :orm_wrapper;
import :orm_xml_crud;
import :orm_page;
import :format_sql;
import cnetmod.coro.task;

namespace cnetmod::mysql::orm {

// =============================================================================
// base_mapper — Generic CRUD operations (MyBatis-Plus style)
// =============================================================================

/// BaseMapper provides common CRUD operations without writing XML
/// Similar to MyBatis-Plus BaseMapper<T>
export template <Model T>
class base_mapper {
public:
    explicit base_mapper(client& cli) noexcept : cli_(cli) {}

    // =========================================================================
    // INSERT operations
    // =========================================================================

    /// Insert a single record
    auto insert(const T& entity) -> task<exec_result> {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("INSERT INTO `{}` (", meta.table_name);

        // Column names
        std::vector<param_value> params;
        bool first = true;
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, col_flag::auto_increment)) continue;
            if (!first) sql += ", ";
            sql += std::format("`{}`", field.col.column_name);
            first = false;
        }
        sql += ") VALUES (";

        // Values
        first = true;
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, col_flag::auto_increment)) continue;
            if (!first) sql += ", ";
            sql += "{}";
            params.push_back(field.getter(entity));
            first = false;
        }
        sql += ")";

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.last_insert_id = rs.last_insert_id;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    /// Insert a single record (returns last_insert_id)
    auto insert_get_id(T& entity) -> task<std::expected<std::int64_t, std::string>> {
        auto result = co_await insert(entity);
        if (result.is_err()) {
            co_return std::unexpected(result.error_msg);
        }
        co_return static_cast<std::int64_t>(result.last_insert_id);
    }

    /// Batch insert (multiple records)
    auto insert_batch(const std::vector<T>& entities) -> task<exec_result> {
        if (entities.empty()) {
            exec_result r;
            r.affected_rows = 0;
            co_return r;
        }

        // Build batch INSERT SQL
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("INSERT INTO `{}` (", meta.table_name);

        // Column names
        bool first = true;
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, col_flag::auto_increment)) continue;
            if (!first) sql += ", ";
            sql += std::format("`{}`", field.col.column_name);
            first = false;
        }
        sql += ") VALUES ";

        // Values
        std::vector<param_value> params;
        for (std::size_t i = 0; i < entities.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += "(";
            first = true;
            for (auto& field : meta.fields) {
                if (has_flag(field.col.flags, col_flag::auto_increment)) continue;
                if (!first) sql += ", ";
                sql += "{}";
                params.push_back(field.getter(entities[i]));
                first = false;
            }
            sql += ")";
        }

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.last_insert_id = rs.last_insert_id;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    // =========================================================================
    // DELETE operations
    // =========================================================================

    /// Delete by primary key
    auto delete_by_id(const auto& id) -> task<exec_result> {
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field) {
            exec_result r;
            r.error_msg = "No primary key defined";
            co_return r;
        }

        std::string sql = std::format("DELETE FROM `{}` WHERE `{}` = {{}}",
            meta.table_name, pk_field->col.column_name);

        std::vector<param_value> params;
        params.push_back(detail::to_param_value(id));

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    /// Delete by IDs (batch)
    template <typename IdType>
    auto delete_batch_ids(const std::vector<IdType>& ids) -> task<exec_result> {
        if (ids.empty()) {
            exec_result r;
            r.affected_rows = 0;
            co_return r;
        }

        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field) {
            exec_result r;
            r.error_msg = "No primary key defined";
            co_return r;
        }

        std::string sql = std::format("DELETE FROM `{}` WHERE `{}` IN (",
            meta.table_name, pk_field->col.column_name);

        std::vector<param_value> params;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += "{}";
            params.push_back(detail::to_param_value(ids[i]));
        }
        sql += ")";

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    // =========================================================================
    // UPDATE operations
    // =========================================================================

    /// Update by primary key (all fields)
    auto update_by_id(const T& entity) -> task<exec_result> {
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field) {
            exec_result r;
            r.error_msg = "No primary key defined";
            co_return r;
        }

        std::string sql = std::format("UPDATE `{}` SET ", meta.table_name);
        std::vector<param_value> params;

        bool first = true;
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, col_flag::primary_key)) continue;
            if (!first) sql += ", ";
            sql += std::format("`{}` = {{}}", field.col.column_name);
            params.push_back(field.getter(entity));
            first = false;
        }

        sql += std::format(" WHERE `{}` = {{}}", pk_field->col.column_name);
        params.push_back(pk_field->getter(entity));

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    /// Update by primary key (selective - only non-null fields)
    auto update_selective(const T& entity) -> task<exec_result> {
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field) {
            exec_result r;
            r.error_msg = "No primary key defined";
            co_return r;
        }

        std::string sql = std::format("UPDATE `{}` SET ", meta.table_name);
        std::vector<param_value> params;

        bool first = true;
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, col_flag::primary_key)) continue;
            if (has_flag(field.col.flags, col_flag::auto_increment)) continue;

            // Check if field is null (for optional fields)
            auto value = field.getter(entity);
            if (value.kind == param_value::kind_t::null_kind) continue;

            if (!first) sql += ", ";
            sql += std::format("`{}` = {{}}", field.col.column_name);
            params.push_back(value);
            first = false;
        }

        if (first) {
            exec_result r;
            r.error_msg = "No fields to update";
            co_return r;
        }

        sql += std::format(" WHERE `{}` = {{}}", pk_field->col.column_name);
        params.push_back(pk_field->getter(entity));

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    // =========================================================================
    // SELECT operations
    // =========================================================================

    /// Select by primary key
    auto select_by_id(const auto& id) -> task<std::optional<T>> {
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field) co_return std::nullopt;

        std::string sql = std::format("SELECT * FROM `{}` WHERE `{}` = {{}}",
            meta.table_name, pk_field->col.column_name);

        std::vector<param_value> params;
        params.push_back(detail::to_param_value(id));

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) co_return std::nullopt;

        auto rs = co_await cli_.execute(*final_sql);
        if (rs.is_err() || rs.rows.empty()) co_return std::nullopt;

        auto results = from_result_set<T>(rs);
        if (results.empty()) co_return std::nullopt;
        co_return results[0];
    }

    /// Select by IDs (batch)
    template <typename IdType>
    auto select_batch_ids(const std::vector<IdType>& ids) -> task<std::vector<T>> {
        if (ids.empty()) co_return std::vector<T>{};

        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field) co_return std::vector<T>{};

        std::string sql = std::format("SELECT * FROM `{}` WHERE `{}` IN (",
            meta.table_name, pk_field->col.column_name);

        std::vector<param_value> params;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += "{}";
            params.push_back(detail::to_param_value(ids[i]));
        }
        sql += ")";

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) co_return std::vector<T>{};

        auto rs = co_await cli_.execute(*final_sql);
        if (rs.is_err()) co_return std::vector<T>{};

        co_return from_result_set<T>(rs);
    }

    /// Select all records
    auto select_list() -> task<std::vector<T>> {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("SELECT * FROM `{}`", meta.table_name);

        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return std::vector<T>{};

        co_return from_result_set<T>(rs);
    }

    /// Count all records
    auto select_count() -> task<std::int64_t> {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("SELECT COUNT(*) FROM `{}`", meta.table_name);

        auto rs = co_await cli_.execute(sql);
        if (rs.is_err() || rs.rows.empty()) co_return 0;

        auto& row = rs.rows[0];
        if (row.empty()) co_return 0;

        auto count_val = row[0].as_int64();
        co_return count_val.value_or(0);
    }

    /// Check if record exists by ID
    auto exists_by_id(const auto& id) -> task<bool> {
        auto result = co_await select_by_id(id);
        co_return result.has_value();
    }

    // =========================================================================
    // Wrapper-based operations
    // =========================================================================

    /// Select with query_wrapper
    auto select_list(const query_wrapper<T>& wrapper) -> task<std::vector<T>> {
        auto [sql, params] = wrapper.build_select_sql();
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) co_return std::vector<T>{};

        auto rs = co_await cli_.execute(*final_sql);
        if (rs.is_err()) co_return std::vector<T>{};

        co_return from_result_set<T>(rs);
    }

    /// Select one with query_wrapper
    auto select_one(const query_wrapper<T>& wrapper) -> task<std::optional<T>> {
        auto results = co_await select_list(wrapper);
        if (results.empty()) co_return std::nullopt;
        co_return results[0];
    }

    /// Count with query_wrapper
    auto select_count(const query_wrapper<T>& wrapper) -> task<std::int64_t> {
        auto [sql, params] = wrapper.build_count_sql();
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) co_return 0;

        auto rs = co_await cli_.execute(*final_sql);
        if (rs.is_err() || rs.rows.empty()) co_return 0;

        auto& row = rs.rows[0];
        if (row.empty()) co_return 0;

        auto& count_field = row[0];
        if (count_field.is_null()) co_return 0;
        co_return count_field.get_int64();
    }

    /// Delete with query_wrapper
    auto delete_by_wrapper(const query_wrapper<T>& wrapper) -> task<exec_result> {
        auto [sql, params] = wrapper.build_delete_sql();
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    /// Update with query_wrapper
    auto update_by_wrapper(const T& entity, const query_wrapper<T>& wrapper) -> task<exec_result> {
        auto [sql, params] = wrapper.build_update_sql(entity);
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    /// Update with update_wrapper
    auto update_by_wrapper(const update_wrapper<T>& wrapper) -> task<exec_result> {
        auto [sql, params] = wrapper.build_sql();
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        co_return r;
    }

    // =========================================================================
    // Pagination operations
    // =========================================================================

    /// Select page with wrapper
    auto select_page(std::int64_t page_num,
                    std::int64_t page_size,
                    const query_wrapper<T>& wrapper) -> task<page<T>> {
        co_return co_await page_helper::select_page<T>(cli_, page_num, page_size, wrapper);
    }

    /// Select page without wrapper
    auto select_page(std::int64_t page_num, std::int64_t page_size) -> task<page<T>> {
        co_return co_await page_helper::select_page<T>(cli_, page_num, page_size);
    }

    // =========================================================================
    // Access underlying client
    // =========================================================================

    auto client() noexcept -> mysql::client& { return cli_; }

private:
    mysql::client& cli_;
};

} // namespace cnetmod::mysql::orm
