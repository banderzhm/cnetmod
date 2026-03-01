export module cnetmod.protocol.mysql:orm_query;

import std;
import :types;
import :format_sql;
import :orm_id_gen;
import :orm_meta;
import :orm_mapper;

namespace cnetmod::mysql::orm {

// =============================================================================
// select_builder<T> — SELECT query builder
// =============================================================================

export template <Model T>
class select_builder {
public:
    select_builder() = default;

    /// Custom SELECT columns (default generates all columns from meta)
    auto columns(std::string_view cols) -> select_builder& {
        custom_cols_ = cols;
        return *this;
    }

    /// WHERE clause (raw SQL)
    auto where(std::string_view clause) -> select_builder& {
        where_ = clause;
        return *this;
    }

    /// WHERE clause + format_sql parameters
    auto where(std::string_view fmt,
               std::initializer_list<param_value> args) -> select_builder& {
        where_fmt_ = fmt;
        where_args_.assign(args);
        return *this;
    }

    /// WHERE clause + vector parameters
    auto where(std::string_view fmt,
               std::vector<param_value> args) -> select_builder& {
        where_fmt_ = fmt;
        where_args_ = std::move(args);
        return *this;
    }

    auto order_by(std::string_view clause) -> select_builder& {
        order_by_ = clause;
        return *this;
    }

    auto limit(std::size_t n) -> select_builder& {
        limit_ = n;
        return *this;
    }

    auto offset(std::size_t n) -> select_builder& {
        offset_ = n;
        return *this;
    }

    /// Generate final SQL
    [[nodiscard]] auto build_sql(
        const format_options& opts = {}
    ) const -> std::string
    {
        auto& meta = model_traits<T>::meta();
        std::string sql = "SELECT ";

        // Column list
        if (!custom_cols_.empty()) {
            sql.append(custom_cols_);
        } else {
            bool first = true;
            for (auto& f : meta.fields) {
                if (!first) sql.append(", ");
                first = false;
                sql.push_back('`');
                sql.append(f.col.column_name);
                sql.push_back('`');
            }
        }

        sql.append(" FROM `");
        sql.append(meta.table_name);
        sql.push_back('`');

        // WHERE
        if (!where_fmt_.empty()) {
            sql.append(" WHERE ");
            auto r = format_sql(opts, where_fmt_, where_args_);
            if (r) sql.append(*r);
            else sql.append(where_fmt_); // fallback
        } else if (!where_.empty()) {
            sql.append(" WHERE ");
            sql.append(where_);
        }

        // ORDER BY
        if (!order_by_.empty()) {
            sql.append(" ORDER BY ");
            sql.append(order_by_);
        }

        // LIMIT / OFFSET
        if (limit_ > 0) {
            sql.append(" LIMIT ");
            sql.append(std::to_string(limit_));
        }
        if (offset_ > 0) {
            sql.append(" OFFSET ");
            sql.append(std::to_string(offset_));
        }

        return sql;
    }

private:
    std::string              custom_cols_;
    std::string              where_;
    std::string              where_fmt_;
    std::vector<param_value> where_args_;
    std::string              order_by_;
    std::size_t              limit_  = 0;
    std::size_t              offset_ = 0;
};

/// Factory function
export template <Model T>
auto select() -> select_builder<T> { return {}; }

// =============================================================================
// insert_builder<T> — INSERT builder
// =============================================================================

export template <Model T>
class insert_builder {
public:
    /// Set the model to insert
    auto values(const T& model) -> insert_builder& {
        models_.clear();
        models_.push_back(model);
        return *this;
    }

    /// Batch insert
    auto values(std::span<const T> models) -> insert_builder& {
        models_.assign(models.begin(), models.end());
        return *this;
    }

    /// Generate SQL + params
    struct built_sql {
        std::string              sql;
        std::vector<param_value> params;
    };

    [[nodiscard]] auto build(const format_options& opts = {}) const -> built_sql {
        auto& meta = model_traits<T>::meta();
        auto ins_fields = meta.insertable_fields();

        std::string sql = "INSERT INTO `";
        sql.append(meta.table_name);
        sql.append("` (");

        // Column names
        for (std::size_t i = 0; i < ins_fields.size(); ++i) {
            if (i > 0) sql.append(", ");
            sql.push_back('`');
            sql.append(ins_fields[i]->col.column_name);
            sql.push_back('`');
        }
        sql.append(") VALUES ");

        std::vector<param_value> params;

        // One group (?, ?, ...) per row
        for (std::size_t m = 0; m < models_.size(); ++m) {
            if (m > 0) sql.append(", ");
            sql.push_back('(');
            for (std::size_t i = 0; i < ins_fields.size(); ++i) {
                if (i > 0) sql.append(", ");
                sql.append("{}");
                params.push_back(ins_fields[i]->getter(models_[m]));
            }
            sql.push_back(')');
        }

        // format_sql expands placeholders
        auto final_sql = format_sql(opts, sql, params);
        return {final_sql ? std::move(*final_sql) : sql, std::move(params)};
    }

private:
    std::vector<T> models_;
};

/// Factory function
export template <Model T>
auto insert_of() -> insert_builder<T> { return {}; }

// =============================================================================
// update_builder<T> — UPDATE builder
// =============================================================================

export template <Model T>
class update_builder {
public:
    /// Update all non-PK fields by model PK
    auto set(const T& model) -> update_builder& {
        model_ = model;
        has_model_ = true;
        return *this;
    }

