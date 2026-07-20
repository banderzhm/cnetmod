module cnetmod.protocol.mysql;
import :orm_expr;

namespace cnetmod::mysql::orm {
auto expr_value::is_truthy() const noexcept -> bool {
  switch (type) {
  case type_t::bool_type:
    return bool_val;
  case type_t::int_type:
    return int_val != 0;
  case type_t::double_type:
    return double_val != 0.0;
  case type_t::string_type:
    return !str_val.empty();
  default:
    return false;
  }
}
auto expr_value::is_null() const noexcept -> bool {
  return type == type_t::null_type;
}
auto expr_value::to_double() const noexcept -> double {
  return type == type_t::int_type      ? static_cast<double>(int_val)
         : type == type_t::double_type ? double_val
         : type == type_t::bool_type   ? (bool_val ? 1.0 : 0.0)
                                       : 0.0;
}
auto expr_value::to_int() const noexcept -> std::int64_t {
  return type == type_t::int_type      ? int_val
         : type == type_t::double_type ? static_cast<std::int64_t>(double_val)
         : type == type_t::bool_type   ? (bool_val ? 1 : 0)
                                       : 0;
}
auto expr_value::to_string() const -> std::string {
  switch (type) {
  case type_t::null_type:
    return "null";
  case type_t::bool_type:
    return bool_val ? "true" : "false";
  case type_t::int_type:
    return std::to_string(int_val);
  case type_t::double_type:
    return std::format("{}", double_val);
  case type_t::string_type:
    return str_val;
  }
  return {};
}
auto expr_value::make_null() -> expr_value { return {}; }
auto expr_value::make_bool(bool v) -> expr_value {
  expr_value r;
  r.type = type_t::bool_type;
  r.bool_val = v;
  return r;
}
auto expr_value::make_int(std::int64_t v) -> expr_value {
  expr_value r;
  r.type = type_t::int_type;
  r.int_val = v;
  return r;
}
auto expr_value::make_double(double v) -> expr_value {
  expr_value r;
  r.type = type_t::double_type;
  r.double_val = v;
  return r;
}
auto expr_value::make_string(std::string v) -> expr_value {
  expr_value r;
  r.type = type_t::string_type;
  r.str_val = std::move(v);
  return r;
}
auto expr_value::from_param(const param_value &v) -> expr_value {
  using K = param_value::kind_t;
  switch (v.kind) {
  case K::int64_kind:
    return make_int(v.int_val);
  case K::uint64_kind:
    return make_int(static_cast<std::int64_t>(v.uint_val));
  case K::double_kind:
    return make_double(v.double_val);
  case K::string_kind:
  case K::blob_kind:
    return make_string(std::string(v.str_val));
  case K::date_kind:
    return make_string(v.date_val.to_string());
  case K::datetime_kind:
    return make_string(v.datetime_val.to_string());
  case K::time_kind:
    return make_string(v.time_val.to_string());
  default:
    return make_null();
  }
}
auto expr_value::to_param() const -> param_value {
  switch (type) {
  case type_t::bool_type:
    return param_value::from_int(bool_val);
  case type_t::int_type:
    return param_value::from_int(int_val);
  case type_t::double_type:
    return param_value::from_double(double_val);
  case type_t::string_type:
    return param_value::from_string(str_val);
  default:
    return param_value::null();
  }
}

expr_lexer::expr_lexer(std::string_view s) noexcept : src_(s) {}
void expr_lexer::skip_whitespace() noexcept {
  while (pos_ < src_.size() &&
         std::isspace(static_cast<unsigned char>(src_[pos_])))
    ++pos_;
}
auto expr_lexer::scan_number() -> token {
  auto start = pos_;
  bool dot = false;
  while (pos_ < src_.size()) {
    auto c = src_[pos_];
    if (c == '.' && !dot) {
      dot = true;
      ++pos_;
    } else if (std::isdigit(static_cast<unsigned char>(c)))
      ++pos_;
    else
      break;
  }
  return {dot ? token_type::double_lit : token_type::integer_lit,
          src_.substr(start, pos_ - start), start};
}
auto expr_lexer::scan_string(char quote) -> token {
  auto start = pos_++;
  while (pos_ < src_.size() && src_[pos_] != quote) {
    if (src_[pos_] == '\\' && pos_ + 1 < src_.size())
      ++pos_;
    ++pos_;
  }
  if (pos_ < src_.size())
    ++pos_;
  return {token_type::string_lit, src_.substr(start, pos_ - start), start};
}
auto expr_lexer::scan_identifier() -> token {
  auto start = pos_;
  while (pos_ < src_.size() &&
         (std::isalnum(static_cast<unsigned char>(src_[pos_])) ||
          src_[pos_] == '_'))
    ++pos_;
  auto l = src_.substr(start, pos_ - start);
  if (l == "and")
    return {token_type::and_op, l, start};
  if (l == "or")
    return {token_type::or_op, l, start};
  if (l == "not")
    return {token_type::not_op, l, start};
  if (l == "true")
    return {token_type::true_lit, l, start};
  if (l == "false")
    return {token_type::false_lit, l, start};
  if (l == "null" || l == "nil")
    return {token_type::null_lit, l, start};
  return {token_type::identifier, l, start};
}
auto expr_lexer::next_token() -> token {
  skip_whitespace();
  if (pos_ >= src_.size())
    return {token_type::eof, {}, pos_};
  auto start = pos_;
  auto c = src_[pos_];
  if (pos_ + 1 < src_.size()) {
    auto d = src_[pos_ + 1];
    if ((c == '=' && d == '=') || (c == '!' && d == '=') ||
        (c == '<' && d == '=') || (c == '>' && d == '=') ||
        (c == '&' && d == '&') || (c == '|' && d == '|')) {
      pos_ += 2;
      return {c == '='   ? token_type::eq
              : c == '!' ? token_type::ne
              : c == '<' ? token_type::le
              : c == '>' ? token_type::ge
              : c == '&' ? token_type::and_op
                         : token_type::or_op,
              src_.substr(start, 2), start};
    }
  }
  ++pos_;
  switch (c) {
  case '<':
    return {token_type::lt, src_.substr(start, 1), start};
  case '>':
    return {token_type::gt, src_.substr(start, 1), start};
  case '!':
    return {token_type::not_op, src_.substr(start, 1), start};
  case '+':
    return {token_type::plus, src_.substr(start, 1), start};
  case '-':
    return {token_type::minus, src_.substr(start, 1), start};
  case '*':
    return {token_type::star, src_.substr(start, 1), start};
  case '/':
    return {token_type::slash, src_.substr(start, 1), start};
  case '%':
    return {token_type::percent, src_.substr(start, 1), start};
  case '.':
    return {token_type::dot, src_.substr(start, 1), start};
  case '(':
    return {token_type::lparen, src_.substr(start, 1), start};
  case ')':
    return {token_type::rparen, src_.substr(start, 1), start};
  case ',':
    return {token_type::comma, src_.substr(start, 1), start};
  default:
    break;
  }
  --pos_;
  if (c == '\'' || c == '"')
    return scan_string(c);
  if (std::isdigit(static_cast<unsigned char>(c)))
    return scan_number();
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
    return scan_identifier();
  ++pos_;
  return {token_type::eof, src_.substr(start, 1), start};
}

auto ast_node::make_literal(expr_value v) -> std::unique_ptr<ast_node> {
  auto n = std::make_unique<ast_node>();
  n->lit_value = std::move(v);
  return n;
}
auto ast_node::make_property(std::vector<std::string> p)
    -> std::unique_ptr<ast_node> {
  auto n = std::make_unique<ast_node>();
  n->kind = ast_kind::property;
  n->path = std::move(p);
  return n;
}
auto ast_node::make_unary(token_type o, std::unique_ptr<ast_node> v)
    -> std::unique_ptr<ast_node> {
  auto n = std::make_unique<ast_node>();
  n->kind = ast_kind::unary;
  n->op = o;
  n->operand = std::move(v);
  return n;
}
auto ast_node::make_binary(token_type o, std::unique_ptr<ast_node> l,
                           std::unique_ptr<ast_node> r)
    -> std::unique_ptr<ast_node> {
  auto n = std::make_unique<ast_node>();
  n->kind = ast_kind::binary;
  n->op = o;
  n->left = std::move(l);
  n->right = std::move(r);
  return n;
}

expr_parser::expr_parser(std::string_view s) : lexer_(s) { advance(); }
auto expr_parser::advance() -> token {
  auto old = cur_;
  cur_ = lexer_.next_token();
  return old;
}
auto expr_parser::match(token_type t) -> bool {
  if (cur_.type != t)
    return false;
  advance();
  return true;
}
auto expr_parser::parse() -> std::unique_ptr<ast_node> { return parse_or(); }
auto expr_parser::parse_or() -> std::unique_ptr<ast_node> {
  auto n = parse_and();
  while (cur_.type == token_type::or_op)
    n = ast_node::make_binary(advance().type, std::move(n), parse_and());
  return n;
}
auto expr_parser::parse_and() -> std::unique_ptr<ast_node> {
  auto n = parse_equality();
  while (cur_.type == token_type::and_op)
    n = ast_node::make_binary(advance().type, std::move(n), parse_equality());
  return n;
}
auto expr_parser::parse_equality() -> std::unique_ptr<ast_node> {
  auto n = parse_comparison();
  while (cur_.type == token_type::eq || cur_.type == token_type::ne)
    n = ast_node::make_binary(advance().type, std::move(n), parse_comparison());
  return n;
}
auto expr_parser::parse_comparison() -> std::unique_ptr<ast_node> {
  auto n = parse_additive();
  while (cur_.type == token_type::lt || cur_.type == token_type::gt ||
         cur_.type == token_type::le || cur_.type == token_type::ge)
    n = ast_node::make_binary(advance().type, std::move(n), parse_additive());
  return n;
}
auto expr_parser::parse_additive() -> std::unique_ptr<ast_node> {
  auto n = parse_multiplicative();
  while (cur_.type == token_type::plus || cur_.type == token_type::minus)
    n = ast_node::make_binary(advance().type, std::move(n),
                              parse_multiplicative());
  return n;
}
auto expr_parser::parse_multiplicative() -> std::unique_ptr<ast_node> {
  auto n = parse_unary();
  while (cur_.type == token_type::star || cur_.type == token_type::slash ||
         cur_.type == token_type::percent)
    n = ast_node::make_binary(advance().type, std::move(n), parse_unary());
  return n;
}
auto expr_parser::parse_unary() -> std::unique_ptr<ast_node> {
  if (cur_.type == token_type::not_op || cur_.type == token_type::minus)
    return ast_node::make_unary(advance().type, parse_unary());
  return parse_postfix();
}
auto expr_parser::parse_postfix() -> std::unique_ptr<ast_node> {
  auto n = parse_primary();
  while (cur_.type == token_type::dot) {
    advance();
    if (cur_.type != token_type::identifier)
      break;
    auto p = std::string(advance().lexeme);
    if (n->kind == ast_kind::property)
      n->path.push_back(std::move(p));
    else
      n = ast_node::make_binary(token_type::dot, std::move(n),
                                ast_node::make_property({std::move(p)}));
  }
  return n;
}
auto expr_parser::parse_primary() -> std::unique_ptr<ast_node> {
  switch (cur_.type) {
  case token_type::integer_lit: {
    auto l = advance().lexeme;
    std::int64_t v{};
    std::from_chars(l.data(), l.data() + l.size(), v);
    return ast_node::make_literal(expr_value::make_int(v));
  }
  case token_type::double_lit: {
    auto l = advance().lexeme;
    try {
      return ast_node::make_literal(
          expr_value::make_double(std::stod(std::string(l))));
    } catch (...) {
      return ast_node::make_literal(expr_value::make_double(0));
    }
  }
  case token_type::string_lit: {
    auto l = advance().lexeme;
    return ast_node::make_literal(expr_value::make_string(
        std::string(l.substr(1, l.size() > 1 ? l.size() - 2 : 0))));
  }
  case token_type::true_lit:
    advance();
    return ast_node::make_literal(expr_value::make_bool(true));
  case token_type::false_lit:
    advance();
    return ast_node::make_literal(expr_value::make_bool(false));
  case token_type::null_lit:
    advance();
    return ast_node::make_literal(expr_value::make_null());
  case token_type::identifier:
    return ast_node::make_property({std::string(advance().lexeme)});
  case token_type::lparen:
    advance();
    {
      auto n = parse_or();
      match(token_type::rparen);
      return n;
    }
  default:
    advance();
    return ast_node::make_literal(expr_value::make_null());
  }
}

auto expr_evaluator::evaluate(const ast_node &n,
                              const property_resolver &c) const -> expr_value {
  switch (n.kind) {
  case ast_kind::literal:
    return n.lit_value;
  case ast_kind::property:
    return eval_property(n, c);
  case ast_kind::unary:
    return eval_unary(n, c);
  case ast_kind::binary:
    return eval_binary(n, c);
  }
  return expr_value::make_null();
}
auto expr_evaluator::eval_property(const ast_node &n,
                                   const property_resolver &c) const
    -> expr_value {
  if (n.path.empty())
    return expr_value::make_null();
  if (n.path.size() == 1)
    return c.resolve(n.path[0]);
  std::string name = n.path[0];
  for (std::size_t i = 1; i < n.path.size(); ++i) {
    name += '.';
    name += n.path[i];
  }
  return c.resolve(name);
}
auto expr_evaluator::eval_unary(const ast_node &n,
                                const property_resolver &c) const
    -> expr_value {
  auto v = evaluate(*n.operand, c);
  if (n.op == token_type::not_op)
    return expr_value::make_bool(!v.is_truthy());
  if (n.op == token_type::minus)
    return v.type == expr_value::type_t::double_type
               ? expr_value::make_double(-v.double_val)
               : expr_value::make_int(-v.to_int());
  return v;
}
auto expr_evaluator::compare_eq(const expr_value &a, const expr_value &b)
    -> bool {
  using T = expr_value::type_t;
  if (a.is_null() || b.is_null())
    return a.is_null() && b.is_null();
  if (a.type == b.type) {
    if (a.type == T::bool_type)
      return a.bool_val == b.bool_val;
    if (a.type == T::int_type)
      return a.int_val == b.int_val;
    if (a.type == T::double_type)
      return a.double_val == b.double_val;
    if (a.type == T::string_type)
      return a.str_val == b.str_val;
  }
  if ((a.type == T::int_type || a.type == T::double_type) &&
      (b.type == T::int_type || b.type == T::double_type))
    return a.to_double() == b.to_double();
  return a.to_string() == b.to_string();
}
auto expr_evaluator::compare_ord(const expr_value &a, const expr_value &b)
    -> int {
  using T = expr_value::type_t;
  if (a.is_null() || b.is_null())
    return 0;
  if ((a.type == T::int_type || a.type == T::double_type) &&
      (b.type == T::int_type || b.type == T::double_type)) {
    auto x = a.to_double(), y = b.to_double();
    return x < y ? -1 : x > y ? 1 : 0;
  }
  return a.to_string().compare(b.to_string());
}
auto expr_evaluator::arithmetic(token_type o, const expr_value &a,
                                const expr_value &b) -> expr_value {
  using T = expr_value::type_t;
  if (o == token_type::plus &&
      (a.type == T::string_type || b.type == T::string_type))
    return expr_value::make_string(a.to_string() + b.to_string());
  if (a.type == T::double_type || b.type == T::double_type) {
    auto x = a.to_double(), y = b.to_double();
    switch (o) {
    case token_type::plus:
      return expr_value::make_double(x + y);
    case token_type::minus:
      return expr_value::make_double(x - y);
    case token_type::star:
      return expr_value::make_double(x * y);
    case token_type::slash:
      return y ? expr_value::make_double(x / y) : expr_value::make_null();
    case token_type::percent:
      return y ? expr_value::make_double(std::fmod(x, y))
               : expr_value::make_null();
    default:
      break;
    }
  }
  auto x = a.to_int(), y = b.to_int();
  switch (o) {
  case token_type::plus:
    return expr_value::make_int(x + y);
  case token_type::minus:
    return expr_value::make_int(x - y);
  case token_type::star:
    return expr_value::make_int(x * y);
  case token_type::slash:
    return y ? expr_value::make_int(x / y) : expr_value::make_null();
  case token_type::percent:
    return y ? expr_value::make_int(x % y) : expr_value::make_null();
  default:
    return expr_value::make_null();
  }
}
auto expr_evaluator::eval_binary(const ast_node &n,
                                 const property_resolver &c) const
    -> expr_value {
  if (n.op == token_type::and_op) {
    auto l = evaluate(*n.left, c);
    return expr_value::make_bool(l.is_truthy() &&
                                 evaluate(*n.right, c).is_truthy());
  }
  if (n.op == token_type::or_op) {
    auto l = evaluate(*n.left, c);
    return expr_value::make_bool(l.is_truthy() ||
                                 evaluate(*n.right, c).is_truthy());
  }
  auto l = evaluate(*n.left, c), r = evaluate(*n.right, c);
  switch (n.op) {
  case token_type::eq:
    return expr_value::make_bool(compare_eq(l, r));
  case token_type::ne:
    return expr_value::make_bool(!compare_eq(l, r));
  case token_type::lt:
    return expr_value::make_bool(compare_ord(l, r) < 0);
  case token_type::gt:
    return expr_value::make_bool(compare_ord(l, r) > 0);
  case token_type::le:
    return expr_value::make_bool(compare_ord(l, r) <= 0);
  case token_type::ge:
    return expr_value::make_bool(compare_ord(l, r) >= 0);
  default:
    return arithmetic(n.op, l, r);
  }
}
auto parse_expr(std::string_view e) -> std::unique_ptr<ast_node> {
  return expr_parser(e).parse();
}
auto eval_expr(std::string_view e, const property_resolver &c) -> expr_value {
  auto a = parse_expr(e);
  return a ? expr_evaluator{}.evaluate(*a, c) : expr_value::make_null();
}
auto eval_ast(const ast_node &a, const property_resolver &c) -> expr_value {
  return expr_evaluator{}.evaluate(a, c);
}
} // namespace cnetmod::mysql::orm
