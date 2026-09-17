/// cnetmod.protocol.openai:checkpoint — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :foundation;
import :checkpoint;

namespace cnetmod::openai {

namespace {
    auto valid_identity(std::string_view thread_id,
        std::string_view branch) -> bool
    {
        return !thread_id.empty() && !branch.empty();
    }
} // namespace

in_memory_checkpoint_store::in_memory_checkpoint_store(clock now)
    : now_(std::move(now))
{
    if (!now_)
    {
        now_ = []
        {
            return std::chrono::system_clock::now();
        };
    }
}

auto in_memory_checkpoint_store::load_latest(std::string thread_id,
    std::string branch)
    -> task<std::expected<std::optional<checkpoint_record>, std::string>>
{
    if (!valid_identity(thread_id, branch))
        co_return std::unexpected("checkpoint thread and branch cannot be empty");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto found = histories_.find({thread_id, branch});
    if (found == histories_.end() || found->second.empty())
        co_return std::optional<checkpoint_record>{};
    co_return std::optional<checkpoint_record>{found->second.back()};
}

auto in_memory_checkpoint_store::load(std::string thread_id,
    std::string branch, std::uint64_t version)
    -> task<std::expected<std::optional<checkpoint_record>, std::string>>
{
    if (!valid_identity(thread_id, branch) || version == 0)
        co_return std::unexpected(
            "checkpoint thread, branch, and version must be valid");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto found = histories_.find({thread_id, branch});
    if (found == histories_.end() || version > found->second.size())
        co_return std::optional<checkpoint_record>{};
    co_return std::optional<checkpoint_record>{
        found->second[static_cast<std::size_t>(version - 1)]};
}

auto in_memory_checkpoint_store::list(std::string thread_id,
    std::string branch, std::size_t limit,
    std::optional<std::uint64_t> before_version)
    -> task<std::expected<std::vector<checkpoint_record>, std::string>>
{
    if (!valid_identity(thread_id, branch))
        co_return std::unexpected("checkpoint thread and branch cannot be empty");
    if (limit == 0)
        co_return std::vector<checkpoint_record>{};
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto found = histories_.find({thread_id, branch});
    if (found == histories_.end())
        co_return std::vector<checkpoint_record>{};
    std::vector<checkpoint_record> result;
    result.reserve(std::min(limit, found->second.size()));
    for (auto current = found->second.rbegin();
        current != found->second.rend() && result.size() < limit; ++current)
    {
        if (!before_version || current->version < *before_version)
            result.push_back(*current);
    }
    co_return result;
}

auto in_memory_checkpoint_store::commit(checkpoint_commit request)
    -> task<std::expected<checkpoint_record, std::string>>
{
    if (!valid_identity(request.thread_id, request.branch) ||
        !request.state.is_object() || !request.metadata.is_object())
        co_return std::unexpected(
            "checkpoint commit requires valid identity and object state");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    auto& history = histories_[{request.thread_id, request.branch}];
    const auto head = history.empty() ? std::uint64_t{0} : history.back().version;
    if (request.expected_head_version &&
        *request.expected_head_version != head)
        co_return std::unexpected(std::format(
            "checkpoint version conflict: expected {}, current {}",
            *request.expected_head_version, head));
    checkpoint_record record{.thread_id = std::move(request.thread_id),
        .branch = std::move(request.branch),
        .version = head + 1,
        .parent_version = head == 0
            ? std::nullopt
            : std::optional<std::uint64_t>{head},
        .state = std::move(request.state),
        .metadata = std::move(request.metadata),
        .created_at = now_()};
    history.push_back(record);
    co_return record;
}

auto in_memory_checkpoint_store::put_pending_writes(
    std::string thread_id, std::string branch, std::uint64_t version,
    std::vector<checkpoint_pending_write> writes,
    std::optional<std::uint64_t> expected_write_revision)
    -> task<std::expected<checkpoint_record, std::string>>
{
    if (!valid_identity(thread_id, branch) || version == 0 ||
        std::ranges::any_of(writes, [](const auto& write)
            {
                return write.id.empty() || write.channel.empty();
            }))
        co_return std::unexpected("pending checkpoint writes are invalid");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto found = histories_.find({thread_id, branch});
    if (found == histories_.end() || version > found->second.size())
        co_return std::unexpected("checkpoint version was not found");
    auto& record = found->second[static_cast<std::size_t>(version - 1)];
    if (expected_write_revision &&
        *expected_write_revision != record.write_revision)
        co_return std::unexpected(std::format(
            "checkpoint write revision conflict: expected {}, current {}",
            *expected_write_revision, record.write_revision));
    for (auto& write : writes)
    {
        const auto duplicate = std::ranges::find(record.pending_writes,
            write.id, &checkpoint_pending_write::id);
        if (duplicate == record.pending_writes.end())
            record.pending_writes.push_back(std::move(write));
    }
    ++record.write_revision;
    co_return record;
}

auto in_memory_checkpoint_store::fork(std::string thread_id,
    std::string source_branch, std::uint64_t source_version,
    std::string target_branch)
    -> task<std::expected<checkpoint_record, std::string>>
{
    if (!valid_identity(thread_id, source_branch) || target_branch.empty() ||
        source_version == 0 || source_branch == target_branch)
        co_return std::unexpected("checkpoint fork request is invalid");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto source = histories_.find({thread_id, source_branch});
    if (source == histories_.end() || source_version > source->second.size())
        co_return std::unexpected("checkpoint fork source was not found");
    const auto target_key = branch_key{thread_id, target_branch};
    if (histories_.contains(target_key))
        co_return std::unexpected("checkpoint target branch already exists");
    const auto& source_record =
        source->second[static_cast<std::size_t>(source_version - 1)];
    checkpoint_record record{.thread_id = std::move(thread_id),
        .branch = std::move(target_branch),
        .version = 1,
        .origin = checkpoint_reference{.branch = std::move(source_branch),
            .version = source_version},
        .state = source_record.state,
        .metadata = source_record.metadata,
        .pending_writes = source_record.pending_writes,
        .write_revision = source_record.write_revision,
        .created_at = now_()};
    histories_[target_key].push_back(record);
    co_return record;
}

auto in_memory_checkpoint_store::rollback(std::string thread_id,
    std::string branch, std::uint64_t target_version,
    std::optional<std::uint64_t> expected_head_version)
    -> task<std::expected<checkpoint_record, std::string>>
{
    if (!valid_identity(thread_id, branch) || target_version == 0)
        co_return std::unexpected("checkpoint rollback request is invalid");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    auto found = histories_.find({thread_id, branch});
    if (found == histories_.end() || target_version > found->second.size())
        co_return std::unexpected("checkpoint rollback target was not found");
    auto& history = found->second;
    const auto head = history.back().version;
    if (expected_head_version && *expected_head_version != head)
        co_return std::unexpected(std::format(
            "checkpoint version conflict: expected {}, current {}",
            *expected_head_version, head));
    const auto target = history[static_cast<std::size_t>(target_version - 1)];
    checkpoint_record record{.thread_id = std::move(thread_id),
        .branch = branch,
        .version = head + 1,
        .parent_version = head,
        .origin = checkpoint_reference{.branch = std::move(branch),
            .version = target_version},
        .state = target.state,
        .metadata = target.metadata,
        .created_at = now_()};
    history.push_back(record);
    co_return record;
}

auto in_memory_checkpoint_store::erase_thread(std::string thread_id)
    -> task<std::expected<void, std::string>>
{
    if (thread_id.empty())
        co_return std::unexpected("checkpoint thread cannot be empty");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    std::erase_if(histories_, [&thread_id](const auto& value)
        {
            return value.first.first == thread_id;
        });
    co_return std::expected<void, std::string>{};
}

auto in_memory_checkpoint_store::erase_branch(std::string thread_id,
    std::string branch) -> task<std::expected<void, std::string>>
{
    if (!valid_identity(thread_id, branch))
        co_return std::unexpected("checkpoint thread and branch cannot be empty");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    histories_.erase({thread_id, branch});
    co_return std::expected<void, std::string>{};
}

} // namespace cnetmod::openai
