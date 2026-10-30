module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :authentication;
import :sasl_reply;
import :scram_sha256;
import :connection;

namespace cnetmod::mongodb {
auto connection::authenticate_scram() -> task<result<void>>
{
    if (options_.username.empty())
        co_return std::unexpected(make_error(error_code::authentication_failed, "SCRAM requires a username"));
    scram_sha256_client scram{options_.username, options_.password};
    auto initial = scram.initial_message();
    if (!initial)
        co_return std::unexpected(initial.error());
    auto first = co_await sasl_start(scram_sha256_mechanism_name, std::move(*initial));
    if (!first)
        co_return std::unexpected(first.error());
    if (first->done)
        co_return std::unexpected(make_error(error_code::authentication_failed, "premature SCRAM completion"));
    auto response = scram.respond(first->payload);
    if (!response)
        co_return std::unexpected(response.error());
    auto second = co_await sasl_continue(first->conversation_id, std::move(*response));
    if (!second)
        co_return std::unexpected(second.error());
    auto verified = scram.verify(second->payload);
    if (!verified)
        co_return std::unexpected(verified.error());
    if (!second->done)
    {
        auto final = co_await sasl_continue(first->conversation_id, {});
        if (!final)
            co_return std::unexpected(final.error());
        if (!final->done)
            co_return std::unexpected(make_error(error_code::authentication_failed, "SASL exchange did not complete"));
    }
    co_return result<void>{};
}
} // namespace cnetmod::mongodb
