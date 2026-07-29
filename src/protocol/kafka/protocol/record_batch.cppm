module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.record_batch;
import std;
import cnetmod.protocol.kafka.protocol_constants;

export namespace cnetmod::kafka {
class compression_codec
{
public:
    virtual ~compression_codec() = default;
    [[nodiscard]] virtual auto algorithm() const noexcept -> compression = 0;
    [[nodiscard]] virtual auto compress(std::span<const std::byte>)
        -> result<bytes> = 0;
    [[nodiscard]] virtual auto decompress(std::span<const std::byte>, std::size_t)
        -> result<bytes> = 0;
};

class compression_registry
{
public:
    compression_registry();
    void install(std::shared_ptr<compression_codec>);
    [[nodiscard]] auto find(compression) const
        -> std::shared_ptr<compression_codec>;
    [[nodiscard]] auto supports(compression) const noexcept -> bool;
    [[nodiscard]] auto available() const -> std::vector<compression>;

private:
    std::map<compression, std::shared_ptr<compression_codec>> codecs_;
};

struct record_batch_options
{
    compression compression_type = compression::none;
    std::optional<std::string> transactional_id;
    std::int16_t attributes = 0;
    std::int64_t producer_id = -1;
    std::int16_t producer_epoch = -1;
    std::int32_t base_sequence = -1;
    bool transactional = false;
};

struct decoded_record_batch
{
    std::int64_t base_offset = -1;
    std::int64_t last_offset = -1;
    std::int64_t producer_id = -1;
    bool transactional = false;
    bool control = false;
    std::vector<consumed_record> records;
};

[[nodiscard]] auto encode_record_batch(std::span<const record>,
    const record_batch_options&,
    const compression_registry&)
    -> result<bytes>;
[[nodiscard]] auto decode_record_batch(std::span<const std::byte>,
    const topic_partition&,
    const compression_registry&)
    -> result<decoded_record_batch>;
} // namespace cnetmod::kafka
