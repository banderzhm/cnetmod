export module cnetmod.orm.automatic_field_fill;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.model_metadata;
import cnetmod.orm.query_wrapper;

namespace cnetmod::orm {

/**
 * @brief Metadata flags used by the automatic field-fill policy.
 *
 * These aliases deliberately use the canonical model metadata bits; the
 * previous independent bit values could never be observed by a mapped model.
 */
export constexpr col_flag FILL_INSERT = col_flag::fill_insert;
export constexpr col_flag FILL_UPDATE = col_flag::fill_insert_update;
export constexpr col_flag FILL_INSERT_UPDATE = col_flag::fill_insert_update;

export enum class fill_strategy
{
    current_timestamp,
    current_date,
    current_time,
    uuid,
    custom
};
export using field_fill_handler = std::function<param_value()>;

export struct auto_fill_config
{
    std::string field_name;
    fill_strategy strategy = fill_strategy::current_timestamp;
    field_fill_handler custom_handler;
    bool on_insert = true;
    bool on_update = false;
};

export class auto_fill_interceptor
{
public:
    template <Model T> void register_field(auto_fill_config config)
    {
        const auto& meta = model_traits<T>::meta();
        configs_[std::format("{}:{}", meta.table_name, config.field_name)] =
            std::move(config);
    }

    template <Model T> void register_from_metadata()
    {
        const auto& meta = model_traits<T>::meta();
        for (const auto& field : meta.fields)
        {
            auto config = metadata_config(field.col);
            if (!config)
                continue;
            configs_[std::format("{}:{}", meta.table_name, field.col.column_name)] =
                std::move(*config);
        }
    }

    template <Model T> auto fill_insert_fields(T& entity) const -> void
    {
        fill_fields<T>(entity, true);
    }

    template <Model T> auto fill_update_fields(T& entity) const -> void
    {
        fill_fields<T>(entity, false);
    }

    template <Model T>
    auto fill_update_wrapper(update_wrapper<T>& update) const -> void
    {
        const auto& meta = model_traits<T>::meta();
        std::optional<param_value> timestamp;
        for (const auto& field : meta.fields)
        {
            auto config = config_for(meta.table_name, field.col);
            if (!config || !config->on_update ||
                update.has_assignment(field.col.column_name))
                continue;
            if (config->strategy == fill_strategy::current_timestamp)
            {
                if (!timestamp)
                    timestamp = generate_value(*config);
                update.set_if_absent(field.col.column_name, *timestamp);
            }
            else
                update.set_if_absent(field.col.column_name,
                    generate_value(*config));
        }
    }

    template <Model T>
    auto get_insert_fill_params() const
        -> std::unordered_map<std::string, param_value>
    {
        return get_fill_params<T>(true);
    }

    template <Model T>
    auto get_update_fill_params() const
        -> std::unordered_map<std::string, param_value>
    {
        return get_fill_params<T>(false);
    }

    template <Model T>
    auto inject_insert_fields(std::string sql,
        std::vector<param_value>& params) const
        -> std::string
    {
        auto fill_params = get_insert_fill_params<T>();
        if (fill_params.empty())
            return sql;
        const auto values_pos = sql.find(" VALUES ");
        const auto col_start = sql.find('(');
        const auto col_end = sql.find(')', col_start);
        if (values_pos == std::string::npos || col_start == std::string::npos ||
            col_end == std::string::npos)
            return sql;
        std::string fill_cols, fill_vals;
        for (auto& [col, value] : fill_params)
        {
            fill_cols += std::format(", `{}`", col);
            fill_vals += ", {}";
            params.push_back(std::move(value));
        }
        sql.insert(col_end, fill_cols);
        const auto val_start = sql.find('(', values_pos);
        if (const auto val_end = sql.find(')', val_start);
            val_start != std::string::npos && val_end != std::string::npos)
            sql.insert(val_end, fill_vals);
        return sql;
    }

    template <Model T>
    auto inject_update_fields(std::string sql,
        std::vector<param_value>& params) const
        -> std::string
    {
        auto fill_params = get_update_fill_params<T>();
        if (fill_params.empty())
            return sql;
        const auto set_pos = sql.find(" SET ");
        if (set_pos == std::string::npos)
            return sql;
        const auto where_pos = sql.find(" WHERE ", set_pos);
        const auto insert_pos =
            where_pos == std::string::npos ? sql.size() : where_pos;
        std::string fill_set;
        for (auto& [col, value] : fill_params)
        {
            fill_set += std::format(", `{}` = {{}}", col);
            params.push_back(std::move(value));
        }
        sql.insert(insert_pos, fill_set);
        return sql;
    }

private:
    template <Model T> auto fill_fields(T& entity, bool insert) const -> void
    {
        const auto& meta = model_traits<T>::meta();
        std::optional<param_value> timestamp;
        for (const auto& field : meta.fields)
        {
            auto config = config_for(meta.table_name, field.col);
            if (config && (insert ? config->on_insert : config->on_update))
            {
                // Caller-supplied timestamps (for imports and historical rows)
                // are authoritative on INSERT; only default/empty fields are filled.
                if (insert && !is_unset(field.getter(entity)))
                    continue;
                if (config->strategy == fill_strategy::current_timestamp)
                {
                    if (!timestamp)
                        timestamp = generate_value(*config);
                    field.setter(entity, param_to_field_value(*timestamp));
                }
                else
                    field.setter(entity,
                        param_to_field_value(generate_value(*config)));
            }
        }
    }

    template <Model T>
    auto get_fill_params(bool insert) const
        -> std::unordered_map<std::string, param_value>
    {
        std::unordered_map<std::string, param_value> result;
        const auto& meta = model_traits<T>::meta();
        std::optional<param_value> timestamp;
        for (const auto& field : meta.fields)
        {
            auto config = config_for(meta.table_name, field.col);
            if (!config || !(insert ? config->on_insert : config->on_update))
                continue;
            if (config->strategy == fill_strategy::current_timestamp)
            {
                if (!timestamp)
                    timestamp = generate_value(*config);
                result.emplace(std::string(field.col.column_name), *timestamp);
            }
            else
                result.emplace(std::string(field.col.column_name),
                    generate_value(*config));
        }
        return result;
    }

    std::unordered_map<std::string, auto_fill_config> configs_;
    [[nodiscard]] auto config_for(std::string_view table, const column_def& column) const
        -> std::optional<auto_fill_config>
    {
        const auto it = configs_.find(std::format("{}:{}", table, column.column_name));
        if (it != configs_.end())
            return it->second;
        return metadata_config(column);
    }
    [[nodiscard]] static auto metadata_config(const column_def& column)
        -> std::optional<auto_fill_config>
    {
        if (!has_flag(column.flags, FILL_INSERT) &&
            !has_flag(column.flags, FILL_INSERT_UPDATE))
            return std::nullopt;
        auto_fill_config config;
        config.field_name = std::string(column.column_name);
        config.on_insert = true;
        config.on_update = has_flag(column.flags, FILL_INSERT_UPDATE);
        if (column.column_name.ends_with("_date") ||
            column.column_name.ends_with("Date"))
            config.strategy = fill_strategy::current_date;
        return config;
    }
    static auto is_unset(const param_value& value) noexcept -> bool;
    static auto param_to_field_value(const param_value& pv) -> field_value;
    static auto generate_value(const auto_fill_config& config) -> param_value;
};

export auto global_auto_fill_interceptor() -> auto_fill_interceptor&;

} // namespace cnetmod::orm
