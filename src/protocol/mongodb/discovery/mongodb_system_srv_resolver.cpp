module;
#include <cnetmod/config.hpp>
#ifdef _WIN32
    #include <windns.h>
    #include <winsock2.h>
#else
    #include <arpa/nameser.h>
    #include <netdb.h>
    #include <resolv.h>
#endif
module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.executor.pool;
import cnetmod.io.io_context;
import :error;
import :server_address;
import :srv_seedlist;
import :system_srv_resolver;

namespace cnetmod::mongodb {
namespace {
    auto lookup(std::string_view name, std::string_view service) -> result<srv_dns_records>
    {
        srv_dns_records result;
        const std::string srv_name = "_" + std::string(service) + "._tcp." + std::string(name);
#ifdef _WIN32
        for (const auto type : {DNS_TYPE_SRV, DNS_TYPE_TEXT})
        {
            PDNS_RECORDA records{};
            const auto query_name = type == DNS_TYPE_SRV ? srv_name : std::string(name);
            const auto status = DnsQuery_A(query_name.c_str(), type, DNS_QUERY_STANDARD, nullptr, &records, nullptr);
            if (status != ERROR_SUCCESS)
            {
                if (type == DNS_TYPE_TEXT && (status == DNS_INFO_NO_RECORDS || status == DNS_ERROR_RCODE_NAME_ERROR))
                    continue;
                return std::unexpected(make_error(error_code::server_selection_failed, std::format("MongoDB DNS lookup failed (status {})", status)));
            }

            struct guard
            {
                PDNS_RECORDA value;

                ~guard()
                {
                    DnsRecordListFree(value, DnsFreeRecordList);
                }
            } cleanup{records};

            for (auto record = records; record; record = record->pNext)
            {
                if (record->wType == DNS_TYPE_SRV)
                    result.servers.push_back({{record->Data.SRV.pNameTarget, record->Data.SRV.wPort}, std::chrono::seconds{record->dwTtl}});
                else if (record->wType == DNS_TYPE_TEXT)
                {
                    std::string text;
                    for (DWORD i{}; i < record->Data.TXT.dwStringCount; ++i)
                        text += record->Data.TXT.pStringArray[i];
                    result.txt_records.push_back(std::move(text));
                }
            }
        }
#else
        struct __res_state resolver{};
        if (res_ninit(&resolver) != 0)
            return std::unexpected(make_error(error_code::server_selection_failed, "MongoDB DNS resolver initialization failed"));

        struct guard
        {
            struct __res_state* value;

            ~guard()
            {
    #ifdef __APPLE__
                res_ndestroy(value);
    #else
                res_nclose(value);
    #endif
            }
        } cleanup{&resolver};

        resolver.retrans = 2;
        resolver.retry = 2;
        for (const auto type : {ns_t_srv, ns_t_txt})
        {
            std::array<unsigned char, 65536> buffer{};
            const auto query_name = type == ns_t_srv ? srv_name : std::string(name);
            const auto size = res_nquery(&resolver, query_name.c_str(), ns_c_in, type, buffer.data(), buffer.size());
            if (size < 0)
            {
                if (type == ns_t_txt && (resolver.res_h_errno == HOST_NOT_FOUND || resolver.res_h_errno == NO_DATA))
                    continue;
                return std::unexpected(make_error(error_code::server_selection_failed, std::format("MongoDB SRV/TXT DNS lookup failed (resolver error {})", resolver.res_h_errno)));
            }
            ns_msg message{};
            if (ns_initparse(buffer.data(), size, &message) != 0)
                return std::unexpected(make_error(error_code::server_selection_failed, "malformed MongoDB DNS response"));
            for (int index{}; index < ns_msg_count(message, ns_s_an); ++index)
            {
                ns_rr record{};
                if (ns_parserr(&message, ns_s_an, index, &record) != 0)
                    return std::unexpected(make_error(error_code::server_selection_failed, "malformed MongoDB DNS record"));
                const auto data = ns_rr_rdata(record);
                const auto length = ns_rr_rdlen(record);
                if (ns_rr_type(record) == ns_t_srv)
                {
                    std::array<char, NS_MAXDNAME> target{};
                    if (length < 7 || dn_expand(buffer.data(), buffer.data() + size, data + 6, target.data(), target.size()) < 0)
                        return std::unexpected(make_error(error_code::server_selection_failed, "malformed MongoDB SRV record"));
                    const auto port = static_cast<std::uint16_t>((data[4] << 8) | data[5]);
                    result.servers.push_back({{target.data(), port}, std::chrono::seconds{ns_rr_ttl(record)}});
                }
                else if (ns_rr_type(record) == ns_t_txt)
                {
                    std::string text;
                    for (std::size_t pos{}; pos < length;)
                    {
                        const auto part = data[pos++];
                        if (part > length - pos)
                            return std::unexpected(make_error(error_code::server_selection_failed, "truncated MongoDB TXT character string"));
                        text.append(reinterpret_cast<const char*>(data + pos), part);
                        pos += part;
                    }
                    result.txt_records.push_back(std::move(text));
                }
            }
        }
#endif
        return result;
    }
} // namespace

auto system_srv_lookup(thread_pool& pool, io_context& io, std::string hostname,
    std::string service) -> task<result<srv_dns_records>>
{
    auto normalized = normalize_dns_hostname(hostname);
    if (!normalized)
        co_return std::unexpected(normalized.error());
    if (service.empty() || service.size() > 62 || !std::ranges::all_of(service, [](unsigned char c)
                                                      {
                                                          return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
                                                      }))
        co_return std::unexpected(make_error(error_code::server_selection_failed, "invalid MongoDB SRV service name"));
    co_return co_await blocking_invoke(pool, io, [name = std::move(*normalized), service = std::move(service)]
        {
            return lookup(name, service);
        });
}
} // namespace cnetmod::mongodb
