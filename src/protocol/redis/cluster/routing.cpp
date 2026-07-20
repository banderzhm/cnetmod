module cnetmod.protocol.redis;

import std;
import :value;
import :routing;

namespace cnetmod::redis {
namespace {
auto hash_slot(std::string_view key) noexcept -> std::uint16_t {
  const auto open = key.find('{');
  if (open != std::string_view::npos) {
    const auto close = key.find('}', open + 1);
    if (close != std::string_view::npos && close > open + 1)
      key = key.substr(open + 1, close - open - 1);
  }
  std::uint16_t crc{};
  for (const auto byte : key) {
    crc = static_cast<std::uint16_t>(
        crc ^
        static_cast<std::uint16_t>(static_cast<unsigned char>(byte) << 8));
    for (int bit{}; bit < 8; ++bit)
      crc = static_cast<std::uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021
                                                      : crc << 1);
  }
  return static_cast<std::uint16_t>(crc % 16384);
}
} // namespace
void cluster_slot_cache::clear() { slots_.fill(std::nullopt); }
void cluster_slot_cache::update(const std::vector<cluster_slot_range> &ranges) {
  clear();
  for (const auto &range : ranges) {
    const auto end = std::min<std::uint16_t>(range.end, 16383);
    for (auto slot = range.start; slot <= end; ++slot) {
      slots_[slot] = range.master;
      if (slot == 16383)
        break;
    }
  }
}
void cluster_slot_cache::update_slot(std::uint16_t slot,
                                     endpoint_info endpoint) {
  if (slot < slots_.size())
    slots_[slot] = std::move(endpoint);
}
auto cluster_slot_cache::endpoint_for_slot(std::uint16_t slot) const
    -> std::optional<endpoint_info> {
  return slot < slots_.size() ? slots_[slot] : std::nullopt;
}
auto cluster_slot_cache::endpoint_for_key(std::string_view key) const
    -> std::optional<endpoint_info> {
  return endpoint_for_slot(hash_slot(key));
}
auto cluster_slot_cache::covered_slots() const noexcept -> std::size_t {
  return static_cast<std::size_t>(std::ranges::count_if(
      slots_, [](const auto &value) { return value.has_value(); }));
}
auto first_value(const std::vector<resp3_node> &nodes) noexcept
    -> std::string_view {
  for (const auto &node : nodes)
    if (!node.is_aggregate() && !node.is_null())
      return node.value;
  return {};
}
auto all_values(const std::vector<resp3_node> &nodes)
    -> std::vector<std::string_view> {
  std::vector<std::string_view> values;
  for (const auto &node : nodes)
    if (!node.is_aggregate() && !node.is_null())
      values.push_back(node.value);
  return values;
}
auto is_ok(const std::vector<resp3_node> &nodes) noexcept -> bool {
  return !nodes.empty() &&
         nodes.front().data_type == resp3_type::simple_string &&
         nodes.front().value == "OK";
}
auto has_error(const std::vector<resp3_node> &nodes) noexcept -> bool {
  return std::ranges::any_of(nodes,
                             [](const auto &node) { return node.is_error(); });
}
auto error_message(const std::vector<resp3_node> &nodes) noexcept
    -> std::string_view {
  for (const auto &node : nodes)
    if (node.is_error())
      return node.value;
  return {};
}
} // namespace cnetmod::redis
