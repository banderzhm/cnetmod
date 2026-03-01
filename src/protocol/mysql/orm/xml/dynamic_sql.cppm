export module cnetmod.protocol.mysql:orm_dynamic_sql;

import std;
import :types;
import :format_sql;
import :orm_expr;
import :orm_reflect;
import :orm_xml_parser;

// Debug macro - define CNETMOD_ORM_DEBUG to enable debug output
#ifdef CNETMOD_ORM_DEBUG
    #define ORM_DEBUG_LOG(...) std::println(__VA_ARGS__)
#else
    #define ORM_DEBUG_LOG(...) ((void)0)
#endif

namespace cnetmod::mysql::orm {

// =============================================================================
// built_dynamic_sql — Result of dynamic SQL processing
// =============================================================================

export struct built_dynamic_sql {
    std::string sql;
    std::vector<param_value> params;
};

// =============================================================================
// fragment_map — <sql id="..."> fragments for <include>
// =============================================================================

export using fragment_map = std::unordered_map<std::string, const xml_node*>;

// =============================================================================
// dynamic_sql_processor — Processes XML nodes into SQL + params
// =============================================================================

export class dynamic_sql_processor {
public:
    explicit dynamic_sql_processor(const format_options& opts) : opts_(opts) {}

    auto process(const xml_node& stmt_node,
                 const param_context& ctx,
                 const fragment_map& fragments) -> built_dynamic_sql {
        sql_buf_.clear();
        params_.clear();
        // Create mutable copy for bind support
        param_context mutable_ctx = ctx;
        process_children(stmt_node, mutable_ctx, fragments);
        return {std::move(sql_buf_), std::move(params_)};
    }

private:
    format_options opts_;
    std::string    sql_buf_;
    std::vector<param_value> params_;

    // Expression cache for repeated evaluations
    mutable std::unordered_map<std::string, std::unique_ptr<ast_node>> expr_cache_;

    // Convert param_value to string for direct substitution (${})
    static auto param_value_to_string(const param_value& val) -> std::string {
        switch (val.kind) {
            case param_value::kind_t::null_kind:
                return "NULL";
            case param_value::kind_t::int64_kind:
                return std::to_string(val.int_val);
            case param_value::kind_t::uint64_kind:
                return std::to_string(val.uint_val);
            case param_value::kind_t::double_kind:
                return std::to_string(val.double_val);
            case param_value::kind_t::string_kind:
                // For ${}, we don't add quotes - user controls the format
                return val.str_val;
            case param_value::kind_t::blob_kind:
                // Blob shouldn't be used with ${}, but return hex representation
                return "<blob>";
            case param_value::kind_t::date_kind:
                return std::format("{:04d}-{:02d}-{:02d}",
                    val.date_val.year, val.date_val.month, val.date_val.day);
            case param_value::kind_t::datetime_kind:
                return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                    val.datetime_val.year, val.datetime_val.month, val.datetime_val.day,
                    val.datetime_val.hour, val.datetime_val.minute, val.datetime_val.second);
            case param_value::kind_t::time_kind:
                return std::format("{:02d}:{:02d}:{:02d}",
                    val.time_val.hours, val.time_val.minutes, val.time_val.seconds);
            default:
                return "";
        }
    }

    // Helper: Check if field name indicates a timestamp field
    static auto is_timestamp_field(std::string_view field_name) -> bool {
        // Common timestamp field patterns
        return field_name.ends_with("_at") ||
               field_name.ends_with("_time") ||
               field_name.ends_with("Time") ||
               field_name.ends_with("Timestamp") ||
               field_name == "timestamp" ||
               field_name == "created" ||
               field_name == "updated" ||
               field_name == "deleted";
    }

