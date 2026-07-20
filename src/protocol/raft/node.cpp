module cnetmod.protocol.raft;

import std;
import :node;

namespace cnetmod::raft
{
    namespace
    {
        template <typename Predicate>
        auto group_has_quorum(const std::vector<node_id>& voters, Predicate&& acked) -> bool
        {
            const auto required = voters.size() / 2 + 1;
            std::size_t count = 0;
            for (const auto& voter : voters)
            {
                if (acked(voter) && ++count >= required)
                    return true;
            }
            return false;
        }

        template <typename Predicate>
        auto configuration_has_quorum(const configuration& conf, Predicate&& acked) -> bool
        {
            if (conf.voters().empty())
                return false;
            if (!group_has_quorum(conf.voters(), acked))
                return false;
            return !conf.joint() || group_has_quorum(conf.old_voters(), acked);
        }
    } // namespace

    raft_node::raft_node(raft_config cfg, std::shared_ptr<raft_storage> store)
        : raft_node(std::move(cfg), std::move(store), nullptr)
    {
    }

    raft_node::raft_node(raft_config cfg,
                         std::shared_ptr<raft_storage> store,
                         state_machine* machine)
        : cfg_(std::move(cfg)),
          store_(std::move(store)),
          log_(store_),
          conf_(cfg_.initial_configuration()),
          fsm_(machine)
    {
        if (!store_) throw std::invalid_argument("raft_node requires storage");
        reset_election_deadline();
        refresh_leader_quorum();
        state_ = store_->load_hard_state();
        fsm_.set_last_applied(state_.last_applied);

        if (auto snapshot = store_->load_snapshot_metadata(); snapshot.valid())
        {
            if (!snapshot.configuration.voters.empty())
                conf_ = configuration{snapshot.configuration};
            state_.commit_index = std::max(state_.commit_index, snapshot.last_included_index);
            state_.last_applied = std::max(state_.last_applied, snapshot.last_included_index);
            fsm_.set_last_applied(state_.last_applied);
        }
    }

    auto raft_node::id() const noexcept -> std::string_view
    {
        return cfg_.id;
    }

    auto raft_node::role() const noexcept -> node_role
    {
        return role_;
    }

    auto raft_node::current_term() const noexcept -> term_t
    {
        return state_.current_term;
    }

    auto raft_node::commit_index() const noexcept -> log_index
    {
        return state_.commit_index;
    }

    auto raft_node::last_applied() const noexcept -> log_index
    {
        return state_.last_applied;
    }

    auto raft_node::last_log_index() const -> log_index
    {
        return log_.last_index();
    }

    auto raft_node::voted_for() const noexcept -> std::string_view
    {
        return state_.voted_for;
    }

    auto raft_node::leader_id() const noexcept -> std::string_view
    {
        return leader_id_;
    }

    auto raft_node::current_configuration() const -> configuration_state
    {
        return conf_.state();
    }

    auto raft_node::stopped() const noexcept -> bool
    {
        return stopped_;
    }

    auto raft_node::fatal_error() const noexcept -> const raft_error&
    {
        return fatal_error_;
    }

    auto raft_node::metrics() const -> raft_metrics
    {
        const auto snapshot = store_->load_snapshot_metadata();
        return {
            .current_term = state_.current_term,
            .role = role_,
            .leader_id = leader_id_,
            .first_log_index = log_.first_index(),
            .last_log_index = log_.last_index(),
            .commit_index = state_.commit_index,
            .last_applied = state_.last_applied,
            .snapshot_index = snapshot.last_included_index,
            .snapshot_term = snapshot.last_included_term,
            .voters = conf_.voters().size(),
            .learners = conf_.learners().size(),
            .pending_reads = pending_reads_.size(),
            .active_followers = progress_.active_followers(),
            .stopped = stopped_,
            .last_error = fatal_error_.message,
        };
    }

    auto raft_node::election_timeout_elapsed(std::chrono::steady_clock::duration timeout) const
        -> bool
    {
        if (role_ == node_role::leader)
            return false;
        return std::chrono::steady_clock::now() - last_leader_contact_ >= timeout;
    }

