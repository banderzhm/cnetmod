module cnetmod.protocol.redis;

import std;
import :client;
import :parser;
import :request;
import :value;
import cnetmod.core.buffer;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::redis {

auto client::exchange(const request& batch, cancel_token& cancellation,
    std::size_t response_byte_limit)
    -> task<std::expected<std::vector<resp3_node>, std::error_code>>
{
    if (cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(
            cancellation.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));
    if (batch.empty() || response_byte_limit == 0U)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    if (rpos_ != rbuf_.size())
        co_return std::unexpected(std::make_error_code(std::errc::operation_in_progress));

    /**
     * @brief Prevents partial replies from reaching another command after failure.
     */
    struct exchange_guard
    {
        client& owner;
        bool committed = false;

        ~exchange_guard()
        {
            if (!committed)
                owner.close();
        }
    } guard{*this};

    rbuf_.clear();
    rpos_ = 0;
    const auto payload = batch.payload();
    const_buffer outgoing{payload.data(), payload.size()};
    std::expected<void, std::error_code> written;
#ifdef CNETMOD_HAS_SSL
    if (ssl_)
        written = co_await ssl_->async_write_all(outgoing, cancellation);
    else
#endif
        written = co_await async_write_all(ctx_, sock_, outgoing, cancellation);
    if (!written)
        co_return std::unexpected(written.error());

    std::vector<resp3_node> result;
    std::array<char, 4096> storage{};
    for (std::size_t response = 0; response < batch.size(); ++response)
    {
        resp3_parser parser;
        while (!parser.done())
        {
            std::error_code error;
            auto node = parser.consume(std::string_view{rbuf_}.substr(rpos_), error);
            if (error)
                co_return std::unexpected(error);
            if (node)
            {
                // A push frame is not a reply to the outstanding command.
                if (node->data_type == resp3_type::push)
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                result.push_back(std::move(*node));
                continue;
            }
            if (rbuf_.size() >= response_byte_limit)
                co_return std::unexpected(std::make_error_code(std::errc::message_size));
            const auto capacity = std::min(storage.size(), response_byte_limit - rbuf_.size());
            mutable_buffer incoming{storage.data(), capacity};
            std::expected<std::size_t, std::error_code> received;
#ifdef CNETMOD_HAS_SSL
            if (ssl_)
                received = co_await ssl_->async_read(incoming, cancellation);
            else
#endif
                received = co_await async_read(ctx_, sock_, incoming, cancellation);
            if (!received)
                co_return std::unexpected(received.error());
            if (*received == 0U)
                co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
            rbuf_.append(storage.data(), *received);
        }
        rpos_ += parser.consumed();
    }
    compact_buffer();
    if (!is_reusable())
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    guard.committed = true;
    co_return result;
}

} // namespace cnetmod::redis
