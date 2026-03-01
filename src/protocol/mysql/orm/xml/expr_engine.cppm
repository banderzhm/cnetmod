export module cnetmod.protocol.mysql:orm_expr;

import std;
import :types;

namespace cnetmod::mysql::orm {

// =============================================================================
// expr_value — Dynamically-typed value for expression evaluation
// =============================================================================

export struct expr_value {
    enum class type_t : std::uint8_t {
        null_type, bool_type, int_type, double_type, string_type
    };

    type_t      type       = type_t::null_type;
    bool        bool_val   = false;
    std::int64_t int_val   = 0;
    double      double_val = 0.0;
    std::string str_val;

    auto is_truthy() const noexcept -> bool {
        switch (type) {
        case type_t::null_type:   return false;
        case type_t::bool_type:   return bool_val;
        case type_t::int_type:    return int_val != 0;
        case type_t::double_type: return double_val != 0.0;
        case type_t::string_type: return !str_val.empty();
        }
        return false;
    }

    auto is_null() const noexcept -> bool { return type == type_t::null_type; }

    auto to_double() const noexcept -> double {
        switch (type) {
        case type_t::int_type:    return static_cast<double>(int_val);
        case type_t::double_type: return double_val;
        case type_t::bool_type:   return bool_val ? 1.0 : 0.0;
        default:                  return 0.0;
        }
    }

    auto to_int() const noexcept -> std::int64_t {
        switch (type) {
        case type_t::int_type:    return int_val;
        case type_t::double_type: return static_cast<std::int64_t>(double_val);
        case type_t::bool_type:   return bool_val ? 1 : 0;
        default:                  return 0;
        }
    }

    auto to_string() const -> std::string {
        switch (type) {
        case type_t::null_type:   return "null";
        case type_t::bool_type:   return bool_val ? "true" : "false";
        case type_t::int_type:    return std::to_string(int_val);
        case type_t::double_type: return std::format("{}", double_val);
        case type_t::string_type: return str_val;
        }
        return "";
    }

    // Factories
    static auto make_null() -> expr_value { return {}; }

    static auto make_bool(bool v) -> expr_value {
        expr_value r; r.type = type_t::bool_type; r.bool_val = v; return r;
    }

    static auto make_int(std::int64_t v) -> expr_value {
        expr_value r; r.type = type_t::int_type; r.int_val = v; return r;
    }

    static auto make_double(double v) -> expr_value {
        expr_value r; r.type = type_t::double_type; r.double_val = v; return r;
    }

    static auto make_string(std::string v) -> expr_value {
        expr_value r; r.type = type_t::string_type; r.str_val = std::move(v); return r;
    }

    static auto from_param(const param_value& pv) -> expr_value {
        using K = param_value::kind_t;
        switch (pv.kind) {
        case K::null_kind:     return make_null();
        case K::int64_kind:    return make_int(pv.int_val);
        case K::uint64_kind:   return make_int(static_cast<std::int64_t>(pv.uint_val));
        case K::double_kind:   return make_double(pv.double_val);
        case K::string_kind:   return make_string(std::string(pv.str_val));
        case K::blob_kind:     return make_string(std::string(pv.str_val));
        case K::date_kind:     return make_string(pv.date_val.to_string());
        case K::datetime_kind: return make_string(pv.datetime_val.to_string());
        case K::time_kind:     return make_string(pv.time_val.to_string());
        }
        return make_null();
    }

    auto to_param() const -> param_value {
        switch (type) {
        case type_t::null_type:   return param_value::null();
        case type_t::bool_type:   return param_value::from_int(bool_val ? 1 : 0);
        case type_t::int_type:    return param_value::from_int(int_val);
        case type_t::double_type: return param_value::from_double(double_val);
        case type_t::string_type: return param_value::from_string(str_val);
        }
        return param_value::null();
    }
};

// =============================================================================
// Token types
// =============================================================================

export enum class token_type : std::uint8_t {
    // Literals
    integer_lit, double_lit, string_lit,
    true_lit, false_lit, null_lit,
    // Identifier
    identifier,
    // Comparison
    eq, ne, lt, gt, le, ge,
    // Logical
    and_op, or_op, not_op,
    // Arithmetic
    plus, minus, star, slash, percent,
    // Punctuation
    dot, lparen, rparen, comma,
    // End
    eof,
};

export struct token {
    token_type       type   = token_type::eof;
    std::string_view lexeme;
    std::size_t      pos    = 0;
};

// =============================================================================
// Lexer
// =============================================================================

export class expr_lexer {
public:
    explicit expr_lexer(std::string_view src) noexcept : src_(src) {}

