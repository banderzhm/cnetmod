export module cnetmod.protocol.raft:memory_store;

import std;
import :types;
import :storage;

namespace cnetmod::raft
{
    export class memory_store final : public raft_storage
    {
    public:
        auto load_hard_state() -> hard_state override
        {
            return state_;
        }

        void save_hard_state(const hard_state& state) override
        {
            state_ = state;
        }

        auto load_snapshot_metadata() -> snapshot_metadata override
        {
            return snapshot_;
        }

        void save_snapshot_metadata(const snapshot_metadata& metadata) override
        {
            snapshot_ = metadata;
        }

        auto first_log_index() const -> log_index override
        {
            if (!entries_.empty())
                return entries_.front().index;
            return snapshot_.last_included_index + 1;
        }

        auto last_log_index() const -> log_index override
        {
            if (!entries_.empty())
                return entries_.back().index;
            return snapshot_.last_included_index;
        }

        auto term_at(log_index index) const -> term_t override
        {
            if (index == 0) return 0;
            if (index == snapshot_.last_included_index) return snapshot_.last_included_term;
            auto pos = find_offset(index);
            return pos ? entries_[*pos].term : 0;
        }

        auto entry_at(log_index index) const -> std::optional<log_entry> override
        {
            auto pos = find_offset(index);
            if (!pos) return std::nullopt;
            return entries_[*pos];
        }

        auto entries(log_index first_index, std::size_t max_entries) const
            -> std::vector<log_entry> override
        {
            std::vector<log_entry> out;
            if (max_entries == 0) return out;

            auto first = lower_bound_offset(first_index);
            if (first >= entries_.size()) return out;

            const auto count = std::min(max_entries, entries_.size() - first);
            out.reserve(count);
            out.insert(out.end(), entries_.begin() + static_cast<std::ptrdiff_t>(first),
                       entries_.begin() + static_cast<std::ptrdiff_t>(first + count));
            return out;
        }

        void append(const std::vector<log_entry>& entries) override
        {
            if (entries.size() == 1)
            {
                append_one(entries.front());
                return;
            }

            std::vector<log_entry> to_append;
            to_append.reserve(entries.size());
            for (const auto& entry : entries)
            {
                if (entry.index <= snapshot_.last_included_index) continue;
                to_append.push_back(entry);
            }
            if (to_append.empty()) return;

            std::ranges::sort(to_append, {}, &log_entry::index);
            const auto first_new = to_append.front().index;
            truncate_suffix(first_new);
            entries_.insert(entries_.end(), to_append.begin(), to_append.end());
        }

        void append_one(log_entry entry) override
        {
            if (entry.index <= snapshot_.last_included_index) return;
            if (!entries_.empty() && entry.index == entries_.back().index + 1)
            {
                entries_.push_back(std::move(entry));
                return;
            }
            if (entries_.empty() && entry.index == snapshot_.last_included_index + 1)
            {
                entries_.push_back(std::move(entry));
                return;
            }
            truncate_suffix(entry.index);
            entries_.push_back(std::move(entry));
        }

        void truncate_prefix(log_index first_kept_index) override
        {
            const auto first_kept = lower_bound_offset(first_kept_index);
            entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(first_kept));
        }

        void truncate_suffix(log_index first_removed_index) override
        {
            if (first_removed_index == 0)
            {
                entries_.clear();
                return;
            }
            const auto first_removed = lower_bound_offset(first_removed_index);
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(first_removed), entries_.end());
        }

        void reset_to_snapshot(const snapshot_metadata& metadata) override
        {
            snapshot_ = metadata;
            entries_.clear();
            if (state_.commit_index < metadata.last_included_index)
                state_.commit_index = metadata.last_included_index;
            if (state_.last_applied < metadata.last_included_index)
                state_.last_applied = metadata.last_included_index;
        }

    private:
        [[nodiscard]] auto lower_bound_offset(log_index index) const -> std::size_t
        {
            auto it = std::ranges::lower_bound(entries_, index, {}, &log_entry::index);
            return static_cast<std::size_t>(std::distance(entries_.begin(), it));
        }

        [[nodiscard]] auto find_offset(log_index index) const -> std::optional<std::size_t>
        {
            auto pos = lower_bound_offset(index);
            if (pos >= entries_.size() || entries_[pos].index != index)
                return std::nullopt;
            return pos;
        }

        hard_state state_;
        snapshot_metadata snapshot_;
        std::vector<log_entry> entries_;
    };
} // namespace cnetmod::raft
