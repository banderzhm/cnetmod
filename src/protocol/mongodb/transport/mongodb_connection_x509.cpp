module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :authentication;
import :connection;

namespace cnetmod::mongodb {
auto connection::authenticate_x509() -> task<result<void>>
{
    if (!options_.password.empty() || options_.tls_cert_file.empty())
        co_return std::unexpected(make_error(error_code::authentication_failed, "X509 requires a TLS client certificate and no password"));
    bson_document command{{"authenticate", std::int32_t{1}}, {"mechanism", x509_mechanism_name}};
    if (!options_.username.empty())
        command.append("user", options_.username);
    auto reply = co_await execute_command(external_authentication_database, std::move(command));
    if (!reply)
        co_return std::unexpected(reply.error());
    co_return result<void>{};
}
} // namespace cnetmod::mongodb
