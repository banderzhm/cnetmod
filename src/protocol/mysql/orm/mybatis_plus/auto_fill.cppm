export module cnetmod.protocol.mysql:orm_auto_fill;

import std;
import :types;
import :orm_meta;

namespace cnetmod::mysql::orm {

// =============================================================================
// Auto fill field flags
// =============================================================================

export constexpr col_flag FILL_INSERT = static_cast<col_flag>(0x80);
export constexpr col_flag FILL_UPDATE = static_cast<col_flag>(0x100);
export constexpr col_flag FILL_INSERT_UPDATE = FILL_INSERT | FILL_UPDATE;

// =============================================================================
// fill_strategy — Strategy for auto-filling fields
// =============================================================================

export enum class fill_strategy {
    current_timestamp,  // Use current timestamp (for datetime fields)
    current_date,       // Use current date (for date fields)
    current_time,       // Use current time (for time fields)
    uuid,               // Generate UUID (for string fields)
    custom,             // Use custom handler
};

// =============================================================================
// field_fill_handler — Custom handler for field auto-fill
// =============================================================================

export using field_fill_handler = std::function<param_value()>;

// =============================================================================
// auto_fill_config — Configuration for a single field
// =============================================================================

export struct auto_fill_config {
    std::string field_name;
    fill_strategy strategy = fill_strategy::current_timestamp;
    field_fill_handler custom_handler;
    bool on_insert = true;
    bool on_update = false;
};

// =============================================================================
// auto_fill_interceptor — Intercepts INSERT/UPDATE to auto-fill fields
// =============================================================================

export class auto_fill_interceptor {
public:
    /// Register auto-fill configuration for a field
    template <Model T>
    void register_field(auto_fill_config config) {
        auto& meta = model_traits<T>::meta();
        std::string key = std::format("{}:{}", meta.table_name, config.field_name);
        configs_[key] = std::move(config);
    }

    /// Register auto-fill by field flags
    template <Model T>
    void register_from_metadata() {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            if (has_flag(field.col.flags, FILL_INSERT) || has_flag(field.col.flags, FILL_UPDATE)) {
                auto_fill_config config;
                config.field_name = std::string(field.col.column_name);
                config.on_insert = has_flag(field.col.flags, FILL_INSERT);
                config.on_update = has_flag(field.col.flags, FILL_UPDATE);

                // Detect strategy based on field name
                if (field.col.column_name.ends_with("_at") ||
                    field.col.column_name.ends_with("_time") ||
                    field.col.column_name.ends_with("Time") ||
                    field.col.column_name == "created_at" ||
                    field.col.column_name == "updated_at" ||
                    field.col.column_name == "createTime" ||
                    field.col.column_name == "updateTime") {
                    config.strategy = fill_strategy::current_timestamp;
                } else if (field.col.column_name.ends_with("_date") ||
                          field.col.column_name.ends_with("Date")) {
                    config.strategy = fill_strategy::current_date;
                }

                std::string key = std::format("{}:{}", meta.table_name, field.col.column_name);
                configs_[key] = std::move(config);
            }
        }
    }

    /// Fill fields for INSERT operation
    template <Model T>
    auto fill_insert_fields(T& entity) const -> void {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            std::string key = std::format("{}:{}", meta.table_name, field.col.column_name);
            auto it = configs_.find(key);
            if (it != configs_.end() && it->second.on_insert) {
                auto value = generate_value(it->second);
                field.setter(entity, param_to_field_value(value));
            }
        }
    }

    /// Fill fields for UPDATE operation
    template <Model T>
    auto fill_update_fields(T& entity) const -> void {
        auto& meta = model_traits<T>::meta();
        for (auto& field : meta.fields) {
            std::string key = std::format("{}:{}", meta.table_name, field.col.column_name);
            auto it = configs_.find(key);
            if (it != configs_.end() && it->second.on_update) {
                auto value = generate_value(it->second);
                field.setter(entity, param_to_field_value(value));
            }
        }
    }

    /// Get fill parameters for INSERT (returns map of column -> value)
    template <Model T>
    auto get_insert_fill_params() const -> std::unordered_map<std::string, param_value> {
        std::unordered_map<std::string, param_value> result;
        auto& meta = model_traits<T>::meta();

        for (auto& field : meta.fields) {
            std::string key = std::format("{}:{}", meta.table_name, field.col.column_name);
            auto it = configs_.find(key);
            if (it != configs_.end() && it->second.on_insert) {
                result[std::string(field.col.column_name)] = generate_value(it->second);
            }
        }

        return result;
    }

    /// Get fill parameters for UPDATE
    template <Model T>
    auto get_update_fill_params() const -> std::unordered_map<std::string, param_value> {
        std::unordered_map<std::string, param_value> result;
        auto& meta = model_traits<T>::meta();

        for (auto& field : meta.fields) {
            std::string key = std::format("{}:{}", meta.table_name, field.col.column_name);
            auto it = configs_.find(key);
            if (it != configs_.end() && it->second.on_update) {
                result[std::string(field.col.column_name)] = generate_value(it->second);
            }
        }

        return result;
    }

    /// Inject auto-fill into INSERT SQL
    /// Transforms: INSERT INTO users (name, email) VALUES (?, ?)
    /// To:         INSERT INTO users (name, email, created_at, updated_at) VALUES (?, ?, NOW(), NOW())
    template <Model T>
    auto inject_insert_fields(std::string sql, std::vector<param_value>& params) const -> std::string {
        auto fill_params = get_insert_fill_params<T>();
        if (fill_params.empty()) return sql;

        // Find the column list and VALUES clause
        auto values_pos = sql.find(" VALUES ");
        if (values_pos == std::string::npos) return sql;

        // Find column list (between first '(' and ')')
        auto col_start = sql.find('(');
        auto col_end = sql.find(')', col_start);
        if (col_start == std::string::npos || col_end == std::string::npos) return sql;

        // Add fill columns
        std::string fill_cols;
        std::string fill_vals;
        for (auto& [col, val] : fill_params) {
            fill_cols += std::format(", `{}`", col);
            fill_vals += ", {}";
            params.push_back(val);
        }

        // Insert into SQL
        sql.insert(col_end, fill_cols);

        // Find VALUES clause end
        auto val_start = sql.find('(', values_pos);
        auto val_end = sql.find(')', val_start);
        if (val_start != std::string::npos && val_end != std::string::npos) {
            sql.insert(val_end, fill_vals);
        }

        return sql;
    }

    /// Inject auto-fill into UPDATE SQL
    template <Model T>
    auto inject_update_fields(std::string sql, std::vector<param_value>& params) const -> std::string {
        auto fill_params = get_update_fill_params<T>();
        if (fill_params.empty()) return sql;

        // Find SET clause
        auto set_pos = sql.find(" SET ");
        if (set_pos == std::string::npos) return sql;

        // Find WHERE clause (or end of SQL)
        auto where_pos = sql.find(" WHERE ", set_pos);
        auto insert_pos = where_pos != std::string::npos ? where_pos : sql.size();

        // Build fill SET clauses
        std::string fill_set;
        for (auto& [col, val] : fill_params) {
            fill_set += std::format(", `{}` = {{}}", col);
            params.push_back(val);
        }

        sql.insert(insert_pos, fill_set);
        return sql;
    }