    auto raft_node::leader_lease_valid(std::chrono::steady_clock::duration timeout) const
        -> bool
    {
        if (role_ != node_role::leader)
            return false;
        if (conf_.quorum_size() == 1)
            return true;
        return std::chrono::steady_clock::now() - last_quorum_contact_ <= timeout;
    }

    auto raft_node::make_append_entries(const node_id& peer) const
        -> append_entries_request
    {
        log_index next_index = log_.last_index() + 1;
        if (const auto* progress = progress_.get(peer))
            next_index = progress->next_index;

        const auto first_index = log_.first_index();
        if (next_index < first_index)
            next_index = first_index;

        const auto prev_index = next_index > 0 ? next_index - 1 : 0;
        append_entries_request req;
        req.term = state_.current_term;
        req.leader_id = cfg_.id;
        req.prev_log_index = prev_index;
        req.prev_log_term = log_.term_at(prev_index);
        const auto* peer_progress = progress_.get(peer);
        const bool inflight_full =
            peer_progress &&
            peer_progress->state == replication_state::replicate &&
            peer_progress->inflight.full();
        if (!inflight_full)
            req.entries = log_.entries_from(next_index, cfg_.options.max_entries_per_append);
        req.leader_commit = state_.commit_index;
        for (const auto& [_, pending] : pending_reads_)
        {
            if (!pending.ready)
            {
                req.read_contexts.push_back(read_index_request{
                    .id = pending.id,
                    .context = pending.context,
                });
            }
        }
        return req;
    }

    auto raft_node::should_send_snapshot(const node_id& peer) const -> bool
    {
        const auto* progress = progress_.get(peer);
        if (!progress)
            return false;
        const auto snapshot = store_->load_snapshot_metadata();
        return snapshot.valid() && progress->next_index < log_.first_index();
    }

    auto raft_node::make_install_snapshot(const node_id& peer) const
        -> install_snapshot_request
    {
        (void)peer;
        auto metadata = store_->load_snapshot_metadata();
        return {
            .term = state_.current_term,
            .leader_id = cfg_.id,
            .metadata = metadata,
            .uri = metadata.uri,
        };
    }

    void raft_node::mark_snapshot_sent(const node_id& peer)
    {
        const auto snapshot = store_->load_snapshot_metadata();
        if (snapshot.valid())
            progress_.mark_snapshot(peer, snapshot.last_included_index);
    }

    void raft_node::mark_append_sent(const node_id& peer, log_index last_index)
    {
        progress_.mark_sent(peer, last_index);
    }

    auto raft_node::begin_pre_vote() -> request_vote_request
    {
        if (stopped_) return {};
        if (!conf_.contains(cfg_.id)) return {};
        role_ = node_role::pre_candidate;
        votes_granted_.clear();
        votes_rejected_.clear();
        votes_granted_.insert(cfg_.id);

        return {
            .term = state_.current_term + 1,
            .candidate_id = cfg_.id,
            .last_log_index = log_.last_index(),
            .last_log_term = log_.last_term(),
            .pre_vote = true,
        };
    }

    auto raft_node::begin_election() -> request_vote_request
    {
        if (stopped_) return {};
        if (!conf_.contains(cfg_.id)) return {};
        role_ = node_role::candidate;
        leader_id_.clear();
        ++state_.current_term;
        state_.voted_for = cfg_.id;
        votes_granted_.clear();
        votes_rejected_.clear();
        votes_granted_.insert(cfg_.id);
        persist();

        request_vote_request req{
            .term = state_.current_term,
            .candidate_id = cfg_.id,
            .last_log_index = log_.last_index(),
            .last_log_term = log_.last_term(),
        };

        if (conf_.has_quorum(votes_granted_))
            become_leader();

        return req;
    }

