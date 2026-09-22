export module cnetmod.orm.model_metadata;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.database.datetime;
import cnetmod.orm.id_generation;
export import cnetmod.json;

export namespace cnetmod::orm {

enum class col_flag : std::uint16_t
{
    none = 0,
    primary_key = 1 << 0,
    auto_increment = 1 << 1,
    nullable = 1 << 2,
    version = 1 << 3,
    logic_delete = 1 << 4,
    fill_insert = 1 << 5,
    fill_insert_update = 1 << 6,
    tenant_id = 1 << 7,
    unique = 1 << 8,
    data_partition = 1 << 9,
    data_owner = 1 << 10,
};

// Kept constexpr because downstream model declarations form compile-time flag
// constants.
[[nodiscard]] constexpr auto operator|(col_flag left, col_flag right) noexcept
    -> col_flag
{
    return static_cast<col_flag>(static_cast<std::uint16_t>(left) |
        static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr auto operator&(col_flag left, col_flag right) noexcept
    -> col_flag
{
    return static_cast<col_flag>(static_cast<std::uint16_t>(left) &
        static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr auto has_flag(col_flag flags, col_flag flag) noexcept
    -> bool
{
    return static_cast<std::uint16_t>(flags & flag) != 0;
}

struct column_def
{
    std::string_view field_name;
    std::string_view column_name;
    column_type type;
    col_flag flags = col_flag::none;
    id_strategy strategy = id_strategy::none;
    std::size_t member_offset = 0;
    [[nodiscard]] auto is_pk() const noexcept -> bool;
    [[nodiscard]] auto is_auto() const noexcept -> bool;
    [[nodiscard]] auto is_nullable() const noexcept -> bool;
    [[nodiscard]] auto is_unique() const noexcept -> bool;
    [[nodiscard]] auto is_uuid() const noexcept -> bool;
    [[nodiscard]] auto is_snowflake() const noexcept -> bool;
};

template <class T> using field_setter = void (*)(T&, const field_value&);
template <class T> using field_getter = param_value (*)(const T&);
template <class T> using json_field_setter = std::expected<void, std::error_code> (*)(
    T&, const cnetmod::json::document&);
template <class T> using json_field_getter = std::expected<cnetmod::json::document,
    std::error_code> (*)(const T&);

template <class T> struct field_mapping
{
    column_def col;
    field_setter<T> setter;
    field_getter<T> getter;
    json_field_setter<T> json_setter;
    json_field_getter<T> json_getter;
};

template <class T> struct table_meta
{
    std::string_view table_name;
    std::span<const field_mapping<T>> fields;

    [[nodiscard]] auto pk() const noexcept -> const field_mapping<T>*
    {
        for (auto& field : fields)
            if (field.col.is_pk())
                return &field;
        return nullptr;
    }

    [[nodiscard]] auto find_column(std::string_view name) const noexcept
        -> const field_mapping<T>*
    {
        for (auto& field : fields)
            if (field.col.column_name == name)
                return &field;
        return nullptr;
    }

    [[nodiscard]] auto insertable_fields() const
        -> std::vector<const field_mapping<T>*>
    {
        std::vector<const field_mapping<T>*> result;
        for (auto& field : fields)
            if (!field.col.is_auto())
                result.push_back(&field);
        return result;
    }

    [[nodiscard]] auto updatable_fields() const
        -> std::vector<const field_mapping<T>*>
    {
        std::vector<const field_mapping<T>*> result;
        for (auto& field : fields)
            if (!field.col.is_pk())
                result.push_back(&field);
        return result;
    }
};
template <class T> struct model_traits;
template <class T>
concept Model = requires {
    { model_traits<T>::meta() } -> std::same_as<const table_meta<T>&>;
};

/**
 * @brief Describes a read-only result projection without database table identity.
 */
template <class T> struct projection_meta
{
    std::span<const field_mapping<T>> fields;

    [[nodiscard]] auto find_column(std::string_view name) const noexcept
        -> const field_mapping<T>*
    {
        for (const auto& field : fields)
            if (field.col.column_name == name)
                return &field;
        return nullptr;
    }
};

template <class T> struct projection_traits;
template <class T>
concept Projection = requires {
    { projection_traits<T>::meta() } -> std::same_as<const projection_meta<T>&>;
};

/**
 * @brief A type that can receive database result columns.
 */
template <class T>
concept ResultRecord = Model<T> || Projection<T>;

/**
 * @brief Returns the field metadata used to materialize a result record.
 */
template <ResultRecord T>
[[nodiscard]] auto result_fields() noexcept
    -> std::span<const field_mapping<T>>
{
    if constexpr (Model<T>)
        return model_traits<T>::meta().fields;
    else
        return projection_traits<T>::meta().fields;
}

/**
 * @brief Finds a result field by its declared database column name.
 */
template <ResultRecord T>
[[nodiscard]] auto find_result_column(std::string_view name) noexcept
    -> const field_mapping<T>*
{
    if constexpr (Model<T>)
        return model_traits<T>::meta().find_column(name);
    else
        return projection_traits<T>::meta().find_column(name);
}

[[nodiscard]] auto sql_type_str(column_type type) noexcept -> std::string_view;

/**
 * @brief Serializes a registered ORM model or projection with its field metadata.
 */
template <ResultRecord T>
[[nodiscard]] auto record_to_document(const T& value, bool emit_nulls)
    -> std::expected<cnetmod::json::document, std::error_code>
{
    auto result = cnetmod::json::object();
    for (const auto& field : result_fields<T>())
    {
        auto encoded = field.json_getter(value);
        if (!encoded)
            return std::unexpected(encoded.error());
        if (!emit_nulls && encoded->is_null())
            continue;
        result[field.col.field_name] = std::move(*encoded);
    }
    return result;
}

/**
 * @brief Deserializes a document with the same metadata used by ORM mapping.
 */
template <ResultRecord T>
[[nodiscard]] auto record_from_document(
    const cnetmod::json::document& source, bool reject_unknown)
    -> std::expected<T, std::error_code>
{
    if (!source.is_object())
        return std::unexpected(
            cnetmod::json::make_error_code(cnetmod::json::errc::type_mismatch));
    T result{};
    std::size_t consumed{};
    for (const auto& field : result_fields<T>())
    {
        const auto* found = cnetmod::json::find(source, field.col.field_name);
        if (found == nullptr)
        {
            if (!field.col.is_nullable())
                return std::unexpected(cnetmod::json::make_error_code(
                    cnetmod::json::errc::missing_field));
            continue;
        }
        ++consumed;
        auto assigned = field.json_setter(result, *found);
        if (!assigned)
            return std::unexpected(assigned.error());
    }
    if (reject_unknown && consumed != source.size())
        return std::unexpected(
            cnetmod::json::make_error_code(cnetmod::json::errc::unknown_field));
    return result;
}

} // namespace cnetmod::orm

export namespace cnetmod::orm::detail {
[[nodiscard]] auto encode_json_member(const calendar_date& value)
    -> std::expected<cnetmod::json::document, std::error_code>;
[[nodiscard]] auto encode_json_member(const calendar_datetime& value)
    -> std::expected<cnetmod::json::document, std::error_code>;
[[nodiscard]] auto encode_json_member(const clock_time& value)
    -> std::expected<cnetmod::json::document, std::error_code>;
[[nodiscard]] auto encode_json_member(
    const std::optional<calendar_datetime>& value)
    -> std::expected<cnetmod::json::document, std::error_code>;
[[nodiscard]] auto encode_json_member(const uuid& value)
    -> std::expected<cnetmod::json::document, std::error_code>;

[[nodiscard]] auto decode_json_member(
    calendar_date& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>;
[[nodiscard]] auto decode_json_member(
    calendar_datetime& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>;
[[nodiscard]] auto decode_json_member(
    clock_time& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>;
[[nodiscard]] auto decode_json_member(std::optional<calendar_datetime>& member,
    const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>;
[[nodiscard]] auto decode_json_member(
    uuid& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>;

template <typename Member>
[[nodiscard]] auto encode_json_member(const Member& value)
    -> std::expected<cnetmod::json::document, std::error_code>
{
    return cnetmod::json::to_document(value);
}

template <typename Member>
[[nodiscard]] auto decode_json_member(
    Member& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>
{
    auto decoded = cnetmod::json::from_document<Member>(source);
    if (!decoded)
        return std::unexpected(decoded.error());
    member = std::move(*decoded);
    return {};
}

void set_member(std::int64_t&, const field_value&);
void set_member(std::uint64_t&, const field_value&);
void set_member(int&, const field_value&);
void set_member(std::uint32_t&, const field_value&);
void set_member(float&, const field_value&);
void set_member(double&, const field_value&);
void set_member(std::string&, const field_value&);
void set_member(bool&, const field_value&);
void set_member(calendar_date&, const field_value&);
void set_member(calendar_datetime&, const field_value&);
void set_member(clock_time&, const field_value&);
void set_member(std::optional<std::string>&, const field_value&);
void set_member(std::optional<std::int64_t>&, const field_value&);
void set_member(std::optional<double>&, const field_value&);
void set_member(std::optional<calendar_datetime>&, const field_value&);
void set_member(uuid&, const field_value&);
[[nodiscard]] auto get_member(std::int64_t) -> param_value;
[[nodiscard]] auto get_member(std::uint64_t) -> param_value;
[[nodiscard]] auto get_member(int) -> param_value;
[[nodiscard]] auto get_member(std::uint32_t) -> param_value;
[[nodiscard]] auto get_member(float) -> param_value;
[[nodiscard]] auto get_member(double) -> param_value;
[[nodiscard]] auto get_member(const std::string&) -> param_value;
[[nodiscard]] auto get_member(std::string_view) -> param_value;
[[nodiscard]] auto get_member(bool) -> param_value;
[[nodiscard]] auto get_member(const calendar_date&) -> param_value;
[[nodiscard]] auto get_member(const calendar_datetime&) -> param_value;
[[nodiscard]] auto get_member(const clock_time&) -> param_value;
[[nodiscard]] auto get_member(const std::optional<std::string>&)
    -> param_value;
[[nodiscard]] auto get_member(const std::optional<std::int64_t>&)
    -> param_value;
[[nodiscard]] auto get_member(const std::optional<double>&) -> param_value;
[[nodiscard]] auto get_member(const std::optional<calendar_datetime>&)
    -> param_value;
[[nodiscard]] auto get_member(const uuid&) -> param_value;

template <typename E>
requires std::is_enum_v<E>
inline void set_member(E& member, const field_value& value)
{
    if (value.is_int64())
        member = static_cast<E>(value.get_int64());
    else if (value.is_uint64())
        member = static_cast<E>(value.get_uint64());
}

template <typename E>
requires std::is_enum_v<E>
[[nodiscard]] inline auto get_member(E value) -> param_value
{
    return param_value::from_int(static_cast<std::int64_t>(value));
}

template <typename T>
requires std::same_as<T, std::time_t> &&
    (!std::same_as<std::time_t, std::int64_t>)
inline void set_member(T& member, const field_value& value)
{
    if (value.is_int64())
        member = static_cast<std::time_t>(value.get_int64());
    else if (value.is_uint64())
        member = static_cast<std::time_t>(value.get_uint64());
    else if (value.is_datetime())
    {
        if (const auto seconds =
                database::unix_seconds_from_datetime(value.get_datetime()))
            member = static_cast<std::time_t>(*seconds);
    }
}

template <typename T>
requires std::same_as<T, std::time_t> &&
    (!std::same_as<std::time_t, std::int64_t>)
[[nodiscard]] inline auto get_member(T value) -> param_value
{
    return param_value::from_int(static_cast<std::int64_t>(value));
}
} // namespace cnetmod::orm::detail

export namespace cnetmod::json::detail {

/**
 * @brief Maps every registered ORM record through its declared field metadata.
 */
template <cnetmod::orm::ResultRecord T>
struct document_codec<T>
{
    [[nodiscard]] static auto encode(const T& value, bool emit_nulls)
        -> std::expected<document, std::error_code>
    {
        return cnetmod::orm::record_to_document(value, emit_nulls);
    }

    [[nodiscard]] static auto decode(
        const document& source, bool reject_unknown)
        -> std::expected<T, std::error_code>
    {
        return cnetmod::orm::record_from_document<T>(source, reject_unknown);
    }
};

} // namespace cnetmod::json::detail
