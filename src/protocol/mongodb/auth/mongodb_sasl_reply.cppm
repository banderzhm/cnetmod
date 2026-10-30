export module cnetmod.protocol.mongodb:sasl_reply;
import std;
import :error;
import :bson_document;

export namespace cnetmod::mongodb {
struct sasl_reply
{
    std::int32_t conversation_id = 0;
    bool done = false;
    std::vector<std::byte> payload;
};

auto parse_sasl_reply(const bson_document& reply, std::optional<std::int32_t> expected_conversation = {}) -> result<sasl_reply>;
} // namespace cnetmod::mongodb
