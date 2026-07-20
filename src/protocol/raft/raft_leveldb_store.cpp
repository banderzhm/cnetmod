module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_LEVELDB
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#endif

module cnetmod.protocol.raft;

import std;
import :leveldb_store;

#ifdef CNETMOD_HAS_LEVELDB
namespace cnetmod::raft {
namespace {
constexpr std::string_view hard_state_key = "meta:hard_state",
                           snapshot_key = "meta:snapshot";
auto log_key(log_index index) -> std::string {
  return std::format("log:{:020}", index);
}
auto parse_log_index(std::string_view key) -> log_index {
  return key.starts_with("log:")
             ? static_cast<log_index>(std::stoull(std::string(key.substr(4))))
             : 0;
}
void put_u64(std::string &out, std::uint64_t value) {
  for (auto i = 0; i < 8; ++i)
    out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
}
auto get_u64(std::string_view in, std::size_t &pos) -> std::uint64_t {
  if (pos + 8 > in.size())
    throw std::runtime_error("corrupt raft leveldb record");
  std::uint64_t value = 0;
  for (auto i = 0; i < 8; ++i)
    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(in[pos++]))
             << (i * 8);
  return value;
}
void put_string(std::string &out, std::string_view value) {
  put_u64(out, value.size());
  out.append(value);
}
auto get_string(std::string_view in, std::size_t &pos) -> std::string {
  const auto size = static_cast<std::size_t>(get_u64(in, pos));
  if (pos + size > in.size())
    throw std::runtime_error("corrupt raft leveldb string");
  std::string out{in.substr(pos, size)};
  pos += size;
  return out;
}
void put_strings(std::string &out, const std::vector<node_id> &values) {
  put_u64(out, values.size());
  for (const auto &value : values)
    put_string(out, value);
}
auto get_strings(std::string_view in, std::size_t &pos)
    -> std::vector<node_id> {
  const auto size = static_cast<std::size_t>(get_u64(in, pos));
  std::vector<node_id> out;
  out.reserve(size);
  for (std::size_t i = 0; i < size; ++i)
    out.push_back(get_string(in, pos));
  return out;
}
void put_config(std::string &out, const configuration_state &conf) {
  put_strings(out, conf.voters);
  put_strings(out, conf.old_voters);
  put_strings(out, conf.learners);
}
auto get_config(std::string_view in, std::size_t &pos) -> configuration_state {
  configuration_state out{.voters = get_strings(in, pos),
                          .old_voters = get_strings(in, pos)};
  if (pos < in.size())
    out.learners = get_strings(in, pos);
  return out;
}
auto serialize_state(const hard_state &s) -> std::string {
  std::string out;
  put_u64(out, s.current_term);
  put_string(out, s.voted_for);
  put_u64(out, s.commit_index);
  put_u64(out, s.last_applied);
  return out;
}
auto deserialize_state(std::string_view data) -> hard_state {
  if (data.empty())
    return {};
  std::size_t pos = 0;
  return {.current_term = get_u64(data, pos),
          .voted_for = get_string(data, pos),
          .commit_index = get_u64(data, pos),
          .last_applied = get_u64(data, pos)};
}
auto serialize_snapshot(const snapshot_metadata &s) -> std::string {
  std::string out;
  put_u64(out, s.last_included_index);
  put_u64(out, s.last_included_term);
  put_string(out, s.uri);
  put_config(out, s.configuration);
  return out;
}
auto deserialize_snapshot(std::string_view data) -> snapshot_metadata {
  if (data.empty())
    return {};
  std::size_t pos = 0;
  return {.last_included_index = get_u64(data, pos),
          .last_included_term = get_u64(data, pos),
          .uri = get_string(data, pos),
          .configuration = get_config(data, pos)};
}
auto serialize_entry(const log_entry &e) -> std::string {
  std::string out;
  put_u64(out, e.term);
  put_u64(out, static_cast<std::uint64_t>(e.type));
  put_string(out, e.command);
  put_config(out, e.configuration);
  return out;
}
auto deserialize_entry(log_index index, std::string_view data) -> log_entry {
  std::size_t pos = 0;
  log_entry e;
  e.index = index;
  e.term = get_u64(data, pos);
  e.type = static_cast<entry_type>(get_u64(data, pos));
  e.command = get_string(data, pos);
  e.configuration = get_config(data, pos);
  return e;
}
} // namespace

