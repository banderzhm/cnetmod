export module cnetmod.protocol.mysql:orm_base_mapper;

import std;
import :types;
import :connection_client;
import :orm_meta;
import :orm_crud;
import :orm_mapper;
import :orm_mysql_result_adapter;
import cnetmod.orm.database_session;
import :orm_reflect;
import :orm_wrapper;
import :orm_xml_crud;
import :orm_page;
import :format_sql;
import cnetmod.coro.task;
import cnetmod.orm.sql_parameters;

namespace cnetmod::orm::mysql_detail {
using namespace cnetmod::mysql;
using namespace cnetmod::orm;
using param_value = cnetmod::orm::param_value;

// =============================================================================
// base_mapper — Generic CRUD operations (MyBatis-Plus style)
// =============================================================================

/// BaseMapper provides common CRUD operations without writing XML
/// Similar to MyBatis-Plus BaseMapper<T>
template <Model T> class base_mapper
{
public:
    explicit base_mapper(client& cli) noexcept
        : cli_(cli) {}

    /**
     * @brief Executes a strict, diagnostics-preserving single-row lookup.
     *
     * The legacy optional-returning methods remain source compatible, while
     * new code can opt into the database_session error contract explicitly.
     */
    auto select_one_result(const query_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template find_one<T>(wrapper);
    }

    /**
     * @brief Executes a primary-key lookup without collapsing errors to empty.
     */
    auto select_by_id_result(const auto& id) -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template find_by_id<T>(
            cnetmod::orm::to_query_parameter(id));
    }

