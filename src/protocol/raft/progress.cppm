export module cnetmod.protocol.raft:progress;

import std;
import :types;

namespace cnetmod::raft
{
    export enum class replication_state
    {
        probe,
        replicate,
        snapshot,
    };

    export struct inflight_window
    {
        std::size_t capacity = 256;
        std::deque<log_index> entries;

        [[nodiscard]] auto full() const noexcept -> bool
        {
            return entries.size() >= capacity;
        }

        void add(log_index index)
        {
            if (capacity == 0) return;
            if (full()) entries.pop_front();
            entries.push_back(index);
        }

        void free_to(log_index index)
        {
            while (!entries.empty() && entries.front() <= index)
                entries.pop_front();
        }

        void reset()
        {
            entries.clear();
        }
    };

    export struct peer_progress
    {
        log_index next_index = 1;
        log_index match_index = 0;
        log_index pending_snapshot = 0;
        replication_state state = replication_state::probe;
        bool recent_active = false;
        inflight_window inflight;
    };

    export class progress_tracker
    {
    public:
        using peer_table = std::vector<std::pair<node_id, peer_progress>>;

        progress_tracker() = default;

        progress_tracker(std::vector<node_id> peers,
                         log_index next_index,
                         std::size_t inflight_capacity)
        {
            reset(std::move(peers), next_index, inflight_capacity);
        }

        void reset(std::vector<node_id> peers,
                   log_index next_index,
                   std::size_t inflight_capacity = 256)
        {
            peers_.clear();
            std::ranges::sort(peers);
            peers.erase(std::ranges::unique(peers).begin(), peers.end());
            peers_.reserve(peers.size());
            for (auto& peer : peers)
            {
                peers_.emplace_back(std::move(peer), peer_progress{
                                        .next_index = next_index,
                                        .inflight = inflight_window{.capacity = inflight_capacity},
                                    });
            }
        }

        auto get(const node_id& peer) -> peer_progress*
        {
            auto it = find_peer(peer);
            return it == peers_.end() ? nullptr : &it->second;
        }

        auto get(const node_id& peer) const -> const peer_progress*
        {
            auto it = find_peer(peer);
            return it == peers_.end() ? nullptr : &it->second;
        }

        [[nodiscard]] auto peers() const noexcept -> const peer_table&
        {
            return peers_;
        }

        void mark_sent(const node_id& peer, log_index last_index)
        {
            auto* progress = get(peer);
            if (!progress) return;
            progress->recent_active = true;
            if (last_index == 0) return;
            if (progress->state == replication_state::replicate)
            {
                progress->next_index = std::max(progress->next_index, last_index + 1);
                progress->inflight.add(last_index);
            }
        }

        void optimistic_update(const node_id& peer, log_index last_index)
        {
            auto* progress = get(peer);
            if (!progress || last_index == 0) return;
            progress->next_index = std::max(progress->next_index, last_index + 1);
        }

        void mark_replicated(const node_id& peer, log_index index)
        {
            auto* progress = get(peer);
            if (!progress) return;
            progress->match_index = std::max(progress->match_index, index);
            progress->next_index = std::max(progress->next_index, progress->match_index + 1);
            progress->pending_snapshot = 0;
            progress->state = replication_state::replicate;
            progress->recent_active = true;
            progress->inflight.free_to(index);
        }

        void mark_rejected(const node_id& peer,
                           log_index rejected_next_index,
                           log_index conflict_index = 0)
        {
            auto* progress = get(peer);
            if (!progress) return;
            progress->state = replication_state::probe;
            progress->inflight.reset();
            progress->recent_active = true;
            if (conflict_index != 0)
            {
                progress->next_index = conflict_index;
            }
            else if (rejected_next_index > 1)
            {
                progress->next_index = rejected_next_index - 1;
            }
            else
            {
                progress->next_index = 1;
            }
        }

        void mark_snapshot(const node_id& peer, log_index snapshot_index)
        {
            auto* progress = get(peer);
            if (!progress) return;
            progress->state = replication_state::snapshot;
            progress->pending_snapshot = snapshot_index;
            progress->inflight.reset();
        }

        [[nodiscard]] auto committed_index(log_index leader_match,
                                           std::size_t majority) const -> log_index
        {
            std::vector<log_index> matches;
            matches.reserve(peers_.size() + 1);
            matches.push_back(leader_match);
            for (const auto& [_, progress] : peers_)
                matches.push_back(progress.match_index);

            std::ranges::sort(matches, std::greater<>{});
            if (majority == 0 || majority > matches.size()) return 0;
            return matches[majority - 1];
        }

        [[nodiscard]] auto active_followers() const -> std::size_t
        {
            return static_cast<std::size_t>(std::ranges::count_if(peers_, [](const auto& item)
            {
                return item.second.recent_active;
            }));
        }

    private:
        auto find_peer(const node_id& peer) -> peer_table::iterator
        {
            auto it = std::ranges::lower_bound(peers_, peer, {}, &peer_table::value_type::first);
            if (it == peers_.end() || it->first != peer)
                return peers_.end();
            return it;
        }

        auto find_peer(const node_id& peer) const -> peer_table::const_iterator
        {
            auto it = std::ranges::lower_bound(peers_, peer, {}, &peer_table::value_type::first);
            if (it == peers_.end() || it->first != peer)
                return peers_.end();
            return it;
        }

        peer_table peers_;
    };
} // namespace cnetmod::raft
