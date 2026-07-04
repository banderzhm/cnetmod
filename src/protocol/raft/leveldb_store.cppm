module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_LEVELDB
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#endif

export module cnetmod.protocol.raft:leveldb_store;

import std;
import :types;
import :storage;

namespace cnetmod::raft
{
    export constexpr bool leveldb_store_available =
#ifdef CNETMOD_HAS_LEVELDB
        true;
#else
    false;
#endif

    namespace detail
    {
        inline constexpr std::string_view hard_state_key = "meta:hard_state";
        inline constexpr std::string_view snapshot_key = "meta:snapshot";

        inline auto log_key(log_index index) -> std::string
        {
            return std::format("log:{:020}", index);
        }

        inline auto parse_log_index(std::string_view key) -> log_index
        {
            if (!key.starts_with("log:")) return 0;
            return static_cast<log_index>(std::stoull(std::string(key.substr(4))));
        }

        inline void put_u64(std::string& out, std::uint64_t value)
        {
            for (auto i = 0; i < 8; ++i)
                out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
        }

        inline auto get_u64(std::string_view in, std::size_t& pos) -> std::uint64_t
        {
            if (pos + 8 > in.size())
                throw std::runtime_error("corrupt raft leveldb record");
            std::uint64_t value = 0;
            for (auto i = 0; i < 8; ++i)
                value |= static_cast<std::uint64_t>(static_cast<unsigned char>(in[pos++])) << (i * 8);
            return value;
        }

        inline void put_string(std::string& out, std::string_view value)
        {
            put_u64(out, value.size());
            out.append(value);
        }

        inline auto get_string(std::string_view in, std::size_t& pos) -> std::string
        {
            const auto size = static_cast<std::size_t>(get_u64(in, pos));
            if (pos + size > in.size())
                throw std::runtime_error("corrupt raft leveldb string");
            std::string out{in.substr(pos, size)};
            pos += size;
            return out;
        }

        inline void put_strings(std::string& out, const std::vector<node_id>& values)
        {
            put_u64(out, values.size());
            for (const auto& value : values)
                put_string(out, value);
        }

        inline auto get_strings(std::string_view in, std::size_t& pos) -> std::vector<node_id>
        {
            const auto size = static_cast<std::size_t>(get_u64(in, pos));
            std::vector<node_id> out;
            out.reserve(size);
            for (std::size_t i = 0; i < size; ++i)
                out.push_back(get_string(in, pos));
            return out;
        }

        inline void put_config(std::string& out, const configuration_state& conf)
        {
            put_strings(out, conf.voters);
            put_strings(out, conf.old_voters);
            put_strings(out, conf.learners);
        }

        inline auto get_config(std::string_view in, std::size_t& pos) -> configuration_state
        {
            configuration_state out{
                .voters = get_strings(in, pos),
                .old_voters = get_strings(in, pos),
            };
            if (pos < in.size())
                out.learners = get_strings(in, pos);
            return out;
        }

        inline auto serialize_state(const hard_state& state) -> std::string
        {
            std::string out;
            put_u64(out, state.current_term);
            put_string(out, state.voted_for);
            put_u64(out, state.commit_index);
            put_u64(out, state.last_applied);
            return out;
        }

        inline auto deserialize_state(std::string_view data) -> hard_state
        {
            if (data.empty()) return {};
            std::size_t pos = 0;
            return {
                .current_term = get_u64(data, pos),
                .voted_for = get_string(data, pos),
                .commit_index = get_u64(data, pos),
                .last_applied = get_u64(data, pos),
            };
        }

        inline auto serialize_snapshot(const snapshot_metadata& metadata) -> std::string
        {
            std::string out;
            put_u64(out, metadata.last_included_index);
            put_u64(out, metadata.last_included_term);
            put_string(out, metadata.uri);
            put_config(out, metadata.configuration);
            return out;
        }

