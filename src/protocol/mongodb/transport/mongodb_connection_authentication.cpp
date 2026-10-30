module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :authentication;
import :sasl_reply;
import :connection;

namespace cnetmod::mongodb {
auto connection::authenticate() -> task<result<void>>
{
    if (options_.authentication == authentication_mechanism::automatic && options_.username.empty())
    {
        if (!options_.password.empty())
            co_return std::unexpected(make_error(error_code::authentication_failed, "authentication password has no username"));
        co_return result<void>{};
    }
    if (options_.authentication == authentication_mechanism::automatic ||
        options_.authentication == authentication_mechanism::scram_sha256)
        co_return co_await authenticate_scram();
    if (!options_.tls)
        co_return std::unexpected(make_error(error_code::authentication_failed, "cloud authentication requires TLS"));
    if (options_.authentication_database != external_authentication_database &&
        (options_.authentication_database_explicit || options_.authentication_database != default_authentication_database))
        co_return std::unexpected(make_error(error_code::authentication_failed, "cloud authentication requires $external"));
    options_.authentication_database = external_authentication_database;
    switch (options_.authentication)
    {
    case authentication_mechanism::aws:
        co_return co_await authenticate_aws();
    case authentication_mechanism::oidc:
        co_return co_await authenticate_oidc();
    case authentication_mechanism::x509:
        co_return co_await authenticate_x509();
    default:
        co_return std::unexpected(make_error(error_code::authentication_failed, "unknown authentication mechanism"));
    }
}

auto connection::sasl_start(std::string_view mechanism, std::vector<std::byte> payload) -> task<result<sasl_reply>>
{
    bson_document command{{"saslStart", std::int32_t{1}}, {"mechanism", mechanism},
        {"payload", bson_binary{0, std::move(payload)}},
        {"options", bson_document{{"skipEmptyExchange", true}}}};
    auto reply = reauthenticating_
        ? co_await execute_command_without_deadline(options_.authentication_database, std::move(command))
        : co_await execute_command(options_.authentication_database, std::move(command));
    if (!reply)
        co_return std::unexpected(reply.error());
    co_return parse_sasl_reply(*reply);
}

auto connection::sasl_continue(std::int32_t conversation, std::vector<std::byte> payload) -> task<result<sasl_reply>>
{
    bson_document command{{"saslContinue", std::int32_t{1}}, {"conversationId", conversation},
        {"payload", bson_binary{0, std::move(payload)}}};
    auto reply = reauthenticating_
        ? co_await execute_command_without_deadline(options_.authentication_database, std::move(command))
        : co_await execute_command(options_.authentication_database, std::move(command));
    if (!reply)
        co_return std::unexpected(reply.error());
    co_return parse_sasl_reply(*reply, conversation);
}
} // namespace cnetmod::mongodb
