module cnetmod.protocol.kafka.partitioner;
import std;

namespace cnetmod::kafka {
auto kafka_murmur2(std::span<const std::byte> d) noexcept -> std::uint32_t
{
    std::uint32_t h = 0x9747b28c, m = 0x5bd1e995;
    std::size_t i = 0, n = d.size();
    while (n >= 4)
    {
        std::uint32_t k = std::to_integer<unsigned>(d[i]) |
            std::to_integer<unsigned>(d[i + 1]) << 8 |
            std::to_integer<unsigned>(d[i + 2]) << 16 |
            std::to_integer<unsigned>(d[i + 3]) << 24;
        k *= m;
        k ^= k >> 24;
        k *= m;
        h *= m;
        h ^= k;
        i += 4;
        n -= 4;
    }
    if (n == 3)
        h ^= std::to_integer<unsigned>(d[i + 2]) << 16;
    if (n >= 2)
        h ^= std::to_integer<unsigned>(d[i + 1]) << 8;
    if (n >= 1)
    {
        h ^= std::to_integer<unsigned>(d[i]);
        h *= m;
    }
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
}

auto murmur2_partitioner::select(std::string_view,
    std::span<const std::byte> key,
    std::span<const std::int32_t> p)
    -> result<std::int32_t>
{
    if (p.empty())
        return std::unexpected(make_error(error_code::unknown_topic_or_partition));
    if (key.empty())
        return p[round_robin_++ % p.size()];
    return p[(kafka_murmur2(key) & 0x7fffffff) % p.size()];
}

auto uniform_sticky_partitioner::select(std::string_view topic,
    std::span<const std::byte>,
    std::span<const std::int32_t> p)
    -> result<std::int32_t>
{
    if (p.empty())
        return std::unexpected(make_error(error_code::unknown_topic_or_partition));
    std::scoped_lock l(mutex_);
    auto it = sticky_.find(topic);
    if (it != sticky_.end() && std::ranges::find(p, it->second) != p.end())
        return it->second;
    std::uniform_int_distribution<std::size_t> x(0, p.size() - 1);
    return sticky_[std::string(topic)] = p[x(engine_)];
}

void uniform_sticky_partitioner::rotate(std::string_view t)
{
    std::scoped_lock l(mutex_);
    sticky_.erase(std::string(t));
}
} // namespace cnetmod::kafka
