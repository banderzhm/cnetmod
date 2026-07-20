module cnetmod.protocol.raft;

import std;
import :log_manager;

namespace cnetmod::raft
{
    log_manager::log_manager(std::shared_ptr<raft_storage> storage)
        : storage_(std::move(storage))
    {
        if (!storage_) throw std::invalid_argument("log_manager requires storage");
    }

    auto log_manager::storage() const noexcept -> const raft_storage&
    {
        return *storage_;
    }

    auto log_manager::storage() noexcept -> raft_storage&
    {
        return *storage_;
    }

    auto log_manager::first_index() const -> log_index
    {
        return storage_->first_log_index();
    }

    auto log_manager::last_index() const -> log_index
    {
        return storage_->last_log_index();
    }

    auto log_manager::last_term() const -> term_t
    {
        return storage_->term_at(storage_->last_log_index());
    }

    auto log_manager::last_id() const -> log_id
    {
        return last_log_id(*storage_);
    }

    auto log_manager::term_at(log_index index) const -> term_t
    {
        return storage_->term_at(index);
    }

    auto log_manager::entry_at(log_index index) const -> std::optional<log_entry>
    {
        return storage_->entry_at(index);
    }

    auto log_manager::entries_from(log_index index, std::size_t max_entries) const
        -> std::vector<log_entry>
    {
        return storage_->entries(index, max_entries);
    }

    auto log_manager::match(log_index index, term_t term) const -> bool
    {
        return storage_->term_at(index) == term;
    }

    auto log_manager::append_as_leader(term_t term, std::string command) -> log_entry
    {
        log_entry entry{
            .index = storage_->last_log_index() + 1,
            .term = term,
            .type = entry_type::command,
            .command = std::move(command),
        };
        append_one(entry);
        return entry;
    }

    auto log_manager::append_noop(term_t term) -> log_entry
    {
        log_entry entry{
            .index = storage_->last_log_index() + 1,
            .term = term,
            .type = entry_type::no_op,
        };
        append_one(entry);
        return entry;
    }

    auto log_manager::append_configuration(term_t term, configuration_state conf) -> log_entry
    {
        log_entry entry{
            .index = storage_->last_log_index() + 1,
            .term = term,
            .type = entry_type::configuration,
            .configuration = std::move(conf),
        };
        append_one(entry);
        return entry;
    }

    auto log_manager::append_from_leader(log_index prev_index,
                                         term_t prev_term,
                                         const std::vector<log_entry>& entries) -> append_result
    {
        if (!match(prev_index, prev_term))
            return conflict_result(prev_index, prev_term);

        log_index match_index = prev_index;
        const auto snapshot_index = storage_->load_snapshot_metadata().last_included_index;
        std::vector<log_entry> to_append;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];
            if (entry.index <= snapshot_index)
            {
                match_index = std::max(match_index, entry.index);
                continue;
            }

            const auto local_term = storage_->term_at(entry.index);
            if (local_term != 0 && local_term != entry.term)
            {
                storage_->truncate_suffix(entry.index);
                to_append.assign(entries.begin() + static_cast<std::ptrdiff_t>(i), entries.end());
                break;
            }

            if (local_term == 0)
            {
                to_append.assign(entries.begin() + static_cast<std::ptrdiff_t>(i), entries.end());
                break;
            }

            match_index = std::max(match_index, entry.index);
        }
        if (!to_append.empty())
        {
            std::erase_if(to_append, [snapshot_index](const log_entry& entry)
            {
                return entry.index <= snapshot_index;
            });
            if (!to_append.empty())
            {
                match_index = std::max(match_index, to_append.back().index);
                storage_->append(to_append);
            }
        }

        return {
            .success = true,
            .match_index = match_index,
            .error = {},
        };
    }

    auto log_manager::restore_snapshot(const snapshot_metadata& metadata) -> append_result
    {
        if (!metadata.valid())
        {
            return {
                .success = false,
                .error = {
                    .code = raft_errc::configuration_error,
                    .message = "snapshot metadata is empty",
                },
            };
        }
        storage_->reset_to_snapshot(metadata);
        return {
            .success = true,
            .match_index = metadata.last_included_index,
        };
    }

    void log_manager::compact_prefix(log_index first_kept_index)
    {
        storage_->truncate_prefix(first_kept_index);
    }

    auto log_manager::conflict_result(log_index prev_index, term_t prev_term) const
        -> append_result
    {
        append_result result{
            .success = false,
            .match_index = prev_index > 0 ? prev_index - 1 : 0,
            .conflict_index = storage_->last_log_index() + 1,
            .conflict_term = 0,
            .error = {
                .code = raft_errc::log_inconsistent,
                .message = "previous log term mismatch",
            },
        };

        const auto local_term = storage_->term_at(prev_index);
        if (local_term == 0)
            return result;

        result.conflict_term = local_term;
        auto index = prev_index;
        while (index > storage_->first_log_index() && storage_->term_at(index - 1) == local_term)
            --index;
        result.conflict_index = index;
        (void)prev_term;
        return result;
    }

    void log_manager::append_one(log_entry entry)
    {
        storage_->append_one(std::move(entry));
    }
} // namespace cnetmod::raft