    // Helper: Convert Unix timestamp to mysql_datetime using C++20 chrono
    static auto unix_timestamp_to_datetime(std::int64_t timestamp) -> param_value {
        using namespace std::chrono;

        // Convert Unix timestamp to system_clock time_point
        auto tp = system_clock::time_point{seconds{timestamp}};

        // Convert to year_month_day and hh_mm_ss
        auto dp = floor<days>(tp);
        auto ymd = year_month_day{dp};
        auto time = hh_mm_ss{tp - dp};

        mysql_datetime dt;
        dt.year = static_cast<std::uint16_t>(static_cast<int>(ymd.year()));
        dt.month = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.month()));
        dt.day = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.day()));
        dt.hour = static_cast<std::uint8_t>(time.hours().count());
        dt.minute = static_cast<std::uint8_t>(time.minutes().count());
        dt.second = static_cast<std::uint8_t>(time.seconds().count());
        dt.microsecond = 0;

        param_value result;
        result.kind = param_value::kind_t::datetime_kind;
        result.datetime_val = dt;
        return result;
    }


    auto eval_test(std::string_view test_expr, const param_context& ctx) const -> bool {
        if (test_expr.empty()) return true;

        // Check cache
        std::string key(test_expr);
        auto it = expr_cache_.find(key);
        if (it == expr_cache_.end()) {
            auto ast = parse_expr(test_expr);
            it = expr_cache_.emplace(key, std::move(ast)).first;
        }

        if (!it->second) return false;
        return eval_ast(*it->second, ctx).is_truthy();
    }

    void process_children(const xml_node& node, param_context& ctx,
                          const fragment_map& fragments) {
        for (auto& child : node.children) {
            if (child.is_text) {
                process_text(child.text, ctx);
            } else if (child.element) {
                process_node(*child.element, ctx, fragments);
            }
        }
    }

    void process_node(const xml_node& node, param_context& ctx,
                      const fragment_map& fragments) {
        auto& tag = node.tag;
        if (tag == "if")        { process_if(node, ctx, fragments); }
        else if (tag == "where")    { process_where(node, ctx, fragments); }
        else if (tag == "set")      { process_set(node, ctx, fragments); }
        else if (tag == "trim")     { process_trim(node, ctx, fragments); }
        else if (tag == "foreach")  { process_foreach(node, ctx, fragments); }
        else if (tag == "choose")   { process_choose(node, ctx, fragments); }
        else if (tag == "include")  { process_include(node, ctx, fragments); }
        else if (tag == "bind")     { process_bind(node, ctx, fragments); }
        else {
            // Unknown tag — process children
            process_children(node, ctx, fragments);
        }
    }

    // #{param} and ${param} substitution:
    // - #{param}: Parameterized query (safe, adds to params_ vector)
    // - ${param}: Direct string substitution (use with caution, for ORDER BY, etc.)
    void process_text(std::string_view text, const param_context& ctx) {
        // Debug: log text being processed
        ORM_DEBUG_LOG("[DEBUG process_text] Input: '{}'", text);

        std::size_t i = 0;
        bool last_was_space = !sql_buf_.empty() && std::isspace(static_cast<unsigned char>(sql_buf_.back()));

        while (i < text.size()) {
            // Find next substitution marker (either #{ or ${)
            auto hash_pos = text.find("#{", i);
            auto dollar_pos = text.find("${", i);

            // Determine which comes first
            std::size_t start;
            bool is_parameterized;

            if (hash_pos == std::string_view::npos && dollar_pos == std::string_view::npos) {
                // No more substitutions
                append_normalized(text.substr(i), last_was_space);
                break;
            } else if (hash_pos == std::string_view::npos) {
                start = dollar_pos;
                is_parameterized = false;
            } else if (dollar_pos == std::string_view::npos) {
                start = hash_pos;
                is_parameterized = true;
            } else {
                start = std::min(hash_pos, dollar_pos);
                is_parameterized = (start == hash_pos);
            }

            // Append text before substitution
            append_normalized(text.substr(i, start - i), last_was_space);
            last_was_space = !sql_buf_.empty() && std::isspace(static_cast<unsigned char>(sql_buf_.back()));

            // Find closing }
            auto end = text.find('}', start + 2);
            if (end == std::string_view::npos) {
                append_normalized(text.substr(start), last_was_space);
                break;
            }

            // Extract param name
            auto param_name = text.substr(start + 2, end - start - 2);
            // Trim whitespace
            while (!param_name.empty() && std::isspace(static_cast<unsigned char>(param_name.front())))
                param_name.remove_prefix(1);
            while (!param_name.empty() && std::isspace(static_cast<unsigned char>(param_name.back())))
                param_name.remove_suffix(1);

            if (is_parameterized) {
                // #{param}: Parameterized query - add placeholder and param
                auto param_val = ctx.get_param(param_name);

                // Auto-convert Unix timestamp to datetime for timestamp fields
                if (param_val.kind == param_value::kind_t::int64_kind &&
                    is_timestamp_field(param_name)) {
                    param_val = unix_timestamp_to_datetime(param_val.int_val);
                    ORM_DEBUG_LOG("[DEBUG] Auto-converted timestamp field '{}' to datetime", param_name);
                }

                sql_buf_.append("{}");
                params_.push_back(param_val);
                ORM_DEBUG_LOG("[DEBUG] Parameterized: #{{{}}}", param_name);
            } else {
                // ${param}: Direct substitution - insert value directly into SQL
                auto param_val = ctx.get_param(param_name);
                std::string value_str = param_value_to_string(param_val);
                sql_buf_.append(value_str);
                ORM_DEBUG_LOG("[DEBUG] Direct substitution: ${{{}}} = '{}'", param_name, value_str);
            }
            last_was_space = false;

            i = end + 1;
        }
    }

    // Helper: append text with whitespace normalization
    void append_normalized(std::string_view text, bool& last_was_space) {
        for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!last_was_space && !sql_buf_.empty()) {
                    sql_buf_.push_back(' ');
                    last_was_space = true;
                }
            } else {
                sql_buf_.push_back(c);
                last_was_space = false;
            }
        }
    }

    void process_if(const xml_node& node, param_context& ctx,
                    const fragment_map& fragments) {
        auto test = node.attr("test");
        if (eval_test(test, ctx))
            process_children(node, ctx, fragments);
    }

    void process_where(const xml_node& node, param_context& ctx,
                       const fragment_map& fragments) {
        // Capture SQL before processing children
        auto before_size = sql_buf_.size();
        process_children(node, ctx, fragments);
        auto after_size = sql_buf_.size();

        if (after_size == before_size) return; // No content added

        // Extract added content - COPY to avoid invalidation
        std::string content = sql_buf_.substr(before_size, after_size - before_size);

        // Trim leading whitespace
        while (!content.empty() && std::isspace(static_cast<unsigned char>(content.front())))
            content.erase(0, 1);

        // Strip leading AND/OR
        if (content.starts_with("AND ") || content.starts_with("and "))
            content.erase(0, 4);
        else if (content.starts_with("OR ") || content.starts_with("or "))
            content.erase(0, 3);

        // Rebuild: WHERE + trimmed content
        sql_buf_.resize(before_size);
        sql_buf_.append(" WHERE ");
        sql_buf_.append(content);
    }

    void process_set(const xml_node& node, param_context& ctx,
                     const fragment_map& fragments) {
        auto before_size = sql_buf_.size();
        process_children(node, ctx, fragments);
        auto after_size = sql_buf_.size();

        if (after_size == before_size) return;

        // Extract added content - COPY to avoid invalidation
        std::string content = sql_buf_.substr(before_size, after_size - before_size);

        // Trim trailing whitespace and commas
        while (!content.empty() && (std::isspace(static_cast<unsigned char>(content.back())) ||
                                    content.back() == ','))
            content.pop_back();

        sql_buf_.resize(before_size);
        sql_buf_.append(" SET ");
        sql_buf_.append(content);
    }

    void process_trim(const xml_node& node, param_context& ctx,
                      const fragment_map& fragments) {
        auto prefix = node.attr("prefix");
        auto suffix = node.attr("suffix");
        auto prefix_overrides = node.attr("prefixOverrides");
        auto suffix_overrides = node.attr("suffixOverrides");

        auto before_size = sql_buf_.size();
        process_children(node, ctx, fragments);
        auto after_size = sql_buf_.size();

        if (after_size == before_size) return;

        // Extract added content - COPY to avoid invalidation
        std::string content = sql_buf_.substr(before_size, after_size - before_size);

        // Strip prefix overrides
        if (!prefix_overrides.empty()) {
            content = strip_leading(content, prefix_overrides);
        }

        // Strip suffix overrides
        if (!suffix_overrides.empty()) {
            content = strip_trailing(content, suffix_overrides);
        }

        sql_buf_.resize(before_size);
        if (!prefix.empty()) sql_buf_.append(prefix);
        sql_buf_.append(content);
        if (!suffix.empty()) sql_buf_.append(suffix);
    }

    void process_foreach(const xml_node& node, param_context& ctx,
                         const fragment_map& fragments) {
        auto collection_name = node.attr("collection");
        auto item_name = node.attr("item");
        auto open = node.attr("open");
        auto close = node.attr("close");
        auto separator = node.attr("separator");

        auto* collection = ctx.get_collection(collection_name);
        if (!collection || collection->empty()) return;

        if (!open.empty()) sql_buf_.append(open);

        bool first = true;
        for (auto& item_ctx : *collection) {
            if (!first && !separator.empty()) sql_buf_.append(separator);
            first = false;

            // Create a merged context: parent + item
            param_context merged = ctx;

            // Merge item context values directly into merged context
            // This allows #{id} to work when item="id" and the item context contains {"id": value}
            for (auto& key : item_ctx.keys()) {
                merged.set(key, item_ctx.get_param(key));
            }

            // Also add as nested for compatibility (allows #{item.field} syntax)
            if (!item_name.empty()) {
                merged.add_nested(std::string(item_name), item_ctx);
            }

            process_children(node, merged, fragments);
        }

        if (!close.empty()) sql_buf_.append(close);
    }

    void process_choose(const xml_node& node, param_context& ctx,
                        const fragment_map& fragments) {
        for (auto& child : node.children) {
            if (child.is_text) continue;
            if (!child.element) continue;

            auto& tag = child.element->tag;
            if (tag == "when") {
                auto test = child.element->attr("test");
                if (eval_test(test, ctx)) {
                    process_children(*child.element, ctx, fragments);
                    return; // First match wins
                }
            } else if (tag == "otherwise") {
                process_children(*child.element, ctx, fragments);
                return;
            }
        }
    }

    void process_include(const xml_node& node, param_context& ctx,
                         const fragment_map& fragments) {
        auto refid = node.attr("refid");
        ORM_DEBUG_LOG("[DEBUG process_include] refid='{}', fragments.size()={}", refid, fragments.size());

        auto it = fragments.find(std::string(refid));
        if (it != fragments.end()) {
            ORM_DEBUG_LOG("[DEBUG process_include] Found fragment, tag='{}', children.size()={}",
                it->second->tag, it->second->children.size());

            // Add leading space if SQL buffer doesn't end with whitespace
            if (!sql_buf_.empty() && !std::isspace(static_cast<unsigned char>(sql_buf_.back()))) {
                sql_buf_.push_back(' ');
            }

            process_children(*it->second, ctx, fragments);

            // Add trailing space to prevent concatenation with following text
            if (!sql_buf_.empty() && !std::isspace(static_cast<unsigned char>(sql_buf_.back()))) {
                sql_buf_.push_back(' ');
            }
        } else {
            ORM_DEBUG_LOG("[DEBUG process_include] Fragment '{}' NOT FOUND", refid);
        }
    }

    // <bind name="var" value="expression"/>
    // Evaluates expression and binds result to a variable in context
    void process_bind(const xml_node& node, param_context& ctx,
                      const fragment_map& fragments) {
        auto name = node.attr("name");
        auto value_expr = node.attr("value");

        if (name.empty() || value_expr.empty()) return;

        // Evaluate expression
        auto result = eval_expr(value_expr, ctx);

        // Bind to context
        ctx.set(std::string(name), result.to_param());

        // Process children (if any)
        process_children(node, ctx, fragments);
    }

    // Helper: strip leading tokens (e.g., "AND|OR")
    static auto strip_leading(std::string s, std::string_view prefixes) -> std::string {
        // Trim whitespace first
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.erase(0, 1);

        // Split prefixes by '|'
        std::size_t i = 0;
        while (i < prefixes.size()) {
            auto pipe = prefixes.find('|', i);
            auto token = prefixes.substr(i, pipe == std::string_view::npos ? pipe : pipe - i);
            if (s.starts_with(token)) {
                s.erase(0, token.size());
                // Trim whitespace after removal
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                    s.erase(0, 1);
                return s;
            }
            i = (pipe == std::string_view::npos) ? prefixes.size() : pipe + 1;
        }
        return s;
    }

    static auto strip_trailing(std::string s, std::string_view suffixes) -> std::string {
        // Trim whitespace first
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.pop_back();

        // Split suffixes by '|'
        std::size_t i = 0;
        while (i < suffixes.size()) {
            auto pipe = suffixes.find('|', i);
            auto token = suffixes.substr(i, pipe == std::string_view::npos ? pipe : pipe - i);
            if (s.ends_with(token)) {
                s.erase(s.size() - token.size());
                // Trim whitespace after removal
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                    s.pop_back();
                return s;
            }
            i = (pipe == std::string_view::npos) ? suffixes.size() : pipe + 1;
        }
        return s;
    }
};

} // namespace cnetmod::mysql::orm