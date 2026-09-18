module cnetmod.orm.logical_delete;

namespace cnetmod::orm {

namespace {

    auto valid_identifier(std::string_view value) noexcept -> bool
    {
        if (value.empty())
            return false;
        const auto first = static_cast<unsigned char>(value.front());
        if (!(std::isalpha(first) || value.front() == '_'))
            return false;
        return std::ranges::all_of(value, [](char character)
            {
                const auto byte = static_cast<unsigned char>(character);
                return std::isalnum(byte) || character == '_';
            });
    }

    auto touch_value_sql(logical_delete_touch_value value)
        -> std::string_view
    {
        switch (value)
        {
        case logical_delete_touch_value::current_timestamp:
            return "CURRENT_TIMESTAMP";
        case logical_delete_touch_value::current_date:
            return "CURRENT_DATE";
        case logical_delete_touch_value::current_time:
            return "CURRENT_TIME";
        }
        throw std::invalid_argument("invalid logical-delete touch value");
    }

    void validate_config(const logical_delete_config& config)
    {
        if (!valid_identifier(config.field_name))
            throw std::invalid_argument("invalid logical-delete field name");
        std::unordered_set<std::string_view> fields;
        fields.emplace(config.field_name);
        for (const auto& touch : config.touch_fields)
        {
            if (!valid_identifier(touch.field_name) ||
                !fields.emplace(touch.field_name).second)
                throw std::invalid_argument(
                    "invalid or duplicate logical-delete touch field");
            (void)touch_value_sql(touch.value);
        }
    }

} // namespace

logical_delete_interceptor::logical_delete_interceptor(
    logical_delete_config config)
    : config_(std::move(config))
{
    validate_config(config_);
}

auto logical_delete_interceptor::inject_select_condition_impl(
    std::string sql, std::string_view field,
    const param_value& not_deleted_value, logical_delete_mode mode) -> std::string
{
    const auto predicate = mode == logical_delete_mode::nullable_datetime
        ? std::format("`{}` IS NULL", field)
        : std::format("`{}` = {}", field,
              not_deleted_value.kind == param_value::kind_t::int64_kind
                  ? std::to_string(not_deleted_value.int_val)
                  : "0");
    if (const auto where_pos = sql.find(" WHERE ");
        where_pos != std::string::npos)
    {
        sql.insert(where_pos + 7, predicate + " AND ");
        return sql;
    }

    const auto order_pos = sql.find(" ORDER BY");
    const auto group_pos = sql.find(" GROUP BY");
    const auto limit_pos = sql.find(" LIMIT");
    const auto insert_pos =
        std::min({order_pos != std::string::npos ? order_pos : sql.size(),
            group_pos != std::string::npos ? group_pos : sql.size(),
            limit_pos != std::string::npos ? limit_pos : sql.size()});
    sql.insert(insert_pos, " WHERE " + predicate);
    return sql;
}

auto logical_delete_interceptor::transform_delete_to_update_impl(
    std::string sql, std::string_view table_name, std::string_view field,
    const param_value& deleted_value, logical_delete_mode mode,
    std::span<const logical_delete_touch_field> touch_fields) -> std::string
{
    if (!sql.starts_with("DELETE FROM") && !sql.starts_with("delete from"))
        return sql;
    const auto where_pos = sql.find(" WHERE ");
    const auto where_clause =
        where_pos == std::string::npos ? std::string{} : sql.substr(where_pos);
    const auto value = mode == logical_delete_mode::nullable_datetime
        ? std::string{"CURRENT_TIMESTAMP"}
        : deleted_value.kind == param_value::kind_t::int64_kind
        ? std::to_string(deleted_value.int_val)
        : "1";
    auto assignments = std::format("`{}` = {}", field, value);
    for (const auto& touch : touch_fields)
        assignments += std::format(", `{}` = {}", touch.field_name,
            touch_value_sql(touch.value));
    return std::format("UPDATE `{}` SET {}{}", table_name, assignments,
        where_clause);
}

auto logical_delete_interceptor::config() const noexcept
    -> const logical_delete_config&
{
    return config_;
}

void logical_delete_interceptor::set_config(logical_delete_config config)
{
    validate_config(config);
    config_ = std::move(config);
}

void logical_delete_interceptor::set_enabled(bool enabled)
{
    config_.enabled = enabled;
}

auto global_logical_delete_interceptor() -> logical_delete_interceptor&
{
    static logical_delete_interceptor instance;
    return instance;
}

} // namespace cnetmod::orm