    auto next_token() -> token {
        skip_whitespace();
        if (pos_ >= src_.size()) return {token_type::eof, {}, pos_};

        auto start = pos_;
        char c = src_[pos_];

        // Two-char operators
        if (pos_ + 1 < src_.size()) {
            char c2 = src_[pos_ + 1];
            if (c == '=' && c2 == '=') { pos_ += 2; return {token_type::eq, src_.substr(start, 2), start}; }
            if (c == '!' && c2 == '=') { pos_ += 2; return {token_type::ne, src_.substr(start, 2), start}; }
            if (c == '<' && c2 == '=') { pos_ += 2; return {token_type::le, src_.substr(start, 2), start}; }
            if (c == '>' && c2 == '=') { pos_ += 2; return {token_type::ge, src_.substr(start, 2), start}; }
            if (c == '&' && c2 == '&') { pos_ += 2; return {token_type::and_op, src_.substr(start, 2), start}; }
            if (c == '|' && c2 == '|') { pos_ += 2; return {token_type::or_op, src_.substr(start, 2), start}; }
        }

        // Single-char operators
        ++pos_;
        switch (c) {
        case '<': return {token_type::lt, src_.substr(start, 1), start};
        case '>': return {token_type::gt, src_.substr(start, 1), start};
        case '!': return {token_type::not_op, src_.substr(start, 1), start};
        case '+': return {token_type::plus, src_.substr(start, 1), start};
        case '-': return {token_type::minus, src_.substr(start, 1), start};
        case '*': return {token_type::star, src_.substr(start, 1), start};
        case '/': return {token_type::slash, src_.substr(start, 1), start};
        case '%': return {token_type::percent, src_.substr(start, 1), start};
        case '.': return {token_type::dot, src_.substr(start, 1), start};
        case '(': return {token_type::lparen, src_.substr(start, 1), start};
        case ')': return {token_type::rparen, src_.substr(start, 1), start};
        case ',': return {token_type::comma, src_.substr(start, 1), start};
        default: break;
        }
        --pos_; // undo

        // String literal
        if (c == '\'' || c == '"') return scan_string(c);

        // Number
        if (std::isdigit(static_cast<unsigned char>(c))) return scan_number();

        // Identifier / keyword
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            return scan_identifier();

        // Unknown char — skip and return eof
        ++pos_;
        return {token_type::eof, src_.substr(start, 1), start};
    }

private:
    std::string_view src_;
    std::size_t      pos_ = 0;

    void skip_whitespace() noexcept {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_])))
            ++pos_;
    }

    auto scan_number() -> token {
        auto start = pos_;
        bool has_dot = false;
        while (pos_ < src_.size()) {
            char ch = src_[pos_];
            if (ch == '.' && !has_dot) { has_dot = true; ++pos_; }
            else if (std::isdigit(static_cast<unsigned char>(ch))) ++pos_;
            else break;
        }
        auto lexeme = src_.substr(start, pos_ - start);
        return {has_dot ? token_type::double_lit : token_type::integer_lit, lexeme, start};
    }

    auto scan_string(char quote) -> token {
        auto start = pos_;
        ++pos_; // skip opening quote
        while (pos_ < src_.size() && src_[pos_] != quote) {
            if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) ++pos_; // skip escape
            ++pos_;
        }
        if (pos_ < src_.size()) ++pos_; // skip closing quote
        return {token_type::string_lit, src_.substr(start, pos_ - start), start};
    }

    auto scan_identifier() -> token {
        auto start = pos_;
        while (pos_ < src_.size()) {
            char ch = src_[pos_];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') ++pos_;
            else break;
        }
        auto lexeme = src_.substr(start, pos_ - start);

        // Keywords
        if (lexeme == "and")   return {token_type::and_op, lexeme, start};
        if (lexeme == "or")    return {token_type::or_op, lexeme, start};
        if (lexeme == "not")   return {token_type::not_op, lexeme, start};
        if (lexeme == "true")  return {token_type::true_lit, lexeme, start};
        if (lexeme == "false") return {token_type::false_lit, lexeme, start};
        if (lexeme == "null")  return {token_type::null_lit, lexeme, start};
        if (lexeme == "nil")   return {token_type::null_lit, lexeme, start};

        return {token_type::identifier, lexeme, start};
    }
};

// =============================================================================
// AST node
// =============================================================================

export enum class ast_kind : std::uint8_t { literal, property, unary, binary };

export struct ast_node {
    ast_kind kind = ast_kind::literal;

    // literal
    expr_value lit_value;

    // property: ["user", "name"] for user.name
    std::vector<std::string> path;

