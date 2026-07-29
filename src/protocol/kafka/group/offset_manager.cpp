module cnetmod.protocol.kafka.offset_manager;
import std;

namespace cnetmod::kafka {
offset_manager::offset_manager(std::shared_ptr<offset_backend> b)
    : backend_(std::move(b)) {}

void offset_manager::stage(topic_partition t, offset_and_metadata o)
{
    staged_[std::move(t)] = std::move(o);
}

auto offset_manager::commit(std::string_view g, std::int32_t n,
    std::string_view m, cancel_token* t)
    -> task<result<void>>
{
    auto x = co_await backend_->commit(g, n, m, staged_, t);
    if (x)
        staged_.clear();
    co_return x;
}

auto offset_manager::fetch(std::string_view g,
    std::span<const topic_partition> p, cancel_token* t)
    -> task<result<std::map<topic_partition, offset_and_metadata>>>
{
    co_return co_await backend_->fetch(g, p, t);
}
} // namespace cnetmod::kafka
