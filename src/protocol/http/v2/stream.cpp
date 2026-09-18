module cnetmod.protocol.http.v2.stream;

import std;

namespace cnetmod::http::v2 {
stream::stream(std::uint32_t id, std::int32_t initial_window) noexcept
    : id_(id), receive_window_(initial_window), send_window_(initial_window) {}

auto stream::id() const noexcept -> std::uint32_t
{
    return id_;
}

auto stream::state() const noexcept -> stream_state
{
    return state_;
}

auto stream::headers() const noexcept -> const std::vector<header_field>&
{
    return headers_;
}

auto stream::body() const noexcept -> std::span<const std::byte>
{
    return body_;
}

void stream::attach_body_stream(
    std::shared_ptr<cnetmod::http::request_body_stream> body_stream)
{
    body_stream_ = std::move(body_stream);
}

auto stream::body_stream() const noexcept
    -> const std::shared_ptr<cnetmod::http::request_body_stream>&
{
    return body_stream_;
}

auto stream::receive_window() noexcept -> flow_window&
{
    return receive_window_;
}

auto stream::send_window() noexcept -> flow_window&
{
    return send_window_;
}

auto stream::receive_headers(std::vector<header_field> fields, bool end_stream)
    -> bool
{
    if (state_ != stream_state::idle && state_ != stream_state::open)
        return false;
    headers_ = std::move(fields);
    state_ = end_stream ? stream_state::half_closed_remote : stream_state::open;
    return true;
}

auto stream::receive_data(std::span<const std::byte> data, bool end_stream)
    -> bool
{
    if (state_ != stream_state::open ||
        !receive_window_.consume(static_cast<std::uint32_t>(data.size())))
        return false;
    if (body_stream_)
    {
        cnetmod::http::request_body_chunk chunk{data.begin(), data.end()};
        if (!body_stream_->push(std::move(chunk)))
            return false;
    }
    else
        body_.insert(body_.end(), data.begin(), data.end());
    if (end_stream)
    {
        state_ = stream_state::half_closed_remote;
        if (body_stream_)
            body_stream_->close();
    }
    return true;
}
} // namespace cnetmod::http::v2
