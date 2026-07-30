#pragma once

namespace postgresql_example {

class http_request_controller
{
public:
    http_request_controller(request_application_service& requests,
        failover_connection_manager& connections, const service_config& config,
        service_health& health, std::function<void()> request_shutdown)
        : requests_(requests), connections_(connections), config_(config), health_(health), request_shutdown_(std::move(request_shutdown)) {}

    void register_routes(cnetmod::http::router& router)
    {
        router.get("/health/live", [this](auto& context)
            {
                return live(context);
            });
        router.get("/health/ready", [this](auto& context)
            {
                return ready(context);
            });
        router.post("/api/requests", [this](auto& context)
            {
                return create(context);
            });
        router.get("/api/requests/:request_id",
            [this](auto& context)
            {
                return find(context);
            });
        router.put("/api/requests/:request_id",
            [this](auto& context)
            {
                return update(context);
            });
        router.del("/api/requests/:request_id",
            [this](auto& context)
            {
                return remove(context);
            });
        router.post("/admin/shutdown",
            [this](auto& context)
            {
                return shutdown(context);
            });
    }

private:
    auto live(cnetmod::http::request_context& context) -> cnetmod::task<void>
    {
        const auto snapshot = health_.snapshot();
        if (snapshot.live)
            http_response_mapper::send(context,
                R<nlohmann::json>::ok({{"status", "UP"}}));
        else
            http_response_mapper::send(context,
                R<empty_response>::error(
                    application_error_code::service_unavailable,
                    "service is not live"));
        co_return;
    }

    auto ready(cnetmod::http::request_context& context) -> cnetmod::task<void>
    {
        const bool database_ready = co_await connections_.probe();
        const auto snapshot = health_.snapshot();
        const auto& endpoint = config_.endpoints.at(snapshot.active_endpoint);
        nlohmann::json response{
            {"status", database_ready && snapshot.ready ? "UP" : "DOWN"},
            {"active_endpoint", endpoint.display_name()},
            {"pool", {{"size", connections_.pool_size()}, {"idle", connections_.idle_connections()}, {"checked_out", connections_.checked_out_connections()}, {"waiting", connections_.waiting_requests()}}},
            {"pool_size", connections_.pool_size()},
            {"idle_connections", connections_.idle_connections()},
            {"checked_out_connections", connections_.checked_out_connections()},
            {"waiting_requests", connections_.waiting_requests()},
            {"in_flight", snapshot.in_flight},
            {"succeeded", snapshot.succeeded},
            {"failed", snapshot.failed}};
        if (database_ready && snapshot.ready)
            http_response_mapper::send(
                context, R<nlohmann::json>::ok(std::move(response)));
        else
            http_response_mapper::send(context,
                R<empty_response>::error(
                    application_error_code::service_unavailable,
                    "service is not ready"));
        co_return;
    }

    auto create(cnetmod::http::request_context& context) -> cnetmod::task<void>
    {
        const auto body = parse_body(context);
        if (!body || !body->contains("request_id") ||
            !(*body)["request_id"].is_string() ||
            !body->contains("sequence_number") ||
            !(*body)["sequence_number"].is_number_integer())
        {
            bad_request(context, "request_id and integer sequence_number are required");
            co_return;
        }
        auto request_id = (*body)["request_id"].get<std::string>();
        if (!valid_request_id(request_id))
        {
            bad_request(context, "request_id must contain 1 to 128 characters");
            co_return;
        }
        auto result = co_await requests_.create(std::move(request_id),
            (*body)["sequence_number"].get<std::int64_t>());
        http_response_mapper::send(context, result);
    }

    auto find(cnetmod::http::request_context& context) -> cnetmod::task<void>
    {
        auto request_id = std::string(context.param("request_id"));
        if (!valid_request_id(request_id))
        {
            bad_request(context, "invalid request_id");
            co_return;
        }
        http_response_mapper::send(
            context, co_await requests_.find(std::move(request_id)));
    }

    auto update(cnetmod::http::request_context& context) -> cnetmod::task<void>
    {
        auto request_id = std::string(context.param("request_id"));
        const auto body = parse_body(context);
        if (!valid_request_id(request_id) || !body ||
            !body->contains("sequence_number") ||
            !(*body)["sequence_number"].is_number_integer())
        {
            bad_request(context, "valid request_id and integer sequence_number are required");
            co_return;
        }
        http_response_mapper::send(context,
            co_await requests_.update(std::move(request_id),
                (*body)["sequence_number"].get<std::int64_t>()));
    }

    auto remove(cnetmod::http::request_context& context) -> cnetmod::task<void>
    {
        auto request_id = std::string(context.param("request_id"));
        if (!valid_request_id(request_id))
        {
            bad_request(context, "invalid request_id");
            co_return;
        }
        http_response_mapper::send(
            context, co_await requests_.remove(std::move(request_id)));
    }

    auto shutdown(cnetmod::http::request_context& context) -> cnetmod::task<void>
    {
        if (!config_.enable_remote_shutdown)
        {
            http_response_mapper::send(context,
                R<empty_response>::error(
                    application_error_code::operation_forbidden,
                    "remote shutdown is disabled"));
            co_return;
        }
        http_response_mapper::send(context,
            R<nlohmann::json>::ok({{"status", "shutdown requested"}}),
            cnetmod::http::status::accepted);
        request_shutdown_();
        co_return;
    }

    static auto parse_body(cnetmod::http::request_context& context)
        -> std::optional<nlohmann::json>
    {
        auto value = nlohmann::json::parse(
            std::string(context.body()), nullptr, false);
        if (value.is_discarded() || !value.is_object())
            return std::nullopt;
        return value;
    }

    static auto valid_request_id(std::string_view value) noexcept -> bool
    {
        return !value.empty() && value.size() <= 128;
    }

    static void bad_request(cnetmod::http::request_context& context,
        std::string_view message)
    {
        http_response_mapper::send(context,
            R<empty_response>::error(application_error_code::invalid_request,
                std::string(message)));
    }

    request_application_service& requests_;
    failover_connection_manager& connections_;
    const service_config& config_;
    service_health& health_;
    std::function<void()> request_shutdown_;
};

} // namespace postgresql_example
