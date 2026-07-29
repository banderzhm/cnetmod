module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.partitioner;
import std;
import cnetmod.protocol.kafka.protocol_constants;

export namespace cnetmod::kafka {
class partitioner
{
public:
    virtual ~partitioner() = default;
    [[nodiscard]] virtual auto select(std::string_view,
        std::span<const std::byte>,
        std::span<const std::int32_t>)
        -> result<std::int32_t> = 0;
};

class murmur2_partitioner final : public partitioner
{
public:
    auto select(std::string_view, std::span<const std::byte>,
        std::span<const std::int32_t>) -> result<std::int32_t> override;

private:
    std::atomic<std::uint32_t> round_robin_ = 0;
};

class uniform_sticky_partitioner final : public partitioner
{
public:
    auto select(std::string_view, std::span<const std::byte>,
        std::span<const std::int32_t>) -> result<std::int32_t> override;
    void rotate(std::string_view);

private:
    std::mutex mutex_;
    std::map<std::string, std::int32_t, std::less<>> sticky_;
    std::mt19937 engine_{std::random_device{}()};
};

[[nodiscard]] auto kafka_murmur2(std::span<const std::byte>) noexcept
    -> std::uint32_t;
} // namespace cnetmod::kafka
