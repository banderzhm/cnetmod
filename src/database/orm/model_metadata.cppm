export module cnetmod.orm.model_metadata;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.database.datetime;
import cnetmod.orm.id_generation;

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
    [[nodiscard]] auto is_pk() const noexcept -> bool;
    [[nodiscard]] auto is_auto() const noexcept -> bool;
    [[nodiscard]] auto is_nullable() const noexcept -> bool;
    [[nodiscard]] auto is_unique() const noexcept -> bool;
    [[nodiscard]] auto is_uuid() const noexcept -> bool;
    [[nodiscard]] auto is_snowflake() const noexcept -> bool;
};

template <class T> using field_setter = void (*)(T&, const field_value&);
template <class T> using field_getter = param_value (*)(const T&);
namespace detail {

using record_json_scalar = std::variant<std::monostate, std::nullptr_t, bool,
    std::int64_t, std::uint64_t, double, std::string_view, std::string>;
using record_json_setter = std::expected<void, std::error_code> (*)(
    void*, std::size_t, const record_json_scalar*);
using record_json_getter = record_json_scalar (*)(const void*, std::size_t);

[[nodiscard]] auto read_record_json(std::string_view input,
    std::span<const std::string_view> names, bool reject_unknown,
    void* context, record_json_setter assign) -> std::expected<void, std::error_code>;
[[nodiscard]] auto write_record_json(std::span<const std::string_view> names,
    bool emit_nulls, const void* context, record_json_getter get)
    -> std::expected<std::string, std::error_code>;

} // namespace detail

template <class T> using json_field_setter = std::expected<void, std::error_code> (*)(
    T&, const detail::record_json_scalar&);
template <class T> using json_field_getter = detail::record_json_scalar (*)(const T&);

namespace detail {

    // Recover the registered type from the injected trait name inside macros.
    template <template <class> class Traits, class T>
    auto registered_record_type(const Traits<T>*) -> T;

    template <class Pointer>
    inline constexpr std::byte member_pointer_type_tag{};

    template <class Pointer>
    [[nodiscard]] constexpr auto member_pointer_type() noexcept -> const void*
    {
        return std::addressof(member_pointer_type_tag<Pointer>);
    }

    template <auto Expected>
    [[nodiscard]] auto matches_member_pointer(const void* candidate) noexcept -> bool
    {
        using pointer_type = decltype(Expected);
        return *static_cast<const pointer_type*>(candidate) == Expected;
    }

} // namespace detail

template <class T> struct field_mapping
{
    column_def col;
    field_setter<T> setter;
    field_getter<T> getter;
    json_field_setter<T> json_setter;
    json_field_getter<T> json_getter;
    const void* member_pointer_type = nullptr;
    bool (*member_pointer_matches)(const void*) noexcept = nullptr;

    template <class U>
    [[nodiscard]] auto matches(U T::* candidate) const noexcept -> bool
    {
        using pointer_type = U T::*;
        return member_pointer_matches != nullptr &&
            member_pointer_type == detail::member_pointer_type<pointer_type>() &&
            member_pointer_matches(std::addressof(candidate));
    }
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
            if (!field.col.is_pk() &&
                !has_flag(field.col.flags, col_flag::tenant_id) &&
                !(has_flag(field.col.flags, col_flag::fill_insert) &&
                    !has_flag(field.col.flags, col_flag::fill_insert_update)))
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

namespace detail {

template <ResultRecord T>
struct record_json_layout
{
    std::vector<const field_mapping<T>*> fields;
    std::vector<std::string_view> names;
};

template <ResultRecord T>
[[nodiscard]] auto json_layout() -> const record_json_layout<T>&
{
    static const auto layout = [] {
        record_json_layout<T> result;
        const auto fields = result_fields<T>();
        result.fields.reserve(fields.size());
        for (const auto& field : fields)
            result.fields.push_back(&field);
        std::ranges::stable_sort(result.fields, {}, [](const auto* field) {
            return field->col.field_name;
        });
        result.names.reserve(fields.size());
        for (const auto* field : result.fields)
            result.names.push_back(field->col.field_name);
        return result;
    }();
    return layout;
}

} // namespace detail

/// Serialize a registered model or projection using its member names.
template <ResultRecord T>
[[nodiscard]] auto write_json(const T& value, bool emit_nulls = false)
    -> std::expected<std::string, std::error_code>
{
    const auto& layout = detail::json_layout<T>();
    struct write_context
    {
        const T& record;
        const detail::record_json_layout<T>& layout;
    };
    const write_context context{value, layout};
    const auto get = [](const void* state, std::size_t index) {
        const auto& typed = *static_cast<const write_context*>(state);
        return typed.layout.fields[index]->json_getter(typed.record);
    };
    return detail::write_record_json(layout.names, emit_nulls, &context, get);
}

/// Parse a record, requiring all non-nullable members and checking scalar types.
template <ResultRecord T>
[[nodiscard]] auto parse_json(std::string_view input, bool reject_unknown = true)
    -> std::expected<T, std::error_code>
{
    const auto& layout = detail::json_layout<T>();
    struct read_context
    {
        T record{};
        const detail::record_json_layout<T>& layout;
    };
    read_context context{.layout = layout};
    const auto assign = [](void* state, std::size_t index, const detail::record_json_scalar* value)
        -> std::expected<void, std::error_code> {
        auto& typed = *static_cast<read_context*>(state);
        const auto& field = *typed.layout.fields[index];
        if (value)
            return field.json_setter(typed.record, *value);
        if (!field.col.is_nullable())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return {};
    };
    if (auto read = detail::read_record_json(input, layout.names, reject_unknown, &context, assign);
        !read)
        return std::unexpected(read.error());
    return std::move(context.record);
}

} // namespace cnetmod::orm

