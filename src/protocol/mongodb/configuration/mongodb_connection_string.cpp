module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :error;
import :authentication;
import :connection_options;
import :topology_connection_pool;
import :server_address;
import :srv_seedlist;
import :system_srv_resolver;
import :connection_string;

namespace cnetmod::mongodb {
namespace {
    auto validate_options(const connection_configuration& config) -> result<void>
    {
        const auto& connection = config.topology.per_server_pool.connection;
        if (config.topology.per_server_pool.minimum_size > config.topology.per_server_pool.maximum_size)
            return std::unexpected(make_error(error_code::protocol_error, "MongoDB minPoolSize exceeds maxPoolSize"));
        if ((connection.authentication == authentication_mechanism::aws && (!connection.username.empty() || !connection.password.empty())) ||
            ((connection.authentication == authentication_mechanism::oidc || connection.authentication == authentication_mechanism::x509) && !connection.password.empty()))
            return std::unexpected(make_error(error_code::authentication_failed, "credentials are not allowed in this mechanism's URI/options"));
        if (config.srv_max_hosts && (config.topology.replica_set_name || connection.load_balanced))
            return std::unexpected(make_error(error_code::protocol_error, "srvMaxHosts conflicts with replicaSet/loadBalanced"));
        if (connection.load_balanced && (config.topology.replica_set_name || config.topology.seeds.size() != 1))
            return std::unexpected(make_error(error_code::protocol_error, "loadBalanced requires one seed and no replicaSet"));
        return {};
    }
} // namespace

auto parse_connection_string(std::string_view uri) -> result<connection_configuration>
{
    constexpr std::string_view srv_scheme{"mongodb+srv://"}, regular_scheme{"mongodb://"};
    connection_configuration config;
    const bool srv = uri.starts_with(srv_scheme);
    if (!srv && !uri.starts_with(regular_scheme))
        return std::unexpected(make_error(error_code::protocol_error, "invalid MongoDB URI scheme"));
    uri.remove_prefix(srv ? srv_scheme.size() : regular_scheme.size());
    if (uri.find('#') != std::string_view::npos)
        return std::unexpected(make_error(error_code::protocol_error, "MongoDB URI fragments are not supported"));
    const auto path = uri.find_first_of("/?");
    bool userinfo_present = false;
    auto authority = uri.substr(0, path);
    auto suffix = path == std::string_view::npos ? std::string_view{} : uri.substr(path);
    auto& connection = config.topology.per_server_pool.connection;
    if (const auto at = authority.find('@'); at != std::string_view::npos)
    {
        userinfo_present = true;
        const auto userinfo = authority.substr(0, at);
        if (authority.find('@', at + 1) != std::string_view::npos)
            return std::unexpected(make_error(error_code::protocol_error, "MongoDB URI credentials must be percent encoded"));
        const auto colon = userinfo.find(':');
        auto username = uri_detail::decode(userinfo.substr(0, colon));
        auto password = uri_detail::decode(colon == std::string_view::npos ? std::string_view{} : userinfo.substr(colon + 1));
        if (!username || !password)
            return std::unexpected(username ? password.error() : username.error());
        connection.username = std::move(*username);
        connection.password = std::move(*password);
        authority.remove_prefix(at + 1);
    }
    if (authority.empty())
        return std::unexpected(make_error(error_code::protocol_error, "MongoDB URI has no seed host"));
    if (suffix.starts_with('/'))
        suffix.remove_prefix(1);
    const auto question = suffix.find('?');
    auto database = uri_detail::decode(suffix.substr(0, question));
    if (!database || database->find_first_of("/\\ \"$.") != std::string::npos)
        return std::unexpected(make_error(error_code::protocol_error, "invalid MongoDB URI database"));
    if (!database->empty())
        connection.database = connection.authentication_database = std::move(*database);
    if (question != std::string_view::npos)
    {
        auto query = uri_detail::query(suffix.substr(question + 1));
        if (!query)
            return std::unexpected(query.error());
        config.uri_options = std::move(*query);
    }
    config.topology.seeds.clear();
    if (srv)
    {
        if (authority.find_first_of(":,[]") != std::string_view::npos ||
            std::ranges::all_of(authority, [](unsigned char c)
                {
                    return (c >= '0' && c <= '9') || c == '.';
                }))
            return std::unexpected(make_error(error_code::protocol_error, "SRV URI requires one DNS hostname without a port"));
        auto hostname = normalize_dns_hostname(authority);
        if (!hostname)
            return std::unexpected(hostname.error());
        config.srv_hostname = std::move(*hostname);
        connection.tls = true;
    }
    else
    {
        while (!authority.empty())
        {
            const auto comma = authority.find(',');
            const auto host = authority.substr(0, comma);
            if (host.empty() || host.ends_with(':') ||
                (std::ranges::count(host, ':') > 1 && !host.starts_with('[')))
                return std::unexpected(make_error(error_code::protocol_error, "invalid MongoDB URI seed address"));
            auto address = parse_server_address(host);
            if (!address)
                return std::unexpected(address.error());
            config.topology.seeds.push_back(std::move(*address));
            if (comma == std::string_view::npos)
                break;
            authority.remove_prefix(comma + 1);
            if (authority.empty())
                return std::unexpected(make_error(error_code::protocol_error, "empty MongoDB URI seed address"));
        }
    }
    auto applied = uri_detail::apply(config, config.uri_options);
    if (!applied)
        return std::unexpected(applied.error());
    if (userinfo_present && connection.authentication == authentication_mechanism::aws)
        return std::unexpected(make_error(error_code::authentication_failed, "MONGODB-AWS credentials must not appear in a URI"));
    std::optional<std::string> tls_alias;
    for (const auto& [key, value] : config.uri_options)
    {
        constexpr std::array<std::string_view, 2> aliases{"tls", "ssl"};
        if (std::ranges::find(aliases, key) != aliases.end())
        {
            if (tls_alias && *tls_alias != value)
                return std::unexpected(make_error(error_code::protocol_error, "tls and ssl URI aliases conflict"));
            tls_alias = value;
        }
    }
    if (connection.authentication == authentication_mechanism::aws || connection.authentication == authentication_mechanism::oidc ||
        connection.authentication == authentication_mechanism::x509)
        if (!connection.authentication_database_explicit)
            connection.authentication_database = external_authentication_database;
    if (!srv)
    {
        for (const auto& [key, value] : config.uri_options)
            if (key.starts_with("srv"))
                return std::unexpected(make_error(error_code::protocol_error, "SRV options require mongodb+srv"));
        auto valid = validate_options(config);
        if (!valid)
            return std::unexpected(valid.error());
    }
    return config;
}

auto resolve_connection_string(io_context& io, thread_pool& pool, std::string uri,
    srv_lookup_callback lookup) -> task<result<connection_configuration>>
{
    auto config = parse_connection_string(uri);
    if (!config || !config->srv_hostname)
        co_return config;
    if (!lookup)
        lookup = [&io, &pool](std::string hostname, std::string service)
        {
            return system_srv_lookup(pool, io, std::move(hostname), std::move(service));
        };
    result<srv_dns_records> records;
    try
    {
        records = co_await lookup(*config->srv_hostname, config->srv_service_name);
    }
    catch (...)
    {
        co_return std::unexpected(make_error(error_code::server_selection_failed, "MongoDB SRV/TXT lookup provider failed"));
    }
    if (!records)
        co_return std::unexpected(records.error());
    auto seeds = validate_srv_seedlist(*config->srv_hostname, std::move(*records));
    if (!seeds)
        co_return std::unexpected(seeds.error());
    if (seeds->txt_options)
    {
        auto defaults = uri_detail::query(*seeds->txt_options);
        if (!defaults)
            co_return std::unexpected(defaults.error());
        for (const auto& option : *defaults)
        {
            constexpr std::array<std::string_view, 3> allowed{"authsource", "replicaset", "loadbalanced"};
            if (std::ranges::find(allowed, option.first) == allowed.end())
                co_return std::unexpected(make_error(error_code::protocol_error, "forbidden MongoDB TXT option"));
            auto scratch = *config;
            auto valid = uri_detail::apply(scratch, std::span{&option, 1});
            if (!valid)
                co_return std::unexpected(valid.error());
            if (std::ranges::none_of(config->uri_options, [&](const auto& explicit_option)
                    {
                        return explicit_option.first == option.first;
                    }))
            {
                auto applied = uri_detail::apply(*config, std::span{&option, 1});
                if (!applied)
                    co_return std::unexpected(applied.error());
            }
        }
    }
    config->topology.seeds = seeds->seeds;
    auto valid = validate_options(*config);
    if (!valid)
        co_return std::unexpected(valid.error());
    if (config->srv_max_hosts && config->topology.seeds.size() > config->srv_max_hosts)
    {
        std::mt19937 random{std::random_device{}()};
        std::ranges::shuffle(config->topology.seeds, random);
        config->topology.seeds.resize(config->srv_max_hosts);
    }
    const auto hostname = *config->srv_hostname;
    const auto service = config->srv_service_name;
    const auto maximum = config->srv_max_hosts;
    config->topology.srv_refresh_interval = seeds->ttl;
    config->topology.refresh_srv = [lookup = std::move(lookup), hostname, service, maximum](std::vector<server_address> existing)
        -> task<result<srv_seedlist>>
    {
        auto records = co_await lookup(hostname, service);
        if (!records)
            co_return std::unexpected(records.error());
        co_return validate_srv_seedlist(hostname, std::move(*records), maximum, existing);
    };
    co_return config;
}
} // namespace cnetmod::mongodb
