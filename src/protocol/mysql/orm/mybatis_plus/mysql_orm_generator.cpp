module cnetmod.protocol.mysql;

import cnetmod.coro.task;
import :orm_generator;

namespace cnetmod::mysql::orm {

code_generator::code_generator(generator_config config)
    : config_(std::move(config)) {}

auto code_generator::fetch_table_info(client &cli, std::string_view table_name)
    -> task<std::expected<table_info, std::string>> {
  table_info info;
  info.name = table_name;

  const auto comment_sql =
      std::format("SELECT TABLE_COMMENT FROM information_schema.TABLES "
                  "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}'",
                  table_name);
  auto comment_rs = co_await cli.execute(comment_sql);
  if (!comment_rs.is_err() && !comment_rs.rows.empty()) {
    const auto &comment_field = comment_rs.rows[0][0];
    if (!comment_field.is_null() && comment_field.is_string())
      info.comment = std::string(comment_field.get_string());
  }

  const auto columns_sql =
      std::format("SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, COLUMN_KEY, "
                  "EXTRA, COLUMN_DEFAULT, COLUMN_COMMENT "
                  "FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = "
                  "DATABASE() AND TABLE_NAME = '{}' "
                  "ORDER BY ORDINAL_POSITION",
                  table_name);
  auto columns_rs = co_await cli.execute(columns_sql);
  if (columns_rs.is_err())
    co_return std::unexpected(columns_rs.error_msg);

  for (const auto &row : columns_rs.rows) {
    table_column column;
    if (row[0].is_string())
      column.name = std::string(row[0].get_string());
    if (row[1].is_string())
      column.type = std::string(row[1].get_string());
    if (row[2].is_string())
      column.is_nullable = row[2].get_string() == "YES";
    if (row[3].is_string())
      column.is_primary_key = row[3].get_string() == "PRI";
    if (row[4].is_string())
      column.is_auto_increment =
          row[4].get_string().find("auto_increment") != std::string::npos;
    if (!row[5].is_null() && row[5].is_string())
      column.default_value = std::string(row[5].get_string());
    if (!row[6].is_null() && row[6].is_string())
      column.comment = std::string(row[6].get_string());
    info.columns.push_back(std::move(column));
  }
  co_return info;
}

auto code_generator::generate_model(const table_info &table) const
    -> std::string {
  const auto class_name = to_pascal_case(remove_prefix(table.name));
  std::string code = std::format("// Generated from table: {}\n", table.name);
  if (!table.comment.empty())
    code += std::format("// {}\n", table.comment);
  code += "\n#include <cnetmod/orm.hpp>\nimport std;\nimport "
          "cnetmod.protocol.mysql;\n\n";
  code += std::format("namespace {} {{\n\nstruct {} {{\n",
                      config_.namespace_name, class_name);
  for (const auto &column : table.columns) {
    if (!column.comment.empty())
      code += std::format("    /// {}\n", column.comment);
    code += std::format("    {} {};\n",
                        mysql_type_to_cpp(column.type, column.is_nullable),
                        to_camel_case(column.name));
  }
  code += "};\n\n";
  code += std::format("CNETMOD_MODEL({}, \"{}\",\n", class_name, table.name);
  for (std::size_t index = 0; index < table.columns.size(); ++index) {
    const auto &column = table.columns[index];
    std::string flags;
    if (column.is_primary_key)
      flags = "PK";
    if (column.is_auto_increment)
      flags += flags.empty() ? "AUTO_INC" : " | AUTO_INC";
    if (column.is_nullable)
      flags += flags.empty() ? "NULLABLE" : " | NULLABLE";
    if (flags.empty())
      flags = "NONE";
    code += std::format("    CNETMOD_FIELD({}, \"{}\", {}, {}){}\n",
                        to_camel_case(column.name), column.name,
                        mysql_type_to_sql_type(column.type), flags,
                        index + 1 == table.columns.size() ? "" : ",");
  }
  code += std::format(")\n\n}} // namespace {}\n", config_.namespace_name);
  return code;
}

auto code_generator::generate_xml_mapper(const table_info &table) const
    -> std::string {
  const auto class_name = to_pascal_case(remove_prefix(table.name));
  std::string xml =
      std::format("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mapper "
                  "namespace=\"{}Mapper\">\n\n",
                  class_name);
  xml += "    <sql id=\"columns\">\n        ";
  for (std::size_t index = 0; index < table.columns.size(); ++index) {
    if (index != 0)
      xml += ", ";
    xml += std::format("`{}`", table.columns[index].name);
  }
  xml += "\n    </sql>\n\n";
  std::string primary_key;
  for (const auto &column : table.columns)
    if (column.is_primary_key) {
      primary_key = column.name;
      break;
    }
  xml += "    <select id=\"findById\">\n        SELECT <include "
         "refid=\"columns\"/>\n";
  xml += std::format("        FROM `{}`\n", table.name);
  if (!primary_key.empty())
    xml += std::format("        WHERE `{}` = #{{id}}\n", primary_key);
  xml += "    </select>\n\n    <select id=\"findAll\">\n        SELECT "
         "<include refid=\"columns\"/>\n";
  xml += std::format(
      "        FROM `{}`\n    </select>\n\n    <insert id=\"insert\">\n",
      table.name);
  xml += std::format("        INSERT INTO `{}` (\n", table.name);
  std::vector<std::string> insert_columns;
  for (const auto &column : table.columns)
    if (!column.is_auto_increment)
      insert_columns.push_back(column.name);
  for (std::size_t index = 0; index < insert_columns.size(); ++index)
    xml += std::format("            `{}`{}\n", insert_columns[index],
                       index + 1 == insert_columns.size() ? "" : ",");
  xml += "        ) VALUES (\n";
  for (std::size_t index = 0; index < insert_columns.size(); ++index)
    xml += std::format("            #{{{}}}{}\n",
                       to_camel_case(insert_columns[index]),
                       index + 1 == insert_columns.size() ? "" : ",");
  xml += "        )\n    </insert>\n\n";
  if (!primary_key.empty()) {
    xml += std::format(
        "    <update id=\"update\">\n        UPDATE `{}`\n        <set>\n",
        table.name);
    for (const auto &column : table.columns) {
      if (column.is_primary_key || column.is_auto_increment)
        continue;
      const auto field_name = to_camel_case(column.name);
      xml += std::format("            <if test=\"{} != null\">\n               "
                         " `{}` = #{{{}}},\n            </if>\n",
                         field_name, column.name, field_name);
    }
    xml += std::format(
        "        </set>\n        WHERE `{}` = #{{id}}\n    </update>\n\n",
        primary_key);
    xml += std::format("    <delete id=\"delete\">\n        DELETE FROM `{}`\n "
                       "       WHERE `{}` = #{{id}}\n    </delete>\n\n",
                       table.name, primary_key);
  }
  xml += "</mapper>\n";
  return xml;
}

auto code_generator::generate_all(client &cli, std::string_view table_name)
    -> task<std::expected<void, std::string>> {
  auto table_result = co_await fetch_table_info(cli, table_name);
  if (!table_result)
    co_return std::unexpected(table_result.error());
  const auto &table = *table_result;
  std::filesystem::create_directories(config_.output_dir);
  const auto class_name = to_pascal_case(remove_prefix(table.name));
  if (config_.generate_mapper) {
    std::ofstream output(std::format("{}/{}.hpp", config_.output_dir,
                                     to_snake_case(class_name)));
    if (!output)
      co_return std::unexpected("Failed to write model file");
    output << generate_model(table);
  }
  if (config_.generate_xml) {
    std::ofstream output(
        std::format("{}/{}Mapper.xml", config_.output_dir, class_name));
    if (!output)
      co_return std::unexpected("Failed to write XML file");
    output << generate_xml_mapper(table);
  }
  co_return {};
}

auto code_generator::remove_prefix(std::string_view name) const -> std::string {
  return !config_.table_prefix.empty() && name.starts_with(config_.table_prefix)
             ? std::string(name.substr(config_.table_prefix.size()))
             : std::string(name);
}

auto code_generator::to_pascal_case(std::string_view snake) -> std::string {
  std::string result;
  bool upper = true;
  for (const char character : snake) {
    if (character == '_')
      upper = true;
    else {
      result.push_back(upper ? static_cast<char>(std::toupper(
                                   static_cast<unsigned char>(character)))
                             : character);
      upper = false;
    }
  }
  return result;
}

auto code_generator::to_camel_case(std::string_view snake) -> std::string {
  std::string result;
  bool upper = false;
  for (const char character : snake) {
    if (character == '_')
      upper = true;
    else {
      result.push_back(upper ? static_cast<char>(std::toupper(
                                   static_cast<unsigned char>(character)))
                             : character);
      upper = false;
    }
  }
  return result;
}

auto code_generator::to_snake_case(std::string_view pascal) -> std::string {
  std::string result;
  for (std::size_t index = 0; index < pascal.size(); ++index) {
    const auto character = pascal[index];
    if (std::isupper(static_cast<unsigned char>(character))) {
      if (index != 0)
        result.push_back('_');
      result.push_back(static_cast<char>(
          std::tolower(static_cast<unsigned char>(character))));
    } else
      result.push_back(character);
  }
  return result;
}

auto code_generator::mysql_type_to_cpp(std::string_view mysql_type,
                                       bool is_nullable) -> std::string {
  std::string base_type = "std::string";
  if (mysql_type.starts_with("int") || mysql_type.starts_with("tinyint(1)"))
    base_type = "std::int32_t";
  else if (mysql_type.starts_with("bigint"))
    base_type = "std::int64_t";
  else if (mysql_type.starts_with("datetime") ||
           mysql_type.starts_with("timestamp") ||
           mysql_type.starts_with("date"))
    base_type = "std::time_t";
  else if (mysql_type.starts_with("double") ||
           mysql_type.starts_with("float") || mysql_type.starts_with("decimal"))
    base_type = "double";
  return is_nullable && base_type != "std::string"
             ? std::format("std::optional<{}>", base_type)
             : base_type;
}

auto code_generator::mysql_type_to_sql_type(std::string_view mysql_type)
    -> std::string {
  if (mysql_type.starts_with("int"))
    return "int_";
  if (mysql_type.starts_with("bigint"))
    return "bigint";
  if (mysql_type.starts_with("varchar"))
    return "varchar";
  if (mysql_type.starts_with("text"))
    return "text";
  if (mysql_type.starts_with("datetime"))
    return "datetime";
  if (mysql_type.starts_with("timestamp"))
    return "timestamp";
  if (mysql_type.starts_with("date"))
    return "date";
  if (mysql_type.starts_with("double"))
    return "double_";
  if (mysql_type.starts_with("float"))
    return "float_";
  if (mysql_type.starts_with("decimal"))
    return "decimal";
  if (mysql_type.starts_with("tinyint(1)"))
    return "tinyint";
  return "varchar";
}

} // namespace cnetmod::mysql::orm
