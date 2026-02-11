module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:types;

import std;
import :diagnostics;

namespace cnetmod::mysql {

// =============================================================================
// 字段类型 (protocol_field_type) — 协议级别
// =============================================================================

export enum class field_type : std::uint8_t {
    decimal     = 0x00,
    tiny        = 0x01,   // TINYINT
    short_type  = 0x02,   // SMALLINT
    long_type   = 0x03,   // INT
    float_type  = 0x04,   // FLOAT
    double_type = 0x05,   // DOUBLE
    null_type   = 0x06,
    timestamp   = 0x07,   // TIMESTAMP
    longlong    = 0x08,   // BIGINT
    int24       = 0x09,   // MEDIUMINT
    date        = 0x0A,   // DATE
    time_type   = 0x0B,   // TIME
    datetime    = 0x0C,   // DATETIME
    year        = 0x0D,   // YEAR
    varchar     = 0x0F,   // VARCHAR (not typically sent)
    bit         = 0x10,   // BIT
    json        = 0xF5,   // JSON
    newdecimal  = 0xF6,   // DECIMAL/NUMERIC
    enum_type   = 0xF7,   // ENUM
    set_type    = 0xF8,   // SET
    tiny_blob   = 0xF9,   // TINYBLOB/TINYTEXT
    medium_blob = 0xFA,   // MEDIUMBLOB/MEDIUMTEXT
    long_blob   = 0xFB,   // LONGBLOB/LONGTEXT
    blob        = 0xFC,   // BLOB/TEXT
    var_string  = 0xFD,   // VARCHAR/VARBINARY
    string      = 0xFE,   // CHAR/BINARY/ENUM/SET
    geometry    = 0xFF,   // GEOMETRY
};

// =============================================================================
// field_kind — 字段值的 C++ 存储类型（参考 Boost.MySQL field_kind）
// =============================================================================

export enum class field_kind : std::uint8_t {
    null = 0,  ///< NULL
    int64,     ///< std::int64_t  (有符号整数)
    uint64,    ///< std::uint64_t (无符号整数 / YEAR / BIT)
    string,    ///< std::string   (CHAR/VARCHAR/TEXT/DECIMAL/ENUM/SET/JSON)
    blob,      ///< blob / std::string (BINARY/VARBINARY/BLOB/GEOMETRY)
    float_,    ///< float (FLOAT)
    double_,   ///< double (DOUBLE)
    date,      ///< mysql_date (DATE)
    datetime,  ///< mysql_datetime (DATETIME / TIMESTAMP)
    time,      ///< mysql_time (TIME)
};

export inline auto field_kind_to_str(field_kind k) noexcept -> const char* {
    switch (k) {
    case field_kind::null:     return "null";
    case field_kind::int64:    return "int64";
    case field_kind::uint64:   return "uint64";
    case field_kind::string:   return "string";
    case field_kind::blob:     return "blob";
    case field_kind::float_:   return "float";
    case field_kind::double_:  return "double";
    case field_kind::date:     return "date";
    case field_kind::datetime: return "datetime";
    case field_kind::time:     return "time";
    default:                   return "<unknown>";
    }
}

// =============================================================================
// blob / blob_view — 二进制数据类型
// =============================================================================

export using mysql_blob      = std::vector<unsigned char>;
export using mysql_blob_view  = std::span<const unsigned char>;

// =============================================================================
// bad_field_access — 类型不匹配异常（参考 Boost.MySQL bad_field_access）
// =============================================================================

export class bad_field_access : public std::exception {
public:
    const char* what() const noexcept override { return "bad_field_access"; }
};

// =============================================================================
// column_type — 高级列类型（参考 Boost.MySQL column_type）
// =============================================================================

export enum class column_type {
    tinyint,      ///< TINYINT (signed/unsigned)
    smallint,     ///< SMALLINT (signed/unsigned)
    mediumint,    ///< MEDIUMINT (signed/unsigned)
    int_,         ///< INT (signed/unsigned)
    bigint,       ///< BIGINT (signed/unsigned)
    float_,       ///< FLOAT
    double_,      ///< DOUBLE
    decimal,      ///< DECIMAL / NUMERIC
    bit,          ///< BIT
    year,         ///< YEAR
    time,         ///< TIME
    date,         ///< DATE
    datetime,     ///< DATETIME
    timestamp,    ///< TIMESTAMP
    char_,        ///< CHAR
    varchar,      ///< VARCHAR
    binary,       ///< BINARY
    varbinary,    ///< VARBINARY
    text,         ///< TEXT (TINYTEXT/MEDIUMTEXT/TEXT/LONGTEXT)
    blob,         ///< BLOB (TINYBLOB/MEDIUMBLOB/BLOB/LONGBLOB)
    enum_,        ///< ENUM
    set,          ///< SET
    json,         ///< JSON
    geometry,     ///< GEOMETRY
    unknown,      ///< 未知类型
};

// =============================================================================
// column_flags — 列标志位（参考 Boost.MySQL column_flags）
// =============================================================================

export namespace column_flags {
    inline constexpr std::uint16_t not_null          = 1;      // NOT NULL
    inline constexpr std::uint16_t pri_key           = 2;      // PRIMARY KEY
    inline constexpr std::uint16_t unique_key        = 4;      // UNIQUE KEY
    inline constexpr std::uint16_t multiple_key      = 8;      // KEY (non-unique)
    inline constexpr std::uint16_t is_blob           = 16;     // BLOB/TEXT
    inline constexpr std::uint16_t is_unsigned       = 32;     // UNSIGNED
    inline constexpr std::uint16_t zerofill          = 64;     // ZEROFILL
    inline constexpr std::uint16_t is_binary         = 128;    // BINARY
    inline constexpr std::uint16_t is_enum           = 256;    // ENUM
    inline constexpr std::uint16_t auto_increment    = 512;    // AUTO_INCREMENT
    inline constexpr std::uint16_t is_timestamp      = 1024;   // TIMESTAMP
    inline constexpr std::uint16_t is_set            = 2048;   // SET
    inline constexpr std::uint16_t no_default_value  = 4096;   // No default value
    inline constexpr std::uint16_t on_update_now     = 8192;   // ON UPDATE CURRENT_TIMESTAMP
    inline constexpr std::uint16_t part_key          = 16384;  // Part of some key
    inline constexpr std::uint16_t num               = 32768;  // Numeric
}

inline constexpr std::uint16_t binary_collation = 63;

// =============================================================================
// compute_column_type — 从协议级 field_type + flags + charset 计算高级 column_type
// =============================================================================

export inline auto compute_column_type(
    field_type proto_type,
    std::uint16_t flags,
    std::uint16_t collation
) noexcept -> column_type
{
    switch (proto_type) {
    case field_type::decimal:
    case field_type::newdecimal:  return column_type::decimal;
    case field_type::tiny:        return column_type::tinyint;
    case field_type::short_type:  return column_type::smallint;
    case field_type::int24:       return column_type::mediumint;
    case field_type::long_type:   return column_type::int_;
    case field_type::longlong:    return column_type::bigint;
    case field_type::float_type:  return column_type::float_;
    case field_type::double_type: return column_type::double_;
    case field_type::bit:         return column_type::bit;
    case field_type::date:        return column_type::date;
    case field_type::datetime:    return column_type::datetime;
    case field_type::timestamp:   return column_type::timestamp;
    case field_type::time_type:   return column_type::time;
    case field_type::year:        return column_type::year;
    case field_type::json:        return column_type::json;
    case field_type::geometry:    return column_type::geometry;
    case field_type::enum_type:   return column_type::enum_;
    case field_type::set_type:    return column_type::set;
    case field_type::string: {
        // CHAR/BINARY/ENUM/SET — 通过 flags 和 collation 区分
        if (flags & column_flags::is_set)  return column_type::set;
        if (flags & column_flags::is_enum) return column_type::enum_;
        if (collation == binary_collation) return column_type::binary;
        return column_type::char_;
    }
    case field_type::varchar:
    case field_type::var_string:
        return collation == binary_collation ? column_type::varbinary : column_type::varchar;
    case field_type::tiny_blob:
    case field_type::medium_blob:
    case field_type::long_blob:
    case field_type::blob:
        return collation == binary_collation ? column_type::blob : column_type::text;
    default:
        return column_type::unknown;
    }
}

// =============================================================================
// column_type_to_str — 列类型转可读字符串
// =============================================================================

export inline auto column_type_to_str(
    column_type ct,
    bool is_unsigned = false
) noexcept -> const char*
{
    switch (ct) {
    case column_type::tinyint:   return is_unsigned ? "TINYINT UNSIGNED" : "TINYINT";
    case column_type::smallint:  return is_unsigned ? "SMALLINT UNSIGNED" : "SMALLINT";
    case column_type::mediumint: return is_unsigned ? "MEDIUMINT UNSIGNED" : "MEDIUMINT";
    case column_type::int_:      return is_unsigned ? "INT UNSIGNED" : "INT";
    case column_type::bigint:    return is_unsigned ? "BIGINT UNSIGNED" : "BIGINT";
    case column_type::float_:    return "FLOAT";
    case column_type::double_:   return "DOUBLE";
    case column_type::decimal:   return "DECIMAL";
    case column_type::bit:       return "BIT";
    case column_type::year:      return "YEAR";
    case column_type::time:      return "TIME";
    case column_type::date:      return "DATE";
    case column_type::datetime:  return "DATETIME";
    case column_type::timestamp: return "TIMESTAMP";
    case column_type::char_:     return "CHAR";
    case column_type::varchar:   return "VARCHAR";
    case column_type::binary:    return "BINARY";
    case column_type::varbinary: return "VARBINARY";
    case column_type::text:      return "TEXT";
    case column_type::blob:      return "BLOB";
    case column_type::enum_:     return "ENUM";
    case column_type::set:       return "SET";
    case column_type::json:      return "JSON";
    case column_type::geometry:  return "GEOMETRY";
    default:                     return "<unknown column type>";
    }
}

// =============================================================================
// mysql_date — DATE 类型（参考 Boost.MySQL date）
// =============================================================================

export struct mysql_date {
    std::uint16_t year  = 0;
    std::uint8_t  month = 0;
    std::uint8_t  day   = 0;

