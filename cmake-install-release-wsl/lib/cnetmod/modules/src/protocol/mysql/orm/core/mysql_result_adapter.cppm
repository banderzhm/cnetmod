export module cnetmod.protocol.mysql:orm_mysql_result_adapter;

import std;
import :types;
export import cnetmod.orm.result_mapper;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.model_metadata;

export namespace cnetmod::orm {

/// Converts a single protocol-owned MySQL field without losing its value kind.
auto mysql_adapt_field(const cnetmod::mysql::field_value& source) -> field_value;

/// Converts bind parameters in both directions at the protocol boundary.
auto mysql_adapt_parameter(const query_parameter& source)
    -> cnetmod::mysql::param_value;
auto mysql_adapt_parameter(const cnetmod::mysql::param_value& source)
    -> query_parameter;

/// Converts the protocol-owned MySQL result representation into the stable,
/// database-independent ORM result contract.
auto mysql_adapt_result(const cnetmod::mysql::result_set& source) -> query_result;

template <Model ModelType>
auto mysql_map_result(const cnetmod::mysql::result_set& source)
    -> std::vector<ModelType>
{
    return from_result_set<ModelType>(mysql_adapt_result(source));
}

template <typename... ElementTypes>
auto mysql_map_result_to_tuples(const cnetmod::mysql::result_set& source)
    -> std::vector<std::tuple<ElementTypes...>>
{
    return from_result_set_to_tuple<ElementTypes...>(mysql_adapt_result(source));
}

} // namespace cnetmod::orm
