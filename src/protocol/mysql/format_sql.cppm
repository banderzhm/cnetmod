module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:format_sql;

import std;
import :types;
import :diagnostics;

namespace cnetmod::mysql {

// =============================================================================
// format_error — 格式化错误码
// =============================================================================

export enum class format_errc : std::uint8_t {
    ok = 0,
    invalid_format_string,      // 格式字符串语法错误
    arg_not_found,              // 参数索引越界或名称未找到
    invalid_encoding,           // 字符串编码无效
    manual_auto_mix,            // 混合使用 {} 和 {0}
};

// =============================================================================
// format_context — SQL 格式化上下文（参考 Boost.MySQL format_context）
// =============================================================================

export class format_context {
public:
    explicit format_context(format_options opts = {}) noexcept
        : opts_(std::move(opts)) {}

    format_context(format_options opts, std::string storage) noexcept
        : opts_(std::move(opts)), output_(std::move(storage))
    {
        output_.clear();
    }

    // 追加原始 SQL（不转义）
    auto append_raw(std::string_view sql) -> format_context& {
        output_.append(sql);
        return *this;
    }

    // 追加格式化后的 param_value
    auto append_value(const param_value& v) -> format_context& {
        format_one(v);
        return *this;
    }

    // 设置错误
    void add_error(format_errc ec) noexcept {
        if (ec_ == format_errc::ok)
            ec_ = ec;
    }

    auto error_state() const noexcept -> format_errc { return ec_; }
    auto format_opts() const noexcept -> const format_options& { return opts_; }

    // 获取结果
    auto get() && -> std::expected<std::string, format_errc> {
        if (ec_ != format_errc::ok)
            return std::unexpected(ec_);
        return std::move(output_);
    }

private:
    format_options opts_;
    std::string    output_;
    format_errc    ec_ = format_errc::ok;