        inline auto deserialize_snapshot(std::string_view data) -> snapshot_metadata
        {
            if (data.empty()) return {};
            std::size_t pos = 0;
            return {
                .last_included_index = get_u64(data, pos),
                .last_included_term = get_u64(data, pos),
                .uri = get_string(data, pos),
                .configuration = get_config(data, pos),
            };
        }

        inline auto serialize_entry(const log_entry& entry) -> std::string
        {
            std::string out;
            put_u64(out, entry.term);
            put_u64(out, static_cast<std::uint64_t>(entry.type));
            put_string(out, entry.command);
            put_config(out, entry.configuration);
            return out;
        }

        inline auto deserialize_entry(log_index index, std::string_view data) -> log_entry
        {
            std::size_t pos = 0;
            log_entry entry;
            entry.index = index;
            entry.term = get_u64(data, pos);
            entry.type = static_cast<entry_type>(get_u64(data, pos));
            entry.command = get_string(data, pos);
            entry.configuration = get_config(data, pos);
            return entry;
        }
    } // namespace detail

#ifdef CNETMOD_HAS_LEVELDB

    export class leveldb_store final : public raft_storage
    {
    public:
        explicit leveldb_store(std::string path)
        {
            leveldb::Options options;
            options.create_if_missing = true;
            leveldb::DB* raw = nullptr;
            auto status = leveldb::DB::Open(options, path, &raw);
            if (!status.ok())
                throw std::runtime_error(status.ToString());
            db_.reset(raw);
        }

        auto load_hard_state() -> hard_state override
        {
            std::string data;
            auto status = db_->Get(leveldb::ReadOptions{}, std::string(detail::hard_state_key), &data);
            if (status.IsNotFound()) return {};
            check(status);
            return detail::deserialize_state(data);
        }

        void save_hard_state(const hard_state& state) override
        {
            leveldb::WriteOptions options;
            options.sync = sync_;
            check(db_->Put(options, std::string(detail::hard_state_key), detail::serialize_state(state)));
        }

        auto load_snapshot_metadata() -> snapshot_metadata override
        {
            std::string data;
            auto status = db_->Get(leveldb::ReadOptions{}, std::string(detail::snapshot_key), &data);
            if (status.IsNotFound()) return {};
            check(status);
            return detail::deserialize_snapshot(data);
        }

        void save_snapshot_metadata(const snapshot_metadata& metadata) override
        {
            leveldb::WriteOptions options;
            options.sync = sync_;
            check(db_->Put(options, std::string(detail::snapshot_key), detail::serialize_snapshot(metadata)));
        }