    constexpr auto valid() const noexcept -> bool {
        return year >= 1 && year <= 9999 &&
               month >= 1 && month <= 12 &&
               day >= 1 && day <= 31;
    }

    auto to_string() const -> std::string {
        return std::format("{:04d}-{:02d}-{:02d}", year, month, day);
    }

    friend constexpr auto operator==(const mysql_date&, const mysql_date&) noexcept -> bool = default;
};

// =============================================================================
// mysql_datetime — DATETIME / TIMESTAMP 类型（参考 Boost.MySQL datetime）
// =============================================================================

export struct mysql_datetime {
    std::uint16_t year        = 0;
    std::uint8_t  month       = 0;
    std::uint8_t  day         = 0;
    std::uint8_t  hour        = 0;
    std::uint8_t  minute      = 0;
    std::uint8_t  second      = 0;
    std::uint32_t microsecond = 0;

    constexpr auto valid() const noexcept -> bool {
        return year >= 1 && year <= 9999 &&
               month >= 1 && month <= 12 &&
               day >= 1 && day <= 31 &&
               hour <= 23 && minute <= 59 && second <= 59 &&
               microsecond <= 999999;
    }

    auto to_string() const -> std::string {
        if (microsecond > 0)
            return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:06d}",
                               year, month, day, hour, minute, second, microsecond);
        return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                           year, month, day, hour, minute, second);
    }

    auto to_date() const noexcept -> mysql_date {
        return {year, month, day};
    }

    friend constexpr auto operator==(const mysql_datetime&, const mysql_datetime&) noexcept -> bool = default;
};

