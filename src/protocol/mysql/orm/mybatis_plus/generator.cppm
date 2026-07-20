export module cnetmod.protocol.mysql:orm_generator;

import std;
import :types;
import :connection_client;
import cnetmod.coro.task;

namespace cnetmod::mysql::orm {

// =============================================================================
// table_column — Database column information
// =============================================================================

export struct table_column {
  std::string name;
  std::string type;
  bool is_nullable = false;
  bool is_primary_key = false;
  bool is_auto_increment = false;
  std::string default_value;
  std::string comment;
};

// =============================================================================
// table_info — Database table information
// =============================================================================

export struct table_info {
  std::string name;
  std::string comment;
  std::vector<table_column> columns;
};

// =============================================================================
// generator_config — Code generator configuration
// =============================================================================

export struct generator_config {
  std::string output_dir = "./generated";
  std::string namespace_name = "generated";
  bool generate_mapper = true;
  bool generate_xml = true;
  bool generate_service = false;
  std::string table_prefix = ""; // Remove prefix from class names
};

// =============================================================================
// code_generator — Generate C++ code from database tables
// =============================================================================

export class code_generator {
public:
  explicit code_generator(generator_config config = {});

  /// Fetch table information from database
  static auto fetch_table_info(client &cli, std::string_view table_name)
      -> task<std::expected<table_info, std::string>>;

  /// Generate model class code
  auto generate_model(const table_info &table) const -> std::string;

  /// Generate XML mapper
  auto generate_xml_mapper(const table_info &table) const -> std::string;

  /// Generate all files for a table
  auto generate_all(client &cli, std::string_view table_name)
      -> task<std::expected<void, std::string>>;

private:
  generator_config config_;

  auto remove_prefix(std::string_view name) const -> std::string;

  static auto to_pascal_case(std::string_view snake) -> std::string;

  static auto to_camel_case(std::string_view snake) -> std::string;

  static auto to_snake_case(std::string_view pascal) -> std::string;

  static auto mysql_type_to_cpp(std::string_view mysql_type, bool is_nullable)
      -> std::string;

  static auto mysql_type_to_sql_type(std::string_view mysql_type)
      -> std::string;
};

} // namespace cnetmod::mysql::orm
