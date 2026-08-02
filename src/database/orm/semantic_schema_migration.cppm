export module cnetmod.orm.semantic_schema_migration;

import std;

export namespace cnetmod::orm {

enum class relational_dialect : std::uint8_t
{
    mysql,
    postgresql
};
enum class relational_column_type : std::uint8_t
{
    boolean,
    int32,
    int64,
    decimal,
    varchar,
    text,
    uuid,
    timestamp,
    json,
    binary
};
enum class referential_action : std::uint8_t
{
    restrict_,
    cascade,
    set_null,
    no_action
};

struct current_timestamp_default
{
};

using schema_literal = std::variant<bool, std::int64_t, std::string>;
using column_default = std::variant<current_timestamp_default, schema_literal>;
enum class check_comparison : std::uint8_t
{
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal
};

struct column_literal_predicate
{
    std::string column;
    check_comparison comparison{};
    schema_literal value;
};

struct column_set_predicate
{
    std::string column;
    std::vector<schema_literal> values;
};

struct column_like_predicate
{
    std::string column;
    std::string pattern;
};

struct column_comparison_predicate
{
    std::string left_column;
    check_comparison comparison{};
    std::string right_column;
};

using check_predicate = std::variant<column_literal_predicate, column_set_predicate, column_like_predicate, column_comparison_predicate>;

struct semantic_version
{
    std::uint32_t major{}, minor{}, patch{};
    [[nodiscard]] auto valid() const noexcept -> bool;
    [[nodiscard]] auto to_string() const -> std::string;
};

struct column_definition
{
    std::string name;
    relational_column_type type{};
    std::optional<std::uint32_t> length;
    bool nullable{true};
    bool primary_key{};
    bool auto_generated{};
    std::optional<column_default> default_value;
};

struct index_definition
{
    std::string name;
    std::vector<std::string> columns;
    bool unique{};
};

struct unique_constraint_definition
{
    std::string name;
    std::vector<std::string> columns;
};

struct foreign_key_definition
{
    std::string name;
    std::vector<std::string> columns;
    std::string referenced_table;
    std::vector<std::string> referenced_columns;
    referential_action on_delete{referential_action::restrict_};
};

struct check_constraint_definition
{
    std::string name;
    check_predicate predicate;
};

struct table_definition
{
    std::string name;
    std::vector<column_definition> columns;
    std::vector<index_definition> indexes;
    std::vector<unique_constraint_definition> unique_constraints;
    std::vector<foreign_key_definition> foreign_keys;
    std::vector<check_constraint_definition> checks;
};

struct create_table_operation
{
    table_definition table;
};

struct add_column_operation
{
    std::string table;
    column_definition column;
};

struct add_index_operation
{
    std::string table;
    index_definition index;
};

struct add_unique_constraint_operation
{
    std::string table;
    unique_constraint_definition constraint;
};

struct add_foreign_key_operation
{
    std::string table;
    foreign_key_definition constraint;
};

struct add_check_constraint_operation
{
    std::string table;
    check_constraint_definition constraint;
};

using schema_operation = std::variant<create_table_operation, add_column_operation, add_index_operation, add_unique_constraint_operation, add_foreign_key_operation, add_check_constraint_operation>;

struct schema_migration
{
    semantic_version version;
    std::string name;
    std::vector<schema_operation> operations;
};

struct schema_render_error
{
    std::string message;
};

auto render_schema_migration(const schema_migration&, relational_dialect) -> std::expected<std::vector<std::string>, schema_render_error>;

// Driver-neutral migration history. A database adapter owns persistence of this
// ledger; planning deliberately performs no I/O and cannot execute DDL.
struct schema_migration_checksum
{
    std::array<std::byte, 32> bytes{};
    [[nodiscard]] auto to_hex() const -> std::string;
    auto operator<=>(const schema_migration_checksum&) const = default;
};

struct applied_schema_migration
{
    semantic_version version;
    std::string name;
    schema_migration_checksum checksum;
};

enum class schema_migration_history_error_code : std::uint8_t
{
    invalid_declared_migration,
    duplicate_declared_version,
    duplicate_applied_version,
    unknown_applied_version,
    checksum_mismatch,
    name_mismatch,
    rollback_detected
};

struct schema_migration_history_error
{
    schema_migration_history_error_code code{};
    std::string message;
};

struct forward_schema_migration_plan
{
    std::vector<const schema_migration*> pending;
};

// SHA-256 of an unambiguous migration serialization. It never uses container
// addresses or std::hash, so its value is reproducible across drivers/builds.
[[nodiscard]] auto canonical_schema_migration_checksum(const schema_migration&)
    -> std::expected<schema_migration_checksum, schema_migration_history_error>;

// Validates an existing ledger against the declared migration set, rejects
// unknown/tampered history and generates only forward migrations. Adapters
// execute each pending migration and atomically append its ledger record.
[[nodiscard]] auto plan_forward_schema_migrations(
    std::span<const schema_migration> declared,
    std::span<const applied_schema_migration> applied)
    -> std::expected<forward_schema_migration_plan, schema_migration_history_error>;

} // namespace cnetmod::orm