export namespace cnetmod::orm::detail {

[[nodiscard]] auto decode_json_member(calendar_date&, const record_json_scalar&)
    -> std::expected<void, std::error_code>;
[[nodiscard]] auto decode_json_member(calendar_datetime&, const record_json_scalar&)
    -> std::expected<void, std::error_code>;
[[nodiscard]] auto decode_json_member(clock_time&, const record_json_scalar&)
    -> std::expected<void, std::error_code>;
[[nodiscard]] auto decode_json_member(uuid&, const record_json_scalar&)
    -> std::expected<void, std::error_code>;

template <typename Member>
[[nodiscard]] auto encode_json_member(const Member& value) -> record_json_scalar
{
    if constexpr (std::same_as<Member, bool>)
        return value;
    else if constexpr (std::same_as<Member, std::string>)
        return std::string_view{value};
    else if constexpr (std::is_enum_v<Member>)
        return encode_json_member(static_cast<std::underlying_type_t<Member>>(value));
    else if constexpr (std::unsigned_integral<Member>)
        return static_cast<std::uint64_t>(value);
    else if constexpr (std::signed_integral<Member>)
        return static_cast<std::int64_t>(value);
    else if constexpr (std::floating_point<Member>)
        return static_cast<double>(value);
    else if constexpr (requires { value.to_string(); })
        return value.to_string();
    else
        return value ? encode_json_member(*value) : record_json_scalar{nullptr};
}

template <typename Member>
[[nodiscard]] auto decode_json_member(Member& member, const record_json_scalar& source)
    -> std::expected<void, std::error_code>
{
    if constexpr (std::same_as<Member, bool> || std::same_as<Member, std::string>)
    {
        using scalar_type = std::conditional_t<std::same_as<Member, bool>, bool, std::string_view>;
        if (const auto* value = std::get_if<scalar_type>(&source))
        {
            member = *value;
            return {};
        }
    }
    else if constexpr (std::is_enum_v<Member>)
    {
        std::underlying_type_t<Member> value;
        if (auto decoded = decode_json_member(value, source); !decoded)
            return decoded;
        member = static_cast<Member>(value);
        return {};
    }
    else if constexpr (std::integral<Member>)
    {
        using integer_type = std::conditional_t<std::is_signed_v<Member>,
            std::int64_t, std::uint64_t>;
        const auto assign = [&](const auto* value) {
            if (!value || !std::in_range<integer_type>(*value))
                return false;
            const auto number = static_cast<integer_type>(*value);
            if (number < static_cast<integer_type>(std::numeric_limits<Member>::min()) ||
                number > static_cast<integer_type>(std::numeric_limits<Member>::max()))
                return false;
            member = static_cast<Member>(number);
            return true;
        };
        if (assign(std::get_if<std::int64_t>(&source)) ||
            assign(std::get_if<std::uint64_t>(&source)))
            return {};
    }
    else if constexpr (std::floating_point<Member>)
    {
        if (const auto* value = std::get_if<double>(&source))
        {
            if (*value <= std::numeric_limits<Member>::max() &&
                *value >= -std::numeric_limits<Member>::max())
            {
                member = static_cast<Member>(*value);
                return {};
            }
        }
        if (const auto* value = std::get_if<std::int64_t>(&source))
        {
            member = static_cast<Member>(*value);
            return {};
        }
        if (const auto* value = std::get_if<std::uint64_t>(&source))
        {
            member = static_cast<Member>(*value);
            return {};
        }
    }
    else
    {
        if (std::holds_alternative<std::nullptr_t>(source))
        {
            member.reset();
            return {};
        }
        typename Member::value_type value{};
        if (auto decoded = decode_json_member(value, source); !decoded)
            return decoded;
        member = std::move(value);
        return {};
    }
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
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
template <class T, auto Accessor>
[[nodiscard]] constexpr auto make_field_mapping(column_def column) -> field_mapping<T>
{
    field_mapping<T> mapping{
        .col = column,
        .setter = [](T& record, const field_value& value) {
            set_member(std::invoke(Accessor, record), value);
        },
        .getter = [](const T& record) {
            return get_member(std::invoke(Accessor, record));
        },
        .json_setter = [](T& record, const record_json_scalar& value) {
            return decode_json_member(std::invoke(Accessor, record), value);
        },
        .json_getter = [](const T& record) {
            return encode_json_member(std::invoke(Accessor, record));
        },
    };
    if constexpr (std::is_member_object_pointer_v<decltype(Accessor)>)
    {
        mapping.member_pointer_type = member_pointer_type<decltype(Accessor)>();
        mapping.member_pointer_matches = &matches_member_pointer<Accessor>;
    }
    return mapping;
}

} // namespace cnetmod::orm::detail
