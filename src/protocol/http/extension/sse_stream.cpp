module cnetmod.protocol.http;

import :router;

namespace cnetmod::http {

sse_stream::sse_stream(request_context& context,
    sse_stream_options options) noexcept
    : context_(&context)
{
    context_->configure_sse(options);
}

auto sse_stream::started() const noexcept -> bool
{
    return context_->sse_started();
}

auto sse_stream::state() const noexcept -> sse_stream_state
{
    return context_->sse_state();
}

auto sse_stream::begin(int status_code) -> task<bool>
{
    co_return co_await context_->sse_begin(status_code);
}

auto sse_stream::send(std::string_view payload, std::string_view event)
    -> task<bool>
{
    co_return co_await context_->sse_send(payload, event);
}

auto sse_stream::comment(std::string_view value) -> task<bool>
{
    co_return co_await context_->sse_comment(value);
}

auto sse_stream::heartbeat() -> task<bool>
{
    co_return co_await context_->sse_heartbeat();
}

auto sse_stream::finish() -> task<bool>
{
    co_return co_await context_->sse_done();
}

auto sse_stream::callback(std::string event)
    -> std::function<task<bool>(std::string_view)>
{
    return [this, event = std::move(event)](std::string_view payload)
               -> task<bool>
    {
        co_return co_await send(payload, event);
    };
}

} // namespace cnetmod::http
