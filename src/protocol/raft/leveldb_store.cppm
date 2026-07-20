module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_LEVELDB
#include <leveldb/db.h>
#endif

export module cnetmod.protocol.raft:leveldb_store;

import std;
import :types;
import :storage;

namespace cnetmod::raft {
export constexpr bool leveldb_store_available =
#ifdef CNETMOD_HAS_LEVELDB
    true;
#else
    false;
#endif

#ifdef CNETMOD_HAS_LEVELDB
export class leveldb_store final : public raft_storage {
public:
  explicit leveldb_store(std::string path);
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
  void set_sync(bool enabled) noexcept;

private:
  void delete_suffix(leveldb::WriteBatch &batch,
                     log_index first_removed_index) const;
  auto load_snapshot_metadata_const() const -> snapshot_metadata;
  static void check(const leveldb::Status &status);
  std::unique_ptr<leveldb::DB> db_;
  bool sync_ = true;
};
#endif
} // namespace cnetmod::raft
