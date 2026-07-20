module cnetmod.protocol.raft;

import std;
import :progress;

namespace cnetmod::raft {

auto inflight_window::full() const noexcept -> bool {
  return entries.size() >= capacity;
}
void inflight_window::add(log_index index) {
  if (capacity == 0)
    return;
  if (full())
    entries.pop_front();
  entries.push_back(index);
}
void inflight_window::free_to(log_index index) {
  while (!entries.empty() && entries.front() <= index)
    entries.pop_front();
}
void inflight_window::reset() { entries.clear(); }

progress_tracker::progress_tracker(std::vector<node_id> peers,
                                   log_index next_index,
                                   std::size_t inflight_capacity) {
  reset(std::move(peers), next_index, inflight_capacity);
}
void progress_tracker::reset(std::vector<node_id> peers, log_index next_index,
                             std::size_t inflight_capacity) {
  peers_.clear();
  std::ranges::sort(peers);
  peers.erase(std::ranges::unique(peers).begin(), peers.end());
  peers_.reserve(peers.size());
  for (auto &peer : peers)
    peers_.emplace_back(std::move(peer),
                        peer_progress{.next_index = next_index,
                                      .inflight = inflight_window{
                                          .capacity = inflight_capacity}});
}
auto progress_tracker::get(const node_id &peer) -> peer_progress * {
  auto it = find_peer(peer);
  return it == peers_.end() ? nullptr : &it->second;
}
auto progress_tracker::get(const node_id &peer) const -> const peer_progress * {
  auto it = find_peer(peer);
  return it == peers_.end() ? nullptr : &it->second;
}
auto progress_tracker::peers() const noexcept -> const peer_table & {
  return peers_;
}
void progress_tracker::mark_sent(const node_id &peer, log_index last_index) {
  auto *p = get(peer);
  if (!p)
    return;
  p->recent_active = true;
  if (last_index != 0 && p->state == replication_state::replicate) {
    p->next_index = std::max(p->next_index, last_index + 1);
    p->inflight.add(last_index);
  }
}
void progress_tracker::optimistic_update(const node_id &peer,
                                         log_index last_index) {
  auto *p = get(peer);
  if (p && last_index != 0)
    p->next_index = std::max(p->next_index, last_index + 1);
}
void progress_tracker::mark_replicated(const node_id &peer, log_index index) {
  auto *p = get(peer);
  if (!p)
    return;
  p->match_index = std::max(p->match_index, index);
  p->next_index = std::max(p->next_index, p->match_index + 1);
  p->pending_snapshot = 0;
  p->state = replication_state::replicate;
  p->recent_active = true;
  p->inflight.free_to(index);
}
void progress_tracker::mark_rejected(const node_id &peer,
                                     log_index rejected_next_index,
                                     log_index conflict_index) {
  auto *p = get(peer);
  if (!p)
    return;
  p->state = replication_state::probe;
  p->inflight.reset();
  p->recent_active = true;
  p->next_index = conflict_index != 0
                      ? conflict_index
                      : (rejected_next_index > 1 ? rejected_next_index - 1 : 1);
}
void progress_tracker::mark_snapshot(const node_id &peer,
                                     log_index snapshot_index) {
  auto *p = get(peer);
  if (!p)
    return;
  p->state = replication_state::snapshot;
  p->pending_snapshot = snapshot_index;
  p->inflight.reset();
}
auto progress_tracker::committed_index(log_index leader_match,
                                       std::size_t majority) const
    -> log_index {
  std::vector<log_index> matches;
  matches.reserve(peers_.size() + 1);
  matches.push_back(leader_match);
  for (const auto &[_, p] : peers_)
    matches.push_back(p.match_index);
  std::ranges::sort(matches, std::greater<>{});
  return majority == 0 || majority > matches.size() ? 0 : matches[majority - 1];
}
auto progress_tracker::active_followers() const -> std::size_t {
  return static_cast<std::size_t>(std::ranges::count_if(
      peers_, [](const auto &item) { return item.second.recent_active; }));
}
auto progress_tracker::find_peer(const node_id &peer) -> peer_table::iterator {
  auto it = std::ranges::lower_bound(peers_, peer, {},
                                     &peer_table::value_type::first);
  return it == peers_.end() || it->first != peer ? peers_.end() : it;
}
auto progress_tracker::find_peer(const node_id &peer) const
    -> peer_table::const_iterator {
  auto it = std::ranges::lower_bound(peers_, peer, {},
                                     &peer_table::value_type::first);
  return it == peers_.end() || it->first != peer ? peers_.end() : it;
}

} // namespace cnetmod::raft