private:
    std::unordered_map<std::string, auto_fill_config> configs_;

    // Helper to convert param_value to field_value
    static auto param_to_field_value(const param_value& pv) -> field_value {
        switch (pv.kind) {
        case param_value::kind_t::null_kind:
            return field_value::null();
        case param_value::kind_t::int64_kind:
            return field_value::from_int64(pv.int_val);
        case param_value::kind_t::uint64_kind:
            return field_value::from_uint64(pv.uint_val);
        case param_value::kind_t::double_kind:
            return field_value::from_double(pv.double_val);
        case param_value::kind_t::string_kind:
            return field_value::from_string(pv.str_val);
        case param_value::kind_t::blob_kind:
            return field_value::from_blob(pv.str_val);
        case param_value::kind_t::date_kind:
            return field_value::from_date(pv.date_val);
        case param_value::kind_t::datetime_kind:
            return field_value::from_datetime(pv.datetime_val);
        case param_value::kind_t::time_kind:
            return field_value::from_time(pv.time_val);
        default:
            return field_value::null();
        }
    }

    static auto generate_value(const auto_fill_config& config) -> param_value {
        switch (config.strategy) {
        case fill_strategy::current_timestamp: {
            auto now = std::chrono::system_clock::now();

            // Convert to mysql_datetime
            using namespace std::chrono;
            auto dp = floor<days>(now);
            auto ymd = year_month_day{dp};
            auto time = hh_mm_ss{now - dp};

            mysql_datetime dt;
            dt.year = static_cast<std::uint16_t>(static_cast<int>(ymd.year()));
            dt.month = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.month()));
            dt.day = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.day()));
            dt.hour = static_cast<std::uint8_t>(time.hours().count());
            dt.minute = static_cast<std::uint8_t>(time.minutes().count());
            dt.second = static_cast<std::uint8_t>(time.seconds().count());
            dt.microsecond = 0;

            param_value result;
            result.kind = param_value::kind_t::datetime_kind;
            result.datetime_val = dt;
            return result;
        }
        case fill_strategy::current_date: {
            auto now = std::chrono::system_clock::now();
            auto dp = std::chrono::floor<std::chrono::days>(now);
            auto ymd = std::chrono::year_month_day{dp};

            mysql_date date;
            date.year = static_cast<std::uint16_t>(static_cast<int>(ymd.year()));
            date.month = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.month()));
            date.day = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.day()));

            param_value result;
            result.kind = param_value::kind_t::date_kind;
            result.date_val = date;
            return result;
        }
        case fill_strategy::current_time: {
            auto now = std::chrono::system_clock::now();
            auto dp = std::chrono::floor<std::chrono::days>(now);
            auto time = std::chrono::hh_mm_ss{now - dp};

            mysql_time t;
            t.negative = false;
            t.hours = static_cast<std::uint32_t>(time.hours().count());
            t.minutes = static_cast<std::uint8_t>(time.minutes().count());
            t.seconds = static_cast<std::uint8_t>(time.seconds().count());
            t.microsecond = 0;

            param_value result;
            result.kind = param_value::kind_t::time_kind;
            result.time_val = t;
            return result;
        }
        case fill_strategy::uuid: {
            // Simple UUID v4 generation
            std::random_device rd;
            std::mt19937_64 gen(rd());
            std::uniform_int_distribution<std::uint64_t> dis;

            auto uuid = std::format("{:08x}-{:04x}-4{:03x}-{:04x}-{:012x}",
                dis(gen) & 0xFFFFFFFF,
                (dis(gen) >> 32) & 0xFFFF,
                dis(gen) & 0xFFF,
                (dis(gen) & 0x3FFF) | 0x8000,
                dis(gen) & 0xFFFFFFFFFFFF);

            return param_value::from_string(uuid);
        }
        case fill_strategy::custom:
            if (config.custom_handler) {
                return config.custom_handler();
            }
            return param_value::null();
        default:
            return param_value::null();
        }
    }
};

// =============================================================================
// Global auto-fill interceptor instance
// =============================================================================

export inline auto_fill_interceptor& global_auto_fill_interceptor() {
    static auto_fill_interceptor instance;
    return instance;
}

} // namespace cnetmod::mysql::orm
