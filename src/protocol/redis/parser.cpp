module cnetmod.protocol.redis;

import std;
import :value;
import :parser;

namespace cnetmod::redis {
resp3_parser::resp3_parser() { reset(); }
auto resp3_parser::bulk_expected() const noexcept -> bool {
  return bulk_type_ != resp3_type::invalid;
}
auto resp3_parser::is_bulk_type(resp3_type type) noexcept -> bool {
  return type == resp3_type::blob_string || type == resp3_type::blob_error ||
         type == resp3_type::verbatim_string ||
         type == resp3_type::streamed_string_part;
}
auto resp3_parser::done() const noexcept -> bool {
  return depth_ == 0 && sizes_[0] == 0;
}
auto resp3_parser::consumed() const noexcept -> std::size_t {
  return consumed_;
}
auto resp3_parser::is_parsing() const noexcept -> bool {
  return consumed_ > 0 || depth_ > 0;
}
void resp3_parser::reset() {
  depth_ = 0;
  sizes_.fill(1);
  bulk_length_ = 0;
  bulk_type_ = resp3_type::invalid;
  consumed_ = 0;
}
auto resp3_parser::parse_length(std::string_view input, std::error_code &ec)
    -> std::int64_t {
  std::int64_t value{};
  const auto [ptr, error] =
      std::from_chars(input.data(), input.data() + input.size(), value);
  if (error != std::errc{}) {
    ec = make_error_code(redis_errc::not_a_number);
    return -1;
  }
  return value;
}
auto resp3_parser::consume_bulk_body(std::string_view remaining,
                                     std::error_code &)
    -> std::optional<std::string> {
  const auto needed = bulk_length_ + 2;
  if (remaining.size() < needed)
    return std::nullopt;
  auto body = std::string(remaining.substr(0, bulk_length_));
  consumed_ += needed;
  bulk_type_ = resp3_type::invalid;
  return body;
}
auto resp3_parser::make_node(resp3_type type, std::string value) -> resp3_node {
  return {type, 0, depth_, std::move(value)};
}
auto resp3_parser::consume_simple(resp3_type type, std::string_view body,
                                  std::error_code &ec) -> resp3_node {
  switch (type) {
  case resp3_type::simple_string:
  case resp3_type::simple_error:
  case resp3_type::number:
  case resp3_type::doublean:
  case resp3_type::big_number:
    return make_node(type, std::string(body));
  case resp3_type::boolean:
    if (body == "t" || body == "f")
      return make_node(type, std::string(body));
    ec = make_error_code(redis_errc::unexpected_bool_value);
    return {};
  case resp3_type::null:
    return make_node(type, {});
  default:
    ec = make_error_code(redis_errc::invalid_data_type);
    return {};
  }
}
void resp3_parser::commit_elem() noexcept {
  while (depth_ > 0) {
    --sizes_[depth_];
    if (sizes_[depth_] > 0)
      return;
    --depth_;
  }
  if (sizes_[0] > 0)
    --sizes_[0];
}
auto resp3_parser::consume(std::string_view data, std::error_code &ec)
    -> std::optional<resp3_node> {
  while (consumed_ < data.size()) {
    auto remaining = data.substr(consumed_);
    if (bulk_expected()) {
      const auto type = bulk_type_;
      auto result = consume_bulk_body(remaining, ec);
      if (ec || !result)
        return std::nullopt;
      auto node = make_node(type, std::move(*result));
      commit_elem();
      return node;
    }
    const auto crlf = remaining.find(sep);
    if (crlf == std::string_view::npos)
      return std::nullopt;
    const auto line = remaining.substr(0, crlf);
    consumed_ += crlf + 2;
    if (line.empty()) {
      ec = make_error_code(redis_errc::invalid_data_type);
      return std::nullopt;
    }
    const auto type = to_type(line.front());
    const auto body = line.substr(1);
    if (type == resp3_type::invalid) {
      ec = make_error_code(redis_errc::invalid_data_type);
      return std::nullopt;
    }
    if (is_bulk_type(type)) {
      const auto length = parse_length(body, ec);
      if (ec)
        return std::nullopt;
      if (length < 0) {
        auto node = make_node(resp3_type::null, {});
        commit_elem();
        return node;
      }
      bulk_length_ = static_cast<std::size_t>(length);
      bulk_type_ = type;
      auto result = consume_bulk_body(data.substr(consumed_), ec);
      if (ec || !result)
        return std::nullopt;
      auto node = make_node(type, std::move(*result));
      commit_elem();
      return node;
    }
    if (is_aggregate(type)) {
      const auto count = parse_length(body, ec);
      if (ec)
        return std::nullopt;
      if (count < 0) {
        auto node = make_node(resp3_type::null, {});
        commit_elem();
        return node;
      }
      const auto size = static_cast<std::size_t>(count);
      resp3_node node{type, size, depth_, {}};
      if (size == 0) {
        commit_elem();
        return node;
      }
      if (depth_ >= max_embedded_depth) {
        ec = make_error_code(redis_errc::exceeds_max_nested_depth);
        return std::nullopt;
      }
      ++depth_;
      sizes_[depth_] = size * element_multiplicity(type);
      return node;
    }
    auto node = consume_simple(type, body, ec);
    if (ec)
      return std::nullopt;
    commit_elem();
    return node;
  }
  return std::nullopt;
}
auto parse_response(std::string_view data, std::size_t expected)
    -> std::expected<std::vector<resp3_node>, std::error_code> {
  std::vector<resp3_node> nodes;
  std::error_code ec;
  for (std::size_t response{}; response < expected; ++response) {
    resp3_parser parser;
    while (!parser.done()) {
      auto node = parser.consume(data, ec);
      if (ec)
        return std::unexpected(ec);
      if (!node)
        return std::unexpected(make_error_code(redis_errc::empty_field));
      nodes.push_back(std::move(*node));
    }
    data.remove_prefix(parser.consumed());
  }
  return nodes;
}
} // namespace cnetmod::redis