    void format_one(const param_value& v) {
        using K = param_value::kind_t;
        switch (v.kind) {
        case K::null_kind:
            output_.append("NULL");
            break;

        case K::int64_kind:
            output_.append(std::to_string(v.int_val));
            break;

        case K::uint64_kind:
            output_.append(std::to_string(v.uint_val));
            break;

        case K::double_kind: {
            auto s = std::format("{}", v.double_val);
            // NaN / Inf 不可格式化为 SQL
            if (std::isnan(v.double_val) || std::isinf(v.double_val)) {
                add_error(format_errc::invalid_encoding);
                return;
            }
            output_.append(s);
            break;
        }

        case K::string_kind: {
            std::string escaped;
            escape_string(v.str_val, opts_, quoting_context::single_quote, escaped);
            output_.push_back('\'');
            output_.append(escaped);
            output_.push_back('\'');
            break;
        }

        case K::blob_kind: {
            // X'hex'
            output_.append("X'");
            for (unsigned char c : v.str_val) {
                output_.append(std::format("{:02X}", c));
            }
            output_.push_back('\'');
            break;
        }

        case K::date_kind:
            output_.push_back('\'');
            output_.append(v.date_val.to_string());
            output_.push_back('\'');
            break;

        case K::datetime_kind:
            output_.push_back('\'');
            output_.append(v.datetime_val.to_string());
            output_.push_back('\'');
            break;

        case K::time_kind:
            output_.push_back('\'');
            output_.append(v.time_val.to_string());
            output_.push_back('\'');
            break;
        }
    }
};

// =============================================================================
// format_sql_to — 带占位符的 SQL 格式化（追加到 context）
// =============================================================================
//
// 占位符语法:
//   {}     — 自动索引（依次递增）
//   {0}    — 手动索引
//   {{     — 转义的 '{'
//   }}     — 转义的 '}'

export inline void format_sql_to(
    format_context& ctx,
    std::string_view fmt,
    std::span<const param_value> args)
{
    std::size_t auto_idx = 0;
    bool used_auto = false;
    bool used_manual = false;

    std::size_t i = 0;
    while (i < fmt.size()) {
        // 转义 {{ → {
        if (i + 1 < fmt.size() && fmt[i] == '{' && fmt[i + 1] == '{') {
            ctx.append_raw("{");
            i += 2;
            continue;
        }
        // 转义 }} → }
        if (i + 1 < fmt.size() && fmt[i] == '}' && fmt[i + 1] == '}') {
            ctx.append_raw("}");
            i += 2;
            continue;
        }

        if (fmt[i] == '{') {
            ++i; // skip '{'
            // 查找闭合 '}'
            auto close = fmt.find('}', i);
            if (close == std::string_view::npos) {
                ctx.add_error(format_errc::invalid_format_string);
                return;
            }

            auto spec = fmt.substr(i, close - i);
            std::size_t arg_idx = 0;

            if (spec.empty()) {
                // 自动索引 {}
                used_auto = true;
                arg_idx = auto_idx++;
            } else {
                // 手动索引 {N}
                used_manual = true;
                auto [ptr, ec] = std::from_chars(spec.data(), spec.data() + spec.size(), arg_idx);
                if (ec != std::errc{} || ptr != spec.data() + spec.size()) {
                    ctx.add_error(format_errc::invalid_format_string);
                    return;
                }
            }

            if (used_auto && used_manual) {
                ctx.add_error(format_errc::manual_auto_mix);
                return;
            }

            if (arg_idx >= args.size()) {
                ctx.add_error(format_errc::arg_not_found);
                return;
            }

            ctx.append_value(args[arg_idx]);
            i = close + 1;
            continue;
        }

        // 普通字符 — 批量追加直到下一个 '{' 或 '}'
        auto next = fmt.find_first_of("{}", i);
        if (next == std::string_view::npos) next = fmt.size();
        ctx.append_raw(fmt.substr(i, next - i));
        i = next;
    }
}

// =============================================================================
// format_sql — 一步格式化，返回 SQL 字符串
// =============================================================================

export inline auto format_sql(
    const format_options& opts,
    std::string_view fmt,
    std::span<const param_value> args
) -> std::expected<std::string, format_errc>
{
    format_context ctx(opts);
    format_sql_to(ctx, fmt, args);
    return std::move(ctx).get();
}

/// 无参数便捷重载
export inline auto format_sql(
    const format_options& opts,
    std::string_view fmt
) -> std::expected<std::string, format_errc>
{
    return format_sql(opts, fmt, std::span<const param_value>{});
}

// =============================================================================
// with_params_t — 查询 + 参数绑定（参考 Boost.MySQL with_params）
// =============================================================================
//
// 将格式字符串和参数打包在一起，传给 client::execute(with_params_t)
// 在执行时由客户端自动调用 format_sql 展开。

export struct with_params_t {
    std::string_view              query;
    std::vector<param_value>      args;
};

/// 便捷工厂：with_params("SELECT {} FROM t WHERE id = {}", p1, p2)
export inline auto with_params(
    std::string_view query,
    std::initializer_list<param_value> args
) -> with_params_t
{
    return {query, std::vector<param_value>(args)};
}

/// 可变参数版
export inline auto with_params(
    std::string_view query,
    std::vector<param_value> args
) -> with_params_t
{
    return {query, std::move(args)};
}

// =============================================================================
// format_sequence — 范围格式化助手（参考 Boost.MySQL sequence）
// =============================================================================
//
// 将一个范围的每个元素通过格式化函数写入 SQL，元素间用 glue 字符串连接。
// 用法: sequence(vec, [](const auto& elem, format_context& ctx) { ... }, ", ")

export template <class Range, class FormatFn>
struct format_sequence {
    Range            range;
    FormatFn         format_function;
    std::string_view glue;
};

/// 将 format_sequence 追加到 format_context
export template <class Range, class FormatFn>
inline void format_sequence_to(
    format_context& ctx,
    const format_sequence<Range, FormatFn>& seq)
{
    bool first = true;
    for (auto&& elem : seq.range) {
        if (!first) ctx.append_raw(seq.glue);
        first = false;
        seq.format_function(elem, ctx);
    }
}

/// 便捷工厂：创建 format_sequence
export template <class Range, class FormatFn>
inline auto sequence(
    Range&& range,
    FormatFn&& fn,
    std::string_view glue = ", "
) -> format_sequence<std::remove_cvref_t<Range>, std::remove_cvref_t<FormatFn>>
{
    return {std::forward<Range>(range), std::forward<FormatFn>(fn), glue};
}

} // namespace cnetmod::mysql
