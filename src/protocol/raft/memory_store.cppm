export module cnetmod.protocol.raft:memory_store;

import std;
import :types;
import :storage;

namespace cnetmod::raft {
export class memory_store final : public raft_storage {
public:
  auto load_hard_state() -> hard_state override;
  void save_hard_state(const hard_state &state) override;
  auto load_snapshot_metadata() -> snapshot_metadata override;
  void save_snapshot_metadata(const snapshot_metadata &metadata) override;
  auto first_log_index() const -> log_index override;
  auto last_log_index() const -> log_index override;
  auto term_at(log_index index) const -> term_t override;
  auto entry_at(log_index index) const -> std::optional<log_entry> override;
  auto entries(log_index first_index, std::size_t max_entries) const
      -> std::vector<log_entry> override;
  void append(const std::vector<log_entry> &entries) override;
  void append_one(log_entry entry) override;
  void truncate_prefix(log_index first_kept_index) override;
  void truncate_suffix(log_index first_removed_index) override;
  void reset_to_snapshot(const snapshot_metadata &metadata) override;

private:
  [[nodiscard]] auto lower_bound_offset(log_index index) const -> std::size_t;
  [[nodiscard]] auto find_offset(log_index index) const
      -> std::optional<std::size_t>;

  hard_state state_;
  snapshot_metadata snapshot_;
  std::vector<log_entry> entries_;
};
} // namespace cnetmod::raft
