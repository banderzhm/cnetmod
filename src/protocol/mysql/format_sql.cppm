export module cnetmod.protocol.mysql:format_sql;

import std;
import :types;
import :diagnostics;

export namespace cnetmod::mysql {

enum class format_errc : std::uint8_t {
  ok = 0,
  invalid_format_string,
  arg_not_found,
  invalid_encoding,
  manual_auto_mix
};

class format_context {
public:
  explicit format_context(format_options opts = {}) noexcept;
  format_context(format_options opts, std::string storage) noexcept;
  auto append_raw(std::string_view sql) -> format_context &;
  auto append_value(const param_value &v) -> format_context &;
  void add_error(format_errc ec) noexcept;
  [[nodiscard]] auto error_state() const noexcept -> format_errc;
  [[nodiscard]] auto format_opts() const noexcept -> const format_options &;
  auto get() && -> std::expected<std::string, format_errc>;

private:
  format_options opts_;
  std::string output_;
  format_errc ec_ = format_errc::ok;
  void format_one(const param_value &v);
};

void format_sql_to(format_context &ctx, std::string_view fmt,
                   std::span<const param_value> args);
auto format_sql(const format_options &opts, std::string_view fmt,
                std::span<const param_value> args)
    -> std::expected<std::string, format_errc>;
auto format_sql(const format_options &opts, std::string_view fmt)
    -> std::expected<std::string, format_errc>;

struct with_params_t {
  std::string_view query;
  std::vector<param_value> args;
};
auto with_params(std::string_view query,
                 std::initializer_list<param_value> args) -> with_params_t;
auto with_params(std::string_view query, std::vector<param_value> args)
    -> with_params_t;

template <class Range, class FormatFn> struct format_sequence {
  Range range;
  FormatFn format_function;
  std::string_view glue;
};
template <class Range, class FormatFn>
inline void format_sequence_to(format_context &ctx,
                               const format_sequence<Range, FormatFn> &seq) {
  bool first = true;
  for (auto &&elem : seq.range) {
    if (!first)
      ctx.append_raw(seq.glue);
    first = false;
    seq.format_function(elem, ctx);
  }
}
template <class Range, class FormatFn>
inline auto sequence(Range &&range, FormatFn &&fn, std::string_view glue = ", ")
    -> format_sequence<std::remove_cvref_t<Range>,
                       std::remove_cvref_t<FormatFn>> {
  return {std::forward<Range>(range), std::forward<FormatFn>(fn), glue};
}

} // namespace cnetmod::mysql
