export module cnetmod.orm.sql_dialect;

import std;

export namespace cnetmod::orm {

// =============================================================================
// SQL dialect enumeration
// =============================================================================

enum class sql_dialect
{
    mysql,
    postgresql,
};

// =============================================================================
// Dialect configuration
// =============================================================================

struct dialect_config
{
    sql_dialect dialect = sql_dialect::mysql;
    char identifier_quote = '`';           // MySQL: `, PostgreSQL: "
    bool supports_returning = false;       // PostgreSQL supports RETURNING clause
    bool std_boolean_literals = false;     // PostgreSQL: TRUE/FALSE vs MySQL: 1/0
};

// =============================================================================
// Factory
// =============================================================================

inline auto get_dialect_config(sql_dialect dialect) -> dialect_config
{
    switch (dialect)
    {
    case sql_dialect::postgresql:
        return {
            .dialect = sql_dialect::postgresql,
            .identifier_quote = '"',
            .supports_returning = true,
            .std_boolean_literals = true,
        };
    case sql_dialect::mysql:
    default:
        return {
            .dialect = sql_dialect::mysql,
            .identifier_quote = '`',
            .supports_returning = false,
            .std_boolean_literals = false,
        };
    }
}

// =============================================================================
// Dialect-aware SQL building helpers
// =============================================================================

/// Quote an identifier (table / column name) with the dialect's quote character.
/// Handles embedded quote characters by doubling them.
inline auto quote_identifier(std::string_view name, const dialect_config& cfg) -> std::string
{
    const char q = cfg.identifier_quote;
    std::string result;
    result.reserve(name.size() + 2);
    result.push_back(q);
    for (char c : name)
    {
        if (c == q)
        {
            result.push_back(q);
            result.push_back(q);
        }
        else
        {
            result.push_back(c);
        }
    }
    result.push_back(q);
    return result;
}

/// Generate a positional parameter placeholder.
///   MySQL:      `{}`   (consumed later by format_sql)
///   PostgreSQL: `$N`   (1-based positional, consumed by libpq)
inline auto make_placeholder(int param_index, const dialect_config& cfg) -> std::string
{
    if (cfg.dialect == sql_dialect::postgresql)
        return std::format("${}", param_index);
    return "{}";
}

/// Boolean literal string for IS TRUE / IS FALSE checks.
inline auto boolean_literal(bool value, const dialect_config& cfg) -> std::string_view
{
    if (cfg.std_boolean_literals)
        return value ? "TRUE" : "FALSE";
    return value ? "1" : "0";
}

} // namespace cnetmod::orm
