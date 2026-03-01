export module cnetmod.protocol.mysql:orm_migrate;

import std;
import :types;
import :format_sql;
import :client;
import :orm_id_gen;
import :orm_meta;
import :orm_query;
import cnetmod.coro.task;

namespace cnetmod::mysql::orm {

// =============================================================================
// schema_diff — Table structure difference description
// =============================================================================

export struct column_change {
    enum class action_t : std::uint8_t { add, drop, modify };

    action_t    action;
    std::string column_name;
    std::string ddl;           ///< Generated ALTER TABLE clause
};

export struct schema_diff {
    std::vector<column_change> changes;
    bool table_missing = false;  ///< Table doesn't exist, needs CREATE TABLE
};

// =============================================================================
// detect_diff — Compare model metadata from DESCRIBE results
// =============================================================================
//
// DESCRIBE result set columns: Field, Type, Null, Key, Default, Extra
//

namespace detail {

/// Extract column name from DESCRIBE result row
inline auto get_describe_field(const row& r) -> std::string {
    if (!r.empty() && r[0].is_string())
        return std::string(r[0].get_string());
    return {};
}

/// Extract type string from DESCRIBE result row (uppercase normalized)
inline auto get_describe_type(const row& r) -> std::string {
    if (r.size() > 1 && r[1].is_string()) {
        std::string t(r[1].get_string());
        for (auto& c : t)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return t;
    }
    return {};
}

/// Convert ORM column_type + strategy to expected MySQL type prefix (for loose comparison)
inline auto expected_type_prefix(const column_def& col) -> std::string {
    if (col.is_uuid())
        return "CHAR(36)";

    auto sv = sql_type_str(col.type);
    std::string s(sv);
    // sql_type_str returns like "VARCHAR(255)" / "INT" etc
    return s;
}

/// Loose type matching: check if actual type starts with expected prefix (ignore display width)
inline auto type_matches(std::string_view actual, std::string_view expected) -> bool {
    // Normalize: remove display width from actual like "bigint(20)" → "BIGINT"
    // First take the part before '(' for prefix comparison
    auto actual_base = actual.substr(0, actual.find('('));
    auto expected_base = expected.substr(0, expected.find('('));

    // Special handling: CHAR(36) needs exact length match
    if (expected == "CHAR(36)") {
        return actual.find("CHAR(36)") != std::string_view::npos
            || actual.find("char(36)") != std::string_view::npos;
    }

    // Base type names must match
    if (actual_base.size() != expected_base.size())
        return false;
    for (std::size_t i = 0; i < actual_base.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(actual_base[i]))
            != std::toupper(static_cast<unsigned char>(expected_base[i])))
            return false;
    }
    return true;
}

/// Generate ADD COLUMN DDL fragment
inline auto build_add_column_ddl(std::string_view table,
                                  const column_def& col) -> std::string
{
    std::string sql = "ALTER TABLE `";
    sql.append(table);
    sql.append("` ADD COLUMN `");
    sql.append(col.column_name);
    sql.append("` ");

    if (col.is_uuid())
        sql.append("CHAR(36)");
    else
        sql.append(sql_type_str(col.type));

    if (col.is_pk()) {
        sql.append(" NOT NULL");
    } else if (!col.is_nullable()) {
        sql.append(" NOT NULL");
    } else {
        sql.append(" DEFAULT NULL");
    }

    if (col.is_auto() && !col.is_uuid() && !col.is_snowflake())
        sql.append(" AUTO_INCREMENT");

    return sql;
}

/// Generate DROP COLUMN DDL
inline auto build_drop_column_ddl(std::string_view table,
                                   std::string_view col_name) -> std::string
{
    std::string sql = "ALTER TABLE `";
    sql.append(table);
    sql.append("` DROP COLUMN `");
    sql.append(col_name);
    sql.push_back('`');
    return sql;
}

/// Generate MODIFY COLUMN DDL
inline auto build_modify_column_ddl(std::string_view table,
                                     const column_def& col) -> std::string
{
    std::string sql = "ALTER TABLE `";
    sql.append(table);
    sql.append("` MODIFY COLUMN `");
    sql.append(col.column_name);
    sql.append("` ");

    if (col.is_uuid())
        sql.append("CHAR(36)");
    else
        sql.append(sql_type_str(col.type));

    if (col.is_pk()) {
        sql.append(" NOT NULL");
    } else if (!col.is_nullable()) {
        sql.append(" NOT NULL");
    } else {
        sql.append(" DEFAULT NULL");
    }

    if (col.is_auto() && !col.is_uuid() && !col.is_snowflake())
        sql.append(" AUTO_INCREMENT");

    return sql;
}

} // namespace detail

