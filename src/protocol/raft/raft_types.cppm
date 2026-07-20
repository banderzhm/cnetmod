export module cnetmod.protocol.raft:types;

import std;

namespace cnetmod::raft {
export using term_t = std::uint64_t;
export using log_index = std::uint64_t;
export using node_id = std::string;
export using group_id = std::string;
export using request_id = std::uint64_t;

export enum class node_role {
  follower,
  pre_candidate,
  candidate,
  leader,
};

export constexpr auto role_name(node_role role) noexcept -> std::string_view {
  switch (role) {
  case node_role::follower:
    return "follower";
  case node_role::pre_candidate:
    return "pre_candidate";
  case node_role::candidate:
    return "candidate";
  case node_role::leader:
    return "leader";
  }
  return "unknown";
}

export enum class entry_type {
  no_op,
  command,
  configuration,
};

export struct configuration_state {
  std::vector<node_id> voters;
  std::vector<node_id> old_voters;
  std::vector<node_id> learners;

  [[nodiscard]] auto joint() const noexcept -> bool {
    return !old_voters.empty();
  }
};

export struct log_entry {
  log_index index = 0;
  term_t term = 0;
  entry_type type = entry_type::command;
  std::string command;
  configuration_state configuration;
};

export struct log_id {
  log_index index = 0;
  term_t term = 0;

  friend auto operator==(const log_id &, const log_id &) -> bool = default;
};

export struct snapshot_metadata {
  log_index last_included_index = 0;
  term_t last_included_term = 0;
  std::string uri;
  configuration_state configuration;

  [[nodiscard]] auto valid() const noexcept -> bool {
    return last_included_index != 0;
  }
};

export struct hard_state {
  term_t current_term = 0;
  node_id voted_for;
  log_index commit_index = 0;
  log_index last_applied = 0;
};

export struct soft_state {
  node_role role = node_role::follower;
  node_id leader_id;
};

export struct request_vote_request {
  term_t term = 0;
  node_id candidate_id;
  log_index last_log_index = 0;
  term_t last_log_term = 0;
  bool pre_vote = false;
};

export struct request_vote_response {
  term_t term = 0;
  bool vote_granted = false;
  bool pre_vote = false;
};

export struct timeout_now_request {
  term_t term = 0;
  node_id leader_id;
};

export struct read_index_request {
  request_id id = 0;
  std::string context;
};

export struct read_index_response {
  request_id id = 0;
  term_t term = 0;
  log_index index = 0;
  bool ready = false;
};

export struct append_entries_request {
  term_t term = 0;
  node_id leader_id;
  log_index prev_log_index = 0;
  term_t prev_log_term = 0;
  std::vector<log_entry> entries;
  log_index leader_commit = 0;
  std::vector<read_index_request> read_contexts;
};

export struct append_entries_response {
  term_t term = 0;
  bool success = false;
  log_index match_index = 0;
  log_index conflict_index = 0;
  term_t conflict_term = 0;
  std::vector<request_id> read_acks;
};

export struct install_snapshot_request {
  term_t term = 0;
  node_id leader_id;
  snapshot_metadata metadata;
  std::string uri;
  std::string snapshot_id;
  std::uint64_t offset = 0;
  std::uint64_t total_size = 0;
  std::uint32_t chunk_crc32 = 0;
  std::uint32_t file_crc32 = 0;
  bool done = true;
  std::vector<std::byte> data;
};

export struct install_snapshot_response {
  term_t term = 0;
  bool success = false;
  std::string snapshot_id;
  std::uint64_t accepted_offset = 0;
  std::string error;
};

export struct raft_options {
  std::chrono::milliseconds election_timeout{150};
  std::chrono::milliseconds heartbeat_interval{50};
  std::chrono::milliseconds leader_lease_timeout{100};
  std::chrono::milliseconds read_index_timeout{1000};
  bool pre_vote = true;
  bool check_quorum = true;
  bool lease_read = false;
  std::size_t max_entries_per_append = 128;
  std::size_t max_inflight_append = 256;
  std::size_t snapshot_chunk_size = 1024 * 1024;
};

export struct raft_snapshot_policy {
  std::size_t log_entries_threshold = 0;
  std::chrono::milliseconds min_interval{0};
  std::string uri_prefix = "raft-snapshot";
};

export struct raft_config {
  node_id id;
  std::vector<node_id> peers;
  raft_options options;

  [[nodiscard]] auto initial_configuration() const -> configuration_state {
    configuration_state out;
    out.voters.reserve(peers.size() + 1);
    out.voters.push_back(id);
    out.voters.insert(out.voters.end(), peers.begin(), peers.end());
    std::ranges::sort(out.voters);
    out.voters.erase(std::ranges::unique(out.voters).begin(), out.voters.end());
    return out;
  }

  [[nodiscard]] auto cluster_size() const noexcept -> std::size_t {
    return peers.size() + 1;
  }

  [[nodiscard]] auto majority() const noexcept -> std::size_t {
    return cluster_size() / 2 + 1;
  }
};

export enum class raft_errc {
  ok = 0,
  stale_term,
  log_inconsistent,
  not_leader,
  not_voter,
  configuration_error,
  snapshot_required,
  storage_error,
  state_machine_error,
  backpressure,
  stopped,
};

export struct raft_error {
  raft_errc code = raft_errc::ok;
  std::string message;

  [[nodiscard]] auto ok() const noexcept -> bool {
    return code == raft_errc::ok;
  }
};

export struct raft_metrics {
  term_t current_term = 0;
  node_role role = node_role::follower;
  node_id leader_id;
  log_index first_log_index = 1;
  log_index last_log_index = 0;
  log_index commit_index = 0;
  log_index last_applied = 0;
  log_index snapshot_index = 0;
  term_t snapshot_term = 0;
  std::size_t voters = 0;
  std::size_t learners = 0;
  std::size_t pending_reads = 0;
  std::size_t active_followers = 0;
  bool stopped = false;
  std::string last_error;
};
} // namespace cnetmod::raft
