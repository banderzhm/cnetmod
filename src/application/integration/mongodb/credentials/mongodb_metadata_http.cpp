module cnetmod.application.mongodb_credentials;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.mongodb;

namespace cnetmod::application::mongodb_credentials {
auto make_metadata_transport(io_context& io) -> metadata_transport
{
    return [&io](metadata_request request) -> task<mongodb::result<metadata_response>>
    {
        if (request.timeout.count() <= 0)
            co_return std::unexpected(mongodb::make_error(
                mongodb::error_code::operation_timed_out, "cloud metadata deadline expired"));
        http::client_options options;
        options.connect_timeout = std::min(request.timeout, std::chrono::milliseconds{10000});
        options.request_timeout = request.timeout;
        options.follow_redirects = false;
        options.enable_cookies = false;
        options.keep_alive = false;
        options.verify_peer = true;
        options.version_pref = http::http_version_preference::http1_only;
        options.http1_response_body_limit = 1024U * 1024U;
        http::client client{io, options};
        http::request outgoing{request.method == metadata_method::get ? http::http_method::GET : (request.method == metadata_method::put ? http::http_method::PUT : http::http_method::POST), request.url};
        for (const auto& [name, value] : request.headers)
        {
            if (name.find_first_of("\r\n") != std::string::npos || value.find_first_of("\r\n") != std::string::npos)
                co_return std::unexpected(mongodb::make_error(
                    mongodb::error_code::authentication_failed, "invalid cloud metadata header"));
            outgoing.set_header(name, value);
        }
        if (!request.body.empty())
            outgoing.set_body(std::move(request.body));
        auto response = co_await client.send(outgoing);
        if (!response)
            co_return std::unexpected(mongodb::make_error(
                mongodb::error_code::authentication_failed, "cloud metadata transport failed"));
        co_return metadata_response{response->status_code(), std::string(response->body())};
    };
}
} // namespace cnetmod::application::mongodb_credentials

#endif
