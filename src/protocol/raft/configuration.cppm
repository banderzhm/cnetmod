export module cnetmod.protocol.raft:configuration;

import std;
import :types;

namespace cnetmod::raft {
export class configuration {
public:
  configuration() = default;
  explicit configuration(configuration_state state);
  explicit configuration(std::vector<node_id> voters);
  [[nodiscard]] auto state() const -> const configuration_state &;
  [[nodiscard]] auto voters() const -> const std::vector<node_id> &;
  [[nodiscard]] auto old_voters() const -> const std::vector<node_id> &;
  [[nodiscard]] auto learners() const -> const std::vector<node_id> &;
  [[nodiscard]] auto joint() const noexcept -> bool;
  [[nodiscard]] auto empty() const noexcept -> bool;
  [[nodiscard]] auto contains(const node_id &id) const -> bool;
  [[nodiscard]] auto contains_learner(const node_id &id) const -> bool;
  [[nodiscard]] auto is_member(const node_id &id) const -> bool;
  [[nodiscard]] auto all_voters() const -> std::vector<node_id>;
  [[nodiscard]] auto all_members() const -> std::vector<node_id>;
  [[nodiscard]] auto quorum_size() const noexcept -> std::size_t;
  [[nodiscard]] auto old_quorum_size() const noexcept -> std::size_t;
  [[nodiscard]] auto has_quorum(const std::set<node_id> &acked) const -> bool;
  [[nodiscard]] auto with_joint(std::vector<node_id> new_voters) const
      -> std::expected<configuration, raft_error>;
  [[nodiscard]] auto with_learners(std::vector<node_id> learners) const
      -> std::expected<configuration, raft_error>;
  [[nodiscard]] auto promote_learner(const node_id &id) const
      -> std::expected<configuration, raft_error>;
  [[nodiscard]] auto remove_member(const node_id &id) const
      -> std::expected<configuration, raft_error>;
  [[nodiscard]] auto leave_joint() const
      -> std::expected<configuration, raft_error>;

private:
  configuration_state state_;
};

export enum class quorum_result {
  pending,
  won,
  lost,
};

export class vote_tracker {
public:
  explicit vote_tracker(configuration conf);
  void reset();
  void grant(const node_id &id);
  void reject(const node_id &id);
  [[nodiscard]] auto granted() const noexcept -> const std::set<node_id> &;
  [[nodiscard]] auto result() const -> quorum_result;

private:
  configuration conf_;
  std::set<node_id> granted_;
  std::set<node_id> rejected_;
};
} // namespace cnetmod::raft
