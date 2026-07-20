export module cnetmod.protocol.mysql:orm_expr;

import std;
import :types;

export namespace cnetmod::mysql::orm {

struct expr_value {
  enum class type_t : std::uint8_t {
    null_type,
    bool_type,
    int_type,
    double_type,
    string_type
  };
  type_t type = type_t::null_type;
  bool bool_val = false;
  std::int64_t int_val = 0;
  double double_val = 0.0;
  std::string str_val;

  [[nodiscard]] auto is_truthy() const noexcept -> bool;
  [[nodiscard]] auto is_null() const noexcept -> bool;
  [[nodiscard]] auto to_double() const noexcept -> double;
  [[nodiscard]] auto to_int() const noexcept -> std::int64_t;
  [[nodiscard]] auto to_string() const -> std::string;
  [[nodiscard]] static auto make_null() -> expr_value;
  [[nodiscard]] static auto make_bool(bool value) -> expr_value;
  [[nodiscard]] static auto make_int(std::int64_t value) -> expr_value;
  [[nodiscard]] static auto make_double(double value) -> expr_value;
  [[nodiscard]] static auto make_string(std::string value) -> expr_value;
  [[nodiscard]] static auto from_param(const param_value &value) -> expr_value;
  [[nodiscard]] auto to_param() const -> param_value;
};

enum class token_type : std::uint8_t {
  integer_lit,
  double_lit,
  string_lit,
  true_lit,
  false_lit,
  null_lit,
  identifier,
  eq,
  ne,
  lt,
  gt,
  le,
  ge,
  and_op,
  or_op,
  not_op,
  plus,
  minus,
  star,
  slash,
  percent,
  dot,
  lparen,
  rparen,
  comma,
  eof,
};

struct token {
  token_type type = token_type::eof;
  std::string_view lexeme;
  std::size_t pos = 0;
};

class expr_lexer {
public:
  explicit expr_lexer(std::string_view source) noexcept;
  [[nodiscard]] auto next_token() -> token;

private:
  std::string_view src_;
  std::size_t pos_ = 0;
  void skip_whitespace() noexcept;
  [[nodiscard]] auto scan_number() -> token;
  [[nodiscard]] auto scan_string(char quote) -> token;
  [[nodiscard]] auto scan_identifier() -> token;
};

enum class ast_kind : std::uint8_t { literal, property, unary, binary };

struct ast_node {
  ast_kind kind = ast_kind::literal;
  expr_value lit_value;
  std::vector<std::string> path;
  token_type op = token_type::eof;
  std::unique_ptr<ast_node> operand;
  std::unique_ptr<ast_node> left;
  std::unique_ptr<ast_node> right;
  [[nodiscard]] static auto make_literal(expr_value value)
      -> std::unique_ptr<ast_node>;
  [[nodiscard]] static auto make_property(std::vector<std::string> path)
      -> std::unique_ptr<ast_node>;
  [[nodiscard]] static auto make_unary(token_type op,
                                       std::unique_ptr<ast_node> operand)
      -> std::unique_ptr<ast_node>;
  [[nodiscard]] static auto make_binary(token_type op,
                                        std::unique_ptr<ast_node> left,
                                        std::unique_ptr<ast_node> right)
      -> std::unique_ptr<ast_node>;
};

class expr_parser {
public:
  explicit expr_parser(std::string_view source);
  [[nodiscard]] auto parse() -> std::unique_ptr<ast_node>;

private:
  expr_lexer lexer_;
  token cur_{};
  auto advance() -> token;
  auto match(token_type type) -> bool;
  [[nodiscard]] auto parse_or() -> std::unique_ptr<ast_node>;
  [[nodiscard]] auto parse_and() -> std::unique_ptr<ast_node>;
  [[nodiscard]] auto parse_equality() -> std::unique_ptr<ast_node>;
  [[nodiscard]] auto parse_comparison() -> std::unique_ptr<ast_node>;
  [[nodiscard]] auto parse_additive() -> std::unique_ptr<ast_node>;
  [[nodiscard]] auto parse_multiplicative() -> std::unique_ptr<ast_node>;
  [[nodiscard]] auto parse_unary() -> std::unique_ptr<ast_node>;
  [[nodiscard]] auto parse_postfix() -> std::unique_ptr<ast_node>;
  [[nodiscard]] auto parse_primary() -> std::unique_ptr<ast_node>;
};

class property_resolver {
public:
  virtual ~property_resolver() = default;
  virtual auto resolve(std::string_view name) const -> expr_value = 0;
  virtual auto has(std::string_view name) const -> bool = 0;
};

class expr_evaluator {
public:
  [[nodiscard]] auto evaluate(const ast_node &node,
                              const property_resolver &context) const
      -> expr_value;

private:
  [[nodiscard]] auto eval_property(const ast_node &node,
                                   const property_resolver &context) const
      -> expr_value;
  [[nodiscard]] auto eval_unary(const ast_node &node,
                                const property_resolver &context) const
      -> expr_value;
  [[nodiscard]] auto eval_binary(const ast_node &node,
                                 const property_resolver &context) const
      -> expr_value;
  [[nodiscard]] static auto compare_eq(const expr_value &left,
                                       const expr_value &right) -> bool;
  [[nodiscard]] static auto compare_ord(const expr_value &left,
                                        const expr_value &right) -> int;
  [[nodiscard]] static auto arithmetic(token_type op, const expr_value &left,
                                       const expr_value &right) -> expr_value;
};

[[nodiscard]] auto parse_expr(std::string_view expression)
    -> std::unique_ptr<ast_node>;
[[nodiscard]] auto eval_expr(std::string_view expression,
                             const property_resolver &context) -> expr_value;
[[nodiscard]] auto eval_ast(const ast_node &ast,
                            const property_resolver &context) -> expr_value;

} // namespace cnetmod::mysql::orm
