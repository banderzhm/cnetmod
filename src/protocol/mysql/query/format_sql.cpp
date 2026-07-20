module cnetmod.protocol.mysql;
import :format_sql;

namespace cnetmod::mysql {
format_context::format_context(format_options opts) noexcept
    : opts_(std::move(opts)) {}

format_context::format_context(format_options opts,
                               std::string storage) noexcept
    : opts_(std::move(opts)), output_(std::move(storage)) {
  output_.clear();
}

auto format_context::append_raw(std::string_view sql) -> format_context & {
  output_.append(sql);
  return *this;
}

auto format_context::append_value(const param_value &value)
    -> format_context & {
  format_one(value);
  return *this;
}

void format_context::add_error(format_errc ec) noexcept {
  if (ec_ == format_errc::ok)
    ec_ = ec;
}
auto format_context::error_state() const noexcept -> format_errc { return ec_; }
auto format_context::format_opts() const noexcept -> const format_options & {
  return opts_;
}

auto format_context::get() && -> std::expected<std::string, format_errc> {
  if (ec_ != format_errc::ok)
    return std::unexpected(ec_);
  return std::move(output_);
}

void format_context::format_one(const param_value &value) {
  using kind = param_value::kind_t;
  switch (value.kind) {
  case kind::null_kind:
    output_.append("NULL");
    break;
  case kind::int64_kind:
    output_.append(std::to_string(value.int_val));
    break;
  case kind::uint64_kind:
    output_.append(std::to_string(value.uint_val));
    break;
  case kind::double_kind:
    if (std::isnan(value.double_val) || std::isinf(value.double_val)) {
      add_error(format_errc::invalid_encoding);
      return;
    }
    output_.append(std::format("{}", value.double_val));
    break;
  case kind::string_kind: {
    std::string escaped;
    escape_string(value.str_val, opts_, quoting_context::single_quote, escaped);
    output_.append("'").append(escaped).append("'");
    break;
  }
  case kind::blob_kind:
    output_.append("X'");
    for (unsigned char c : value.str_val)
      output_.append(std::format("{:02X}", c));
    output_.append("'");
    break;
  case kind::date_kind:
    output_.append("'").append(value.date_val.to_string()).append("'");
    break;
  case kind::datetime_kind:
    output_.append("'").append(value.datetime_val.to_string()).append("'");
    break;
  case kind::time_kind:
    output_.append("'").append(value.time_val.to_string()).append("'");
    break;
  }
}

void format_sql_to(format_context &ctx, std::string_view fmt,
                   std::span<const param_value> args) {
  std::size_t automatic = 0;
  bool used_automatic = false, used_manual = false;
  for (std::size_t i = 0; i < fmt.size();) {
    if (i + 1 < fmt.size() && fmt[i] == '{' && fmt[i + 1] == '{') {
      ctx.append_raw("{");
      i += 2;
      continue;
    }
    if (i + 1 < fmt.size() && fmt[i] == '}' && fmt[i + 1] == '}') {
      ctx.append_raw("}");
      i += 2;
      continue;
    }
    if (fmt[i] != '{') {
      auto next = fmt.find_first_of("{}", i);
      if (next == std::string_view::npos)
        next = fmt.size();
      ctx.append_raw(fmt.substr(i, next - i));
      i = next;
      continue;
    }
    const auto close = fmt.find('}', ++i);
    if (close == std::string_view::npos) {
      ctx.add_error(format_errc::invalid_format_string);
      return;
    }
    const auto spec = fmt.substr(i, close - i);
    std::size_t index{};
    if (spec.empty()) {
      used_automatic = true;
      index = automatic++;
    } else {
      used_manual = true;
      const auto [ptr, ec] =
          std::from_chars(spec.data(), spec.data() + spec.size(), index);
      if (ec != std::errc{} || ptr != spec.data() + spec.size()) {
        ctx.add_error(format_errc::invalid_format_string);
        return;
      }
    }
    if (used_automatic && used_manual) {
      ctx.add_error(format_errc::manual_auto_mix);
      return;
    }
    if (index >= args.size()) {
      ctx.add_error(format_errc::arg_not_found);
      return;
    }
    ctx.append_value(args[index]);
    i = close + 1;
  }
}

auto format_sql(const format_options &opts, std::string_view fmt,
                std::span<const param_value> args)
    -> std::expected<std::string, format_errc> {
  format_context ctx(opts);
  format_sql_to(ctx, fmt, args);
  return std::move(ctx).get();
}

auto format_sql(const format_options &opts, std::string_view fmt)
    -> std::expected<std::string, format_errc> {
  return format_sql(opts, fmt, {});
}

auto with_params(std::string_view query,
                 std::initializer_list<param_value> args) -> with_params_t {
  return {query, std::vector<param_value>(args)};
}

auto with_params(std::string_view query, std::vector<param_value> args)
    -> with_params_t {
  return {query, std::move(args)};
}
} // namespace cnetmod::mysql