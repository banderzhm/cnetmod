module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:wire_frame_codec;
import std;
import :message;
import :protocol_constants;
import :field_table_codec;

export namespace cnetmod::amqp091 {
struct frame
{
    frame_type type = frame_type::method;
    std::uint16_t channel = 0;
    std::vector<std::byte> payload;
};

struct method_frame
{
    std::uint16_t channel = 0;
    std::uint16_t class_id = 0;
    std::uint16_t method_id = 0;
    std::vector<std::byte> arguments;
};

struct content_header
{
    std::uint16_t channel = 0;
    std::uint16_t class_id = 60;
    std::uint64_t body_size = 0;
    message properties;
};

class frame_parser
{
public:
    explicit frame_parser(std::uint32_t frame_max = 131072) noexcept;
    auto feed(std::span<const std::byte> bytes) -> result<std::vector<frame>>;
    void reset() noexcept;

private:
    std::vector<std::byte> pending_;
    std::uint32_t frame_max_;
};

[[nodiscard]] auto encode_frame(const frame& value)
    -> result<std::vector<std::byte>>;
[[nodiscard]] auto encode_method(const method_frame& value) -> result<frame>;
[[nodiscard]] auto decode_method(const frame& value) -> result<method_frame>;
[[nodiscard]] auto encode_content_header(const content_header& value)
    -> result<frame>;
[[nodiscard]] auto decode_content_header(const frame& value)
    -> result<content_header>;
} // namespace cnetmod::amqp091
