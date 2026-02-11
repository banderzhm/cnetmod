export module cnetmod.protocol.mysql:orm_mapper;

import std;
import :types;
import :orm_meta;

namespace cnetmod::mysql::orm {

// =============================================================================
// from_row — result_set 行 → 模型对象
// =============================================================================

/// 从 row + columns 构造模型 T
/// 按列名匹配（不依赖列序）
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

/// 从 result_set 构造模型列表
export template <Model T>
auto from_result_set(const result_set& rs) -> std::vector<T> {
    std::vector<T> result;
    result.reserve(rs.rows.size());
    for (auto& r : rs.rows)
        result.push_back(from_row<T>(r, rs.columns));
    return result;
}

// =============================================================================
// to_params — 模型对象 → param_value 列表
// =============================================================================

/// 提取所有可插入字段的 param_value（跳过 auto_increment）
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

/// 提取所有可更新字段的 param_value（跳过 PK）+ 末尾追加 PK 值
export template <Model T>
auto to_update_params(const T& model) -> std::vector<param_value> {
    auto& meta = model_traits<T>::meta();
    std::vector<param_value> params;

    // SET 部分
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

/// 提取 PK 值
export template <Model T>
auto to_pk_param(const T& model) -> param_value {
    auto& meta = model_traits<T>::meta();
    auto* pk = meta.pk();
    if (pk && pk->getter)
        return pk->getter(model);
    return param_value::null();
}

/// 回填 last_insert_id 到模型 PK
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
