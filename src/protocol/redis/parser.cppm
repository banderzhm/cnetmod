export module cnetmod.protocol.redis:parser;

import std;
import :value;

export namespace cnetmod::redis {
class resp3_parser {
public:
  static constexpr std::size_t max_embedded_depth = 5;
  static constexpr std::string_view sep = "\r\n";
  resp3_parser();
  [[nodiscard]] auto consume(std::string_view data, std::error_code &ec)
      -> std::optional<resp3_node>;
  [[nodiscard]] auto done() const noexcept -> bool;
  [[nodiscard]] auto consumed() const noexcept -> std::size_t;
  [[nodiscard]] auto is_parsing() const noexcept -> bool;
  void reset();

private:
  [[nodiscard]] auto bulk_expected() const noexcept -> bool;
  static auto is_bulk_type(resp3_type type) noexcept -> bool;
  auto parse_length(std::string_view value, std::error_code &ec)
      -> std::int64_t;
  auto consume_bulk_body(std::string_view remaining, std::error_code &ec)
      -> std::optional<std::string>;
  auto consume_simple(resp3_type type, std::string_view body,
                      std::error_code &ec) -> resp3_node;
  auto make_node(resp3_type type, std::string value) -> resp3_node;
  void commit_elem() noexcept;
  std::size_t depth_ = 0;
  std::array<std::size_t, max_embedded_depth + 1> sizes_{};
  std::size_t bulk_length_ = 0;
  resp3_type bulk_type_ = resp3_type::invalid;
  std::size_t consumed_ = 0;
};
[[nodiscard]] auto parse_response(std::string_view data,
                                  std::size_t expected_responses = 1)
    -> std::expected<std::vector<resp3_node>, std::error_code>;
} // namespace cnetmod::redis
