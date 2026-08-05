export module cnetmod.orm.result_mapper;

import std;
import nlohmann.json;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.model_metadata;

namespace cnetmod::orm {

namespace detail {

inline auto json_from_param(const param_value& value) -> nlohmann::json
{
    using kind = param_value::kind_t;
    switch (value.kind)
    {
    case kind::null_kind: return nullptr;
    case kind::int64_kind: return value.int_val;
    case kind::uint64_kind: return value.uint_val;
    case kind::double_kind: return value.double_val;
    case kind::string_kind:
    case kind::blob_kind: return value.str_val;
    case kind::date_kind: return value.date_val.to_string();
    case kind::datetime_kind: return value.datetime_val.to_string();
    case kind::time_kind: return value.time_val.to_string();
    }
    return nullptr;
}

inline auto field_from_json(const nlohmann::json& value, column_type type)
    -> std::expected<field_value, std::string>
{
    if (value.is_null())
        return field_value::null();
    try
    {
        switch (type)
        {
        case column_type::tinyint:
        case column_type::smallint:
        case column_type::mediumint:
        case column_type::int_:
        case column_type::bigint:
        case column_type::year:
            return field_value::from_int64(value.get<std::int64_t>());
        case column_type::float_:
            return field_value::from_float(value.get<float>());
        case column_type::double_:
        case column_type::decimal:
            return field_value::from_double(value.get<double>());
        case column_type::boolean:
        case column_type::bit:
            return field_value::from_int64(value.get<bool>() ? 1 : 0);
        case column_type::char_:
        case column_type::varchar:
        case column_type::text:
        case column_type::json:
        case column_type::enum_:
        case column_type::set:
        case column_type::uuid:
            return field_value::from_string(value.get<std::string>());
        default:
            return std::unexpected("orm::from_json does not support this column type yet");
        }
    }
    catch (const nlohmann::json::exception& error)
    {
        return std::unexpected(error.what());
    }
}

} // namespace detail

// Convert an ORM model through CNETMOD_MODEL metadata. No nlohmann macro or
// per-model serializer is necessary, so consumers only need `import
// nlohmann.json; import cnetmod.orm;`.
export template <Model T>
auto to_json(const T& model) -> nlohmann::json
{
    nlohmann::json result = nlohmann::json::object();
    for (const auto& field : model_traits<T>::meta().fields)
        if (field.getter)
            result[std::string(field.col.field_name)] =
                detail::json_from_param(field.getter(model));
    return result;
}

// Populate a model through the same metadata. Missing fields retain their
// default value, which makes this suitable for forward-compatible cache data.
export template <Model T>
auto from_json(const nlohmann::json& source) -> std::expected<T, std::string>
{
    if (!source.is_object())
        return std::unexpected("orm::from_json requires a JSON object");
    T model{};
    for (const auto& field : model_traits<T>::meta().fields)
    {
        const auto it = source.find(std::string(field.col.field_name));
        if (it == source.end())
            continue;
        auto value = detail::field_from_json(*it, field.col.type);
        if (!value)
            return std::unexpected(std::format("field '{}': {}",
                field.col.field_name, value.error()));
        if (field.setter)
            field.setter(model, *value);
    }
    return model;
}

// =============================================================================
// from_row — result_set row → model object
// =============================================================================

/// Construct model T from row + columns
/// Match by column name (not dependent on column order)
export template <Model T>
auto from_row(const row& r, const std::vector<column_meta>& columns) -> T
{
    T obj{};
    auto& meta = model_traits<T>::meta();

    for (std::size_t i = 0; i < columns.size() && i < r.size(); ++i)
    {
        auto* fm = meta.find_column(columns[i].name);
        if (fm && fm->setter)
            fm->setter(obj, r[i]);
    }
    return obj;
}

/// Construct model list from result_set
export template <Model T>
auto from_result_set(const result_set& rs) -> std::vector<T>
{
    std::vector<T> result;
    result.reserve(rs.rows.size());
    for (auto& r : rs.rows)
        result.push_back(from_row<T>(r, rs.columns));
    return result;
}

// =============================================================================
// Tuple support — result_set row → std::tuple
// =============================================================================

namespace detail {
    auto parse_double_sv(std::string_view input) -> double;
    auto field_to_double(const field_value& field) -> double;
    auto field_to_float(const field_value& field) -> float;

} // namespace detail

/// Helper: Convert field_value to tuple element type
template <typename T> auto field_to_tuple_element(const field_value& fv) -> T
{
    if constexpr (std::is_same_v<T, std::int64_t>)
    {
        return fv.is_null() ? 0 : fv.as_int64();
    }
    else if constexpr (std::is_same_v<T, std::uint64_t>)
    {
        return fv.is_null() ? 0 : fv.as_uint64();
    }
    else if constexpr (std::is_same_v<T, std::int32_t>)
    {
        return fv.is_null() ? 0 : static_cast<std::int32_t>(fv.as_int64());
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        return detail::field_to_double(fv);
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        // SUM/AVG over DECIMAL may come back as string.
        return detail::field_to_float(fv);
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        return fv.is_null() ? false : (fv.as_int64() != 0);
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        return fv.is_null() ? std::string{} : std::string(fv.as_string());
    }
    else if constexpr (std::is_same_v<T, std::optional<std::int64_t>>)
    {
        return fv.is_null() ? std::nullopt
                            : std::optional<std::int64_t>(fv.as_int64());
    }
    else if constexpr (std::is_same_v<T, std::optional<std::uint64_t>>)
    {
        return fv.is_null() ? std::nullopt
                            : std::optional<std::uint64_t>(fv.as_uint64());
    }
    else if constexpr (std::is_same_v<T, std::optional<double>>)
    {
        return fv.is_null() ? std::nullopt
                            : std::optional<double>(detail::field_to_double(fv));
    }
    else if constexpr (std::is_same_v<T, std::optional<float>>)
    {
        return fv.is_null() ? std::nullopt
                            : std::optional<float>(detail::field_to_float(fv));
    }
    else if constexpr (std::is_same_v<T, std::optional<std::string>>)
    {
        return fv.is_null()
            ? std::nullopt
            : std::optional<std::string>(std::string(fv.as_string()));
    }
    else
    {
        static_assert(std::is_same_v<T, void>, "Unsupported tuple element type");
        return T{};
    }
}

/// Helper: Convert row to tuple (index sequence version)
template <typename... Ts, std::size_t... Is>
auto from_row_to_tuple_impl(const row& r, std::index_sequence<Is...>)
    -> std::tuple<Ts...>
{
    return std::make_tuple(field_to_tuple_element<Ts>(r[Is])...);
}

/// Construct tuple from row
export template <typename... Ts>
auto from_row_to_tuple(const row& r) -> std::tuple<Ts...>
{
    if (r.size() < sizeof...(Ts))
    {
        throw std::runtime_error("Row has fewer columns than tuple elements");
    }
    return from_row_to_tuple_impl<Ts...>(r, std::index_sequence_for<Ts...>{});
}

/// Construct tuple list from result_set
export template <typename... Ts>
auto from_result_set_to_tuple(const result_set& rs)
    -> std::vector<std::tuple<Ts...>>
{
    std::vector<std::tuple<Ts...>> result;
    result.reserve(rs.rows.size());
    for (auto& r : rs.rows)
        result.push_back(from_row_to_tuple<Ts...>(r));
    return result;
}

// =============================================================================
// to_params — Model object → param_value list
// =============================================================================

/// Extract param_value for all insertable fields (skip auto_increment)
export template <Model T>
auto to_insert_params(const T& model) -> std::vector<param_value>
{
    auto& meta = model_traits<T>::meta();
    std::vector<param_value> params;
    for (auto& f : meta.fields)
    {
        if (!f.col.is_auto() && f.getter)
            params.push_back(f.getter(model));
    }
    return params;
}

/// Extract all updatable field param_value (skip PK) + append PK value at end
export template <Model T>
auto to_update_params(const T& model) -> std::vector<param_value>
{
    auto& meta = model_traits<T>::meta();
    std::vector<param_value> params;

    // SET part
    for (auto& f : meta.fields)
    {
        if (!f.col.is_pk() && f.getter)
            params.push_back(f.getter(model));
    }
    // WHERE pk = ?
    auto* pk = meta.pk();
    if (pk && pk->getter)
        params.push_back(pk->getter(model));
    return params;
}

/// Extract PK value
export template <Model T> auto to_pk_param(const T& model) -> param_value
{
    auto& meta = model_traits<T>::meta();
    auto* pk = meta.pk();
    if (pk && pk->getter)
        return pk->getter(model);
    return param_value::null();
}

/// Fill last_insert_id back to model PK
export template <Model T> void fill_insert_id(T& model, std::uint64_t last_id)
{
    auto& meta = model_traits<T>::meta();
    auto* pk = meta.pk();
    if (pk && pk->col.is_auto() && pk->setter)
    {
        field_value fv;
        fv = field_value::from_uint64(last_id);
        pk->setter(model, fv);
    }
}

} // namespace cnetmod::orm
