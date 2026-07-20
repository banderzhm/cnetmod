export module cnetmod.protocol.raft:progress;

import std;
import :types;

namespace cnetmod::raft {

export enum class replication_state { probe, replicate, snapshot };

export struct inflight_window {
  std::size_t capacity = 256;
  std::deque<log_index> entries;

  [[nodiscard]] auto full() const noexcept -> bool;
  void add(log_index index);
  void free_to(log_index index);
  void reset();
};

export struct peer_progress {
  log_index next_index = 1;
  log_index match_index = 0;
  log_index pending_snapshot = 0;
  replication_state state = replication_state::probe;
  bool recent_active = false;
  inflight_window inflight;
};

export class progress_tracker {
public:
  using peer_table = std::vector<std::pair<node_id, peer_progress>>;

  progress_tracker() = default;
  progress_tracker(std::vector<node_id> peers, log_index next_index,
                   std::size_t inflight_capacity);
  void reset(std::vector<node_id> peers, log_index next_index,
             std::size_t inflight_capacity = 256);
  auto get(const node_id &peer) -> peer_progress *;
  auto get(const node_id &peer) const -> const peer_progress *;
  [[nodiscard]] auto peers() const noexcept -> const peer_table &;
  void mark_sent(const node_id &peer, log_index last_index);
  void optimistic_update(const node_id &peer, log_index last_index);
  void mark_replicated(const node_id &peer, log_index index);
  void mark_rejected(const node_id &peer, log_index rejected_next_index,
                     log_index conflict_index = 0);
  void mark_snapshot(const node_id &peer, log_index snapshot_index);
  [[nodiscard]] auto committed_index(log_index leader_match,
                                     std::size_t majority) const -> log_index;
  [[nodiscard]] auto active_followers() const -> std::size_t;

private:
  auto find_peer(const node_id &peer) -> peer_table::iterator;
  auto find_peer(const node_id &peer) const -> peer_table::const_iterator;
  peer_table peers_;
};

} // namespace cnetmod::raft