    // unary / binary operator
    token_type op = token_type::eof;

    // unary operand
    std::unique_ptr<ast_node> operand;

    // binary children
    std::unique_ptr<ast_node> left;
    std::unique_ptr<ast_node> right;

    static auto make_literal(expr_value val) -> std::unique_ptr<ast_node> {
        auto n = std::make_unique<ast_node>();
        n->kind = ast_kind::literal;
        n->lit_value = std::move(val);
        return n;
    }

    static auto make_property(std::vector<std::string> p) -> std::unique_ptr<ast_node> {
        auto n = std::make_unique<ast_node>();
        n->kind = ast_kind::property;
        n->path = std::move(p);
        return n;
    }

    static auto make_unary(token_type op, std::unique_ptr<ast_node> operand) -> std::unique_ptr<ast_node> {
        auto n = std::make_unique<ast_node>();
        n->kind = ast_kind::unary;
        n->op = op;
        n->operand = std::move(operand);
        return n;
    }

    static auto make_binary(token_type op, std::unique_ptr<ast_node> l,
                            std::unique_ptr<ast_node> r) -> std::unique_ptr<ast_node> {
        auto n = std::make_unique<ast_node>();
        n->kind = ast_kind::binary;
        n->op = op;
        n->left = std::move(l);
        n->right = std::move(r);
        return n;
    }
};

// =============================================================================
// Parser — Recursive descent with operator precedence
// =============================================================================

export class expr_parser {
public:
    explicit expr_parser(std::string_view src) : lexer_(src) {
        advance();
    }

    auto parse() -> std::unique_ptr<ast_node> { return parse_or(); }

private:
    expr_lexer lexer_;
    token      cur_{};

    auto advance() -> token {
        auto prev = cur_;
        cur_ = lexer_.next_token();
        return prev;
    }

    auto match(token_type t) -> bool {
        if (cur_.type == t) { advance(); return true; }
        return false;
    }

    // Level 1: || / or
    auto parse_or() -> std::unique_ptr<ast_node> {
        auto node = parse_and();
        while (cur_.type == token_type::or_op) {
            auto op = advance().type;
            node = ast_node::make_binary(op, std::move(node), parse_and());
        }
        return node;
    }

    // Level 2: && / and
    auto parse_and() -> std::unique_ptr<ast_node> {
        auto node = parse_equality();
        while (cur_.type == token_type::and_op) {
            auto op = advance().type;
            node = ast_node::make_binary(op, std::move(node), parse_equality());
        }
        return node;
    }

    // Level 3: == !=
    auto parse_equality() -> std::unique_ptr<ast_node> {
        auto node = parse_comparison();
        while (cur_.type == token_type::eq || cur_.type == token_type::ne) {
            auto op = advance().type;
            node = ast_node::make_binary(op, std::move(node), parse_comparison());
        }
        return node;
    }

    // Level 4: < > <= >=
    auto parse_comparison() -> std::unique_ptr<ast_node> {
        auto node = parse_additive();
        while (cur_.type == token_type::lt || cur_.type == token_type::gt ||
               cur_.type == token_type::le || cur_.type == token_type::ge) {
            auto op = advance().type;
            node = ast_node::make_binary(op, std::move(node), parse_additive());
        }
        return node;
    }

    // Level 5: + -
    auto parse_additive() -> std::unique_ptr<ast_node> {
        auto node = parse_multiplicative();
        while (cur_.type == token_type::plus || cur_.type == token_type::minus) {
            auto op = advance().type;
            node = ast_node::make_binary(op, std::move(node), parse_multiplicative());
        }
        return node;
    }

    // Level 6: * / %
    auto parse_multiplicative() -> std::unique_ptr<ast_node> {
        auto node = parse_unary();
        while (cur_.type == token_type::star || cur_.type == token_type::slash ||
               cur_.type == token_type::percent) {
            auto op = advance().type;
            node = ast_node::make_binary(op, std::move(node), parse_unary());
        }
        return node;
    }

    // Level 7: unary ! not -
    auto parse_unary() -> std::unique_ptr<ast_node> {
        if (cur_.type == token_type::not_op || cur_.type == token_type::minus) {
            auto op = advance().type;
            return ast_node::make_unary(op, parse_unary());
        }
        return parse_postfix();
    }

    // Level 8: dot access (property.sub)
    auto parse_postfix() -> std::unique_ptr<ast_node> {
        auto node = parse_primary();
        while (cur_.type == token_type::dot) {
            advance(); // skip '.'
            if (cur_.type != token_type::identifier) break;
            auto name = std::string(advance().lexeme);
            // Merge into property path if left is already a property
            if (node->kind == ast_kind::property) {
                node->path.push_back(std::move(name));
            } else {
                std::vector<std::string> path;
                path.push_back(std::move(name));
                node = ast_node::make_binary(token_type::dot, std::move(node),
                           ast_node::make_property(std::move(path)));
            }
        }
        return node;
    }