        auto first_log_index() const -> log_index override
        {
            std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions{}));
            it->Seek("log:");
            check(it->status());
            if (!it->Valid() || !it->key().ToString().starts_with("log:"))
                return load_snapshot_metadata_const().last_included_index + 1;
            return detail::parse_log_index(it->key().ToString());
        }

        auto last_log_index() const -> log_index override
        {
            std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions{}));
            log_index last = load_snapshot_metadata_const().last_included_index;
            for (it->Seek("log:"); it->Valid(); it->Next())
            {
                auto key = it->key().ToString();
                if (!key.starts_with("log:")) break;
                last = detail::parse_log_index(key);
            }
            check(it->status());
            return last;
        }

        auto term_at(log_index index) const -> term_t override
        {
            if (index == 0) return 0;
            const auto snapshot = load_snapshot_metadata_const();
            if (index == snapshot.last_included_index) return snapshot.last_included_term;
            auto entry = entry_at(index);
            return entry ? entry->term : 0;
        }

        auto entry_at(log_index index) const -> std::optional<log_entry> override
        {
            std::string data;
            auto status = db_->Get(leveldb::ReadOptions{}, detail::log_key(index), &data);
            if (status.IsNotFound()) return std::nullopt;
            check(status);
            return detail::deserialize_entry(index, data);
        }

        auto entries(log_index first_index, std::size_t max_entries) const
            -> std::vector<log_entry> override
        {
            std::vector<log_entry> entries;
            if (max_entries == 0) return entries;
            std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions{}));
            for (it->Seek(detail::log_key(first_index)); it->Valid(); it->Next())
            {
                auto key = it->key().ToString();
                if (!key.starts_with("log:")) break;
                entries.push_back(detail::deserialize_entry(detail::parse_log_index(key),
                                                            it->value().ToString()));
                if (entries.size() >= max_entries) break;
            }
            check(it->status());
            return entries;
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
                if (entry.index == 0) continue;
                to_append.push_back(entry);
            }
            if (to_append.empty()) return;

            std::ranges::sort(to_append, {}, &log_entry::index);
            leveldb::WriteBatch batch;
            delete_suffix(batch, to_append.front().index);
            for (const auto& entry : to_append)
                batch.Put(detail::log_key(entry.index), detail::serialize_entry(entry));
            leveldb::WriteOptions options;
            options.sync = sync_;
            check(db_->Write(options, &batch));
        }

        void append_one(log_entry entry) override
        {
            if (entry.index == 0) return;
            leveldb::WriteBatch batch;
            delete_suffix(batch, entry.index);
            batch.Put(detail::log_key(entry.index), detail::serialize_entry(entry));
            leveldb::WriteOptions options;
            options.sync = sync_;
            check(db_->Write(options, &batch));
        }

        void truncate_prefix(log_index first_kept_index) override
        {
            leveldb::WriteBatch batch;
            std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions{}));
            for (it->Seek("log:"); it->Valid(); it->Next())
            {
                auto key = it->key().ToString();
                if (!key.starts_with("log:")) break;
                if (detail::parse_log_index(key) >= first_kept_index) break;
                batch.Delete(key);
            }
            check(it->status());
            leveldb::WriteOptions options;
            options.sync = sync_;
            check(db_->Write(options, &batch));
        }

        void truncate_suffix(log_index first_removed_index) override
        {
            leveldb::WriteBatch batch;
            delete_suffix(batch, first_removed_index);
            leveldb::WriteOptions options;
            options.sync = sync_;
            check(db_->Write(options, &batch));
        }

        void reset_to_snapshot(const snapshot_metadata& metadata) override
        {
            leveldb::WriteBatch batch;
            std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions{}));
            for (it->Seek("log:"); it->Valid(); it->Next())
            {
                auto key = it->key().ToString();
                if (!key.starts_with("log:")) break;
                batch.Delete(key);
            }
            check(it->status());

            auto state = load_hard_state();
            state.commit_index = std::max(state.commit_index, metadata.last_included_index);
            state.last_applied = std::max(state.last_applied, metadata.last_included_index);
            batch.Put(std::string(detail::snapshot_key), detail::serialize_snapshot(metadata));
            batch.Put(std::string(detail::hard_state_key), detail::serialize_state(state));

            leveldb::WriteOptions options;
            options.sync = sync_;
            check(db_->Write(options, &batch));
        }

        void set_sync(bool enabled) noexcept
        {
            sync_ = enabled;
        }

    private:
        void delete_suffix(leveldb::WriteBatch& batch, log_index first_removed_index) const
        {
            std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions{}));
            for (it->Seek(detail::log_key(first_removed_index)); it->Valid(); it->Next())
            {
                auto key = it->key().ToString();
                if (!key.starts_with("log:")) break;
                batch.Delete(key);
            }
            check(it->status());
        }

        auto load_snapshot_metadata_const() const -> snapshot_metadata
        {
            std::string data;
            auto status = db_->Get(leveldb::ReadOptions{}, std::string(detail::snapshot_key), &data);
            if (status.IsNotFound()) return {};
            check(status);
            return detail::deserialize_snapshot(data);
        }

        static void check(const leveldb::Status& status)
        {
            if (!status.ok())
                throw std::runtime_error(status.ToString());
        }

        std::unique_ptr<leveldb::DB> db_;
        bool sync_ = true;
    };

#endif
} // namespace cnetmod::raft
