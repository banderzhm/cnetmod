module cnetmod.protocol.mysql;

import std;
import :orm_result_map;

namespace cnetmod::mysql::orm {

auto result_map_def::all_mappings() const -> std::vector<result_mapping> {
  std::vector<result_mapping> all;
  all.insert(all.end(), id_mappings.begin(), id_mappings.end());
  all.insert(all.end(), result_mappings.begin(), result_mappings.end());
  return all;
}

auto result_map_def::find_by_property(std::string_view prop) const
    -> const result_mapping * {
  for (const auto &mapping : id_mappings) {
    if (mapping.property == prop)
      return &mapping;
  }
  for (const auto &mapping : result_mappings) {
    if (mapping.property == prop)
      return &mapping;
  }
  return nullptr;
}

auto result_map_def::find_by_column(std::string_view col) const
    -> const result_mapping * {
  for (const auto &mapping : id_mappings) {
    if (mapping.column == col)
      return &mapping;
  }
  for (const auto &mapping : result_mappings) {
    if (mapping.column == col)
      return &mapping;
  }
  return nullptr;
}

auto result_map_parser::parse(const xml_node &node)
    -> std::expected<result_map_def, std::string> {
  if (node.tag != "resultMap")
    return std::unexpected("Expected <resultMap> tag");

  result_map_def def;
  def.id = std::string(node.attr("id"));
  def.type = std::string(node.attr("type"));
  if (const auto auto_mapping = node.attr("autoMapping");
      !auto_mapping.empty()) {
    def.auto_mapping = auto_mapping == "true";
  }

  for (const auto &child : node.children) {
    if (child.is_text || !child.element)
      continue;
    const auto &element = *child.element;
    if (element.tag == "id") {
      auto mapping = parse_result_mapping(element);
      mapping.is_id = true;
      def.id_mappings.push_back(std::move(mapping));
    } else if (element.tag == "result") {
      def.result_mappings.push_back(parse_result_mapping(element));
    } else if (element.tag == "association") {
      def.associations.push_back(parse_association(element));
    } else if (element.tag == "collection") {
      def.collections.push_back(parse_collection(element));
    }
  }
  return def;
}

auto result_map_parser::parse_result_mapping(const xml_node &node)
    -> result_mapping {
  return {.property = std::string(node.attr("property")),
          .column = std::string(node.attr("column")),
          .jdbc_type = std::string(node.attr("jdbcType")),
          .type_handler = std::string(node.attr("typeHandler"))};
}

auto result_map_parser::parse_association(const xml_node &node) -> association {
  return {.property = std::string(node.attr("property")),
          .column = std::string(node.attr("column")),
          .select = std::string(node.attr("select")),
          .result_map = std::string(node.attr("resultMap")),
          .jdbc_type = std::string(node.attr("jdbcType"))};
}

auto result_map_parser::parse_collection(const xml_node &node) -> collection {
  return {.property = std::string(node.attr("property")),
          .column = std::string(node.attr("column")),
          .select = std::string(node.attr("select")),
          .result_map = std::string(node.attr("resultMap")),
          .of_type = std::string(node.attr("ofType"))};
}

void result_map_registry::register_result_map(result_map_def def) {
  result_maps_[def.id] = std::move(def);
}

auto result_map_registry::find(std::string_view id) const
    -> const result_map_def * {
  const auto it = result_maps_.find(std::string(id));
  return it == result_maps_.end() ? nullptr : &it->second;
}

auto result_map_registry::load_from_xml(const xml_node &node)
    -> std::expected<void, std::string> {
  auto result = result_map_parser::parse(node);
  if (!result)
    return std::unexpected(result.error());
  register_result_map(std::move(*result));
  return {};
}

namespace {
auto field_to_param_value(const field_value &field) -> param_value {
  if (field.is_null())
    return param_value::null();
  if (field.is_int64())
    return param_value::from_int(field.get_int64());
  if (field.is_uint64())
    return param_value::from_uint(field.get_uint64());
  if (field.is_double())
    return param_value::from_double(field.get_double());
  if (field.is_string())
    return param_value::from_string(std::string(field.get_string()));

  param_value value;
  if (field.is_date()) {
    value.kind = param_value::kind_t::date_kind;
    value.date_val = field.get_date();
  } else if (field.is_datetime()) {
    value.kind = param_value::kind_t::datetime_kind;
    value.datetime_val = field.get_datetime();
  } else if (field.is_time()) {
    value.kind = param_value::kind_t::time_kind;
    value.time_val = field.get_time();
  }
  return value;
}
} // namespace

auto result_map_applier::apply_to_row(
    const result_map_def &result_map, const row &result_row,
    const std::vector<std::string> &column_names)
    -> std::unordered_map<std::string, param_value> {
  std::unordered_map<std::string, param_value> properties;
  for (const auto &mapping : result_map.all_mappings()) {
    const auto it =
        std::find(column_names.begin(), column_names.end(), mapping.column);
    if (it == column_names.end())
      continue;
    const auto index =
        static_cast<std::size_t>(std::distance(column_names.begin(), it));
    if (index < result_row.size())
      properties[mapping.property] = field_to_param_value(result_row[index]);
  }

  if (result_map.auto_mapping) {
    const auto count = std::min(column_names.size(), result_row.size());
    for (std::size_t index = 0; index < count; ++index) {
      if (!result_map.find_by_column(column_names[index])) {
        properties[snake_to_camel(column_names[index])] =
            field_to_param_value(result_row[index]);
      }
    }
  }
  return properties;
}

auto result_map_applier::snake_to_camel(std::string_view snake) -> std::string {
  std::string camel;
  camel.reserve(snake.size());
  bool uppercase_next = false;
  for (const char character : snake) {
    if (character == '_') {
      uppercase_next = true;
    } else if (uppercase_next) {
      camel.push_back(static_cast<char>(
          std::toupper(static_cast<unsigned char>(character))));
      uppercase_next = false;
    } else {
      camel.push_back(character);
    }
  }
  return camel;
}

auto global_result_map_registry() -> result_map_registry & {
  static result_map_registry instance;
  return instance;
}

} // namespace cnetmod::mysql::orm
