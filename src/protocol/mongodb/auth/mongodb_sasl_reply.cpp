module cnetmod.protocol.mongodb;
import std;
import :error;
import :bson_document;
import :sasl_reply;

namespace cnetmod::mongodb {
auto parse_sasl_reply(const bson_document& reply, std::optional<std::int32_t> expected) -> result<sasl_reply>
{
    const auto ok = reply.find("ok");
    const bool success = ok && ((ok->get_if<double>() && *ok->get_if<double>() == 1.0) || (ok->get_if<std::int32_t>() && *ok->get_if<std::int32_t>() == 1) || (ok->get_if<std::int64_t>() && *ok->get_if<std::int64_t>() == 1));
    if (!success)
        return std::unexpected(make_error(error_code::authentication_failed, "SASL reply was not successful"));
    const auto c = reply.find("conversationId");
    const auto d = reply.find("done");
    const auto p = reply.find("payload");
    std::optional<std::int64_t> conversation;
    if (c && c->get_if<std::int32_t>())
        conversation = *c->get_if<std::int32_t>();
    else if (c && c->get_if<std::int64_t>())
        conversation = *c->get_if<std::int64_t>();
    const auto binary = p ? p->get_if<bson_binary>() : nullptr;
    if (!conversation || *conversation < 0 || *conversation > std::numeric_limits<std::int32_t>::max() ||
        (expected && *conversation != *expected) || !d || !d->get_if<bool>() ||
        !binary || binary->subtype != 0)
        return std::unexpected(make_error(error_code::authentication_failed, "invalid SASL conversation, completion flag, or payload"));
    return sasl_reply{static_cast<std::int32_t>(*conversation), *d->get_if<bool>(), binary->bytes};
}
} // namespace cnetmod::mongodb