leveldb_store::leveldb_store(std::string path) {
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB *raw = nullptr;
  const auto status = leveldb::DB::Open(options, path, &raw);
  if (!status.ok())
    throw std::runtime_error(status.ToString());
  db_.reset(raw);
}
auto leveldb_store::load_hard_state() -> hard_state {
  std::string data;
  const auto status =
      db_->Get(leveldb::ReadOptions{}, std::string(hard_state_key), &data);
  if (status.IsNotFound())
    return {};
  check(status);
  return deserialize_state(data);
}
void leveldb_store::save_hard_state(const hard_state &state) {
  leveldb::WriteOptions options;
  options.sync = sync_;
  check(db_->Put(options, std::string(hard_state_key), serialize_state(state)));
}
auto leveldb_store::load_snapshot_metadata() -> snapshot_metadata {
  return load_snapshot_metadata_const();
}
void leveldb_store::save_snapshot_metadata(const snapshot_metadata &metadata) {
  leveldb::WriteOptions options;
  options.sync = sync_;
  check(db_->Put(options, std::string(snapshot_key),
                 serialize_snapshot(metadata)));
}
auto leveldb_store::first_log_index() const -> log_index {
  std::unique_ptr<leveldb::Iterator> it(
      db_->NewIterator(leveldb::ReadOptions{}));
  it->Seek("log:");
  check(it->status());
  return !it->Valid() || !it->key().ToString().starts_with("log:")
             ? load_snapshot_metadata_const().last_included_index + 1
             : parse_log_index(it->key().ToString());
}
auto leveldb_store::last_log_index() const -> log_index {
  std::unique_ptr<leveldb::Iterator> it(
      db_->NewIterator(leveldb::ReadOptions{}));
  auto last = load_snapshot_metadata_const().last_included_index;
  for (it->Seek("log:"); it->Valid(); it->Next()) {
    const auto key = it->key().ToString();
    if (!key.starts_with("log:"))
      break;
    last = parse_log_index(key);
  }
  check(it->status());
  return last;
}
auto leveldb_store::term_at(log_index index) const -> term_t {
  if (index == 0)
    return 0;
  const auto snapshot = load_snapshot_metadata_const();
  if (index == snapshot.last_included_index)
    return snapshot.last_included_term;
  const auto entry = entry_at(index);
  return entry ? entry->term : 0;
}
auto leveldb_store::entry_at(log_index index) const
    -> std::optional<log_entry> {
  std::string data;
  const auto status = db_->Get(leveldb::ReadOptions{}, log_key(index), &data);
  if (status.IsNotFound())
    return std::nullopt;
  check(status);
  return deserialize_entry(index, data);
}
auto leveldb_store::entries(log_index first_index,
                            std::size_t max_entries) const
    -> std::vector<log_entry> {
  std::vector<log_entry> out;
  if (max_entries == 0)
    return out;
  std::unique_ptr<leveldb::Iterator> it(
      db_->NewIterator(leveldb::ReadOptions{}));
  for (it->Seek(log_key(first_index)); it->Valid(); it->Next()) {
    const auto key = it->key().ToString();
    if (!key.starts_with("log:"))
      break;
    out.push_back(
        deserialize_entry(parse_log_index(key), it->value().ToString()));
    if (out.size() >= max_entries)
      break;
  }
  check(it->status());
  return out;
}
void leveldb_store::append(const std::vector<log_entry> &entries) {
  if (entries.size() == 1) {
    append_one(entries.front());
    return;
  }
  std::vector<log_entry> pending;
  for (const auto &entry : entries)
    if (entry.index != 0)
      pending.push_back(entry);
  if (pending.empty())
    return;
  std::ranges::sort(pending, {}, &log_entry::index);
  leveldb::WriteBatch batch;
  delete_suffix(batch, pending.front().index);
  for (const auto &entry : pending)
    batch.Put(log_key(entry.index), serialize_entry(entry));
  leveldb::WriteOptions options;
  options.sync = sync_;
  check(db_->Write(options, &batch));
}
void leveldb_store::append_one(log_entry entry) {
  if (entry.index == 0)
    return;
  leveldb::WriteBatch batch;
  delete_suffix(batch, entry.index);
  batch.Put(log_key(entry.index), serialize_entry(entry));
  leveldb::WriteOptions options;
  options.sync = sync_;
  check(db_->Write(options, &batch));
}
void leveldb_store::truncate_prefix(log_index first_kept_index) {
  leveldb::WriteBatch batch;
  std::unique_ptr<leveldb::Iterator> it(
      db_->NewIterator(leveldb::ReadOptions{}));
  for (it->Seek("log:"); it->Valid(); it->Next()) {
    const auto key = it->key().ToString();
    if (!key.starts_with("log:") || parse_log_index(key) >= first_kept_index)
      break;
    batch.Delete(key);
  }
  check(it->status());
  leveldb::WriteOptions options;
  options.sync = sync_;
  check(db_->Write(options, &batch));
}
void leveldb_store::truncate_suffix(log_index first_removed_index) {
  leveldb::WriteBatch batch;
  delete_suffix(batch, first_removed_index);
  leveldb::WriteOptions options;
  options.sync = sync_;
  check(db_->Write(options, &batch));
}
void leveldb_store::reset_to_snapshot(const snapshot_metadata &metadata) {
  leveldb::WriteBatch batch;
  std::unique_ptr<leveldb::Iterator> it(
      db_->NewIterator(leveldb::ReadOptions{}));
  for (it->Seek("log:"); it->Valid(); it->Next()) {
    const auto key = it->key().ToString();
    if (!key.starts_with("log:"))
      break;
    batch.Delete(key);
  }
  check(it->status());
  auto state = load_hard_state();
  state.commit_index =
      std::max(state.commit_index, metadata.last_included_index);
  state.last_applied =
      std::max(state.last_applied, metadata.last_included_index);
  batch.Put(std::string(snapshot_key), serialize_snapshot(metadata));
  batch.Put(std::string(hard_state_key), serialize_state(state));
  leveldb::WriteOptions options;
  options.sync = sync_;
  check(db_->Write(options, &batch));
}
void leveldb_store::set_sync(bool enabled) noexcept { sync_ = enabled; }
void leveldb_store::delete_suffix(leveldb::WriteBatch &batch,
                                  log_index first_removed_index) const {
  std::unique_ptr<leveldb::Iterator> it(
      db_->NewIterator(leveldb::ReadOptions{}));
  for (it->Seek(log_key(first_removed_index)); it->Valid(); it->Next()) {
    const auto key = it->key().ToString();
    if (!key.starts_with("log:"))
      break;
    batch.Delete(key);
  }
  check(it->status());
}
auto leveldb_store::load_snapshot_metadata_const() const -> snapshot_metadata {
  std::string data;
  const auto status =
      db_->Get(leveldb::ReadOptions{}, std::string(snapshot_key), &data);
  if (status.IsNotFound())
    return {};
  check(status);
  return deserialize_snapshot(data);
}
void leveldb_store::check(const leveldb::Status &status) {
  if (!status.ok())
    throw std::runtime_error(status.ToString());
}
} // namespace cnetmod::raft
#endif
