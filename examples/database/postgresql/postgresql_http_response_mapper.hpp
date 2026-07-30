#pragma once

namespace postgresql_example {

class http_response_mapper final
{
public:
    template <class T>
    static void send(cnetmod::http::request_context& context,
        const R<T>& response, int success_status = cnetmod::http::status::ok)
    {
        if (response.error())
        {
            send_error(context, response.failure());
            return;
        }

        context.json(success_status, nlohmann::json{{"code", success_status}, {"message", response.message()}, {"data", response_data(response.data())}}.dump());
    }

private:
    static auto response_data(const request_record& record) -> nlohmann::json
    {
        return {{"id", record.id},
            {"request_id", record.request_id},
            {"sequence_number", record.sequence_number},
            {"payload", record.payload}};
    }

    static auto response_data(const empty_response&) -> nlohmann::json
    {
        return nullptr;
    }

    static auto response_data(const nlohmann::json& data) -> nlohmann::json
    {
        return data;
    }

    static void send_error(cnetmod::http::request_context& context,
        const application_error& error)
    {
        const auto status = http_status(error.code);
        if (!error.diagnostic.empty())
            logger::error("PostgreSQL request failed: {}", error.diagnostic);
        context.json(status, nlohmann::json{{"code", status}, {"message", error.message}, {"data", nullptr}}.dump());
    }

    static auto http_status(application_error_code code) noexcept -> int
    {
        switch (code)
        {
        case application_error_code::invalid_request:
            return cnetmod::http::status::bad_request;
        case application_error_code::resource_not_found:
            return cnetmod::http::status::not_found;
        case application_error_code::operation_forbidden:
            return cnetmod::http::status::forbidden;
        case application_error_code::service_unavailable:
            return cnetmod::http::status::service_unavailable;
        }
        return cnetmod::http::status::internal_server_error;
    }
};

} // namespace postgresql_example