    auto raft_node::handle_request_vote(const request_vote_request& req)
        -> request_vote_response
    {
        if (stopped_)
            return {.term = state_.current_term, .vote_granted = false, .pre_vote = req.pre_vote};

        if (!conf_.contains(cfg_.id) || !conf_.contains(req.candidate_id))
        {
            return {
                .term = state_.current_term,
                .vote_granted = false,
                .pre_vote = req.pre_vote,
            };
        }

        if (req.pre_vote)
        {
            const bool grant =
                req.term >= state_.current_term &&
                is_log_at_least_up_to_date(log_.storage(), req.last_log_index, req.last_log_term);
            return {
                .term = state_.current_term,
                .vote_granted = grant,
                .pre_vote = true,
            };
        }

        if (req.term < state_.current_term)
            return {.term = state_.current_term, .vote_granted = false};

        if (req.term > state_.current_term)
            step_down(req.term);

        const bool can_vote =
            state_.voted_for.empty() || state_.voted_for == req.candidate_id;
        const bool log_ok =
            is_log_at_least_up_to_date(log_.storage(), req.last_log_index, req.last_log_term);
        const bool grant = can_vote && log_ok;

        if (grant)
        {
            state_.voted_for = req.candidate_id;
            leader_id_.clear();
            reset_election_deadline();
            persist();
        }

        return {.term = state_.current_term, .vote_granted = grant};
    }

    auto raft_node::handle_vote_response(const node_id& voter,
                                         const request_vote_response& resp) -> bool
    {
        if (stopped_) return false;

        if (resp.term > state_.current_term)
        {
            step_down(resp.term);
            return false;
        }

        if (resp.pre_vote)
        {
            if (role_ != node_role::pre_candidate || !resp.vote_granted)
                return false;
            votes_granted_.insert(voter);
            return conf_.has_quorum(votes_granted_);
        }

        if (role_ != node_role::candidate || resp.term != state_.current_term)
            return false;

        if (resp.vote_granted)
        {
            votes_granted_.insert(voter);
        }
        else
        {
            votes_rejected_.insert(voter);
        }

        if (conf_.has_quorum(votes_granted_))
        {
            become_leader();
            return true;
        }
        return false;
    }

    auto raft_node::handle_timeout_now(const timeout_now_request& req)
        -> std::optional<request_vote_request>
    {
        if (stopped_ || !conf_.contains(cfg_.id) || !conf_.contains(req.leader_id))
            return std::nullopt;

        if (req.term < state_.current_term)
            return std::nullopt;

        if (req.term > state_.current_term)
            step_down(req.term);

        if (!leader_id_.empty() && leader_id_ != req.leader_id)
            return std::nullopt;

        return begin_election();
    }

    auto raft_node::handle_append_entries(const append_entries_request& req)
        -> append_entries_response
    {
        if (stopped_)
            return {.term = state_.current_term, .success = false, .match_index = log_.last_index()};

        if (req.term < state_.current_term)
        {
            return {
                .term = state_.current_term,
                .success = false,
                .match_index = log_.last_index(),
            };
        }

        if (!conf_.contains(req.leader_id))
        {
            return {
                .term = state_.current_term,
                .success = false,
                .match_index = log_.last_index(),
            };
        }

        if (req.term > state_.current_term || role_ != node_role::follower)
            step_down(req.term);
        leader_id_ = req.leader_id;
        reset_election_deadline();

        append_result appended;
        try
        {
            appended = log_.append_from_leader(req.prev_log_index,
                                               req.prev_log_term,
                                               req.entries);
        }
        catch (const std::exception& e)
        {
            stop({.code = raft_errc::storage_error, .message = e.what()});
            return {.term = state_.current_term, .success = false, .match_index = log_.last_index()};
        }
        catch (...)
        {
            stop({.code = raft_errc::storage_error, .message = "append entries storage failure"});
            return {.term = state_.current_term, .success = false, .match_index = log_.last_index()};
        }
        if (!appended.success)
        {
            return {
                .term = state_.current_term,
                .success = false,
                .match_index = appended.match_index,
                .conflict_index = appended.conflict_index,
                .conflict_term = appended.conflict_term,
            };
        }

        if (req.leader_commit > state_.commit_index)
        {
            state_.commit_index = std::min(req.leader_commit, log_.last_index());
            persist();
            apply_committed();
        }

        append_entries_response resp{
            .term = state_.current_term,
            .success = true,
            .match_index = appended.match_index,
        };
        resp.read_acks.reserve(req.read_contexts.size());
        for (const auto& read : req.read_contexts)
            resp.read_acks.push_back(read.id);
        return resp;
    }

