export module cnetmod.protocol.raft:fsm;

import std;
import :types;
import :storage;

namespace cnetmod::raft {
export struct snapshot_writer {
  snapshot_metadata metadata;
  std::string uri;
};

export struct snapshot_reader {
  snapshot_metadata metadata;
  std::string uri;
};

export class state_machine {
public:
  virtual ~state_machine();

  virtual void on_apply(const log_entry &entry) = 0;

  virtual void on_snapshot_save(const snapshot_metadata &);
  virtual void on_snapshot_load(const snapshot_metadata &);
  virtual auto save_snapshot(const snapshot_writer &writer)
      -> std::expected<void, raft_error>;
  virtual auto load_snapshot(const snapshot_reader &reader)
      -> std::expected<void, raft_error>;
  virtual void on_leader_start(term_t);
  virtual void on_leader_stop(term_t);
};

export class fsm_caller {
public:
  fsm_caller() = default;

  explicit fsm_caller(state_machine *machine);
  void reset(state_machine *machine);
  [[nodiscard]] auto last_applied() const noexcept -> log_index;
  void set_last_applied(log_index index) noexcept;
  auto apply_committed(raft_storage &storage, log_index committed)
      -> std::expected<log_index, raft_error>;
  auto apply_entry(const log_entry &entry)
      -> std::expected<log_index, raft_error>;
  void on_snapshot_load(const snapshot_metadata &metadata);
  auto save_snapshot(const snapshot_writer &writer)
      -> std::expected<void, raft_error>;
  auto load_snapshot(const snapshot_reader &reader)
      -> std::expected<void, raft_error>;
  void on_leader_start(term_t term);
  void on_leader_stop(term_t term);

private:
  state_machine *machine_ = nullptr;
  log_index last_applied_ = 0;
};
} // namespace cnetmod::raft