// =============================================================================
// mysql_time — TIME 类型（参考 Boost.MySQL time）
// 范围: -838:59:59.000000 ~ +838:59:59.000000
// 内部存储为 std::chrono::microseconds
// =============================================================================

export struct mysql_time {
    bool          negative    = false;
    std::uint32_t hours       = 0;   // 0..838
    std::uint8_t  minutes     = 0;   // 0..59
    std::uint8_t  seconds     = 0;   // 0..59
    std::uint32_t microsecond = 0;   // 0..999999

    auto to_microseconds() const noexcept -> std::chrono::microseconds {
        auto total = std::chrono::hours(hours) + std::chrono::minutes(minutes) +
                     std::chrono::seconds(seconds) + std::chrono::microseconds(microsecond);
        return negative ? -total : total;
    }

    auto to_string() const -> std::string {
        if (microsecond > 0)
            return std::format("{}{:02d}:{:02d}:{:02d}.{:06d}",
                               negative ? "-" : "", hours, minutes, seconds, microsecond);
        return std::format("{}{:02d}:{:02d}:{:02d}",
                           negative ? "-" : "", hours, minutes, seconds);
    }

    friend constexpr auto operator==(const mysql_time&, const mysql_time&) noexcept -> bool = default;
};

// =============================================================================
// compute_field_kind — 从协议 field_type + flags + charset 推导 C++ 存储类型
// =============================================================================

