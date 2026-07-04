export module cnetmod.protocol.raft:node;

import std;
import :types;
import :configuration;
import :storage;
import :log_manager;
import :progress;
import :fsm;

namespace cnetmod::raft
{
    export class raft_node
    {
    public:
        raft_node(raft_config cfg, std::shared_ptr<raft_storage> store);
        raft_node(raft_config cfg, std::shared_ptr<raft_storage> store, state_machine* machine);

        [[nodiscard]] auto id() const noexcept -> std::string_view;
        [[nodiscard]] auto role() const noexcept -> node_role;
        [[nodiscard]] auto current_term() const noexcept -> term_t;
        [[nodiscard]] auto commit_index() const noexcept -> log_index;
        [[nodiscard]] auto last_applied() const noexcept -> log_index;
        [[nodiscard]] auto last_log_index() const -> log_index;
        [[nodiscard]] auto voted_for() const noexcept -> std::string_view;
        [[nodiscard]] auto leader_id() const noexcept -> std::string_view;
        [[nodiscard]] auto current_configuration() const -> configuration_state;
        [[nodiscard]] auto metrics() const -> raft_metrics;
        [[nodiscard]] auto stopped() const noexcept -> bool;
        [[nodiscard]] auto fatal_error() const noexcept -> const raft_error&;
        [[nodiscard]] auto election_timeout_elapsed(std::chrono::steady_clock::duration timeout) const
            -> bool;
        [[nodiscard]] auto leader_lease_valid(std::chrono::steady_clock::duration timeout) const
            -> bool;

        [[nodiscard]] auto make_append_entries(const node_id& peer) const
            -> append_entries_request;
        [[nodiscard]] auto should_send_snapshot(const node_id& peer) const -> bool;
        [[nodiscard]] auto make_install_snapshot(const node_id& peer) const
            -> install_snapshot_request;
        void mark_snapshot_sent(const node_id& peer);
        void mark_append_sent(const node_id& peer, log_index last_index);

        auto begin_pre_vote() -> request_vote_request;
        auto begin_election() -> request_vote_request;
        auto handle_request_vote(const request_vote_request& req) -> request_vote_response;
        auto handle_vote_response(const node_id& voter,
                                  const request_vote_response& resp) -> bool;
        auto handle_timeout_now(const timeout_now_request& req)
            -> std::optional<request_vote_request>;
        auto handle_append_entries(const append_entries_request& req)
            -> append_entries_response;
        auto handle_append_entries_response(const node_id& peer,
                                            const append_entries_response& resp) -> bool;
        auto handle_install_snapshot(const install_snapshot_request& req)
            -> install_snapshot_response;
        auto handle_install_snapshot_response(const node_id& peer,
                                              const install_snapshot_response& resp) -> bool;

        auto append_command(std::string command) -> std::expected<log_entry, raft_error>;
        auto enter_joint_configuration(std::vector<node_id> new_voters)
            -> std::expected<log_entry, raft_error>;
        auto leave_joint_configuration() -> std::expected<log_entry, raft_error>;
        auto set_learners(std::vector<node_id> learners) -> std::expected<log_entry, raft_error>;
        auto promote_learner(const node_id& id) -> std::expected<log_entry, raft_error>;
        auto remove_node(const node_id& id) -> std::expected<log_entry, raft_error>;
        auto transfer_leader(const node_id& target)
            -> std::expected<std::optional<timeout_now_request>, raft_error>;
        auto take_pending_leader_transfer(const node_id& peer)
            -> std::optional<timeout_now_request>;
        auto create_snapshot(std::string uri) -> std::expected<snapshot_metadata, raft_error>;
        auto maybe_create_snapshot(const raft_snapshot_policy& policy)
            -> std::expected<std::optional<snapshot_metadata>, raft_error>;
        auto read_index(read_index_request request)
            -> std::expected<read_index_response, raft_error>;
        auto query_read_index(request_id id) const -> std::optional<read_index_response>;
        auto consume_read_index(request_id id) -> std::optional<read_index_response>;
        auto expire_read_indexes(std::chrono::steady_clock::duration timeout) -> std::size_t;
        [[nodiscard]] auto pending_read_count() const noexcept -> std::size_t;
        auto check_leader_quorum(std::chrono::steady_clock::duration timeout) -> bool;
        void stop(raft_error error);

    private:
        struct pending_read
        {
            request_id id = 0;
            term_t term = 0;
            log_index index = 0;
            std::string context;
            std::set<node_id> acked;
            std::chrono::steady_clock::time_point created_at;
            bool ready = false;
        };

        void persist();
        [[nodiscard]] auto stopped_error() const -> std::expected<void, raft_error>;
        void reset_election_deadline();
        void refresh_leader_quorum();
        void step_down(term_t new_term);
        void become_leader();
        void advance_commit_index();
        void apply_committed();
        void acknowledge_read(const node_id& peer, request_id id);
        [[nodiscard]] auto make_timeout_now(const node_id& target) const
            -> std::optional<timeout_now_request>;
        auto append_configuration(configuration next) -> std::expected<log_entry, raft_error>;

        raft_config cfg_;
        std::shared_ptr<raft_storage> store_;
        log_manager log_;
        hard_state state_;
        node_role role_ = node_role::follower;
        node_id leader_id_;
        configuration conf_;
        std::set<node_id> votes_granted_;
        std::set<node_id> votes_rejected_;
        progress_tracker progress_;
        fsm_caller fsm_;
        std::chrono::steady_clock::time_point last_leader_contact_;
        std::chrono::steady_clock::time_point last_quorum_contact_;
        std::map<request_id, pending_read> pending_reads_;
        bool stopped_ = false;
        raft_error fatal_error_;
        std::optional<log_index> pending_configuration_index_;
        std::optional<node_id> pending_leader_transfer_;
        std::chrono::steady_clock::time_point last_auto_snapshot_;
    };
} // namespace cnetmod::raft