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