    // Level 9: primary — literals, identifiers, (expr)
    auto parse_primary() -> std::unique_ptr<ast_node> {
        switch (cur_.type) {
        case token_type::integer_lit: {
            auto lexeme = advance().lexeme;
            std::int64_t val = 0;
            std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), val);
            return ast_node::make_literal(expr_value::make_int(val));
        }
        case token_type::double_lit: {
            auto lexeme = advance().lexeme;
            double val = 0;
            std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), val);
            return ast_node::make_literal(expr_value::make_double(val));
        }
        case token_type::string_lit: {
            auto lexeme = advance().lexeme;
            // Strip quotes
            auto inner = lexeme.substr(1, lexeme.size() >= 2 ? lexeme.size() - 2 : 0);
            return ast_node::make_literal(expr_value::make_string(std::string(inner)));
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
        case token_type::identifier: {
            auto name = std::string(advance().lexeme);
            std::vector<std::string> path;
            path.push_back(std::move(name));
            return ast_node::make_property(std::move(path));
        }
        case token_type::lparen: {
            advance(); // skip '('
            auto node = parse_or();
            match(token_type::rparen); // skip ')'
            return node;
        }
        default:
            // Error recovery: return null literal
            advance();
            return ast_node::make_literal(expr_value::make_null());
        }
    }
};

// =============================================================================
// Property resolver interface — implemented by param_context in reflect.cppm
// =============================================================================

export class property_resolver {
public:
    virtual ~property_resolver() = default;
    virtual auto resolve(std::string_view name) const -> expr_value = 0;
    virtual auto has(std::string_view name) const -> bool = 0;
};

// =============================================================================
// Evaluator — walks AST and produces expr_value
// =============================================================================

export class expr_evaluator {
public:
    auto evaluate(const ast_node& node, const property_resolver& ctx) const -> expr_value {
        switch (node.kind) {
        case ast_kind::literal:  return node.lit_value;
        case ast_kind::property: return eval_property(node, ctx);
        case ast_kind::unary:    return eval_unary(node, ctx);
        case ast_kind::binary:   return eval_binary(node, ctx);
        }
        return expr_value::make_null();
    }

private:
    auto eval_property(const ast_node& node, const property_resolver& ctx) const -> expr_value {
        if (node.path.empty()) return expr_value::make_null();
        // For simple "name", resolve directly
        // For "user.name", join with '.' and resolve
        if (node.path.size() == 1) return ctx.resolve(node.path[0]);
        std::string full;
        for (std::size_t i = 0; i < node.path.size(); ++i) {
            if (i > 0) full.push_back('.');
            full.append(node.path[i]);
        }
        return ctx.resolve(full);
    }

    auto eval_unary(const ast_node& node, const property_resolver& ctx) const -> expr_value {
        auto val = evaluate(*node.operand, ctx);
        switch (node.op) {
        case token_type::not_op:
            return expr_value::make_bool(!val.is_truthy());
        case token_type::minus:
            if (val.type == expr_value::type_t::int_type)
                return expr_value::make_int(-val.int_val);
            if (val.type == expr_value::type_t::double_type)
                return expr_value::make_double(-val.double_val);
            return expr_value::make_int(0);
        default:
            return val;
        }
    }

    auto eval_binary(const ast_node& node, const property_resolver& ctx) const -> expr_value {
        // Short-circuit for logical operators
        if (node.op == token_type::and_op) {
            auto lv = evaluate(*node.left, ctx);
            if (!lv.is_truthy()) return expr_value::make_bool(false);
            return expr_value::make_bool(evaluate(*node.right, ctx).is_truthy());
        }
        if (node.op == token_type::or_op) {
            auto lv = evaluate(*node.left, ctx);
            if (lv.is_truthy()) return expr_value::make_bool(true);
            return expr_value::make_bool(evaluate(*node.right, ctx).is_truthy());
        }

        auto lv = evaluate(*node.left, ctx);
        auto rv = evaluate(*node.right, ctx);

        // Comparison operators
        switch (node.op) {
        case token_type::eq: return expr_value::make_bool(compare_eq(lv, rv));
        case token_type::ne: return expr_value::make_bool(!compare_eq(lv, rv));
        case token_type::lt: return expr_value::make_bool(compare_ord(lv, rv) < 0);
        case token_type::gt: return expr_value::make_bool(compare_ord(lv, rv) > 0);
        case token_type::le: return expr_value::make_bool(compare_ord(lv, rv) <= 0);
        case token_type::ge: return expr_value::make_bool(compare_ord(lv, rv) >= 0);
        // Arithmetic
        case token_type::plus:    return arithmetic(node.op, lv, rv);
        case token_type::minus:   return arithmetic(node.op, lv, rv);
        case token_type::star:    return arithmetic(node.op, lv, rv);
        case token_type::slash:   return arithmetic(node.op, lv, rv);
        case token_type::percent: return arithmetic(node.op, lv, rv);
        default: return expr_value::make_null();
        }
    }