    /**
     * @brief Error-transparent counterpart of the legacy insert operation.
     */
    auto insert_result(T& entity) -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.insert(entity);
    }

    /**
     * @brief Error-transparent counterpart of batch insert.
     */
    auto insert_batch_result(std::span<T> entities, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template insert_batch<T>(entities, batch_size);
    }

    /**
     * @brief Error-transparent counterpart of the legacy primary-key update.
     */
    auto update_by_id_result(const T& entity) -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.update(entity);
    }

    /**
     * @brief Error-transparent counterpart of the legacy primary-key delete.
     */
    auto delete_by_id_result(const auto& id) -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template remove_by_id<T>(
            cnetmod::orm::to_query_parameter(id));
    }

    /**
     * @brief Error-transparent counterpart of a primary-key batch delete.
     */
    template <typename IdType>
    auto delete_batch_ids_result(std::span<const IdType> ids)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template remove_by_ids<T>(ids);
    }

    /**
     * @brief Error-transparent wrapper query returning mapped rows.
     */
    auto select_list_result(const query_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.find(wrapper);
    }

    /**
     * @brief Error-transparent counterpart of a primary-key batch lookup.
     */
    template <typename IdType>
    auto select_batch_ids_result(std::span<const IdType> ids)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template find_by_ids<T>(ids);
    }

    /**
     * @brief Error-transparent wrapper DELETE.
     */
    auto delete_by_wrapper_result(const query_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.remove(wrapper);
    }

    /**
     * @brief Error-transparent wrapper UPDATE.
     */
    auto update_by_wrapper_result(const update_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.update(wrapper);
    }

    /**
     * @brief Error-transparent wrapper count preserving database diagnostics.
     *
     * The count is returned as the first value in `data` and all native
     * diagnostics remain in the same model_result object.
     */
    auto select_count_result(const query_wrapper<T>& wrapper)
        -> task<model_result<std::int64_t>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.count_result(wrapper);
    }

    /**
     * @brief Error-transparent existence check by primary key.
     */
    auto exists_by_id_result(const auto& id) -> task<model_result<bool>>
    {
        mysql_database_session session{cli_};
        const auto* primary_key = model_traits<T>::meta().pk();
        if (!primary_key)
        {
            model_result<bool> result;
            result.error_msg = "model has no primary key";
            result.framework_error = std::make_error_code(
                std::errc::invalid_argument);
            co_return result;
        }
        query_wrapper<T> query;
        query.eq(primary_key->col.column_name,
            cnetmod::orm::to_query_parameter(id));
        co_return co_await session.exists(query);
    }

    /**
     * @brief Error-transparent typed page query.
     */
    auto select_page_result(std::int64_t page_num, std::int64_t page_size,
        const query_wrapper<T>& wrapper = {}) -> task<page_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.page<T>(
            static_cast<std::size_t>(std::max<std::int64_t>(1, page_num)),
            static_cast<std::size_t>(std::max<std::int64_t>(1, page_size)),
            wrapper);
    }

    /**
     * @brief Error-transparent native upsert.
     */
    auto upsert_result(T& entity) -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.upsert(entity);
    }

    /**
     * @brief Updates a bounded batch by primary key with failure location.
     */
    auto update_batch_by_id(std::span<const T> entities,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template update_batch_by_id<T>(
            entities, batch_size);
    }

    /**
     * @brief Saves or updates one entity without collapsing diagnostics.
     */
    auto save_or_update(T& entity) -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template save_or_update<T>(entity);
    }

    /**
     * @brief Saves or updates a transactional batch.
     */
    auto save_or_update_batch(std::span<T> entities,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template save_or_update_batch<T>(
            entities, batch_size);
    }

    /**
     * @brief Executes native MySQL upsert for a bounded transactional batch.
     */
    auto upsert_batch(std::span<T> entities, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template upsert_batch<T>(entities, batch_size);
    }

    /**
     * @brief Selects rows matching mapped column equalities.
     */
    auto select_by_map(
        std::span<const std::pair<std::string, param_value>> values)
        -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template find_by_map<T>(values);
    }

    /**
     * @brief Selects dynamic projections while preserving native diagnostics.
     */
    auto select_maps(const query_wrapper<T>& wrapper)
        -> task<model_result<cnetmod::orm::projection_row>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template select_maps<T>(wrapper);
    }

    /**
     * @brief Selects the first projected column as typed field values.
     */
    auto select_objects(const query_wrapper<T>& wrapper)
        -> task<model_result<cnetmod::orm::field_value>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template select_objects<T>(wrapper);
    }

    /**
     * @brief Selects a diagnostics-preserving page of dynamic projections.
     */
    auto select_maps_page(std::size_t page_number, std::size_t page_size,
        const query_wrapper<T>& wrapper = {})
        -> task<page_result<cnetmod::orm::projection_row>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.template page_maps<T>(
            page_number, page_size, wrapper);
    }

    /**
     * @brief Inserts a model while preserving database diagnostics.
     */
    auto insert(T& entity) -> task<model_result<T>>
    {
        co_return co_await insert_result(entity);
    }

    auto insert(const T& entity) -> task<model_result<T>>
    {
        T copy = entity;
        co_return co_await insert_result(copy);
    }

    auto insert_batch(std::span<T> entities, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        co_return co_await insert_batch_result(entities, batch_size);
    }

    auto insert_batch(const std::vector<T>& entities, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        std::vector<T> copy = entities;
        co_return co_await insert_batch_result(std::span<T>{copy}, batch_size);
    }

    auto update_by_id(const T& entity) -> task<model_result<T>>
    {
        co_return co_await update_by_id_result(entity);
    }

    auto delete_by_id(const auto& id) -> task<model_result<T>>
    {
        co_return co_await delete_by_id_result(id);
    }

    template <typename IdType>
    auto delete_batch_ids(std::span<const IdType> ids)
        -> task<model_result<T>>
    {
        co_return co_await delete_batch_ids_result(ids);
    }

    template <typename IdType>
    auto delete_batch_ids(const std::vector<IdType>& ids)
        -> task<model_result<T>>
    {
        co_return co_await delete_batch_ids_result(
            std::span<const IdType>{ids});
    }

    template <typename IdType>
    auto select_batch_ids(std::span<const IdType> ids)
        -> task<model_result<T>>
    {
        co_return co_await select_batch_ids_result(ids);
    }

    template <typename IdType>
    auto select_batch_ids(const std::vector<IdType>& ids)
        -> task<model_result<T>>
    {
        co_return co_await select_batch_ids_result(
            std::span<const IdType>{ids});
    }

    auto select_by_id(const auto& id) -> task<model_result<T>>
    {
        co_return co_await select_by_id_result(id);
    }

    auto select_list() -> task<model_result<T>>
    {
        mysql_database_session session{cli_};
        co_return co_await session.find_all<T>();
    }

    auto select_list(const query_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        co_return co_await select_list_result(wrapper);
    }

    auto select_one(const query_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        co_return co_await select_one_result(wrapper);
    }

    auto select_count() -> task<model_result<std::int64_t>>
    {
        query_wrapper<T> wrapper;
        co_return co_await select_count_result(wrapper);
    }

    auto select_count(const query_wrapper<T>& wrapper)
        -> task<model_result<std::int64_t>>
    {
        co_return co_await select_count_result(wrapper);
    }

    auto exists_by_id(const auto& id) -> task<model_result<bool>>
    {
        co_return co_await exists_by_id_result(id);
    }

    auto delete_by_wrapper(const query_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        co_return co_await delete_by_wrapper_result(wrapper);
    }

    auto update_by_wrapper(const update_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        co_return co_await update_by_wrapper_result(wrapper);
    }

    auto select_page(std::int64_t page_num, std::int64_t page_size,
        const query_wrapper<T>& wrapper = {}) -> task<page_result<T>>
    {
        co_return co_await select_page_result(page_num, page_size, wrapper);
    }

    // =========================================================================
    // INSERT operations
    // =========================================================================

    /// Insert a single record
    auto legacy_insert(const T& entity) -> task<exec_result>
    {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("INSERT INTO `{}` (", meta.table_name);

        // Column names
        std::vector<param_value> params;
        bool first = true;
        for (auto& field : meta.fields)
        {
            if (has_flag(field.col.flags, col_flag::auto_increment))
                continue;
            if (!first)
                sql += ", ";
            sql += std::format("`{}`", field.col.column_name);
            first = false;
        }
        sql += ") VALUES (";

        // Values
        first = true;
        for (auto& field : meta.fields)
        {
            if (has_flag(field.col.flags, col_flag::auto_increment))
                continue;
            if (!first)
                sql += ", ";
            sql += "{}";
            params.push_back(field.getter(entity));
            first = false;
        }
        sql += ")";

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.last_insert_id = rs.last_insert_id;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    /// Insert a single record (returns last_insert_id)
    auto legacy_insert_get_id(T& entity)
        -> task<std::expected<std::int64_t, std::string>>
    {
        auto result = co_await legacy_insert(entity);
        if (result.is_err())
        {
            co_return std::unexpected(result.error_msg);
        }
        co_return static_cast<std::int64_t>(result.last_insert_id);
    }

    /// Batch insert (multiple records)
    auto legacy_insert_batch(const std::vector<T>& entities) -> task<exec_result>
    {
        if (entities.empty())
        {
            exec_result r;
            r.affected_rows = 0;
            co_return r;
        }

        // Build batch INSERT SQL
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("INSERT INTO `{}` (", meta.table_name);

        // Column names
        bool first = true;
        for (auto& field : meta.fields)
        {
            if (has_flag(field.col.flags, col_flag::auto_increment))
                continue;
            if (!first)
                sql += ", ";
            sql += std::format("`{}`", field.col.column_name);
            first = false;
        }
        sql += ") VALUES ";

        // Values
        std::vector<param_value> params;
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            if (i > 0)
                sql += ", ";
            sql += "(";
            first = true;
            for (auto& field : meta.fields)
            {
                if (has_flag(field.col.flags, col_flag::auto_increment))
                    continue;
                if (!first)
                    sql += ", ";
                sql += "{}";
                params.push_back(field.getter(entities[i]));
                first = false;
            }
            sql += ")";
        }

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.last_insert_id = rs.last_insert_id;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    // =========================================================================
    // DELETE operations
    // =========================================================================

    /// Delete by primary key
    auto legacy_delete_by_id(const auto& id) -> task<exec_result>
    {
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field)
        {
            exec_result r;
            r.error_msg = "No primary key defined";
            co_return r;
        }

        std::string sql = std::format("DELETE FROM `{}` WHERE `{}` = {{}}",
            meta.table_name, pk_field->col.column_name);

        std::vector<param_value> params;
        params.push_back(cnetmod::orm::to_query_parameter(id));

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    /// Delete by IDs (batch)
    template <typename IdType>
    auto legacy_delete_batch_ids(const std::vector<IdType>& ids) -> task<exec_result>
    {
        if (ids.empty())
        {
            exec_result r;
            r.affected_rows = 0;
            co_return r;
        }

        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field)
        {
            exec_result r;
            r.error_msg = "No primary key defined";
            co_return r;
        }

        std::string sql = std::format("DELETE FROM `{}` WHERE `{}` IN (",
            meta.table_name, pk_field->col.column_name);

        std::vector<param_value> params;
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            if (i > 0)
                sql += ", ";
            sql += "{}";
            params.push_back(cnetmod::orm::to_query_parameter(ids[i]));
        }
        sql += ")";

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    // =========================================================================
    // UPDATE operations
    // =========================================================================

    /// Update by primary key (all fields)
    auto legacy_update_by_id(const T& entity) -> task<exec_result>
    {
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field)
        {
            exec_result r;
            r.error_msg = "No primary key defined";
            co_return r;
        }

        std::string sql = std::format("UPDATE `{}` SET ", meta.table_name);
        std::vector<param_value> params;

        bool first = true;
        for (auto& field : meta.fields)
        {
            if (has_flag(field.col.flags, col_flag::primary_key))
                continue;
            if (!first)
                sql += ", ";
            sql += std::format("`{}` = {{}}", field.col.column_name);
            params.push_back(field.getter(entity));
            first = false;
        }

        sql += std::format(" WHERE `{}` = {{}}", pk_field->col.column_name);
        params.push_back(pk_field->getter(entity));

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    /// Update by primary key (selective - only non-null fields)
    auto legacy_update_selective(const T& entity) -> task<exec_result>
    {
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field)
        {
            exec_result r;
            r.error_msg = "No primary key defined";
            co_return r;
        }

        std::string sql = std::format("UPDATE `{}` SET ", meta.table_name);
        std::vector<param_value> params;

        bool first = true;
        for (auto& field : meta.fields)
        {
            if (has_flag(field.col.flags, col_flag::primary_key))
                continue;
            if (has_flag(field.col.flags, col_flag::auto_increment))
                continue;

            // Check if field is null (for optional fields)
            auto value = field.getter(entity);
            if (value.kind == param_value::kind_t::null_kind)
                continue;

            if (!first)
                sql += ", ";
            sql += std::format("`{}` = {{}}", field.col.column_name);
            params.push_back(value);
            first = false;
        }

        if (first)
        {
            exec_result r;
            r.error_msg = "No fields to update";
            co_return r;
        }

        sql += std::format(" WHERE `{}` = {{}}", pk_field->col.column_name);
        params.push_back(pk_field->getter(entity));

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    // =========================================================================
    // SELECT operations
    // =========================================================================

    /// Select by primary key
    auto legacy_select_by_id(const auto& id) -> task<std::optional<T>>
    {
        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field)
            co_return std::nullopt;

        std::string sql = std::format("SELECT * FROM `{}` WHERE `{}` = {{}}",
            meta.table_name, pk_field->col.column_name);

        std::vector<param_value> params;
        params.push_back(cnetmod::orm::to_query_parameter(id));

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
            co_return std::nullopt;

        auto rs = co_await cli_.execute(*final_sql);
        if (rs.is_err() || rs.rows.empty())
            co_return std::nullopt;

        auto results = mysql_map_result<T>(rs);
        if (results.empty())
            co_return std::nullopt;
        co_return results[0];
    }

    /// Select by IDs (batch)
    template <typename IdType>
    auto legacy_select_batch_ids(const std::vector<IdType>& ids)
        -> task<std::vector<T>>
    {
        if (ids.empty())
            co_return std::vector<T>{};

        auto& meta = model_traits<T>::meta();
        auto* pk_field = meta.pk();
        if (!pk_field)
            co_return std::vector<T>{};

        std::string sql = std::format("SELECT * FROM `{}` WHERE `{}` IN (",
            meta.table_name, pk_field->col.column_name);

        std::vector<param_value> params;
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            if (i > 0)
                sql += ", ";
            sql += "{}";
            params.push_back(cnetmod::orm::to_query_parameter(ids[i]));
        }
        sql += ")";

        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
            co_return std::vector<T>{};

        auto rs = co_await cli_.execute(*final_sql);
        if (rs.is_err())
            co_return std::vector<T>{};

        co_return mysql_map_result<T>(rs);
    }

    /// Select all records
    auto legacy_select_list() -> task<std::vector<T>>
    {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("SELECT * FROM `{}`", meta.table_name);

        auto rs = co_await cli_.execute(sql);
        if (rs.is_err())
            co_return std::vector<T>{};

        co_return mysql_map_result<T>(rs);
    }

    /// Count all records
    auto legacy_select_count() -> task<std::int64_t>
    {
        auto& meta = model_traits<T>::meta();
        std::string sql = std::format("SELECT COUNT(*) FROM `{}`", meta.table_name);

        auto rs = co_await cli_.execute(sql);
        if (rs.is_err() || rs.rows.empty())
            co_return 0;

        auto& row = rs.rows[0];
        if (row.empty())
            co_return 0;

        auto count_val = row[0].as_int64();
        co_return count_val.value_or(0);
    }

    /// Check if record exists by ID
    auto legacy_exists_by_id(const auto& id) -> task<bool>
    {
        auto result = co_await legacy_select_by_id(id);
        co_return result.has_value();
    }

    // =========================================================================
    // Wrapper-based operations
    // =========================================================================

    /// Select with query_wrapper
    auto legacy_select_list(const query_wrapper<T>& wrapper) -> task<std::vector<T>>
    {
        auto [sql, params] = wrapper.build_select_sql();
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
            co_return std::vector<T>{};

        auto rs = co_await cli_.execute(*final_sql);
        if (rs.is_err())
            co_return std::vector<T>{};

        co_return mysql_map_result<T>(rs);
    }

    /// Select one with query_wrapper
    auto legacy_select_one(const query_wrapper<T>& wrapper) -> task<std::optional<T>>
    {
        auto results = co_await legacy_select_list(wrapper);
        if (results.empty())
            co_return std::nullopt;
        co_return results[0];
    }

    /// Count with query_wrapper
    auto legacy_select_count(const query_wrapper<T>& wrapper) -> task<std::int64_t>
    {
        auto [sql, params] = wrapper.build_count_sql();
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
            co_return 0;

        auto rs = co_await cli_.execute(*final_sql);
        if (rs.is_err() || rs.rows.empty())
            co_return 0;

        auto& row = rs.rows[0];
        if (row.empty())
            co_return 0;

        auto& count_field = row[0];
        if (count_field.is_null())
            co_return 0;
        co_return count_field.get_int64();
    }

    /// Delete with query_wrapper
    auto legacy_delete_by_wrapper(const query_wrapper<T>& wrapper) -> task<exec_result>
    {
        auto [sql, params] = wrapper.build_delete_sql();
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    /// Update with query_wrapper
    auto legacy_update_by_wrapper(const T& entity, const query_wrapper<T>& wrapper)
        -> task<exec_result>
    {
        auto [sql, params] = wrapper.build_update_sql(entity);
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    /// Update with update_wrapper
    auto legacy_update_by_wrapper(const update_wrapper<T>& wrapper)
        -> task<exec_result>
    {
        auto [sql, params] = wrapper.build_sql();
        auto final_sql = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql)
        {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        auto rs = co_await cli_.execute(*final_sql);
        exec_result r;
        r.affected_rows = rs.affected_rows;
        r.error_msg = rs.error_msg;
        r.sql_state = rs.sql_state;
        r.error_code = rs.error_code;
        co_return r;
    }

    // =========================================================================
    // Pagination operations
    // =========================================================================

    /// Select page with wrapper
    auto legacy_select_page(std::int64_t page_num, std::int64_t page_size,
        const query_wrapper<T>& wrapper) -> task<page<T>>
    {
        co_return co_await page_helper::select_page<T>(cli_, page_num, page_size,
            wrapper);
    }

    /// Select page without wrapper
    auto legacy_select_page(std::int64_t page_num, std::int64_t page_size)
        -> task<page<T>>
    {
        co_return co_await page_helper::select_page<T>(cli_, page_num, page_size);
    }

    // =========================================================================
    // Access underlying client
    // =========================================================================

    auto client() noexcept -> mysql::client&
    {
        return cli_;
    }

private:
    mysql::client& cli_;
};

} // namespace cnetmod::orm::mysql_detail

export namespace cnetmod::orm {
template <Model T>
using mysql_base_mapper = mysql_detail::base_mapper<T>;
}