    auto where(std::string_view clause) -> update_builder& {
        where_ = clause;
        return *this;
    }

    auto where(std::string_view fmt,
               std::initializer_list<param_value> args) -> update_builder& {
        where_fmt_ = fmt;
        where_args_.assign(args);
        return *this;
    }

    struct built_sql {
        std::string              sql;
        std::vector<param_value> params;
    };

    [[nodiscard]] auto build(const format_options& opts = {}) const -> built_sql {
        auto& meta = model_traits<T>::meta();

        std::string sql = "UPDATE `";
        sql.append(meta.table_name);
        sql.append("` SET ");

        std::vector<param_value> params;

        // SET col = {}
        bool first = true;
        for (auto& f : meta.fields) {
            if (f.col.is_pk()) continue;
            if (!first) sql.append(", ");
            first = false;
            sql.push_back('`');
            sql.append(f.col.column_name);
            sql.append("` = {}");
            if (has_model_) params.push_back(f.getter(model_));
        }

        // WHERE
        if (!where_fmt_.empty()) {
            sql.append(" WHERE ");
            sql.append(where_fmt_);
            params.insert(params.end(), where_args_.begin(), where_args_.end());
        } else if (!where_.empty()) {
            sql.append(" WHERE ");
            sql.append(where_);
        } else if (has_model_) {
            // Default by PK
            auto* pk = meta.pk();
            if (pk) {
                sql.append(" WHERE `");
                sql.append(pk->col.column_name);
                sql.append("` = {}");
                params.push_back(pk->getter(model_));
            }
        }

        auto final_sql = format_sql(opts, sql, params);
        return {final_sql ? std::move(*final_sql) : sql, std::move(params)};
    }

private:
    T                        model_{};
    bool                     has_model_ = false;
    std::string              where_;
    std::string              where_fmt_;
    std::vector<param_value> where_args_;
};

/// Factory function
export template <Model T>
auto update_of() -> update_builder<T> { return {}; }

// =============================================================================
// delete_builder<T> — DELETE builder
// =============================================================================

export template <Model T>
class delete_builder {
public:
    auto where(std::string_view clause) -> delete_builder& {
        where_ = clause;
        return *this;
    }

    auto where(std::string_view fmt,
               std::initializer_list<param_value> args) -> delete_builder& {
        where_fmt_ = fmt;
        where_args_.assign(args);
        return *this;
    }

    auto where(std::string_view fmt,
               std::vector<param_value> args) -> delete_builder& {
        where_fmt_ = fmt;
        where_args_ = std::move(args);
        return *this;
    }

    struct built_sql {
        std::string              sql;
        std::vector<param_value> params;
    };

    [[nodiscard]] auto build(const format_options& opts = {}) const -> built_sql {
        auto& meta = model_traits<T>::meta();

        std::string sql = "DELETE FROM `";
        sql.append(meta.table_name);
        sql.push_back('`');

        std::vector<param_value> params;

        if (!where_fmt_.empty()) {
            sql.append(" WHERE ");
            sql.append(where_fmt_);
            params = where_args_;
        } else if (!where_.empty()) {
            sql.append(" WHERE ");
            sql.append(where_);
        }

        auto final_sql = format_sql(opts, sql, params);
        return {final_sql ? std::move(*final_sql) : sql, std::move(params)};
    }

private:
    std::string              where_;
    std::string              where_fmt_;
    std::vector<param_value> where_args_;
};

/// Factory function
export template <Model T>
auto delete_of() -> delete_builder<T> { return {}; }

// =============================================================================
// DDL generation
// =============================================================================

/// Generate CREATE TABLE IF NOT EXISTS SQL
export template <Model T>
auto build_create_table_sql() -> std::string {
    auto& meta = model_traits<T>::meta();

    std::string sql = "CREATE TABLE IF NOT EXISTS `";
    sql.append(meta.table_name);
    sql.append("` (\n");

    std::string pk_col;

    for (std::size_t i = 0; i < meta.fields.size(); ++i) {
        auto& f = meta.fields[i];
        if (i > 0) sql.append(",\n");
        sql.append("  `");
        sql.append(f.col.column_name);
        sql.append("` ");

        // UUID primary key forced to CHAR(36)
        if (f.col.is_uuid())
            sql.append("CHAR(36)");
        else
            sql.append(sql_type_str(f.col.type));

        if (f.col.is_pk()) {
            sql.append(" NOT NULL");
            pk_col = f.col.column_name;
        } else if (!f.col.is_nullable()) {
            sql.append(" NOT NULL");
        } else {
            sql.append(" DEFAULT NULL");
        }

        // Only add AUTO_INCREMENT for auto_increment strategy
        if (f.col.is_auto() && f.col.strategy != id_strategy::uuid
                            && f.col.strategy != id_strategy::snowflake)
            sql.append(" AUTO_INCREMENT");
    }

    if (!pk_col.empty()) {
        sql.append(",\n  PRIMARY KEY (`");
        sql.append(pk_col);
        sql.append("`)");
    }

    sql.append("\n) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    return sql;
}

/// Generate DROP TABLE IF EXISTS SQL
export template <Model T>
auto build_drop_table_sql() -> std::string {
    auto& meta = model_traits<T>::meta();
    std::string sql = "DROP TABLE IF EXISTS `";
    sql.append(meta.table_name);
    sql.push_back('`');
    return sql;
}

} // namespace cnetmod::mysql::orm
