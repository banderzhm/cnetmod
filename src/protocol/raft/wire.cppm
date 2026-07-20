export module cnetmod.protocol.raft:wire;

import std;
import :types;

namespace cnetmod::raft {
export enum class raft_rpc_type : std::uint8_t {
  pre_vote = 1,
  request_vote = 2,
  request_vote_response = 3,
  append_entries = 4,
  append_entries_response = 5,
  install_snapshot = 6,
  install_snapshot_response = 7,
  timeout_now = 8,
};

export struct raft_rpc_message {
  raft_rpc_type type = raft_rpc_type::append_entries;
  node_id from;
  node_id to;
  std::string auth_token;
  request_vote_request vote;
  request_vote_response vote_response;
  timeout_now_request timeout_now;
  append_entries_request append;
  append_entries_response append_response;
  install_snapshot_request snapshot;
  install_snapshot_response snapshot_response;
};

export auto encode_raft_message(const raft_rpc_message &message)
    -> std::vector<std::byte>;
export auto decode_raft_message(std::span<const std::byte> frame)
    -> raft_rpc_message;
export auto raft_frame_payload_size(std::span<const std::byte, 9> header)
    -> std::uint32_t;
} // namespace cnetmod::raft
