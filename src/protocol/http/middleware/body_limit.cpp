module cnetmod.protocol.http.middleware.body_limit;

import std;
import cnetmod.coro.task;
import cnetmod.json;
import cnetmod.protocol.http;
import cnetmod.protocol.http.semantics;

namespace cnetmod {

namespace {

[[nodiscard]] auto body_limit_error(std::size_t limit, std::size_t size)
    -> std::string
{
    auto body = json::document::object();
    body["error"] = "request body too large";
    body["limit"] = limit;
    body["size"] = size;
    return json::write_document(body).value_or(std::string{});
}

} // namespace

auto body_limit(std::size_t max_bytes) -> http::middleware_fn
{
    return [max_bytes](http::request_context& ctx,
               http::next_fn next) -> task<void>
    {
        const auto content_length = ctx.get_header("Content-Length");
        if (!content_length.empty())
        {
            std::size_t length = 0;
            const auto [parsed_until, error] = std::from_chars(content_length.data(),
                content_length.data() + content_length.size(), length);
            static_cast<void>(parsed_until);
            if (error == std::errc{} && length > max_bytes)
            {
                ctx.json(http::status::payload_too_large,
                    body_limit_error(max_bytes, length));
                co_return;
            }
        }

        if (ctx.body().size() > max_bytes)
        {
            ctx.json(http::status::payload_too_large,
                body_limit_error(max_bytes, ctx.body().size()));
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
