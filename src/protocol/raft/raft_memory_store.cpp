module cnetmod.protocol.raft;

import std;
import :memory_store;

namespace cnetmod::raft {
auto memory_store::load_hard_state() -> hard_state { return state_; }
void memory_store::save_hard_state(const hard_state &state) { state_ = state; }
auto memory_store::load_snapshot_metadata() -> snapshot_metadata {
  return snapshot_;
}
void memory_store::save_snapshot_metadata(const snapshot_metadata &metadata) {
  snapshot_ = metadata;
}
auto memory_store::first_log_index() const -> log_index {
  return entries_.empty() ? snapshot_.last_included_index + 1
                          : entries_.front().index;
}
auto memory_store::last_log_index() const -> log_index {
  return entries_.empty() ? snapshot_.last_included_index
                          : entries_.back().index;
}
auto memory_store::term_at(log_index index) const -> term_t {
  if (index == 0)
    return 0;
  if (index == snapshot_.last_included_index)
    return snapshot_.last_included_term;
  auto pos = find_offset(index);
  return pos ? entries_[*pos].term : 0;
}
auto memory_store::entry_at(log_index index) const -> std::optional<log_entry> {
  auto pos = find_offset(index);
  return pos ? std::optional<log_entry>{entries_[*pos]} : std::nullopt;
}
auto memory_store::entries(log_index first_index, std::size_t max_entries) const
    -> std::vector<log_entry> {
  std::vector<log_entry> out;
  if (max_entries == 0)
    return out;
  const auto first = lower_bound_offset(first_index);
  if (first >= entries_.size())
    return out;
  const auto count = std::min(max_entries, entries_.size() - first);
  out.reserve(count);
  out.insert(out.end(), entries_.begin() + static_cast<std::ptrdiff_t>(first),
             entries_.begin() + static_cast<std::ptrdiff_t>(first + count));
  return out;
}
void memory_store::append(const std::vector<log_entry> &entries) {
  if (entries.size() == 1) {
    append_one(entries.front());
    return;
  }
  std::vector<log_entry> to_append;
  to_append.reserve(entries.size());
  for (const auto &entry : entries)
    if (entry.index > snapshot_.last_included_index)
      to_append.push_back(entry);
  if (to_append.empty())
    return;
  std::ranges::sort(to_append, {}, &log_entry::index);
  truncate_suffix(to_append.front().index);
  entries_.insert(entries_.end(), to_append.begin(), to_append.end());
}
void memory_store::append_one(log_entry entry) {
  if (entry.index <= snapshot_.last_included_index)
    return;
  if ((!entries_.empty() && entry.index == entries_.back().index + 1) ||
      (entries_.empty() && entry.index == snapshot_.last_included_index + 1)) {
    entries_.push_back(std::move(entry));
    return;
  }
  truncate_suffix(entry.index);
  entries_.push_back(std::move(entry));
}
void memory_store::truncate_prefix(log_index first_kept_index) {
  const auto first_kept = lower_bound_offset(first_kept_index);
  entries_.erase(entries_.begin(),
                 entries_.begin() + static_cast<std::ptrdiff_t>(first_kept));
}
void memory_store::truncate_suffix(log_index first_removed_index) {
  if (first_removed_index == 0) {
    entries_.clear();
    return;
  }
  const auto first_removed = lower_bound_offset(first_removed_index);
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(first_removed),
                 entries_.end());
}
void memory_store::reset_to_snapshot(const snapshot_metadata &metadata) {
  snapshot_ = metadata;
  entries_.clear();
  state_.commit_index =
      std::max(state_.commit_index, metadata.last_included_index);
  state_.last_applied =
      std::max(state_.last_applied, metadata.last_included_index);
}
auto memory_store::lower_bound_offset(log_index index) const -> std::size_t {
  const auto it =
      std::ranges::lower_bound(entries_, index, {}, &log_entry::index);
  return static_cast<std::size_t>(std::distance(entries_.begin(), it));
}
auto memory_store::find_offset(log_index index) const
    -> std::optional<std::size_t> {
  const auto pos = lower_bound_offset(index);
  return pos < entries_.size() && entries_[pos].index == index
             ? std::optional<std::size_t>{pos}
             : std::nullopt;
}
} // namespace cnetmod::raft