export inline auto compute_field_kind(
    field_type type, std::uint16_t flags, std::uint16_t collation
) noexcept -> field_kind
{
    switch (type) {
    case field_type::null_type:  return field_kind::null;
    case field_type::tiny:
    case field_type::short_type:
    case field_type::long_type:
    case field_type::int24:
    case field_type::longlong:
        return (flags & column_flags::is_unsigned) ? field_kind::uint64 : field_kind::int64;
    case field_type::year:
    case field_type::bit:
        return field_kind::uint64;
    case field_type::float_type:  return field_kind::float_;
    case field_type::double_type: return field_kind::double_;
    case field_type::decimal:
    case field_type::newdecimal:  return field_kind::string;  // 任意精度
    case field_type::date:        return field_kind::date;
    case field_type::datetime:
    case field_type::timestamp:   return field_kind::datetime;
    case field_type::time_type:   return field_kind::time;
    case field_type::json:
    case field_type::enum_type:
    case field_type::set_type:    return field_kind::string;
    case field_type::geometry:    return field_kind::blob;
    case field_type::string:
    case field_type::varchar:
    case field_type::var_string:
        return collation == binary_collation ? field_kind::blob : field_kind::string;
    case field_type::tiny_blob:
    case field_type::medium_blob:
    case field_type::long_blob:
    case field_type::blob:
        return collation == binary_collation ? field_kind::blob : field_kind::string;
    default:
        return field_kind::string;
    }
}

// =============================================================================
// field_value — 单个字段值（参考 Boost.MySQL field / field_view）
// =============================================================================
//
// 以 field_kind 为判别器，存储对应类型的值。
// 提供 kind() / is_xxx() / as_xxx()（带检查）/ get_xxx()（无检查）访问器。

export struct field_value {
    field_kind     kind_        = field_kind::null;
    std::int64_t   int_val      = 0;
    std::uint64_t  uint_val     = 0;
    float          float_val    = 0.0f;
    double         double_val   = 0.0;
    std::string    str_val;              // string & blob 共用
    mysql_date     date_val;
    mysql_datetime datetime_val;
    mysql_time     time_val;

    // ── 判别 ────────────────────────────────────────────────
    auto kind()        const noexcept -> field_kind { return kind_; }
    auto is_null()     const noexcept -> bool { return kind_ == field_kind::null; }
    auto is_int64()    const noexcept -> bool { return kind_ == field_kind::int64; }
    auto is_uint64()   const noexcept -> bool { return kind_ == field_kind::uint64; }
    auto is_string()   const noexcept -> bool { return kind_ == field_kind::string; }
    auto is_blob()     const noexcept -> bool { return kind_ == field_kind::blob; }
    auto is_float()    const noexcept -> bool { return kind_ == field_kind::float_; }
    auto is_double()   const noexcept -> bool { return kind_ == field_kind::double_; }
    auto is_date()     const noexcept -> bool { return kind_ == field_kind::date; }
    auto is_datetime() const noexcept -> bool { return kind_ == field_kind::datetime; }
    auto is_time()     const noexcept -> bool { return kind_ == field_kind::time; }

    // ── as_xxx — 带类型检查的访问（不匹配抛 bad_field_access）──
    auto as_int64()    const -> std::int64_t              { chk(field_kind::int64);    return int_val; }
    auto as_uint64()   const -> std::uint64_t             { chk(field_kind::uint64);   return uint_val; }
    auto as_float()    const -> float                     { chk(field_kind::float_);   return float_val; }
    auto as_double()   const -> double                    { chk(field_kind::double_);  return double_val; }
    auto as_string()   const -> std::string_view          { chk(field_kind::string);   return str_val; }
    auto as_blob()     const -> mysql_blob_view {
        chk(field_kind::blob);
        return {reinterpret_cast<const unsigned char*>(str_val.data()), str_val.size()};
    }
    auto as_date()     const -> const mysql_date&         { chk(field_kind::date);     return date_val; }
    auto as_datetime() const -> const mysql_datetime&     { chk(field_kind::datetime); return datetime_val; }
    auto as_time()     const -> const mysql_time&         { chk(field_kind::time);     return time_val; }

