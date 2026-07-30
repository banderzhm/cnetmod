#pragma once

namespace postgresql_example {

class service_application
{
public:
    service_application(cnetmod::io_context& context, service_config config)
        : context_(context), config_(std::move(config)), connections_(context_, config_, health_), transactions_(context_, connections_, config_), requests_(transactions_, repository_, health_), controller_(requests_, connections_, config_, health_, [this]
                                                                                                                                                                                                       {
                                                                                                                                                                                                           remote_shutdown_requested_.store(true, std::memory_order_release);
                                                                                                                                                                                                       }),
          server_(context_)
    {}

    [[nodiscard]] auto exit_code() const noexcept -> int
    {
        return exit_code_.load(std::memory_order_acquire);
    }

    auto start() -> cnetmod::task<void>
    {
        health_.set_live(true);
        shutdown_.install();
        if (!(co_await connections_.warm_up()) || !(co_await initialize_schema()))
        {
            logger::critical("PostgreSQL service startup failed: no writable endpoint");
            exit_code_.store(1, std::memory_order_release);
            co_await stop_components();
            co_return;
        }

        cnetmod::http::router router;
        controller_.register_routes(router);
        server_.set_router(std::move(router));
        server_.use(cnetmod::recover());
        server_.use(cnetmod::access_log());
        server_.use(cnetmod::request_id());
        server_.use(cnetmod::body_limit(1024U * 1024U));
        server_.use(shutdown_.track_middleware());
        server_.set_max_connections(config_.maximum_pool_connections * 8);
        auto listening = server_.listen(config_.http_host, config_.http_port);
        if (!listening)
        {
            logger::critical("PostgreSQL HTTP listen failed: address={}:{}, error={}",
                config_.http_host, config_.http_port, listening.error().message());
            exit_code_.store(1, std::memory_order_release);
            co_await stop_components();
            co_return;
        }

        health_.set_ready(true);
        logger::info("PostgreSQL HTTP service ready: address={}:{}, endpoints={}, pool={}..{}",
            config_.http_host, config_.http_port, config_.endpoints.size(),
            config_.minimum_pool_connections, config_.maximum_pool_connections);
        cnetmod::spawn(context_, run_http_server());
        co_await wait_for_shutdown_request();

        health_.set_ready(false);
        server_.stop();
        const bool drained = co_await shutdown_.drain(
            [this](auto delay)
            {
                return cnetmod::async_sleep(context_, delay);
            },
            config_.shutdown_grace);
        if (!drained)
            logger::warn("PostgreSQL HTTP shutdown grace period expired");
        co_await wait_for_server_stop();
        co_await stop_components();
        logger::info("PostgreSQL HTTP service stopped cleanly");
    }

private:
    auto initialize_schema() -> cnetmod::task<bool>
    {
        auto created = co_await transactions_.execute(
            [](cnetmod::orm::postgresql_session& session)
                -> cnetmod::task<cnetmod::postgresql::result_set>
            {
                co_return co_await session.create_table<request_record>();
            });
        if (created.is_err())
        {
            logger::error("PostgreSQL schema initialization failed: {}",
                created.error_msg);
            co_return false;
        }
        co_return true;
    }

    auto run_http_server() -> cnetmod::task<void>
    {
        try
        {
            co_await server_.run();
        }
        catch (const std::exception& error)
        {
            logger::error("PostgreSQL HTTP server failed: {}", error.what());
            exit_code_.store(1, std::memory_order_release);
        }
        remote_shutdown_requested_.store(true, std::memory_order_release);
        server_stopped_.store(true, std::memory_order_release);
    }

    auto wait_for_shutdown_request() -> cnetmod::task<void>
    {
        while (!shutdown_.is_signaled() &&
            !remote_shutdown_requested_.load(std::memory_order_acquire))
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{100});
    }

    auto wait_for_server_stop() -> cnetmod::task<void>
    {
        const auto deadline = std::chrono::steady_clock::now() + config_.shutdown_grace;
        while (!server_stopped_.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now() < deadline)
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{25});
    }

    auto stop_components() -> cnetmod::task<void>
    {
        health_.set_ready(false);
        co_await connections_.close();
        health_.set_live(false);
        context_.stop();
    }

    cnetmod::io_context& context_;
    service_config config_;
    service_health health_;
    failover_connection_manager connections_;
    transaction_boundary transactions_;
    request_repository repository_;
    request_application_service requests_;
    http_request_controller controller_;
    cnetmod::shutdown_handler shutdown_;
    cnetmod::http::server server_;
    std::atomic_bool remote_shutdown_requested_{false};
    std::atomic_bool server_stopped_{false};
    std::atomic_int exit_code_{0};
};

} // namespace postgresql_example