    // Equality: null == null is true; type mismatch after coercion is false
    static auto compare_eq(const expr_value& a, const expr_value& b) -> bool {
        using T = expr_value::type_t;
        if (a.is_null() && b.is_null()) return true;
        if (a.is_null() || b.is_null()) return false;

        // Same type — direct compare
        if (a.type == b.type) {
            switch (a.type) {
            case T::bool_type:   return a.bool_val == b.bool_val;
            case T::int_type:    return a.int_val == b.int_val;
            case T::double_type: return a.double_val == b.double_val;
            case T::string_type: return a.str_val == b.str_val;
            default: return false;
            }
        }

        // Numeric promotion: int vs double
        if ((a.type == T::int_type || a.type == T::double_type) &&
            (b.type == T::int_type || b.type == T::double_type))
            return a.to_double() == b.to_double();

        // Fallback: compare as string
        return a.str_val == b.str_val;
    }

    // Ordering: returns <0, 0, >0
    static auto compare_ord(const expr_value& a, const expr_value& b) -> int {
        using T = expr_value::type_t;
        if (a.is_null() || b.is_null()) return 0;

        // Numeric
        if ((a.type == T::int_type || a.type == T::double_type) &&
            (b.type == T::int_type || b.type == T::double_type)) {
            double da = a.to_double(), db = b.to_double();
            return da < db ? -1 : (da > db ? 1 : 0);
        }

        // String
        if (a.type == T::string_type && b.type == T::string_type)
            return a.str_val.compare(b.str_val);

        return 0;
    }

    static auto arithmetic(token_type op, const expr_value& a, const expr_value& b) -> expr_value {
        using T = expr_value::type_t;

        // String concatenation with +
        if (op == token_type::plus &&
            (a.type == T::string_type || b.type == T::string_type))
            return expr_value::make_string(a.to_string() + b.to_string());

        // If either is double, use double arithmetic
        bool use_double = (a.type == T::double_type || b.type == T::double_type);

        if (use_double) {
            double da = a.to_double(), db = b.to_double();
            switch (op) {
            case token_type::plus:    return expr_value::make_double(da + db);
            case token_type::minus:   return expr_value::make_double(da - db);
            case token_type::star:    return expr_value::make_double(da * db);
            case token_type::slash:   return db != 0.0 ? expr_value::make_double(da / db)
                                                       : expr_value::make_null();
            case token_type::percent: return db != 0.0 ? expr_value::make_double(std::fmod(da, db))
                                                       : expr_value::make_null();
            default: break;
            }
        } else {
            auto ia = a.to_int(), ib = b.to_int();
            switch (op) {
            case token_type::plus:    return expr_value::make_int(ia + ib);
            case token_type::minus:   return expr_value::make_int(ia - ib);
            case token_type::star:    return expr_value::make_int(ia * ib);
            case token_type::slash:   return ib != 0 ? expr_value::make_int(ia / ib)
                                                     : expr_value::make_null();
            case token_type::percent: return ib != 0 ? expr_value::make_int(ia % ib)
                                                     : expr_value::make_null();
            default: break;
            }
        }
        return expr_value::make_null();
    }
};

// =============================================================================
// Convenience functions
// =============================================================================

/// Parse an expression string into AST
export inline auto parse_expr(std::string_view expression) -> std::unique_ptr<ast_node> {
    return expr_parser(expression).parse();
}

/// Parse and evaluate an expression in one call
export inline auto eval_expr(std::string_view expression,
                             const property_resolver& ctx) -> expr_value {
    auto ast = parse_expr(expression);
    if (!ast) return expr_value::make_null();
    return expr_evaluator{}.evaluate(*ast, ctx);
}

/// Evaluate a pre-parsed AST
export inline auto eval_ast(const ast_node& ast,
                            const property_resolver& ctx) -> expr_value {
    return expr_evaluator{}.evaluate(ast, ctx);
}

} // namespace cnetmod::mysql::orm
