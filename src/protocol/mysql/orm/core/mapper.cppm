export module cnetmod.protocol.mysql:orm_mapper;

import std;
import :types;
import :orm_meta;

namespace cnetmod::mysql::orm {

// =============================================================================
// from_row — result_set row → model object
// =============================================================================

/// Construct model T from row + columns
/// Match by column name (not dependent on column order)
export template <Model T>
auto from_row(const row& r, const std::vector<column_meta>& columns) -> T {
    T obj{};
    auto& meta = model_traits<T>::meta();

    for (std::size_t i = 0; i < columns.size() && i < r.size(); ++i) {
        auto* fm = meta.find_column(columns[i].name);
        if (fm && fm->setter)
            fm->setter(obj, r[i]);
    }
    return obj;
}

/// Construct model list from result_set
export template <Model T>
auto from_result_set(const result_set& rs) -> std::vector<T> {
    std::vector<T> result;
    result.reserve(rs.rows.size());
    for (auto& r : rs.rows)
        result.push_back(from_row<T>(r, rs.columns));
    return result;
}

// =============================================================================
// Tuple support — result_set row → std::tuple
// =============================================================================

/// Helper: Convert field_value to tuple element type
template <typename T>
auto field_to_tuple_element(const field_value& fv) -> T {
    if constexpr (std::is_same_v<T, std::int64_t>) {
        return fv.is_null() ? 0 : fv.as_int64();
    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
        return fv.is_null() ? 0 : fv.as_uint64();
    } else if constexpr (std::is_same_v<T, std::int32_t>) {
        return fv.is_null() ? 0 : static_cast<std::int32_t>(fv.as_int64());
    } else if constexpr (std::is_same_v<T, double>) {
        return fv.is_null() ? 0.0 : fv.as_double();
    } else if constexpr (std::is_same_v<T, float>) {
        return fv.is_null() ? 0.0f : fv.as_float();
    } else if constexpr (std::is_same_v<T, bool>) {
        return fv.is_null() ? false : (fv.as_int64() != 0);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return fv.is_null() ? std::string{} : std::string(fv.as_string());
    } else if constexpr (std::is_same_v<T, std::optional<std::int64_t>>) {
        return fv.is_null() ? std::nullopt : std::optional<std::int64_t>(fv.as_int64());
    } else if constexpr (std::is_same_v<T, std::optional<std::uint64_t>>) {
        return fv.is_null() ? std::nullopt : std::optional<std::uint64_t>(fv.as_uint64());
    } else if constexpr (std::is_same_v<T, std::optional<double>>) {
        return fv.is_null() ? std::nullopt : std::optional<double>(fv.as_double());
    } else if constexpr (std::is_same_v<T, std::optional<std::string>>) {
        return fv.is_null() ? std::nullopt : std::optional<std::string>(std::string(fv.as_string()));
    } else {
        static_assert(std::is_same_v<T, void>, "Unsupported tuple element type");
        return T{};
    }
}

/// Helper: Convert row to tuple (index sequence version)
template <typename... Ts, std::size_t... Is>
auto from_row_to_tuple_impl(const row& r, std::index_sequence<Is...>) -> std::tuple<Ts...> {
    return std::make_tuple(field_to_tuple_element<Ts>(r[Is])...);
}

/// Construct tuple from row
export template <typename... Ts>
auto from_row_to_tuple(const row& r) -> std::tuple<Ts...> {
    if (r.size() < sizeof...(Ts)) {
        throw std::runtime_error("Row has fewer columns than tuple elements");
    }
    return from_row_to_tuple_impl<Ts...>(r, std::index_sequence_for<Ts...>{});
}

/// Construct tuple list from result_set
export template <typename... Ts>
auto from_result_set_to_tuple(const result_set& rs) -> std::vector<std::tuple<Ts...>> {
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
auto to_insert_params(const T& model) -> std::vector<param_value> {
    auto& meta = model_traits<T>::meta();
    std::vector<param_value> params;
    for (auto& f : meta.fields) {
        if (!f.col.is_auto() && f.getter)
            params.push_back(f.getter(model));
    }
    return params;
}

/// Extract all updatable field param_value (skip PK) + append PK value at end
export template <Model T>
auto to_update_params(const T& model) -> std::vector<param_value> {
    auto& meta = model_traits<T>::meta();
    std::vector<param_value> params;

    // SET part
    for (auto& f : meta.fields) {
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
export template <Model T>
auto to_pk_param(const T& model) -> param_value {
    auto& meta = model_traits<T>::meta();
    auto* pk = meta.pk();
    if (pk && pk->getter)
        return pk->getter(model);
    return param_value::null();
}

/// Fill last_insert_id back to model PK
export template <Model T>
void fill_insert_id(T& model, std::uint64_t last_id) {
    auto& meta = model_traits<T>::meta();
    auto* pk = meta.pk();
    if (pk && pk->col.is_auto() && pk->setter) {
        field_value fv;
        fv = field_value::from_uint64(last_id);
        pk->setter(model, fv);
    }
}

} // namespace cnetmod::mysql::orm
