module cnetmod.protocol.raft;

import std;
import :wire;

namespace cnetmod::raft {
namespace {
constexpr std::uint32_t magic = 0x52414654u;
constexpr std::uint8_t version = 1;
constexpr std::uint32_t max_frame_size = 64u * 1024u * 1024u;

void put_u8(std::vector<std::byte> &o, std::uint8_t v) {
  o.push_back(static_cast<std::byte>(v));
}
void put_u32(std::vector<std::byte> &o, std::uint32_t v) {
  for (auto s : {24, 16, 8, 0})
    o.push_back(static_cast<std::byte>((v >> s) & 0xffu));
}
void put_u64(std::vector<std::byte> &o, std::uint64_t v) {
  for (auto s : {56, 48, 40, 32, 24, 16, 8, 0})
    o.push_back(static_cast<std::byte>((v >> s) & 0xffu));
}
auto get_u8(std::span<const std::byte> i, std::size_t &p) -> std::uint8_t {
  if (p + 1 > i.size())
    throw std::runtime_error("truncated raft frame");
  return static_cast<std::uint8_t>(i[p++]);
}
auto get_u32(std::span<const std::byte> i, std::size_t &p) -> std::uint32_t {
  if (p + 4 > i.size())
    throw std::runtime_error("truncated raft frame");
  std::uint32_t v{};
  for (int n = 0; n < 4; ++n)
    v = (v << 8) | static_cast<std::uint8_t>(i[p++]);
  return v;
}
auto get_u64(std::span<const std::byte> i, std::size_t &p) -> std::uint64_t {
  if (p + 8 > i.size())
    throw std::runtime_error("truncated raft frame");
  std::uint64_t v{};
  for (int n = 0; n < 8; ++n)
    v = (v << 8) | static_cast<std::uint8_t>(i[p++]);
  return v;
}
void put_string(std::vector<std::byte> &o, std::string_view v) {
  if (v.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("raft string is too large");
  put_u32(o, static_cast<std::uint32_t>(v.size()));
  auto b = std::as_bytes(std::span{v.data(), v.size()});
  o.insert(o.end(), b.begin(), b.end());
}
void put_bytes(std::vector<std::byte> &o, std::span<const std::byte> v) {
  if (v.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("raft byte payload is too large");
  put_u32(o, static_cast<std::uint32_t>(v.size()));
  o.insert(o.end(), v.begin(), v.end());
}
auto get_string(std::span<const std::byte> i, std::size_t &p) -> std::string {
  auto n = get_u32(i, p);
  if (p + n > i.size())
    throw std::runtime_error("truncated raft string");
  std::string o(reinterpret_cast<const char *>(i.data() + p), n);
  p += n;
  return o;
}
auto get_bytes(std::span<const std::byte> i, std::size_t &p)
    -> std::vector<std::byte> {
  auto n = get_u32(i, p);
  if (p + n > i.size())
    throw std::runtime_error("truncated raft byte payload");
  std::vector<std::byte> o(i.begin() + static_cast<std::ptrdiff_t>(p),
                           i.begin() + static_cast<std::ptrdiff_t>(p + n));
  p += n;
  return o;
}
void put_nodes(std::vector<std::byte> &o, const std::vector<node_id> &v) {
  if (v.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("too many raft nodes");
  put_u32(o, static_cast<std::uint32_t>(v.size()));
  for (auto const &x : v)
    put_string(o, x);
}
auto get_nodes(std::span<const std::byte> i, std::size_t &p)
    -> std::vector<node_id> {
  auto n = get_u32(i, p);
  std::vector<node_id> o;
  o.reserve(n);
  for (std::uint32_t x = 0; x < n; ++x)
    o.push_back(get_string(i, p));
  return o;
}
void put_config(std::vector<std::byte> &o, const configuration_state &c) {
  put_nodes(o, c.voters);
  put_nodes(o, c.old_voters);
  put_nodes(o, c.learners);
}
auto get_config(std::span<const std::byte> i, std::size_t &p)
    -> configuration_state {
  return {.voters = get_nodes(i, p),
          .old_voters = get_nodes(i, p),
          .learners = get_nodes(i, p)};
}
void put_entry(std::vector<std::byte> &o, const log_entry &e) {
  put_u64(o, e.index);
  put_u64(o, e.term);
  put_u8(o, static_cast<std::uint8_t>(e.type));
  put_string(o, e.command);
  put_config(o, e.configuration);
}
auto get_entry(std::span<const std::byte> i, std::size_t &p) -> log_entry {
  return {.index = get_u64(i, p),
          .term = get_u64(i, p),
          .type = static_cast<entry_type>(get_u8(i, p)),
          .command = get_string(i, p),
          .configuration = get_config(i, p)};
}
void put_snapshot(std::vector<std::byte> &o, const snapshot_metadata &m) {
  put_u64(o, m.last_included_index);
  put_u64(o, m.last_included_term);
  put_string(o, m.uri);
  put_config(o, m.configuration);
}
auto get_snapshot(std::span<const std::byte> i, std::size_t &p)
    -> snapshot_metadata {
  return {.last_included_index = get_u64(i, p),
          .last_included_term = get_u64(i, p),
          .uri = get_string(i, p),
          .configuration = get_config(i, p)};
}
void put_vote(std::vector<std::byte> &o, const request_vote_request &v) {
  put_u64(o, v.term);
  put_string(o, v.candidate_id);
  put_u64(o, v.last_log_index);
  put_u64(o, v.last_log_term);
  put_u8(o, v.pre_vote ? 1 : 0);
}
auto get_vote(std::span<const std::byte> i, std::size_t &p)
    -> request_vote_request {
  return {.term = get_u64(i, p),
          .candidate_id = get_string(i, p),
          .last_log_index = get_u64(i, p),
          .last_log_term = get_u64(i, p),
          .pre_vote = get_u8(i, p) != 0};
}
void put_vote_response(std::vector<std::byte> &o,
                       const request_vote_response &r) {
  put_u64(o, r.term);
  put_u8(o, r.vote_granted ? 1 : 0);
  put_u8(o, r.pre_vote ? 1 : 0);
}
auto get_vote_response(std::span<const std::byte> i, std::size_t &p)
    -> request_vote_response {
  return {.term = get_u64(i, p),
          .vote_granted = get_u8(i, p) != 0,
          .pre_vote = get_u8(i, p) != 0};
}
void put_timeout_now(std::vector<std::byte> &o, const timeout_now_request &r) {
  put_u64(o, r.term);
  put_string(o, r.leader_id);
}
auto get_timeout_now(std::span<const std::byte> i, std::size_t &p)
    -> timeout_now_request {
  return {.term = get_u64(i, p), .leader_id = get_string(i, p)};
}
void put_append(std::vector<std::byte> &o, const append_entries_request &a) {
  put_u64(o, a.term);
  put_string(o, a.leader_id);
  put_u64(o, a.prev_log_index);
  put_u64(o, a.prev_log_term);
  put_u64(o, a.leader_commit);
  if (a.entries.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("too many raft entries");
  put_u32(o, static_cast<std::uint32_t>(a.entries.size()));
  for (auto const &e : a.entries)
    put_entry(o, e);
  if (a.read_contexts.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("too many raft read-index contexts");
  put_u32(o, static_cast<std::uint32_t>(a.read_contexts.size()));
  for (auto const &r : a.read_contexts) {
    put_u64(o, r.id);
    put_string(o, r.context);
  }
}
auto get_append(std::span<const std::byte> i, std::size_t &p)
    -> append_entries_request {
  append_entries_request o;
  o.term = get_u64(i, p);
  o.leader_id = get_string(i, p);
  o.prev_log_index = get_u64(i, p);
  o.prev_log_term = get_u64(i, p);
  o.leader_commit = get_u64(i, p);
  auto n = get_u32(i, p);
  o.entries.reserve(n);
  for (std::uint32_t x = 0; x < n; ++x)
    o.entries.push_back(get_entry(i, p));
  if (p < i.size()) {
    n = get_u32(i, p);
    o.read_contexts.reserve(n);
    for (std::uint32_t x = 0; x < n; ++x)
      o.read_contexts.push_back(
          {.id = get_u64(i, p), .context = get_string(i, p)});
  }
  return o;
}
void put_append_response(std::vector<std::byte> &o,
                         const append_entries_response &r) {
  put_u64(o, r.term);
  put_u8(o, r.success ? 1 : 0);
  put_u64(o, r.match_index);
  put_u64(o, r.conflict_index);
  put_u64(o, r.conflict_term);
  if (r.read_acks.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("too many raft read-index acknowledgements");
  put_u32(o, static_cast<std::uint32_t>(r.read_acks.size()));
  for (auto x : r.read_acks)
    put_u64(o, x);
}
auto get_append_response(std::span<const std::byte> i, std::size_t &p)
    -> append_entries_response {
  append_entries_response o{.term = get_u64(i, p),
                            .success = get_u8(i, p) != 0,
                            .match_index = get_u64(i, p),
                            .conflict_index = get_u64(i, p),
                            .conflict_term = get_u64(i, p)};
  if (p < i.size()) {
    auto n = get_u32(i, p);
    o.read_acks.reserve(n);
    for (std::uint32_t x = 0; x < n; ++x)
      o.read_acks.push_back(get_u64(i, p));
  }
  return o;
}
} // namespace

auto encode_raft_message(const raft_rpc_message &m) -> std::vector<std::byte> {
  std::vector<std::byte> b;
  put_u8(b, static_cast<std::uint8_t>(m.type));
  put_string(b, m.from);
  put_string(b, m.to);
  put_string(b, m.auth_token);
  switch (m.type) {
  case raft_rpc_type::pre_vote:
  case raft_rpc_type::request_vote:
    put_vote(b, m.vote);
    break;
  case raft_rpc_type::request_vote_response:
    put_vote_response(b, m.vote_response);
    break;
  case raft_rpc_type::timeout_now:
    put_timeout_now(b, m.timeout_now);
    break;
  case raft_rpc_type::append_entries:
    put_append(b, m.append);
    break;
  case raft_rpc_type::append_entries_response:
    put_append_response(b, m.append_response);
    break;
  case raft_rpc_type::install_snapshot:
    put_u64(b, m.snapshot.term);
    put_string(b, m.snapshot.leader_id);
    put_snapshot(b, m.snapshot.metadata);
    put_string(b, m.snapshot.uri);
    put_string(b, m.snapshot.snapshot_id);
    put_u64(b, m.snapshot.offset);
    put_u64(b, m.snapshot.total_size);
    put_u32(b, m.snapshot.chunk_crc32);
    put_u32(b, m.snapshot.file_crc32);
    put_u8(b, m.snapshot.done ? 1 : 0);
    put_bytes(b, m.snapshot.data);
    break;
  case raft_rpc_type::install_snapshot_response:
    put_u64(b, m.snapshot_response.term);
    put_u8(b, m.snapshot_response.success ? 1 : 0);
    put_string(b, m.snapshot_response.snapshot_id);
    put_u64(b, m.snapshot_response.accepted_offset);
    put_string(b, m.snapshot_response.error);
    break;
  }
  if (b.size() > max_frame_size)
    throw std::length_error("raft frame is too large");
  std::vector<std::byte> f;
  f.reserve(9 + b.size());
  put_u32(f, magic);
  put_u8(f, version);
  put_u32(f, static_cast<std::uint32_t>(b.size()));
  f.insert(f.end(), b.begin(), b.end());
  return f;
}
auto decode_raft_message(std::span<const std::byte> f) -> raft_rpc_message {
  std::size_t p{};
  if (get_u32(f, p) != magic)
    throw std::runtime_error("invalid raft frame magic");
  if (get_u8(f, p) != version)
    throw std::runtime_error("unsupported raft frame version");
  auto n = get_u32(f, p);
  if (n > max_frame_size || p + n != f.size())
    throw std::runtime_error("invalid raft frame size");
  auto b = f.subspan(p, n);
  p = 0;
  raft_rpc_message m;
  m.type = static_cast<raft_rpc_type>(get_u8(b, p));
  m.from = get_string(b, p);
  m.to = get_string(b, p);
  m.auth_token = get_string(b, p);
  switch (m.type) {
  case raft_rpc_type::pre_vote:
  case raft_rpc_type::request_vote:
    m.vote = get_vote(b, p);
    break;
  case raft_rpc_type::request_vote_response:
    m.vote_response = get_vote_response(b, p);
    break;
  case raft_rpc_type::timeout_now:
    m.timeout_now = get_timeout_now(b, p);
    break;
  case raft_rpc_type::append_entries:
    m.append = get_append(b, p);
    break;
  case raft_rpc_type::append_entries_response:
    m.append_response = get_append_response(b, p);
    break;
  case raft_rpc_type::install_snapshot:
    m.snapshot.term = get_u64(b, p);
    m.snapshot.leader_id = get_string(b, p);
    m.snapshot.metadata = get_snapshot(b, p);
    m.snapshot.uri = get_string(b, p);
    if (p < b.size()) {
      m.snapshot.snapshot_id = get_string(b, p);
      m.snapshot.offset = get_u64(b, p);
      m.snapshot.total_size = get_u64(b, p);
      m.snapshot.chunk_crc32 = get_u32(b, p);
      m.snapshot.file_crc32 = get_u32(b, p);
      m.snapshot.done = get_u8(b, p) != 0;
      m.snapshot.data = get_bytes(b, p);
    }
    break;
  case raft_rpc_type::install_snapshot_response:
    m.snapshot_response.term = get_u64(b, p);
    m.snapshot_response.success = get_u8(b, p) != 0;
    if (p < b.size()) {
      m.snapshot_response.snapshot_id = get_string(b, p);
      m.snapshot_response.accepted_offset = get_u64(b, p);
      m.snapshot_response.error = get_string(b, p);
    }
    break;
  }
  if (p != b.size())
    throw std::runtime_error("raft frame has trailing bytes");
  return m;
}
auto raft_frame_payload_size(std::span<const std::byte, 9> h) -> std::uint32_t {
  std::size_t p{};
  if (get_u32(h, p) != magic)
    throw std::runtime_error("invalid raft frame magic");
  if (get_u8(h, p) != version)
    throw std::runtime_error("unsupported raft frame version");
  auto n = get_u32(h, p);
  if (n > max_frame_size)
    throw std::runtime_error("raft frame exceeds maximum size");
  return n;
}
} // namespace cnetmod::raft
