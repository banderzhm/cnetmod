export module cnetmod.protocol.raft:fsm;

import std;
import :types;
import :storage;

namespace cnetmod::raft
{
    export struct snapshot_writer
    {
        snapshot_metadata metadata;
        std::string uri;
    };

    export struct snapshot_reader
    {
        snapshot_metadata metadata;
        std::string uri;
    };

    export class state_machine
    {
    public:
        virtual ~state_machine() = default;

        virtual void on_apply(const log_entry& entry) = 0;

        virtual void on_snapshot_save(const snapshot_metadata&)
        {
        }

        virtual void on_snapshot_load(const snapshot_metadata&)
        {
        }

        virtual auto save_snapshot(const snapshot_writer& writer) -> std::expected<void, raft_error>
        {
            on_snapshot_save(writer.metadata);
            return {};
        }

        virtual auto load_snapshot(const snapshot_reader& reader) -> std::expected<void, raft_error>
        {
            on_snapshot_load(reader.metadata);
            return {};
        }

        virtual void on_leader_start(term_t)
        {
        }

        virtual void on_leader_stop(term_t)
        {
        }
    };

    export class fsm_caller
    {
    public:
        fsm_caller() = default;

        explicit fsm_caller(state_machine* machine)
            : machine_(machine)
        {
        }

        void reset(state_machine* machine)
        {
            machine_ = machine;
        }

        [[nodiscard]] auto last_applied() const noexcept -> log_index
        {
            return last_applied_;
        }

        void set_last_applied(log_index index) noexcept
        {
            last_applied_ = index;
        }

        auto apply_committed(raft_storage& storage, log_index committed)
            -> std::expected<log_index, raft_error>
        {
            if (!machine_)
            {
                last_applied_ = std::max(last_applied_, committed);
                return last_applied_;
            }

            for (auto index = last_applied_ + 1; index <= committed; ++index)
            {
                auto entry = storage.entry_at(index);
                if (!entry)
                {
                    return std::unexpected(raft_error{
                        .code = raft_errc::log_inconsistent,
                        .message = "committed log entry is missing",
                    });
                }
                if (entry->type == entry_type::command ||
                    entry->type == entry_type::configuration)
                {
                    machine_->on_apply(*entry);
                }
                last_applied_ = index;
            }
            return last_applied_;
        }

        auto apply_entry(const log_entry& entry) -> std::expected<log_index, raft_error>
        {
            if (entry.index != last_applied_ + 1)
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::log_inconsistent,
                    .message = "committed log entry is not contiguous",
                });
            }

            if (machine_ &&
                (entry.type == entry_type::command ||
                    entry.type == entry_type::configuration))
            {
                try
                {
                    machine_->on_apply(entry);
                }
                catch (const std::exception& e)
                {
                    return std::unexpected(raft_error{
                        .code = raft_errc::state_machine_error,
                        .message = e.what(),
                    });
                }
                catch (...)
                {
                    return std::unexpected(raft_error{
                        .code = raft_errc::state_machine_error,
                        .message = "state machine apply failed",
                    });
                }
            }

            last_applied_ = entry.index;
            return last_applied_;
        }

        void on_snapshot_load(const snapshot_metadata& metadata)
        {
            last_applied_ = std::max(last_applied_, metadata.last_included_index);
            if (machine_) machine_->on_snapshot_load(metadata);
        }

        auto save_snapshot(const snapshot_writer& writer) -> std::expected<void, raft_error>
        {
            if (!machine_) return {};
            try
            {
                return machine_->save_snapshot(writer);
            }
            catch (const std::exception& e)
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::state_machine_error,
                    .message = e.what(),
                });
            }
            catch (...)
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::state_machine_error,
                    .message = "state machine snapshot save failed",
                });
            }
        }

        auto load_snapshot(const snapshot_reader& reader) -> std::expected<void, raft_error>
        {
            last_applied_ = std::max(last_applied_, reader.metadata.last_included_index);
            if (!machine_) return {};
            try
            {
                return machine_->load_snapshot(reader);
            }
            catch (const std::exception& e)
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::state_machine_error,
                    .message = e.what(),
                });
            }
            catch (...)
            {
                return std::unexpected(raft_error{
                    .code = raft_errc::state_machine_error,
                    .message = "state machine snapshot load failed",
                });
            }
        }

        void on_leader_start(term_t term)
        {
            if (machine_) machine_->on_leader_start(term);
        }

        void on_leader_stop(term_t term)
        {
            if (machine_) machine_->on_leader_stop(term);
        }

    private:
        state_machine* machine_ = nullptr;
        log_index last_applied_ = 0;
    };
} // namespace cnetmod::raft
