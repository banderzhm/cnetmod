/// Adapters from cnetmod's application result to an HTTP response.
export module cnetmod.protocol.http:application_result;

import std;
import :response;
import cnetmod.protocol.http.semantics;
import cnetmod.utils;

namespace cnetmod::http {

export struct application_response_options
{
    int success_status = status::ok;
    int error_status = status::internal_server_error;
    std::string_view content_type = "application/json";
};

/// Serialize an application result without coupling cnetmod.utils to HTTP or
/// to a particular JSON library. The two serializers own the application's
/// response schema for success and failure respectively.
export template <class T, class ErrorCode, class SuccessSerializer,
    class ErrorSerializer>
auto to_http_response(const utils::R<T, ErrorCode>& value,
    SuccessSerializer&& serialize_success, ErrorSerializer&& serialize_error,
    application_response_options options = {}) -> response
{
    response output{value.ok() ? options.success_status : options.error_status};
    output.set_header("Content-Type", options.content_type);
    if (value.ok())
    {
        output.set_body(std::string(
            std::invoke(std::forward<SuccessSerializer>(serialize_success), value.data())));
    }
    else
    {
        output.set_body(std::string(
            std::invoke(std::forward<ErrorSerializer>(serialize_error), value.failure())));
    }
    return output;
}

export template <class ErrorCode, class SuccessSerializer, class ErrorSerializer>
auto to_http_response(const utils::R<void, ErrorCode>& value,
    SuccessSerializer&& serialize_success, ErrorSerializer&& serialize_error,
    application_response_options options = {}) -> response
{
    response output{value.ok() ? options.success_status : options.error_status};
    output.set_header("Content-Type", options.content_type);
    if (value.ok())
    {
        output.set_body(std::string(
            std::invoke(std::forward<SuccessSerializer>(serialize_success))));
    }
    else
    {
        output.set_body(std::string(
            std::invoke(std::forward<ErrorSerializer>(serialize_error), value.failure())));
    }
    return output;
}

} // namespace cnetmod::http
