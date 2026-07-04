export module cnetmod.protocol.raft:storage;

import std;
import :types;

namespace cnetmod::raft
{
    export class raft_storage
    {
    public:
        virtual ~raft_storage() = default;

        virtual auto load_hard_state() -> hard_state = 0;
        virtual void save_hard_state(const hard_state& state) = 0;

        virtual auto load_snapshot_metadata() -> snapshot_metadata = 0;
        virtual void save_snapshot_metadata(const snapshot_metadata& metadata) = 0;

        virtual auto first_log_index() const -> log_index = 0;
        virtual auto last_log_index() const -> log_index = 0;
        virtual auto term_at(log_index index) const -> term_t = 0;
        virtual auto entry_at(log_index index) const -> std::optional<log_entry> = 0;
        virtual auto entries(log_index first_index, std::size_t max_entries) const
            -> std::vector<log_entry> = 0;

        virtual void append(const std::vector<log_entry>& entries) = 0;
        virtual void append_one(log_entry entry)
        {
            append(std::vector<log_entry>{std::move(entry)});
        }
        virtual void truncate_prefix(log_index first_kept_index) = 0;
        virtual void truncate_suffix(log_index first_removed_index) = 0;
        virtual void reset_to_snapshot(const snapshot_metadata& metadata) = 0;
    };

    export inline auto entries_from(const raft_storage& storage, log_index first_index)
        -> std::vector<log_entry>
    {
        return storage.entries(first_index, std::numeric_limits<std::size_t>::max());
    }

    export inline auto last_log_term(const raft_storage& storage) -> term_t
    {
        return storage.term_at(storage.last_log_index());
    }

    export inline auto last_log_id(const raft_storage& storage) -> log_id
    {
        const auto last = storage.last_log_index();
        return {.index = last, .term = storage.term_at(last)};
    }

    export inline auto is_log_at_least_up_to_date(const raft_storage& storage,
                                                  log_index candidate_last_index,
                                                  term_t candidate_last_term) -> bool
    {
        const auto local_last_term = last_log_term(storage);
        if (candidate_last_term != local_last_term)
            return candidate_last_term > local_last_term;
        return candidate_last_index >= storage.last_log_index();
    }
} // namespace cnetmod::raft