/// Detect differences between model and database table
export template <Model T>
auto detect_diff(const result_set& describe_rs) -> schema_diff {
    schema_diff diff;
    auto& meta = model_traits<T>::meta();

    // Collect actual columns in database
    std::vector<std::pair<std::string, std::string>> db_cols; // {name, type}
    for (auto& r : describe_rs.rows) {
        auto name = detail::get_describe_field(r);
        auto type = detail::get_describe_type(r);
        if (!name.empty())
            db_cols.emplace_back(std::move(name), std::move(type));
    }

    // 1) Model has, database doesn't → ADD COLUMN
    // 2) Type mismatch → MODIFY COLUMN
    for (auto& f : meta.fields) {
        bool found = false;
        for (auto& [db_name, db_type] : db_cols) {
            if (db_name == f.col.column_name) {
                found = true;
                auto expected = detail::expected_type_prefix(f.col);
                if (!detail::type_matches(db_type, expected)) {
                    column_change chg;
                    chg.action      = column_change::action_t::modify;
                    chg.column_name = std::string(f.col.column_name);
                    chg.ddl = detail::build_modify_column_ddl(meta.table_name, f.col);
                    diff.changes.push_back(std::move(chg));
                }
                break;
            }
        }
        if (!found) {
            column_change chg;
            chg.action      = column_change::action_t::add;
            chg.column_name = std::string(f.col.column_name);
            chg.ddl = detail::build_add_column_ddl(meta.table_name, f.col);
            diff.changes.push_back(std::move(chg));
        }
    }

    // 3) Database has, model doesn't → DROP COLUMN
    for (auto& [db_name, db_type] : db_cols) {
        bool found = false;
        for (auto& f : meta.fields) {
            if (f.col.column_name == db_name) {
                found = true;
                break;
            }
        }
        if (!found) {
            column_change chg;
            chg.action      = column_change::action_t::drop;
            chg.column_name = db_name;
            chg.ddl = detail::build_drop_column_ddl(meta.table_name, db_name);
            diff.changes.push_back(std::move(chg));
        }
    }

    return diff;
}

// =============================================================================
// sync_result — Sync result
// =============================================================================

export struct sync_result {
    schema_diff  diff;
    std::string  error_msg;
    bool         created = false;   ///< Table was newly created

    auto ok()     const noexcept -> bool { return error_msg.empty(); }
    auto is_err() const noexcept -> bool { return !ok(); }
};

// =============================================================================
// sync_schema — Async sync table structure
// =============================================================================

export template <Model T>
auto sync_schema(client& cli) -> task<sync_result> {
    sync_result result;
    auto& meta = model_traits<T>::meta();

    // 1) Try DESCRIBE
    std::string desc_sql = "DESCRIBE `";
    desc_sql.append(meta.table_name);
    desc_sql.push_back('`');

    auto rs = co_await cli.query(desc_sql);

    if (rs.is_err()) {
        // Table doesn't exist — error code 1146 (ER_NO_SUCH_TABLE)
        if (rs.error_code == 1146 || rs.error_msg.find("doesn't exist") != std::string::npos) {
            // Directly CREATE TABLE
            auto create_sql = build_create_table_sql<T>();
            auto cr = co_await cli.execute(create_sql);
            if (cr.is_err()) {
                result.error_msg = cr.error_msg;
                co_return result;
            }
            result.created = true;
            result.diff.table_missing = true;
            co_return result;
        }
        result.error_msg = rs.error_msg;
        co_return result;
    }

    // 2) Detect differences
    result.diff = detect_diff<T>(rs);

    if (result.diff.changes.empty())
        co_return result; // No changes

    // 3) Apply differences
    for (auto& chg : result.diff.changes) {
        auto ar = co_await cli.execute(chg.ddl);
        if (ar.is_err()) {
            result.error_msg = "ALTER failed on `" + chg.column_name + "`: " + ar.error_msg;
            co_return result;
        }
    }

    co_return result;
}

} // namespace cnetmod::mysql::orm
