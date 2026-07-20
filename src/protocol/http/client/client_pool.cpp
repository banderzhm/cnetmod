module cnetmod.protocol.http.client.pool;

import std;

namespace cnetmod::http {

client_pool::client_pool(cnetmod::io_context& context, client_options options,
                         std::size_t max_idle)
    : context_(&context), options_(std::move(options)), max_idle_(max_idle) {}

auto client_pool::acquire() -> std::unique_ptr<client> {
    if (idle_.empty()) return std::make_unique<client>(*context_, options_);
    auto value = std::move(idle_.back());
    idle_.pop_back();
    return value;
}

void client_pool::release(std::unique_ptr<client> value) {
    if (!value) return;
    if (idle_.size() >= max_idle_) { value->close(); return; }
    idle_.push_back(std::move(value));
}

auto client_pool::acquire(client_pool_key key) -> std::unique_ptr<client> {
    auto found = endpoint_idle_.find(key);
    if (found == endpoint_idle_.end() || found->second.empty())
        return std::make_unique<client>(*context_, options_);
    auto value = std::move(found->second.back());
    found->second.pop_back();
    if (found->second.empty()) endpoint_idle_.erase(found);
    return value;
}

void client_pool::release(client_pool_key key, std::unique_ptr<client> value) {
    if (!value) return;
    auto& idle = endpoint_idle_[std::move(key)];
    if (idle.size() >= max_idle_) { value->close(); return; }
    idle.push_back(std::move(value));
}

void client_pool::clear() noexcept {
    for (auto& value : idle_) value->close();
    idle_.clear();
    for (auto& [_, values] : endpoint_idle_)
        for (auto& value : values) value->close();
    endpoint_idle_.clear();
}

auto client_pool::idle_count() const noexcept -> std::size_t {
    auto count = idle_.size();
    for (const auto& [_, values] : endpoint_idle_) count += values.size();
    return count;
}

} // namespace cnetmod::http