    // ── get_xxx — 无检查访问（调用者需确保 kind 正确）─────────
    auto get_int64()    const noexcept -> std::int64_t          { return int_val; }
    auto get_uint64()   const noexcept -> std::uint64_t         { return uint_val; }
    auto get_float()    const noexcept -> float                 { return float_val; }
    auto get_double()   const noexcept -> double                { return double_val; }
    auto get_string()   const noexcept -> std::string_view      { return str_val; }
    auto get_date()     const noexcept -> const mysql_date&     { return date_val; }
    auto get_datetime() const noexcept -> const mysql_datetime& { return datetime_val; }
    auto get_time()     const noexcept -> const mysql_time&     { return time_val; }

    // ── 便捷工厂 ────────────────────────────────────────────
    static auto null() -> field_value { return {}; }
    static auto from_int64(std::int64_t v) -> field_value {
        field_value f; f.kind_ = field_kind::int64; f.int_val = v; return f;
    }
    static auto from_uint64(std::uint64_t v) -> field_value {
        field_value f; f.kind_ = field_kind::uint64; f.uint_val = v; return f;
    }
    static auto from_float(float v) -> field_value {
        field_value f; f.kind_ = field_kind::float_; f.float_val = v; return f;
    }
    static auto from_double(double v) -> field_value {
        field_value f; f.kind_ = field_kind::double_; f.double_val = v; return f;
    }
    static auto from_string(std::string v) -> field_value {
        field_value f; f.kind_ = field_kind::string; f.str_val = std::move(v); return f;
    }
    static auto from_blob(std::string v) -> field_value {
        field_value f; f.kind_ = field_kind::blob; f.str_val = std::move(v); return f;
    }
    static auto from_date(mysql_date d) -> field_value {
        field_value f; f.kind_ = field_kind::date; f.date_val = d; return f;
    }
    static auto from_datetime(mysql_datetime dt) -> field_value {
        field_value f; f.kind_ = field_kind::datetime; f.datetime_val = dt; return f;
    }
    static auto from_time(mysql_time t) -> field_value {
        field_value f; f.kind_ = field_kind::time; f.time_val = t; return f;
    }

    // ── 转可读字符串 ────────────────────────────────────────
    auto to_string() const -> std::string {
        switch (kind_) {
        case field_kind::null:     return "NULL";
        case field_kind::int64:    return std::to_string(int_val);
        case field_kind::uint64:   return std::to_string(uint_val);
        case field_kind::float_:   return std::format("{}", float_val);
        case field_kind::double_:  return std::format("{}", double_val);
        case field_kind::date:     return date_val.to_string();
        case field_kind::datetime: return datetime_val.to_string();
        case field_kind::time:     return time_val.to_string();
        case field_kind::string:
        case field_kind::blob:
        default:
            return str_val;
        }
    }

private:
    void chk(field_kind expected) const {
        if (kind_ != expected) throw bad_field_access{};
    }
};

// =============================================================================
// column_meta — 列元信息 / metadata（参考 Boost.MySQL metadata）
// =============================================================================

export struct column_meta {
    std::string   database;
    std::string   table;
    std::string   org_table;
    std::string   name;
    std::string   org_name;
    field_type    type          = field_type::null_type;
    std::uint16_t flags         = 0;
    std::uint8_t  decimals      = 0;
    std::uint16_t charset       = 0;
    std::uint32_t column_length = 0;

    // ── 高级访问器（参考 Boost.MySQL metadata 接口）───────────

    /// 获取高级列类型（通过 field_type + flags + charset 计算）
    auto col_type() const noexcept -> column_type {
        return compute_column_type(type, flags, charset);
    }

    /// 获取列类型的可读字符串
    auto type_str() const noexcept -> const char* {
        return column_type_to_str(col_type(), is_unsigned());
    }

