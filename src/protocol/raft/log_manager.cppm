export module cnetmod.protocol.raft:log_manager;

import std;
import :types;
import :storage;

namespace cnetmod::raft
{
    export struct append_result
    {
        bool success = false;
        log_index match_index = 0;
        log_index conflict_index = 0;
        term_t conflict_term = 0;
        raft_error error;
    };

    export class log_manager
    {
    public:
        explicit log_manager(std::shared_ptr<raft_storage> storage);

        [[nodiscard]] auto storage() const noexcept -> const raft_storage&;
        [[nodiscard]] auto storage() noexcept -> raft_storage&;
        [[nodiscard]] auto first_index() const -> log_index;
        [[nodiscard]] auto last_index() const -> log_index;
        [[nodiscard]] auto last_term() const -> term_t;
        [[nodiscard]] auto last_id() const -> log_id;
        [[nodiscard]] auto term_at(log_index index) const -> term_t;
        [[nodiscard]] auto entry_at(log_index index) const -> std::optional<log_entry>;
        [[nodiscard]] auto entries_from(log_index index, std::size_t max_entries) const
            -> std::vector<log_entry>;
        [[nodiscard]] auto match(log_index index, term_t term) const -> bool;

        auto append_as_leader(term_t term, std::string command) -> log_entry;
        auto append_noop(term_t term) -> log_entry;
        auto append_configuration(term_t term, configuration_state conf) -> log_entry;
        auto append_from_leader(log_index prev_index,
                                term_t prev_term,
                                const std::vector<log_entry>& entries) -> append_result;
        auto restore_snapshot(const snapshot_metadata& metadata) -> append_result;
        void compact_prefix(log_index first_kept_index);

    private:
        auto conflict_result(log_index prev_index, term_t prev_term) const -> append_result;
        void append_one(log_entry entry);

        std::shared_ptr<raft_storage> storage_;
    };
} // namespace cnetmod::raft