    auto raft_node::handle_append_entries_response(const node_id& peer,
                                                   const append_entries_response& resp) -> bool
    {
        if (stopped_) return false;

        if (resp.term > state_.current_term)
        {
            step_down(resp.term);
            return false;
        }
        if (role_ != node_role::leader || resp.term != state_.current_term)
            return false;

        if (resp.success)
        {
            progress_.mark_replicated(peer, resp.match_index);
            for (auto id : resp.read_acks)
                acknowledge_read(peer, id);
            refresh_leader_quorum();
            advance_commit_index();
            return true;
        }

        progress_.mark_rejected(peer, resp.match_index + 1, resp.conflict_index);
        return false;
    }

    auto raft_node::handle_install_snapshot(const install_snapshot_request& req)
        -> install_snapshot_response
    {
        if (stopped_)
            return {.term = state_.current_term, .success = false};

        if (req.term < state_.current_term)
            return {.term = state_.current_term, .success = false};

        if (!conf_.contains(req.leader_id) && !conf_.empty())
            return {.term = state_.current_term, .success = false};

        if (req.term > state_.current_term || role_ != node_role::follower)
            step_down(req.term);
        leader_id_ = req.leader_id;
        reset_election_deadline();

        auto metadata = req.metadata;
        if (!req.uri.empty())
            metadata.uri = req.uri;
        auto loaded = fsm_.load_snapshot(snapshot_reader{
            .metadata = metadata,
            .uri = req.uri,
        });
        if (!loaded)
        {
            stop(loaded.error());
            return {.term = state_.current_term, .success = false};
        }

        append_result restored;
        try
        {
            restored = log_.restore_snapshot(metadata);
        }
        catch (const std::exception& e)
        {
            stop({.code = raft_errc::storage_error, .message = e.what()});
            return {.term = state_.current_term, .success = false};
        }
        catch (...)
        {
            stop({.code = raft_errc::storage_error, .message = "snapshot restore storage failure"});
            return {.term = state_.current_term, .success = false};
        }
        if (!restored.success)
            return {.term = state_.current_term, .success = false};

        if (!metadata.configuration.voters.empty())
            conf_ = configuration{metadata.configuration};

        state_.commit_index = std::max(state_.commit_index, metadata.last_included_index);
        state_.last_applied = std::max(state_.last_applied, metadata.last_included_index);
        persist();
        return {.term = state_.current_term, .success = true};
    }

    auto raft_node::handle_install_snapshot_response(const node_id& peer,
                                                     const install_snapshot_response& resp) -> bool
    {
        if (stopped_) return false;

        if (resp.term > state_.current_term)
        {
            step_down(resp.term);
            return false;
        }
        if (role_ != node_role::leader || resp.term != state_.current_term || !resp.success)
            return false;

        auto* progress = progress_.get(peer);
        if (!progress || progress->pending_snapshot == 0)
            return false;

        progress_.mark_replicated(peer, progress->pending_snapshot);
        refresh_leader_quorum();
        advance_commit_index();
        return true;
    }

