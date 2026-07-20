export module cnetmod.protocol.mysql:orm_dynamic_sql;

import std;
import :types;
import :format_sql;
import :orm_expr;
import :orm_reflect;
import :orm_xml_parser;

namespace cnetmod::mysql::orm {

export struct built_dynamic_sql {
  std::string sql;
  std::vector<param_value> params;
};

export using fragment_map = std::unordered_map<std::string, const xml_node *>;

export class dynamic_sql_processor {
public:
  explicit dynamic_sql_processor(const format_options &opts);

  auto process(const xml_node &stmt_node, const param_context &ctx,
               const fragment_map &fragments) -> built_dynamic_sql;

private:
  format_options opts_;
  std::string sql_buf_;
  std::vector<param_value> params_;
  mutable std::unordered_map<std::string, std::unique_ptr<ast_node>>
      expr_cache_;

  static auto param_value_to_string(const param_value &val) -> std::string;
  static auto is_timestamp_field(std::string_view field_name) -> bool;
  static auto unix_timestamp_to_datetime(std::int64_t timestamp) -> param_value;
  auto eval_test(std::string_view test_expr, const param_context &ctx) const
      -> bool;
  void process_children(const xml_node &node, param_context &ctx,
                        const fragment_map &fragments);
  void process_node(const xml_node &node, param_context &ctx,
                    const fragment_map &fragments);
  void process_text(std::string_view text, const param_context &ctx);
  void append_normalized(std::string_view text, bool &last_was_space);
  void process_if(const xml_node &node, param_context &ctx,
                  const fragment_map &fragments);
  void process_where(const xml_node &node, param_context &ctx,
                     const fragment_map &fragments);
  void process_set(const xml_node &node, param_context &ctx,
                   const fragment_map &fragments);
  void process_trim(const xml_node &node, param_context &ctx,
                    const fragment_map &fragments);
  void process_foreach(const xml_node &node, param_context &ctx,
                       const fragment_map &fragments);
  void process_choose(const xml_node &node, param_context &ctx,
                      const fragment_map &fragments);
  void process_include(const xml_node &node, param_context &ctx,
                       const fragment_map &fragments);
  void process_bind(const xml_node &node, param_context &ctx,
                    const fragment_map &fragments);
  static auto strip_leading(std::string s, std::string_view prefixes)
      -> std::string;
  static auto strip_trailing(std::string s, std::string_view suffixes)
      -> std::string;
};

} // namespace cnetmod::mysql::orm