    /// 是否为 UNSIGNED
    auto is_unsigned()         const noexcept -> bool { return (flags & column_flags::is_unsigned) != 0; }
    /// NOT NULL 约束
    auto is_not_null()         const noexcept -> bool { return (flags & column_flags::not_null) != 0; }
    /// PRIMARY KEY
    auto is_primary_key()      const noexcept -> bool { return (flags & column_flags::pri_key) != 0; }
    /// UNIQUE KEY
    auto is_unique_key()       const noexcept -> bool { return (flags & column_flags::unique_key) != 0; }
    /// KEY (非唯一索引)
    auto is_multiple_key()     const noexcept -> bool { return (flags & column_flags::multiple_key) != 0; }
    /// AUTO_INCREMENT
    auto is_auto_increment()   const noexcept -> bool { return (flags & column_flags::auto_increment) != 0; }
    /// ZEROFILL
    auto is_zerofill()         const noexcept -> bool { return (flags & column_flags::zerofill) != 0; }
    /// BINARY 标志
    auto is_binary()           const noexcept -> bool { return (flags & column_flags::is_binary) != 0; }
    /// BLOB/TEXT 标志
    auto is_blob_or_text()     const noexcept -> bool { return (flags & column_flags::is_blob) != 0; }
    /// ENUM 标志
    auto is_enum()             const noexcept -> bool { return (flags & column_flags::is_enum) != 0; }
    /// SET 标志
    auto is_set()              const noexcept -> bool { return (flags & column_flags::is_set) != 0; }
    /// 无默认值
    auto has_no_default_value() const noexcept -> bool { return (flags & column_flags::no_default_value) != 0; }
    /// ON UPDATE CURRENT_TIMESTAMP
    auto is_set_to_now_on_update() const noexcept -> bool { return (flags & column_flags::on_update_now) != 0; }
};

// =============================================================================
// row / result_set
// =============================================================================

export using row = std::vector<field_value>;

export struct result_set {
    std::vector<column_meta> columns;
    std::vector<row>         rows;
    std::uint64_t affected_rows  = 0;
    std::uint64_t last_insert_id = 0;
    std::uint16_t warning_count  = 0;
    std::uint16_t status_flags   = 0;
    std::string   info;
    std::string   error_msg;
    std::uint16_t error_code     = 0;
    std::string   sql_state;          // 5-char SQL state (e.g. "42S02")
    diagnostics   diag;               // 详细诊断信息

    auto ok()       const noexcept -> bool { return error_code == 0 && error_msg.empty(); }
    auto is_err()   const noexcept -> bool { return !ok(); }
    auto has_rows() const noexcept -> bool { return !rows.empty(); }
};

// =============================================================================
// statement — prepared statement 句柄
// =============================================================================

export struct statement {
    std::uint32_t id           = 0;
    std::uint16_t num_params   = 0;
    std::uint16_t num_columns  = 0;

    auto valid() const noexcept -> bool { return id != 0; }
};

// =============================================================================
// param_value — prepared statement 参数值
// =============================================================================

export struct param_value {
    enum class kind_t : std::uint8_t {
        null_kind, int64_kind, uint64_kind, double_kind,
        string_kind, blob_kind,
        date_kind, datetime_kind, time_kind
    };

    kind_t         kind         = kind_t::null_kind;
    std::int64_t   int_val      = 0;
    std::uint64_t  uint_val     = 0;
    double         double_val   = 0.0;
    std::string    str_val;
    mysql_date     date_val;
    mysql_datetime datetime_val;
    mysql_time     time_val;

    static auto null() -> param_value { return {}; }
    static auto from_int(std::int64_t v) -> param_value {
        param_value p; p.kind = kind_t::int64_kind; p.int_val = v; return p;
    }
    static auto from_uint(std::uint64_t v) -> param_value {
        param_value p; p.kind = kind_t::uint64_kind; p.uint_val = v; return p;
    }
    static auto from_double(double v) -> param_value {
        param_value p; p.kind = kind_t::double_kind; p.double_val = v; return p;
    }
    static auto from_string(std::string v) -> param_value {
        param_value p; p.kind = kind_t::string_kind; p.str_val = std::move(v); return p;
    }
    static auto from_blob(std::string v) -> param_value {
        param_value p; p.kind = kind_t::blob_kind; p.str_val = std::move(v); return p;
    }
    static auto from_date(mysql_date d) -> param_value {
        param_value p; p.kind = kind_t::date_kind; p.date_val = d; return p;
    }
    static auto from_datetime(mysql_datetime dt) -> param_value {
        param_value p; p.kind = kind_t::datetime_kind; p.datetime_val = dt; return p;
    }
    static auto from_time(mysql_time t) -> param_value {
        param_value p; p.kind = kind_t::time_kind; p.time_val = t; return p;
    }
};