    auto raft_node::append_command(std::string command)
        -> std::expected<log_entry, raft_error>
    {
        if (stopped_)
        {
            return std::unexpected(fatal_error_);
        }
        if (role_ != node_role::leader)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::not_leader,
                .message = "append_command requires leader role",
            });
        }

        log_entry entry;
        try
        {
            entry = log_.append_as_leader(state_.current_term, std::move(command));
        }
        catch (const std::exception& e)
        {
            stop({.code = raft_errc::storage_error, .message = e.what()});
            return std::unexpected(fatal_error_);
        }
        catch (...)
        {
            stop({.code = raft_errc::storage_error, .message = "leader append storage failure"});
            return std::unexpected(fatal_error_);
        }
        if (conf_.quorum_size() == 1)
        {
            state_.commit_index = entry.index;
            persist();
            apply_committed();
        }
        return entry;
    }

    auto raft_node::enter_joint_configuration(std::vector<node_id> new_voters)
        -> std::expected<log_entry, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (pending_configuration_index_ && *pending_configuration_index_ > state_.commit_index)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::configuration_error,
                .message = "configuration change already pending",
            });
        }
        auto next = conf_.with_joint(std::move(new_voters));
        if (!next) return std::unexpected(next.error());
        return append_configuration(std::move(*next));
    }

    auto raft_node::leave_joint_configuration() -> std::expected<log_entry, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (pending_configuration_index_ && *pending_configuration_index_ > state_.commit_index)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::configuration_error,
                .message = "configuration change already pending",
            });
        }
        auto next = conf_.leave_joint();
        if (!next) return std::unexpected(next.error());
        return append_configuration(std::move(*next));
    }

    auto raft_node::set_learners(std::vector<node_id> learners)
        -> std::expected<log_entry, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (pending_configuration_index_ && *pending_configuration_index_ > state_.commit_index)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::configuration_error,
                .message = "configuration change already pending",
            });
        }
        auto next = conf_.with_learners(std::move(learners));
        if (!next) return std::unexpected(next.error());
        return append_configuration(std::move(*next));
    }

    auto raft_node::promote_learner(const node_id& id) -> std::expected<log_entry, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (pending_configuration_index_ && *pending_configuration_index_ > state_.commit_index)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::configuration_error,
                .message = "configuration change already pending",
            });
        }
        auto next = conf_.promote_learner(id);
        if (!next) return std::unexpected(next.error());
        return append_configuration(std::move(*next));
    }

    auto raft_node::remove_node(const node_id& id) -> std::expected<log_entry, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (pending_configuration_index_ && *pending_configuration_index_ > state_.commit_index)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::configuration_error,
                .message = "configuration change already pending",
            });
        }
        auto next = conf_.remove_member(id);
        if (!next) return std::unexpected(next.error());
        return append_configuration(std::move(*next));
    }

    auto raft_node::transfer_leader(const node_id& target)
        -> std::expected<std::optional<timeout_now_request>, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (role_ != node_role::leader)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::not_leader,
                .message = "leader transfer requires leader role",
            });
        }
        if (target == cfg_.id)
            return std::optional<timeout_now_request>{std::nullopt};
        if (!conf_.contains(target))
        {
            return std::unexpected(raft_error{
                .code = raft_errc::not_voter,
                .message = "leader transfer target must be a voter",
            });
        }

        pending_leader_transfer_ = target;
        if (auto request = make_timeout_now(target))
        {
            pending_leader_transfer_.reset();
            return std::optional<timeout_now_request>{std::move(*request)};
        }
        return std::optional<timeout_now_request>{std::nullopt};
    }

    auto raft_node::take_pending_leader_transfer(const node_id& peer)
        -> std::optional<timeout_now_request>
    {
        if (!pending_leader_transfer_ || *pending_leader_transfer_ != peer)
            return std::nullopt;
        auto request = make_timeout_now(peer);
        if (request)
            pending_leader_transfer_.reset();
        return request;
    }

    auto raft_node::create_snapshot(std::string uri)
        -> std::expected<snapshot_metadata, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (state_.commit_index == 0)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::snapshot_required,
                .message = "cannot create snapshot before any committed log",
            });
        }

        snapshot_metadata metadata{
            .last_included_index = state_.commit_index,
            .last_included_term = log_.term_at(state_.commit_index),
            .uri = uri,
            .configuration = conf_.state(),
        };
        auto saved = fsm_.save_snapshot(snapshot_writer{
            .metadata = metadata,
            .uri = std::move(uri),
        });
        if (!saved)
        {
            stop(saved.error());
            return std::unexpected(fatal_error_);
        }

        try
        {
            store_->save_snapshot_metadata(metadata);
            log_.compact_prefix(metadata.last_included_index + 1);
        }
        catch (const std::exception& e)
        {
            stop({.code = raft_errc::storage_error, .message = e.what()});
            return std::unexpected(fatal_error_);
        }
        catch (...)
        {
            stop({.code = raft_errc::storage_error, .message = "snapshot metadata storage failure"});
            return std::unexpected(fatal_error_);
        }
        return metadata;
    }

    auto raft_node::maybe_create_snapshot(const raft_snapshot_policy& policy)
        -> std::expected<std::optional<snapshot_metadata>, raft_error>
    {
        if (policy.log_entries_threshold == 0)
            return std::optional<snapshot_metadata>{std::nullopt};
        if (stopped_)
            return std::unexpected(fatal_error_);

        const auto snapshot = store_->load_snapshot_metadata();
        if (state_.commit_index <= snapshot.last_included_index)
            return std::optional<snapshot_metadata>{std::nullopt};

        const auto compactable_entries = state_.commit_index >= log_.first_index()
                                             ? static_cast<std::size_t>(state_.commit_index - log_.first_index() + 1)
                                             : 0u;
        if (compactable_entries < policy.log_entries_threshold)
            return std::optional<snapshot_metadata>{std::nullopt};

        const auto now = std::chrono::steady_clock::now();
        if (last_auto_snapshot_ != std::chrono::steady_clock::time_point{} &&
            now - last_auto_snapshot_ < policy.min_interval)
        {
            return std::optional<snapshot_metadata>{std::nullopt};
        }

        auto uri = std::format("{}-{}-{}.snapshot",
                               policy.uri_prefix.empty() ? std::string{"raft-snapshot"} : policy.uri_prefix,
                               state_.current_term,
                               state_.commit_index);
        auto created = create_snapshot(std::move(uri));
        if (!created)
            return std::unexpected(created.error());
        last_auto_snapshot_ = now;
        return std::optional<snapshot_metadata>{std::move(*created)};
    }

    auto raft_node::read_index(read_index_request request)
        -> std::expected<read_index_response, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (role_ != node_role::leader)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::not_leader,
                .message = "read_index requires leader role",
            });
        }

        pending_read pending{
            .id = request.id,
            .term = state_.current_term,
            .index = state_.commit_index,
            .context = std::move(request.context),
            .acked = {cfg_.id},
            .created_at = std::chrono::steady_clock::now(),
        };
        pending.ready = conf_.has_quorum(pending.acked) ||
        (cfg_.options.lease_read &&
            cfg_.options.check_quorum &&
            leader_lease_valid(cfg_.options.leader_lease_timeout));

        read_index_response response{
            .id = request.id,
            .term = state_.current_term,
            .index = state_.commit_index,
            .ready = pending.ready,
        };
        pending_reads_[response.id] = std::move(pending);
        return response;
    }

    auto raft_node::query_read_index(request_id id) const -> std::optional<read_index_response>
    {
        auto it = pending_reads_.find(id);
        if (it == pending_reads_.end())
            return std::nullopt;
        return read_index_response{
            .id = it->second.id,
            .term = it->second.term,
            .index = it->second.index,
            .ready = it->second.ready,
        };
    }

    auto raft_node::consume_read_index(request_id id) -> std::optional<read_index_response>
    {
        auto it = pending_reads_.find(id);
        if (it == pending_reads_.end() || !it->second.ready)
            return std::nullopt;
        read_index_response response{
            .id = it->second.id,
            .term = it->second.term,
            .index = it->second.index,
            .ready = true,
        };
        pending_reads_.erase(it);
        return response;
    }

    auto raft_node::expire_read_indexes(std::chrono::steady_clock::duration timeout) -> std::size_t
    {
        const auto now = std::chrono::steady_clock::now();
        std::size_t expired = 0;
        for (auto it = pending_reads_.begin(); it != pending_reads_.end();)
        {
            if (!it->second.ready && now - it->second.created_at >= timeout)
            {
                it = pending_reads_.erase(it);
                ++expired;
            }
            else
            {
                ++it;
            }
        }
        return expired;
    }

    auto raft_node::pending_read_count() const noexcept -> std::size_t
    {
        return pending_reads_.size();
    }

    auto raft_node::check_leader_quorum(std::chrono::steady_clock::duration timeout) -> bool
    {
        if (stopped_)
            return false;
        if (role_ != node_role::leader)
            return true;
        if (leader_lease_valid(timeout))
            return true;
        step_down(state_.current_term);
        return false;
    }

    void raft_node::stop(raft_error error)
    {
        if (stopped_) return;
        if (role_ == node_role::leader)
            fsm_.on_leader_stop(state_.current_term);
        stopped_ = true;
        fatal_error_ = std::move(error);
        if (fatal_error_.code == raft_errc::ok)
            fatal_error_.code = raft_errc::stopped;
        role_ = node_role::follower;
        pending_reads_.clear();
        votes_granted_.clear();
        votes_rejected_.clear();
        pending_leader_transfer_.reset();
    }

    void raft_node::persist()
    {
        if (stopped_) return;
        try
        {
            store_->save_hard_state(state_);
        }
        catch (const std::exception& e)
        {
            stop({.code = raft_errc::storage_error, .message = e.what()});
        }
        catch (...)
        {
            stop({.code = raft_errc::storage_error, .message = "hard state storage failure"});
        }
    }

    auto raft_node::stopped_error() const -> std::expected<void, raft_error>
    {
        if (stopped_) return std::unexpected(fatal_error_);
        return {};
    }

    void raft_node::reset_election_deadline()
    {
        last_leader_contact_ = std::chrono::steady_clock::now();
    }

    void raft_node::refresh_leader_quorum()
    {
        if (role_ != node_role::leader)
            return;

        const auto active = [this](const node_id& id)
        {
            if (id == cfg_.id)
                return true;
            const auto* progress = progress_.get(id);
            return progress && progress->recent_active;
        };
        if (configuration_has_quorum(conf_, active))
            last_quorum_contact_ = std::chrono::steady_clock::now();
    }

    void raft_node::step_down(term_t new_term)
    {
        if (stopped_) return;
        if (role_ == node_role::leader)
            fsm_.on_leader_stop(state_.current_term);
        role_ = node_role::follower;
        if (new_term != state_.current_term)
        {
            state_.current_term = new_term;
            state_.voted_for.clear();
        }
        votes_granted_.clear();
        votes_rejected_.clear();
        pending_reads_.clear();
        pending_leader_transfer_.reset();
        reset_election_deadline();
        persist();
    }

    void raft_node::become_leader()
    {
        if (stopped_) return;
        role_ = node_role::leader;
        leader_id_ = cfg_.id;
        votes_granted_.clear();
        votes_rejected_.clear();
        progress_.reset(conf_.all_members(), log_.last_index() + 1,
                        cfg_.options.max_inflight_append);
        if (auto* self = progress_.get(cfg_.id))
            self->match_index = log_.last_index();
        log_entry noop;
        try
        {
            noop = log_.append_noop(state_.current_term);
        }
        catch (const std::exception& e)
        {
            stop({.code = raft_errc::storage_error, .message = e.what()});
            return;
        }
        catch (...)
        {
            stop({.code = raft_errc::storage_error, .message = "leader noop append failure"});
            return;
        }
        if (auto* self = progress_.get(cfg_.id))
        {
            self->match_index = noop.index;
            self->next_index = noop.index + 1;
        }
        fsm_.on_leader_start(state_.current_term);
        refresh_leader_quorum();
        if (conf_.quorum_size() == 1)
        {
            state_.commit_index = noop.index;
            persist();
            apply_committed();
        }
    }

    void raft_node::advance_commit_index()
    {
        if (stopped_) return;
        auto last = log_.last_index();
        while (last > state_.commit_index)
        {
            const auto matched = [this, last](const node_id& id)
            {
                if (id == cfg_.id)
                    return true;
                const auto* progress = progress_.get(id);
                return progress && progress->match_index >= last;
            };

            if (configuration_has_quorum(conf_, matched) &&
                log_.term_at(last) == state_.current_term)
            {
                state_.commit_index = last;
                persist();
                apply_committed();
                return;
            }
            --last;
        }
    }

    void raft_node::apply_committed()
    {
        if (stopped_) return;
        const auto apply_one = [this](const log_entry& entry) -> bool
        {
            if (entry.index != state_.last_applied + 1) return false;

            auto applied = fsm_.apply_entry(entry);
            if (!applied)
            {
                stop(applied.error());
                return false;
            }

            state_.last_applied = *applied;
            if (entry.type == entry_type::configuration)
            {
                conf_ = configuration{entry.configuration};
                if (pending_configuration_index_ && *pending_configuration_index_ <= entry.index)
                    pending_configuration_index_.reset();
                if (role_ == node_role::leader && !conf_.contains(cfg_.id))
                {
                    step_down(state_.current_term);
                }
                else if (role_ == node_role::leader && conf_.joint())
                {
                    auto next = conf_.leave_joint();
                    if (next)
                    {
                        try
                        {
                            auto leave = log_.append_configuration(state_.current_term, next->state());
                            pending_configuration_index_ = leave.index;
                            if (auto* self = progress_.get(cfg_.id))
                            {
                                self->match_index = leave.index;
                                self->next_index = leave.index + 1;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            stop({.code = raft_errc::storage_error, .message = e.what()});
                        }
                        catch (...)
                        {
                            stop({
                                .code = raft_errc::storage_error,
                                .message = "auto leave-joint append failure"
                            });
                        }
                    }
                }
            }
            persist();
            return !stopped_;
        };

        while (state_.last_applied < state_.commit_index)
        {
            const auto first = state_.last_applied + 1;
            const auto committed_gap = state_.commit_index - state_.last_applied;
            if (committed_gap == 1)
            {
                auto entry = log_.entry_at(first);
                if (!entry) return;
                if (!apply_one(*entry)) return;
            }
            else
            {
                auto entries = log_.entries_from(first, static_cast<std::size_t>(committed_gap));
                if (entries.empty() || entries.front().index != first) return;
                for (const auto& entry : entries)
                {
                    if (!apply_one(entry)) return;
                }
            }
        }
    }

    void raft_node::acknowledge_read(const node_id& peer, request_id id)
    {
        auto it = pending_reads_.find(id);
        if (it == pending_reads_.end())
            return;
        if (it->second.term != state_.current_term)
            return;
        it->second.acked.insert(cfg_.id);
        it->second.acked.insert(peer);
        if (conf_.has_quorum(it->second.acked))
            it->second.ready = true;
    }

    auto raft_node::make_timeout_now(const node_id& target) const
        -> std::optional<timeout_now_request>
    {
        if (role_ != node_role::leader || !conf_.contains(target))
            return std::nullopt;
        auto* progress = progress_.get(target);
        if (!progress || progress->match_index < log_.last_index())
            return std::nullopt;
        return timeout_now_request{
            .term = state_.current_term,
            .leader_id = cfg_.id,
        };
    }

    auto raft_node::append_configuration(configuration next)
        -> std::expected<log_entry, raft_error>
    {
        if (stopped_)
            return std::unexpected(fatal_error_);
        if (role_ != node_role::leader)
        {
            return std::unexpected(raft_error{
                .code = raft_errc::not_leader,
                .message = "configuration change requires leader role",
            });
        }

        log_entry entry;
        try
        {
            entry = log_.append_configuration(state_.current_term, next.state());
        }
        catch (const std::exception& e)
        {
            stop({.code = raft_errc::storage_error, .message = e.what()});
            return std::unexpected(fatal_error_);
        }
        catch (...)
        {
            stop({.code = raft_errc::storage_error, .message = "configuration append storage failure"});
            return std::unexpected(fatal_error_);
        }
        pending_configuration_index_ = entry.index;
        conf_ = std::move(next);
        progress_.reset(conf_.all_members(), log_.last_index() + 1,
                        cfg_.options.max_inflight_append);
        if (auto* self = progress_.get(cfg_.id))
        {
            self->match_index = entry.index;
            self->next_index = entry.index + 1;
        }
        if (conf_.quorum_size() == 1)
        {
            state_.commit_index = entry.index;
            persist();
            apply_committed();
        }
        return entry;
    }
} // namespace cnetmod::raft
