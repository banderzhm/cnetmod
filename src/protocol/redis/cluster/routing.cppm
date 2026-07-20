export module cnetmod.protocol.redis:routing;

import std;
import :value;

export namespace cnetmod::redis {
struct endpoint_info {
  std::string host;
  std::uint16_t port = 0;
};
enum class redirect_kind { moved, ask };
struct cluster_redirect {
  redirect_kind kind = redirect_kind::moved;
  std::uint16_t slot = 0;
  endpoint_info endpoint;
};
struct cluster_slot_range {
  std::uint16_t start = 0;
  std::uint16_t end = 0;
  endpoint_info master;
  std::vector<endpoint_info> replicas;
};
struct cluster_pipeline_item {
  std::vector<std::string> args;
  std::string key;
};
class cluster_slot_cache {
public:
  void clear();
  void update(const std::vector<cluster_slot_range> &ranges);
  void update_slot(std::uint16_t slot, endpoint_info endpoint);
  [[nodiscard]] auto endpoint_for_slot(std::uint16_t slot) const
      -> std::optional<endpoint_info>;
  [[nodiscard]] auto endpoint_for_key(std::string_view key) const
      -> std::optional<endpoint_info>;
  [[nodiscard]] auto covered_slots() const noexcept -> std::size_t;

private:
  std::array<std::optional<endpoint_info>, 16384> slots_{};
};
[[nodiscard]] auto first_value(const std::vector<resp3_node> &nodes) noexcept
    -> std::string_view;
[[nodiscard]] auto all_values(const std::vector<resp3_node> &nodes)
    -> std::vector<std::string_view>;
[[nodiscard]] auto is_ok(const std::vector<resp3_node> &nodes) noexcept -> bool;
[[nodiscard]] auto has_error(const std::vector<resp3_node> &nodes) noexcept
    -> bool;
[[nodiscard]] auto error_message(const std::vector<resp3_node> &nodes) noexcept
    -> std::string_view;
} // namespace cnetmod::redis