// =============================================================================
// execution_state — 多结果集状态机（参考 Boost.MySQL execution_state）
// =============================================================================

export class execution_state {
public:
    enum class state_t : std::uint8_t {
        needs_start,     // 初始状态，需要调用 start_execution
        reading_rows,    // 正在读取当前结果集的行
        reading_head,    // 需要读取下一个结果集的头
        complete,        // 所有结果集已读取完毕
    };

    execution_state() noexcept = default;

    auto should_start_op() const noexcept -> bool { return state_ == state_t::needs_start; }
    auto should_read_rows() const noexcept -> bool { return state_ == state_t::reading_rows; }
    auto should_read_head() const noexcept -> bool { return state_ == state_t::reading_head; }
    auto is_complete()     const noexcept -> bool { return state_ == state_t::complete; }

    auto columns() const noexcept -> const std::vector<column_meta>& { return columns_; }
    auto affected_rows()  const noexcept -> std::uint64_t { return affected_rows_; }
    auto last_insert_id() const noexcept -> std::uint64_t { return last_insert_id_; }
    auto warning_count()  const noexcept -> std::uint16_t { return warning_count_; }
    auto info()           const noexcept -> std::string_view { return info_; }
    auto error_msg()      const noexcept -> std::string_view { return error_msg_; }
    auto error_code()     const noexcept -> std::uint16_t { return error_code_; }

    // 内部使用 — 由 client 调用
    void set_state(state_t s) noexcept { state_ = s; }
    void set_columns(std::vector<column_meta> cols) { columns_ = std::move(cols); }
    void set_ok_data(std::uint64_t aff, std::uint64_t lid, std::uint16_t warn,
                     std::uint16_t status, std::string i) {
        affected_rows_  = aff;
        last_insert_id_ = lid;
        warning_count_  = warn;
        status_flags_   = status;
        info_           = std::move(i);
    }
    void set_error(std::uint16_t code, std::string msg) {
        error_code_ = code;
        error_msg_  = std::move(msg);
        state_      = state_t::complete;
    }
    auto status_flags() const noexcept -> std::uint16_t { return status_flags_; }
    auto has_more_results() const noexcept -> bool {
        return (status_flags_ & 0x0008) != 0; // SERVER_MORE_RESULTS_EXISTS
    }

private:
    state_t state_ = state_t::needs_start;
    std::vector<column_meta> columns_;
    std::uint64_t affected_rows_  = 0;
    std::uint64_t last_insert_id_ = 0;
    std::uint16_t warning_count_  = 0;
    std::uint16_t status_flags_   = 0;
    std::uint16_t error_code_     = 0;
    std::string   info_;
    std::string   error_msg_;
};

// =============================================================================
// metadata_mode — 元数据保留策略（参考 Boost.MySQL metadata_mode）
// =============================================================================

export enum class metadata_mode : std::uint8_t {
    minimal,   ///< 仅保留必要元数据（性能优先，部分字段为空）
    full,      ///< 保留完整元数据（所有 column_meta 字段可用）
};

// =============================================================================
// 连接选项
// =============================================================================

export struct connect_options {
    std::string   host     = "*********";
    std::uint16_t port     = 3306;
    std::string   username = "root";
    std::string   password;
    std::string   database;
    std::string   charset  = "utf8mb4";

    // TLS 配置
    ssl_mode    ssl          = ssl_mode::enable;  // disable / enable / require
    bool        tls_verify   = true;
    std::string tls_ca_file;
    std::string tls_cert_file;
    std::string tls_key_file;

    // 高级
    bool multi_statements = false;
    metadata_mode meta_mode = metadata_mode::full;

    // 缓冲区
    std::size_t initial_buffer_size = 8192;
};

} // namespace cnetmod::mysql
