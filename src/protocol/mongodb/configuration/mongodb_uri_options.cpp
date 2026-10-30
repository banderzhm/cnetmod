module cnetmod.protocol.mongodb;
import std;
import :error;
import :authentication;
import :oidc_credentials;
import :connection_options;
import :wire_protocol;
import :connection_pool;
import :topology_monitor;
import :topology_connection_pool;
import :connection_string;

namespace cnetmod::mongodb::uri_detail {
namespace {
    enum class uri_option
    {
        tls,
        ssl,
        loadbalanced,
        authsource,
        replicaset,
        authmechanism,
        authmechanismproperties,
        compressors,
        readpreference,
        readpreferencetags,
        srvservicename,
        srvmaxhosts,
        maxpoolsize,
        minpoolsize,
        maxconnecting,
        connecttimeoutms,
        sockettimeoutms,
        waitqueuetimeoutms,
        maxidletimems,
        maxstalenessseconds
    };
    constexpr std::array option_names{
        std::pair{std::string_view{"tls"}, uri_option::tls},
        std::pair{std::string_view{"ssl"}, uri_option::ssl},
        std::pair{std::string_view{"loadbalanced"}, uri_option::loadbalanced},
        std::pair{std::string_view{"authsource"}, uri_option::authsource},
        std::pair{std::string_view{"replicaset"}, uri_option::replicaset},
        std::pair{std::string_view{"authmechanism"}, uri_option::authmechanism},
        std::pair{std::string_view{"authmechanismproperties"}, uri_option::authmechanismproperties},
        std::pair{std::string_view{"compressors"}, uri_option::compressors},
        std::pair{std::string_view{"readpreference"}, uri_option::readpreference},
        std::pair{std::string_view{"readpreferencetags"}, uri_option::readpreferencetags},
        std::pair{std::string_view{"srvservicename"}, uri_option::srvservicename},
        std::pair{std::string_view{"srvmaxhosts"}, uri_option::srvmaxhosts},
        std::pair{std::string_view{"maxpoolsize"}, uri_option::maxpoolsize},
        std::pair{std::string_view{"minpoolsize"}, uri_option::minpoolsize},
        std::pair{std::string_view{"maxconnecting"}, uri_option::maxconnecting},
        std::pair{std::string_view{"connecttimeoutms"}, uri_option::connecttimeoutms},
        std::pair{std::string_view{"sockettimeoutms"}, uri_option::sockettimeoutms},
        std::pair{std::string_view{"waitqueuetimeoutms"}, uri_option::waitqueuetimeoutms},
        std::pair{std::string_view{"maxidletimems"}, uri_option::maxidletimems},
        std::pair{std::string_view{"maxstalenessseconds"}, uri_option::maxstalenessseconds}};
} // namespace

auto decode(std::string_view text) -> result<std::string>
{
    const auto digit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    std::string value;
    for (std::size_t i{}; i < text.size(); ++i)
    {
        if (text[i] == '%')
        {
            if (i + 2 >= text.size() || digit(text[i + 1]) < 0 || digit(text[i + 2]) < 0)
                return std::unexpected(make_error(error_code::protocol_error, "invalid percent encoding in MongoDB URI"));
            value += static_cast<char>((digit(text[i + 1]) << 4) | digit(text[i + 2]));
            i += 2;
        }
        else
            value += text[i];
    }
    if (value.find('\0') != std::string::npos)
        return std::unexpected(make_error(error_code::protocol_error, "NUL is not allowed in MongoDB URI"));
    return value;
}

auto query(std::string_view text) -> result<std::vector<std::pair<std::string, std::string>>>
{
    std::vector<std::pair<std::string, std::string>> options;
    std::set<std::string> seen;
    while (!text.empty())
    {
        const auto end = text.find('&');
        const auto item = text.substr(0, end);
        const auto equal = item.find('=');
        if (equal == std::string_view::npos || equal == 0)
            return std::unexpected(make_error(error_code::protocol_error, "invalid MongoDB URI option"));
        auto key = decode(item.substr(0, equal));
        auto value = decode(item.substr(equal + 1));
        if (!key || !value)
            return std::unexpected(key ? value.error() : key.error());
        for (auto& c : *key)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        constexpr std::string_view tags{"readpreferencetags"};
        if (!seen.insert(*key).second && *key != tags)
            return std::unexpected(make_error(error_code::protocol_error, "duplicate MongoDB URI option"));
        options.emplace_back(std::move(*key), std::move(*value));
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
        if (text.empty())
            return std::unexpected(make_error(error_code::protocol_error, "empty MongoDB URI option"));
    }
    return options;
}

auto apply(connection_configuration& config, std::span<const std::pair<std::string, std::string>> options) -> result<void>
{
    auto& pool = config.topology.per_server_pool;
    auto& connection = pool.connection;
    constexpr std::array<std::pair<std::string_view, authentication_mechanism>, 4> mechanisms{{{"SCRAM-SHA-256", authentication_mechanism::scram_sha256}, {"MONGODB-AWS", authentication_mechanism::aws},
        {"MONGODB-OIDC", authentication_mechanism::oidc}, {"MONGODB-X509", authentication_mechanism::x509}}};
    constexpr std::array<std::string_view, 5> preferences{"primary", "primaryPreferred", "secondary", "secondaryPreferred", "nearest"};
    for (const auto& [key, value] : options)
    {
        const auto decoded = std::ranges::find(option_names, key, [](const auto& entry)
            {
                return entry.first;
            });
        if (decoded == option_names.end())
            return std::unexpected(make_error(error_code::protocol_error, "unsupported MongoDB URI option: " + key));
        const auto kind = decoded->second;
        auto invalid = [&]() -> result<void>
        {
            return std::unexpected(make_error(error_code::protocol_error, "invalid MongoDB option: " + key));
        };
        const auto number = [&]() -> std::optional<std::int64_t>
        {
            std::int64_t parsed{};
            auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || end != value.data() + value.size() || parsed < 0)
                return {};
            return parsed;
        };
        if (kind == uri_option::tls || kind == uri_option::ssl || kind == uri_option::loadbalanced)
        {
            constexpr std::string_view yes{"true"}, no{"false"};
            if (value != yes && value != no)
                return invalid();
            if (kind == uri_option::loadbalanced)
                connection.load_balanced = value == yes;
            else
                connection.tls = value == yes;
        }
        else if (kind == uri_option::authsource)
        {
            if (value.empty())
                return invalid();
            connection.authentication_database = value;
            connection.authentication_database_explicit = true;
        }
        else if (kind == uri_option::replicaset)
        {
            if (value.empty())
                return invalid();
            config.topology.replica_set_name = value;
        }
        else if (kind == uri_option::authmechanism)
        {
            const auto found = std::ranges::find_if(mechanisms, [&](const auto& item)
                {
                    return item.first == value;
                });
            if (found == mechanisms.end())
                return invalid();
            connection.authentication = found->second;
        }
        else if (kind == uri_option::authmechanismproperties)
        {
            std::string_view remaining = value;
            std::set<std::string> seen;
            while (!remaining.empty())
            {
                const auto end = remaining.find(',');
                const auto item = remaining.substr(0, end);
                const auto colon = item.find(':');
                if (colon == std::string_view::npos)
                    return invalid();
                std::string name(item.substr(0, colon));
                for (auto& c : name)
                    if (c >= 'a' && c <= 'z')
                        c -= 'a' - 'A';
                if (!seen.insert(name).second)
                    return invalid();
                const auto val = item.substr(colon + 1);
                constexpr std::string_view environment_property{"ENVIRONMENT"}, resource_property{"TOKEN_RESOURCE"};
                if (name == environment_property)
                {
                    constexpr std::array environments{
                        std::pair{std::string_view{"azure"}, oidc_environment::azure},
                        std::pair{std::string_view{"gcp"}, oidc_environment::gcp},
                        std::pair{std::string_view{"k8s"}, oidc_environment::kubernetes},
                        std::pair{std::string_view{"test"}, oidc_environment::test}};
                    const auto environment = std::ranges::find(environments, val, [](const auto& item)
                        {
                            return item.first;
                        });
                    if (environment == environments.end())
                        return invalid();
                    connection.oidc_application_environment = environment->second;
                }
                else if (name == resource_property)
                    connection.oidc_token_resource = val;
                else
                    return invalid(); // Callbacks/allowed hosts never come from untrusted URIs.
                if (end == std::string_view::npos)
                    break;
                remaining.remove_prefix(end + 1);
            }
        }
        else if (kind == uri_option::compressors)
        {
            connection.enable_zlib_compression = connection.enable_snappy_compression = connection.enable_zstd_compression = false;
            connection.compression_preference.clear();
            std::string_view remaining = value;
            while (!remaining.empty())
            {
                const auto end = remaining.find(',');
                const auto codec = remaining.substr(0, end);
                std::uint8_t id{};
                if (codec == compressor_snappy_name)
                {
                    connection.enable_snappy_compression = true;
                    id = compressor_snappy;
                }
                else if (codec == compressor_zstd_name)
                {
                    connection.enable_zstd_compression = true;
                    id = compressor_zstd;
                }
                else if (codec == compressor_zlib_name)
                {
                    connection.enable_zlib_compression = true;
                    id = compressor_zlib;
                }
                else
                    return invalid();
                if (std::ranges::find(connection.compression_preference, id) != connection.compression_preference.end())
                    return invalid();
                connection.compression_preference.push_back(id);
                if (end == std::string_view::npos)
                    break;
                remaining.remove_prefix(end + 1);
                if (remaining.empty())
                    return invalid();
            }
        }
        else if (kind == uri_option::readpreference)
        {
            const auto found = std::ranges::find(preferences, value);
            if (found == preferences.end())
                return invalid();
            config.selection.preference = static_cast<read_preference>(found - preferences.begin());
        }
        else if (kind == uri_option::readpreferencetags)
        {
            std::map<std::string, std::string> tags;
            std::string_view remaining = value;
            while (!remaining.empty())
            {
                const auto end = remaining.find(',');
                const auto item = remaining.substr(0, end);
                const auto colon = item.find(':');
                if (colon == std::string_view::npos || colon == 0)
                    return invalid();
                if (!tags.emplace(std::string(item.substr(0, colon)), std::string(item.substr(colon + 1))).second)
                    return invalid();
                if (end == std::string_view::npos)
                    break;
                remaining.remove_prefix(end + 1);
            }
            config.selection.tag_sets.push_back(std::move(tags));
        }
        else if (kind == uri_option::srvservicename)
        {
            if (value.empty() || value.size() > 62 || value.front() == '-' || value.back() == '-' ||
                !std::ranges::all_of(value, [](unsigned char c)
                    {
                        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
                    }))
                return invalid();
            config.srv_service_name = value;
        }
        else if (kind == uri_option::srvmaxhosts)
        {
            const auto n = number();
            if (!n)
                return invalid();
            config.srv_max_hosts = *n;
        }
        else if (kind == uri_option::maxpoolsize)
        {
            const auto n = number();
            if (!n || !*n)
                return invalid();
            pool.maximum_size = *n;
        }
        else if (kind == uri_option::minpoolsize)
        {
            const auto n = number();
            if (!n)
                return invalid();
            pool.minimum_size = *n;
        }
        else if (kind == uri_option::maxconnecting)
        {
            const auto n = number();
            if (!n || !*n)
                return invalid();
            pool.maximum_connecting = *n;
        }
        else if (kind == uri_option::connecttimeoutms)
        {
            const auto n = number();
            if (!n)
                return invalid();
            connection.connect_timeout = std::chrono::milliseconds{*n};
        }
        else if (kind == uri_option::sockettimeoutms)
        {
            const auto n = number();
            if (!n)
                return invalid();
            connection.command_timeout = std::chrono::milliseconds{*n};
        }
        else if (kind == uri_option::waitqueuetimeoutms)
        {
            const auto n = number();
            if (!n)
                return invalid();
            pool.wait_queue_timeout = std::chrono::milliseconds{*n};
        }
        else if (kind == uri_option::maxidletimems)
        {
            const auto n = number();
            if (!n)
                return invalid();
            pool.maximum_idle_time = std::chrono::milliseconds{*n};
        }
        else if (kind == uri_option::maxstalenessseconds)
        {
            const auto n = number();
            if (!n || *n < 90)
                return invalid();
            config.selection.maximum_staleness = std::chrono::seconds{*n};
        }
        else
            return std::unexpected(make_error(error_code::protocol_error, "unsupported MongoDB URI option: " + key));
    }
    return {};
}
} // namespace cnetmod::mongodb::uri_detail
