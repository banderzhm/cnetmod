#pragma once

namespace mongodb_example {

struct health_report
{
    bool ready = false;
    std::string primary_address;
    std::size_t known_servers = 0;
    std::size_t readable_servers = 0;
    std::size_t writable_servers = 0;
    cnetmod::mongodb::topology_connection_pool_statistics pool;
    std::string message;
};

class health_indicator
{
public:
    health_indicator(cnetmod::io_context& context,
        cnetmod::mongodb::topology_connection_pool& pool,
        std::string database, std::chrono::milliseconds interval)
        : context_(context), pool_(pool), database_(std::move(database)), interval_(interval) {}

    auto check_once() -> cnetmod::task<health_report>
    {
        health_report report;
        auto refreshed = co_await pool_.refresh();
        report.pool = pool_.statistics();
        const auto servers = pool_.topology().snapshot();
        report.known_servers = servers.size();
        report.readable_servers = std::ranges::count_if(servers,
            [](const auto& server)
            {
                return server.readable();
            });
        report.writable_servers = std::ranges::count_if(servers,
            [](const auto& server)
            {
                return server.writable();
            });
        if (!refreshed)
        {
            report.message = refreshed.error().message;
            co_return report;
        }
        auto primary = pool_.topology().select_server();
        if (!primary)
        {
            report.message = primary.error().message;
            co_return report;
        }
        report.primary_address = std::format("{}:{}", primary->address.host,
            primary->address.port);
        auto ping = co_await pool_.command(database_,
            cnetmod::mongodb::bson_document{{"ping", std::int32_t{1}}});
        report.ready = ping.has_value();
        report.message = ping ? "ready" : ping.error().message;
        co_return report;
    }

    auto run(std::stop_token stop) -> cnetmod::task<void>
    {
        bool previously_ready = false;
        while (!stop.stop_requested())
        {
            auto report = co_await check_once();
            if (report.ready && !previously_ready)
                logger::info("MongoDB readiness restored: primary={}, known={}, readable={}, writable={}, pools={}, connections={}, idle={}, checked_out={}, waiting={}",
                    report.primary_address, report.known_servers,
                    report.readable_servers, report.writable_servers,
                    report.pool.server_pool_count, report.pool.connection_count,
                    report.pool.idle_connection_count,
                    report.pool.checked_out_connection_count,
                    report.pool.waiting_request_count);
            else if (!report.ready)
                logger::warn("MongoDB readiness failed: reason={}, known={}, readable={}, writable={}, pools={}, connections={}, idle={}, checked_out={}, waiting={}",
                    report.message, report.known_servers,
                    report.readable_servers, report.writable_servers,
                    report.pool.server_pool_count, report.pool.connection_count,
                    report.pool.idle_connection_count,
                    report.pool.checked_out_connection_count,
                    report.pool.waiting_request_count);
            previously_ready = report.ready;
            co_await cnetmod::async_sleep(context_, interval_);
        }
    }

private:
    cnetmod::io_context& context_;
    cnetmod::mongodb::topology_connection_pool& pool_;
    std::string database_;
    std::chrono::milliseconds interval_;
};

} // namespace mongodb_example
