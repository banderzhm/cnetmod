export module cnetmod.protocol.raft:configuration;

import std;
import :types;

namespace cnetmod::raft
{
    namespace detail
    {
        inline auto normalized(std::vector<node_id> ids) -> std::vector<node_id>
        {
            std::erase_if(ids, [](const node_id& id) { return id.empty(); });
            std::ranges::sort(ids);
            ids.erase(std::ranges::unique(ids).begin(), ids.end());
            return ids;
        }

        inline auto majority_of(std::size_t n) noexcept -> std::size_t
        {
            return n / 2 + 1;
        }
    } // namespace detail

    export class configuration
    {
    public:
        configuration() = default;

        explicit configuration(configuration_state state)
            : state_(std::move(state))
        {
            state_.voters = detail::normalized(std::move(state_.voters));
            state_.old_voters = detail::normalized(std::move(state_.old_voters));
            state_.learners = detail::normalized(std::move(state_.learners));
            std::erase_if(state_.learners, [this](const node_id& id)
            {
                return std::ranges::binary_search(state_.voters, id) ||
                    std::ranges::binary_search(state_.old_voters, id);
            });
        }

        explicit configuration(std::vector<node_id> voters)
            : configuration(configuration_state{.voters = std::move(voters)})
        {
        }

        [[nodiscard]] auto state() const -> const configuration_state&
        {
            return state_;
        }

        [[nodiscard]] auto voters() const -> const std::vector<node_id>&
        {
            return state_.voters;
        }

        [[nodiscard]] auto old_voters() const -> const std::vector<node_id>&
        {
            return state_.old_voters;
        }

        [[nodiscard]] auto learners() const -> const std::vector<node_id>&
        {
            return state_.learners;
        }

        [[nodiscard]] auto joint() const noexcept -> bool
        {
            return state_.joint();
        }

        [[nodiscard]] auto empty() const noexcept -> bool
        {
            return state_.voters.empty() && state_.old_voters.empty() && state_.learners.empty();
        }

        [[nodiscard]] auto contains(const node_id& id) const -> bool
        {
            return std::ranges::binary_search(state_.voters, id) ||
                std::ranges::binary_search(state_.old_voters, id);
        }

        [[nodiscard]] auto contains_learner(const node_id& id) const -> bool
        {
            return std::ranges::binary_search(state_.learners, id);
        }

        [[nodiscard]] auto is_member(const node_id& id) const -> bool
        {
            return contains(id) || contains_learner(id);
        }

        [[nodiscard]] auto all_voters() const -> std::vector<node_id>
        {
            auto out = state_.voters;
            out.insert(out.end(), state_.old_voters.begin(), state_.old_voters.end());
            return detail::normalized(std::move(out));
        }

        [[nodiscard]] auto all_members() const -> std::vector<node_id>
        {
            auto out = all_voters();
            out.insert(out.end(), state_.learners.begin(), state_.learners.end());
            return detail::normalized(std::move(out));
        }

        [[nodiscard]] auto quorum_size() const noexcept -> std::size_t
        {
            return detail::majority_of(state_.voters.size());
        }

        [[nodiscard]] auto old_quorum_size() const noexcept -> std::size_t
        {
            return detail::majority_of(state_.old_voters.size());
        }

        [[nodiscard]] auto has_quorum(const std::set<node_id>& acked) const -> bool
        {
            auto count_in = [&acked](const std::vector<node_id>& voters)
            {
                return static_cast<std::size_t>(std::ranges::count_if(voters, [&acked](const node_id& id)
                {
                    return acked.contains(id);
                }));
            };

            if (state_.voters.empty())
                return false;

            const bool new_quorum = count_in(state_.voters) >= quorum_size();
            if (!joint())
                return new_quorum;
            return new_quorum && count_in(state_.old_voters) >= old_quorum_size();
        }

        [[nodiscard]] auto with_joint(std::vector<node_id> new_voters) const
            -> std::expected<configuration, raft_error>
        {
            new_voters = detail::normalized(std::move(new_voters));
            if (new_voters.empty())
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::configuration_error,
                    .message = "joint configuration cannot be empty",
                });
            }
            if (joint())
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::configuration_error,
                    .message = "cannot enter a joint configuration while already joint",
                });
            }
            return configuration{
                configuration_state{
                    .voters = std::move(new_voters),
                    .old_voters = state_.voters,
                    .learners = state_.learners,
                }
            };
        }

        [[nodiscard]] auto with_learners(std::vector<node_id> learners) const
            -> std::expected<configuration, raft_error>
        {
            learners = detail::normalized(std::move(learners));
            std::erase_if(learners, [this](const node_id& id)
            {
                return contains(id);
            });
            return configuration{
                configuration_state{
                    .voters = state_.voters,
                    .old_voters = state_.old_voters,
                    .learners = std::move(learners),
                }
            };
        }

        [[nodiscard]] auto promote_learner(const node_id& id) const
            -> std::expected<configuration, raft_error>
        {
            if (!contains_learner(id))
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::configuration_error,
                    .message = "cannot promote a node that is not a learner",
                });
            }
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

        [[nodiscard]] auto remove_member(const node_id& id) const
            -> std::expected<configuration, raft_error>
        {
            if (contains_learner(id) && !contains(id))
            {
                auto learners = state_.learners;
                std::erase(learners, id);
                return configuration{
                    configuration_state{
                        .voters = state_.voters,
                        .old_voters = state_.old_voters,
                        .learners = std::move(learners),
                    }
                };
            }
            if (!contains(id))
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::configuration_error,
                    .message = "cannot remove a node that is not a member",
                });
            }
            auto voters = state_.voters;
            std::erase(voters, id);
            if (voters.empty())
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::configuration_error,
                    .message = "cannot remove the last voter",
                });
            }
            return with_joint(std::move(voters));
        }

        [[nodiscard]] auto leave_joint() const -> std::expected<configuration, raft_error>
        {
            if (!joint())
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::configuration_error,
                    .message = "configuration is not joint",
                });
            }
            return configuration{
                configuration_state{
                    .voters = state_.voters,
                    .learners = state_.learners,
                }
            };
        }

    private:
        configuration_state state_;
    };

    export enum class quorum_result
    {
        pending,
        won,
        lost,
    };

    export class vote_tracker
    {
    public:
        explicit vote_tracker(configuration conf)
            : conf_(std::move(conf))
        {
        }

        void reset()
        {
            granted_.clear();
            rejected_.clear();
        }

        void grant(const node_id& id)
        {
            if (!conf_.contains(id)) return;
            rejected_.erase(id);
            granted_.insert(id);
        }

        void reject(const node_id& id)
        {
            if (!conf_.contains(id)) return;
            granted_.erase(id);
            rejected_.insert(id);
        }

        [[nodiscard]] auto granted() const noexcept -> const std::set<node_id>&
        {
            return granted_;
        }

        [[nodiscard]] auto result() const -> quorum_result
        {
            if (conf_.has_quorum(granted_))
                return quorum_result::won;

            auto possible = granted_;
            for (const auto& id : conf_.all_voters())
            {
                if (!rejected_.contains(id))
                    possible.insert(id);
            }
            return conf_.has_quorum(possible) ? quorum_result::pending : quorum_result::lost;
        }

    private:
        configuration conf_;
        std::set<node_id> granted_;
        std::set<node_id> rejected_;
    };
} // namespace cnetmod::raft