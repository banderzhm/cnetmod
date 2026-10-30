module;
#include <unicode/uidna.h>
module cnetmod.protocol.mongodb;
import std;
import :error;
import :server_address;
import :srv_seedlist;

namespace cnetmod::mongodb {
auto normalize_dns_hostname(std::string_view hostname) -> result<std::string>
{
    while (hostname.ends_with('.'))
        hostname.remove_suffix(1);
    if (hostname.empty() || hostname.size() > 1024 || hostname.find('\0') != std::string_view::npos)
        return std::unexpected(make_error(error_code::server_selection_failed, "invalid MongoDB DNS hostname"));
    UErrorCode error = U_ZERO_ERROR;
    auto idna = uidna_openUTS46(UIDNA_NONTRANSITIONAL_TO_ASCII | UIDNA_CHECK_BIDI | UIDNA_CHECK_CONTEXTJ, &error);
    if (U_FAILURE(error) || !idna)
        return std::unexpected(make_error(error_code::server_selection_failed, "IDNA processing unavailable"));

    struct guard
    {
        UIDNA* value;

        ~guard()
        {
            uidna_close(value);
        }
    } close{idna};

    UIDNAInfo info = UIDNA_INFO_INITIALIZER;
    std::array<char, 256> output{};
    const auto size = uidna_nameToASCII_UTF8(idna, hostname.data(), static_cast<std::int32_t>(hostname.size()),
        output.data(), output.size(), &info, &error);
    if (U_FAILURE(error) || info.errors || size <= 0 || size > 253)
        return std::unexpected(make_error(error_code::server_selection_failed, "invalid MongoDB IDNA hostname"));
    std::string name(output.data(), size);
    for (auto& c : name)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return name;
}

auto validate_srv_seedlist(std::string_view hostname, srv_dns_records records,
    std::size_t maximum_hosts, std::span<const server_address> existing) -> result<srv_seedlist>
{
    auto source = normalize_dns_hostname(hostname);
    if (!source)
        return std::unexpected(source.error());
    const auto labels = std::ranges::count(*source, '.');
    const auto parent = labels >= 2 ? source->substr(source->find('.') + 1) : *source;
    const auto suffix = "." + parent;
    if (records.servers.empty() || records.txt_records.size() > 1)
        return std::unexpected(make_error(error_code::server_selection_failed, "SRV has no targets or multiple TXT records"));
    srv_seedlist result;
    result.ttl = std::chrono::seconds::max();
    for (auto& record : records.servers)
    {
        auto target = normalize_dns_hostname(record.address.host);
        if (!target || !target->ends_with(suffix) || record.address.port == 0 || record.ttl.count() < 0)
            return std::unexpected(make_error(error_code::server_selection_failed, "SRV target is outside the seed domain or has invalid metadata"));
        record.address.host = std::move(*target);
        if (std::ranges::find(result.seeds, record.address) == result.seeds.end())
            result.seeds.push_back(std::move(record.address));
        result.ttl = std::min(result.ttl, record.ttl);
    }
    result.ttl = std::max(result.ttl, std::chrono::seconds{60});
    if (!records.txt_records.empty())
        result.txt_options = std::move(records.txt_records.front());
    if (maximum_hosts && result.seeds.size() > maximum_hosts)
    {
        std::vector<server_address> retained, remaining;
        for (const auto& address : result.seeds)
            (std::ranges::find(existing, address) != existing.end() ? retained : remaining).push_back(address);
        std::mt19937 random{std::random_device{}()};
        std::ranges::shuffle(retained, random);
        std::ranges::shuffle(remaining, random);
        retained.insert(retained.end(), remaining.begin(), remaining.end());
        retained.resize(maximum_hosts);
        result.seeds = std::move(retained);
    }
    return result;
}
} // namespace cnetmod::mongodb
