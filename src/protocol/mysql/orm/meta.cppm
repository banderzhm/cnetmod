module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:orm_meta;

import std;
import :types;
import :orm_id_gen;

namespace cnetmod::mysql::orm {

// =============================================================================
// Column attribute flags
// =============================================================================

export enum class col_flag : std::uint8_t {
    none           = 0,
    primary_key    = 1,
    auto_increment = 2,
    nullable       = 4,
};

export constexpr auto operator|(col_flag a, col_flag b) noexcept -> col_flag {
    return static_cast<col_flag>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
export constexpr auto operator&(col_flag a, col_flag b) noexcept -> col_flag {
    return static_cast<col_flag>(
        static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
export constexpr auto has_flag(col_flag flags, col_flag f) noexcept -> bool {
    return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(f)) != 0;
}

// =============================================================================
// column_def — Single column metadata
// =============================================================================

export struct column_def {
    std::string_view field_name;      // C++ member name
    std::string_view column_name;     // SQL column name
    column_type      type;            // MySQL column type
    col_flag         flags    = col_flag::none;
    id_strategy      strategy = id_strategy::none;

    [[nodiscard]] constexpr auto is_pk()   const noexcept -> bool {
        return has_flag(flags, col_flag::primary_key);
    }
    [[nodiscard]] constexpr auto is_auto() const noexcept -> bool {
        return has_flag(flags, col_flag::auto_increment);
    }
    [[nodiscard]] constexpr auto is_nullable() const noexcept -> bool {
        return has_flag(flags, col_flag::nullable);
    }
    [[nodiscard]] constexpr auto is_uuid() const noexcept -> bool {
        return strategy == id_strategy::uuid;
    }
    [[nodiscard]] constexpr auto is_snowflake() const noexcept -> bool {
        return strategy == id_strategy::snowflake;
    }
};

// =============================================================================
// field_setter / field_getter — Function pointer types
// =============================================================================

/// Set model member from field_value
export template <class T>
using field_setter = void(*)(T& model, const field_value& val);

/// Extract param_value from model member
export template <class T>
using field_getter = param_value(*)(const T& model);

// =============================================================================
// field_mapping — Column definition + getter/setter binding
// =============================================================================

export template <class T>
struct field_mapping {
    column_def        col;
    field_setter<T>   setter;
    field_getter<T>   getter;
};

// =============================================================================
// table_meta — Table-level metadata
// =============================================================================

export template <class T>
struct table_meta {
    std::string_view                    table_name;
    std::span<const field_mapping<T>>   fields;

    /// Find primary key column
    [[nodiscard]] auto pk() const noexcept -> const field_mapping<T>* {
        for (auto& f : fields)
            if (f.col.is_pk()) return &f;
        return nullptr;
    }

    /// Find by column name
    [[nodiscard]] auto find_column(std::string_view col_name) const noexcept
        -> const field_mapping<T>*
    {
        for (auto& f : fields)
            if (f.col.column_name == col_name) return &f;
        return nullptr;
    }

    /// All non-auto_increment columns
    [[nodiscard]] auto insertable_fields() const
        -> std::vector<const field_mapping<T>*>
    {
        std::vector<const field_mapping<T>*> result;
        for (auto& f : fields)
            if (!f.col.is_auto()) result.push_back(&f);
        return result;
    }

    /// All non-PK columns (for UPDATE SET)
    [[nodiscard]] auto updatable_fields() const
        -> std::vector<const field_mapping<T>*>
    {
        std::vector<const field_mapping<T>*> result;
        for (auto& f : fields)
            if (!f.col.is_pk()) result.push_back(&f);
        return result;
    }
};

// =============================================================================
// model_traits — Users specialize this trait to declare model mapping
// =============================================================================

export template <class T>
struct model_traits;   // Specialized by CNETMOD_MODEL macro

// =============================================================================
// Model concept
// =============================================================================

export template <class T>
concept Model = requires {
    { model_traits<T>::meta() } -> std::same_as<const table_meta<T>&>;
};

// =============================================================================
// column_type → SQL DDL type string
// =============================================================================

export inline auto sql_type_str(column_type ct) noexcept -> std::string_view {
    switch (ct) {
    case column_type::tinyint:   return "TINYINT";
    case column_type::smallint:  return "SMALLINT";
    case column_type::mediumint: return "MEDIUMINT";
    case column_type::int_:      return "INT";
    case column_type::bigint:    return "BIGINT";
    case column_type::float_:    return "FLOAT";
    case column_type::double_:   return "DOUBLE";
    case column_type::decimal:   return "DECIMAL";
    case column_type::bit:       return "BIT";
    case column_type::year:      return "YEAR";
    case column_type::time:      return "TIME";
    case column_type::date:      return "DATE";
    case column_type::datetime:  return "DATETIME";
    case column_type::timestamp: return "TIMESTAMP";
    case column_type::char_:     return "CHAR(255)";
    case column_type::varchar:   return "VARCHAR(255)";
    case column_type::binary:    return "BINARY(255)";
    case column_type::varbinary: return "VARBINARY(255)";
    case column_type::text:      return "TEXT";
    case column_type::blob:      return "BLOB";
    case column_type::enum_:     return "VARCHAR(64)";
    case column_type::set:       return "VARCHAR(255)";
    case column_type::json:      return "JSON";
    case column_type::geometry:  return "GEOMETRY";
    default:                     return "TEXT";
    }
}

} // namespace cnetmod::mysql::orm

// Macro definitions have been moved to include/cnetmod/orm.hpp
// Users can use them after import cnetmod.protocol.mysql; by #include <cnetmod/orm.hpp>
// CNETMOD_MODEL / CNETMOD_FIELD / PK / AUTO_INC / NULLABLE

// =============================================================================
// detail: set_member / get_member automatic type conversion
// =============================================================================

export namespace cnetmod::mysql::orm::detail {

// ── set_member: field_value → C++ member ──

inline void set_member(std::int64_t& m, const field_value& v) {
    if (v.is_int64()) m = v.get_int64();
    else if (v.is_uint64()) m = static_cast<std::int64_t>(v.get_uint64());
    else if (v.is_string()) {
        std::int64_t tmp{};
        auto sv = v.get_string();
        std::from_chars(sv.data(), sv.data() + sv.size(), tmp);
        m = tmp;
    }
}

inline void set_member(std::uint64_t& m, const field_value& v) {
    if (v.is_uint64()) m = v.get_uint64();
    else if (v.is_int64()) m = static_cast<std::uint64_t>(v.get_int64());
    else if (v.is_string()) {
        std::uint64_t tmp{};
        auto sv = v.get_string();
        std::from_chars(sv.data(), sv.data() + sv.size(), tmp);
        m = tmp;
    }
}

inline void set_member(int& m, const field_value& v) {
    std::int64_t tmp{};
    set_member(tmp, v);
    m = static_cast<int>(tmp);
}

inline void set_member(std::uint32_t& m, const field_value& v) {
    std::uint64_t tmp{};
    set_member(tmp, v);
    m = static_cast<std::uint32_t>(tmp);
}

inline void set_member(float& m, const field_value& v) {
    if (v.is_float()) m = v.get_float();
    else if (v.is_double()) m = static_cast<float>(v.get_double());
}

inline void set_member(double& m, const field_value& v) {
    if (v.is_double()) m = v.get_double();
    else if (v.is_float()) m = static_cast<double>(v.get_float());
}

inline void set_member(std::string& m, const field_value& v) {
    if (v.is_string()) m = std::string(v.get_string());
    else if (!v.is_null()) m = v.to_string();
}

inline void set_member(bool& m, const field_value& v) {
    if (v.is_int64()) m = v.get_int64() != 0;
    else if (v.is_uint64()) m = v.get_uint64() != 0;
}

inline void set_member(mysql_date& m, const field_value& v) {
    if (v.is_date()) m = v.get_date();
}

inline void set_member(mysql_datetime& m, const field_value& v) {
    if (v.is_datetime()) m = v.get_datetime();
}

inline void set_member(mysql_time& m, const field_value& v) {
    if (v.is_time()) m = v.get_time();
}

inline void set_member(std::optional<std::string>& m, const field_value& v) {
    if (v.is_null()) m = std::nullopt;
    else if (v.is_string()) m = std::string(v.get_string());
    else m = v.to_string();
}

inline void set_member(std::optional<std::int64_t>& m, const field_value& v) {
    if (v.is_null()) m = std::nullopt;
    else { std::int64_t tmp{}; set_member(tmp, v); m = tmp; }
}

inline void set_member(std::optional<double>& m, const field_value& v) {
    if (v.is_null()) m = std::nullopt;
    else { double tmp{}; set_member(tmp, v); m = tmp; }
}

// ── get_member: C++ member → param_value ──

inline auto get_member(std::int64_t v) -> param_value {
    return param_value::from_int(v);
}

inline auto get_member(std::uint64_t v) -> param_value {
    return param_value::from_uint(v);
}

inline auto get_member(int v) -> param_value {
    return param_value::from_int(static_cast<std::int64_t>(v));
}

inline auto get_member(std::uint32_t v) -> param_value {
    return param_value::from_uint(static_cast<std::uint64_t>(v));
}

inline auto get_member(float v) -> param_value {
    return param_value::from_double(static_cast<double>(v));
}

inline auto get_member(double v) -> param_value {
    return param_value::from_double(v);
}

inline auto get_member(const std::string& v) -> param_value {
    return param_value::from_string(v);
}

inline auto get_member(std::string_view v) -> param_value {
    return param_value::from_string(std::string(v));
}

inline auto get_member(bool v) -> param_value {
    return param_value::from_int(v ? 1 : 0);
}

inline auto get_member(const mysql_date& v) -> param_value {
    return param_value::from_date(v);
}

inline auto get_member(const mysql_datetime& v) -> param_value {
    return param_value::from_datetime(v);
}

inline auto get_member(const mysql_time& v) -> param_value {
    return param_value::from_time(v);
}

inline auto get_member(const std::optional<std::string>& v) -> param_value {
    return v ? param_value::from_string(*v) : param_value::null();
}

inline auto get_member(const std::optional<std::int64_t>& v) -> param_value {
    return v ? param_value::from_int(*v) : param_value::null();
}

inline auto get_member(const std::optional<double>& v) -> param_value {
    return v ? param_value::from_double(*v) : param_value::null();
}

// ── uuid set/get ──

inline void set_member(uuid& m, const field_value& v) {
    if (v.is_string()) {
        auto r = uuid::from_string(v.get_string());
        if (r) m = *r;
    }
}

inline auto get_member(const uuid& v) -> param_value {
    return param_value::from_string(v.to_string());
}

} // namespace cnetmod::mysql::orm::detail
