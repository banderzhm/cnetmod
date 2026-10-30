#include <cnetmod/config.hpp>
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor.async_op;
import cnetmod.protocol.mongodb;
namespace mg = cnetmod::mongodb;

namespace {
template <class T>
auto checked(mg::result<T> value, std::string_view operation) -> T
{
    if (!value)
        throw std::runtime_error(std::string(operation) + ": " + value.error().message);
    if constexpr (!std::is_void_v<T>)
        return std::move(*value);
}
} // namespace

int main()
{
    const auto port_text = std::getenv("CNETMOD_MONGODB_TEST_PORT");
    if (!port_text)
    {
        std::println("Set CNETMOD_MONGODB_TEST_PORT to a disposable local replica set (no auth enforcement).");
        return 77;
    }
    cnetmod::net_init net;
    auto io = cnetmod::make_io_context();
    mg::connection_options options;
    unsigned port{};
    const std::string_view text{port_text};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (error != std::errc{} || end != text.data() + text.size() || port == 0 || port > 65535)
        return 1;
    options.port = static_cast<std::uint16_t>(port);
    if (const auto certificate = std::getenv("CNETMOD_MONGODB_TEST_CA"))
    {
        options.tls = true;
        options.tls_ca_file = certificate;
        options.tls_sni = "localhost";
    }
    options.enable_zstd_compression = !std::getenv("CNETMOD_MONGODB_TEST_SNAPPY");
    options.enable_snappy_compression = std::getenv("CNETMOD_MONGODB_TEST_SNAPPY") != nullptr;
    options.compression_minimum_bytes = 0;
    if (std::getenv("CNETMOD_MONGODB_TEST_COMPRESSION_ORDER"))
    {
        options.enable_zstd_compression = false;
        options.enable_snappy_compression = true;
        options.compression_preference = {mg::compressor_zlib, mg::compressor_snappy};
    }
    const auto database = "cnetmod_native_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto username = database;
    int exit_code = 1;
    auto run = [&]() -> cnetmod::task<void>
    {
        mg::connection admin{*io};
        bool user_created = false;
        try
        {
            checked(co_await admin.connect(options), "connect");
            if (!admin.capabilities().writable_primary)
            {
                checked(co_await admin.command("admin", mg::bson_document{{"replSetInitiate", mg::bson_document{{"_id", "cnetmod-test"}, {"members", mg::bson_array{mg::bson_document{{"_id", 0}, {"host", "127.0.0.1:" + std::to_string(port)}}}}}}}), "init replica set");
                for (unsigned attempt{}; attempt < 200; ++attempt)
                {
                    const auto hello = checked(co_await admin.command("admin", mg::bson_document{{"hello", 1}}), "hello");
                    const auto writable = hello.find("isWritablePrimary");
                    if (writable && writable->get_if<bool>() && *writable->get_if<bool>())
                        break;
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{100});
                }
            }
#ifdef CNETMOD_HAS_ZSTD
            if (admin.capabilities().selected_compressor != (!options.compression_preference.empty() ? options.compression_preference.front() : (options.enable_snappy_compression ? mg::compressor_snappy : mg::compressor_zstd)))
                throw std::runtime_error("zstd was not negotiated");
#endif
            mg::document_sequence documents{"documents", {}};
            for (std::int32_t index{}; index < 5; ++index)
                documents.documents.emplace_back(mg::bson_document{{"n", index}});
            checked(co_await admin.command(database, mg::bson_document{{"insert", "events"}},
                        std::vector<mg::document_sequence>{std::move(documents)}),
                "Kind 1 insert");
            mg::connection_pool_options pool_options;
            pool_options.connection = options;
            mg::connection_pool pool{*io, pool_options};
            {
                mg::cursor cursor{pool, database, "events", {.batch_size = 2}};
                auto [first_open, second_open] = co_await cnetmod::when_all(cursor.find(), cursor.find());
                if (first_open.has_value() == second_open.has_value())
                    throw std::runtime_error("concurrent cursor opens must have exactly one winner");
                unsigned count{};
                while (const auto next = checked(co_await cursor.next(), "cursor next"))
                    ++count;
                if (count != 5 || !cursor.exhausted())
                    throw std::runtime_error("cursor did not consume all batches");
                checked(co_await cursor.close(), "close exhausted cursor");
            }
            {
                mg::cursor cursor{pool, database, "events", {.batch_size = 1}};
                checked(co_await cursor.aggregate({mg::bson_document{{"$match", mg::bson_document{}}}}), "aggregate");
                if (cursor.id() == 0)
                    throw std::runtime_error("aggregate did not open a server cursor");
                checked(co_await cursor.close(), "killCursors");
            }
            {
                mg::client_session session;
                checked(session.start_transaction(), "start transaction");
                checked(co_await session.command(pool, database, mg::bson_document{{"insert", "events"}, {"documents", mg::bson_array{mg::bson_document{{"n", 100}}}}}), "transaction insert");
                checked(co_await session.abort_transaction(pool), "abort transaction");
                checked(session.start_transaction(), "restart transaction");
                checked(co_await session.command(pool, database, mg::bson_document{{"insert", "events"}, {"documents", mg::bson_array{mg::bson_document{{"n", 101}}}}}), "transaction insert");
                checked(co_await session.commit_transaction(pool), "commit transaction");
            }
            {
                mg::change_stream changes{pool, database, "events"};
                checked(co_await changes.open(), "open change stream");
                checked(co_await admin.command(database, mg::bson_document{{"insert", "events"}, {"documents", mg::bson_array{mg::bson_document{{"n", 102}}}}}), "change insert");
                const auto change = checked(co_await changes.next(), "change next");
                if (!change)
                    throw std::runtime_error("change stream lost event");
                co_await changes.close();
            }
            mg::topology_connection_pool_options topology_options;
            topology_options.seeds = {{"127.0.0.1", options.port}};
            topology_options.per_server_pool.connection = options;
            topology_options.per_server_pool.connection.username = "not-a-real-user";
            topology_options.per_server_pool.connection.password = "not-a-real-password";
            topology_options.per_server_pool.connection.authentication = mg::authentication_mechanism::aws;
            unsigned monitor_credential_fetches{};
            topology_options.per_server_pool.connection.aws_provider = std::make_shared<mg::aws_credential_cache>(
                [&]() -> cnetmod::task<mg::result<mg::aws_credentials>>
                {
                    ++monitor_credential_fetches;
                    co_return std::unexpected(mg::make_error(mg::error_code::authentication_failed, "monitor fetched user credentials"));
                });
            mg::topology_connection_pool topology{*io, topology_options};
            checked(co_await topology.refresh(), "unauthenticated monitoring");
            std::stop_source stop;
            const auto started = std::chrono::steady_clock::now();
            auto stopper = [&]() -> cnetmod::task<void>
            {
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{20});
                stop.request_stop();
            };
            co_await cnetmod::when_all(topology.run_monitoring(stop.get_token(), std::chrono::hours{1}), stopper());
            if (std::chrono::steady_clock::now() - started > std::chrono::seconds{2})
                throw std::runtime_error("topology monitoring did not stop promptly");
            topology.close();
            if (monitor_credential_fetches != 0)
                throw std::runtime_error("topology monitor must not authenticate user credentials");
            co_await pool.async_close();
            const std::string password{"I\xc2\xad" "X"};
            checked(co_await admin.command("admin", mg::bson_document{{"createUser", username}, {"pwd", password}, {"roles", mg::bson_array{mg::bson_document{{"role", "readWrite"}, {"db", database}}}}}), "create test user");
            user_created = true;
            auto authenticated_options = options;
            authenticated_options.username = username;
            authenticated_options.password = password;
            mg::connection authenticated{*io};
            checked(co_await authenticated.connect(std::move(authenticated_options)), "Unicode SCRAM authentication");
            checked(co_await authenticated.ping(), "authenticated ping");
            authenticated.close();
            exit_code = 0;
        }
        catch (const std::exception& exception)
        {
            std::println("[FAIL] {}", exception.what());
        }
        if (admin.is_open())
        {
            if (user_created)
                (void)co_await admin.command("admin", mg::bson_document{{"dropUser", username}});
            (void)co_await admin.command(database, mg::bson_document{{"dropDatabase", 1}});
        }
        admin.close();
        if (exit_code == 0)
            std::println("[PASS] Kind 1, zstd, find/getMore, aggregate/killCursors, commit/abort, change stream, Unicode SCRAM");
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    return exit_code;
}
