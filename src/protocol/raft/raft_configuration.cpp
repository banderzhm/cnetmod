module cnetmod.protocol.raft;

import std;
import :configuration;

namespace cnetmod::raft {
namespace {
auto normalized(std::vector<node_id> ids) -> std::vector<node_id> {
  std::erase_if(ids, [](const node_id &id) { return id.empty(); });
  std::ranges::sort(ids);
  ids.erase(std::ranges::unique(ids).begin(), ids.end());
  return ids;
}
auto majority_of(std::size_t n) noexcept -> std::size_t { return n / 2 + 1; }
} // namespace

configuration::configuration(configuration_state state)
    : state_(std::move(state)) {
  state_.voters = normalized(std::move(state_.voters));
  state_.old_voters = normalized(std::move(state_.old_voters));
  state_.learners = normalized(std::move(state_.learners));
  std::erase_if(state_.learners, [this](const node_id &id) {
    return std::ranges::binary_search(state_.voters, id) ||
           std::ranges::binary_search(state_.old_voters, id);
  });
}
configuration::configuration(std::vector<node_id> voters)
    : configuration(configuration_state{.voters = std::move(voters)}) {}
auto configuration::state() const -> const configuration_state & {
  return state_;
}
auto configuration::voters() const -> const std::vector<node_id> & {
  return state_.voters;
}
auto configuration::old_voters() const -> const std::vector<node_id> & {
  return state_.old_voters;
}
auto configuration::learners() const -> const std::vector<node_id> & {
  return state_.learners;
}
auto configuration::joint() const noexcept -> bool { return state_.joint(); }
auto configuration::empty() const noexcept -> bool {
  return state_.voters.empty() && state_.old_voters.empty() &&
         state_.learners.empty();
}
auto configuration::contains(const node_id &id) const -> bool {
  return std::ranges::binary_search(state_.voters, id) ||
         std::ranges::binary_search(state_.old_voters, id);
}
auto configuration::contains_learner(const node_id &id) const -> bool {
  return std::ranges::binary_search(state_.learners, id);
}
auto configuration::is_member(const node_id &id) const -> bool {
  return contains(id) || contains_learner(id);
}
auto configuration::all_voters() const -> std::vector<node_id> {
  auto out = state_.voters;
  out.insert(out.end(), state_.old_voters.begin(), state_.old_voters.end());
  return normalized(std::move(out));
}
auto configuration::all_members() const -> std::vector<node_id> {
  auto out = all_voters();
  out.insert(out.end(), state_.learners.begin(), state_.learners.end());
  return normalized(std::move(out));
}
auto configuration::quorum_size() const noexcept -> std::size_t {
  return majority_of(state_.voters.size());
}
auto configuration::old_quorum_size() const noexcept -> std::size_t {
  return majority_of(state_.old_voters.size());
}
auto configuration::has_quorum(const std::set<node_id> &acked) const -> bool {
  const auto count_in = [&acked](const std::vector<node_id> &voters) {
    return static_cast<std::size_t>(std::ranges::count_if(
        voters, [&acked](const node_id &id) { return acked.contains(id); }));
  };
  if (state_.voters.empty())
    return false;
  const bool new_quorum = count_in(state_.voters) >= quorum_size();
  return !joint()
             ? new_quorum
             : new_quorum && count_in(state_.old_voters) >= old_quorum_size();
}
auto configuration::with_joint(std::vector<node_id> new_voters) const
    -> std::expected<configuration, raft_error> {
  new_voters = normalized(std::move(new_voters));
  if (new_voters.empty())
    return std::unexpected(
        raft_error{.code = raft_errc::configuration_error,
                   .message = "joint configuration cannot be empty"});
  if (joint())
    return std::unexpected(raft_error{
        .code = raft_errc::configuration_error,
        .message = "cannot enter a joint configuration while already joint"});
  return configuration{configuration_state{.voters = std::move(new_voters),
                                           .old_voters = state_.voters,
                                           .learners = state_.learners}};
}
auto configuration::with_learners(std::vector<node_id> learners) const
    -> std::expected<configuration, raft_error> {
  learners = normalized(std::move(learners));
  std::erase_if(learners, [this](const node_id &id) { return contains(id); });
  return configuration{configuration_state{.voters = state_.voters,
                                           .old_voters = state_.old_voters,
                                           .learners = std::move(learners)}};
}
auto configuration::promote_learner(const node_id &id) const
    -> std::expected<configuration, raft_error> {
  if (!contains_learner(id))
    return std::unexpected(
        raft_error{.code = raft_errc::configuration_error,
                   .message = "cannot promote a node that is not a learner"});
  auto voters = state_.voters;
  voters.push_back(id);
  auto learners = state_.learners;
  std::erase(learners, id);
  auto promoted = with_joint(std::move(voters));
  if (!promoted)
    return std::unexpected(promoted.error());
  auto next_state = promoted->state();
  next_state.learners = std::move(learners);
  return configuration{std::move(next_state)};
}
auto configuration::remove_member(const node_id &id) const
    -> std::expected<configuration, raft_error> {
  if (contains_learner(id) && !contains(id)) {
    auto learners = state_.learners;
    std::erase(learners, id);
    return configuration{configuration_state{.voters = state_.voters,
                                             .old_voters = state_.old_voters,
                                             .learners = std::move(learners)}};
  }
  if (!contains(id))
    return std::unexpected(
        raft_error{.code = raft_errc::configuration_error,
                   .message = "cannot remove a node that is not a member"});
  auto voters = state_.voters;
  std::erase(voters, id);
  if (voters.empty())
    return std::unexpected(
        raft_error{.code = raft_errc::configuration_error,
                   .message = "cannot remove the last voter"});
  return with_joint(std::move(voters));
}
auto configuration::leave_joint() const
    -> std::expected<configuration, raft_error> {
  if (!joint())
    return std::unexpected(raft_error{.code = raft_errc::configuration_error,
                                      .message = "configuration is not joint"});
  return configuration{configuration_state{.voters = state_.voters,
                                           .learners = state_.learners}};
}
vote_tracker::vote_tracker(configuration conf) : conf_(std::move(conf)) {}
void vote_tracker::reset() {
  granted_.clear();
  rejected_.clear();
}
void vote_tracker::grant(const node_id &id) {
  if (!conf_.contains(id))
    return;
  rejected_.erase(id);
  granted_.insert(id);
}
void vote_tracker::reject(const node_id &id) {
  if (!conf_.contains(id))
    return;
  granted_.erase(id);
  rejected_.insert(id);
}
auto vote_tracker::granted() const noexcept -> const std::set<node_id> & {
  return granted_;
}
auto vote_tracker::result() const -> quorum_result {
  if (conf_.has_quorum(granted_))
    return quorum_result::won;
  auto possible = granted_;
  for (const auto &id : conf_.all_voters())
    if (!rejected_.contains(id))
      possible.insert(id);
  return conf_.has_quorum(possible) ? quorum_result::pending
                                    : quorum_result::lost;
}
} // namespace cnetmod::raft
