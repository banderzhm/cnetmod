module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp091;
import :amqp091_client;
import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :protocol_connection;
import :logical_channel;

namespace cnetmod::amqp091 {
struct amqp091_client::impl
{
    explicit impl(io_context& ctx)
        : connection(std::make_shared<protocol_connection>(ctx)) {}

    std::shared_ptr<protocol_connection> connection;
};

amqp091_client::amqp091_client(io_context& ctx)
    : impl_(std::make_unique<impl>(ctx)) {}

amqp091_client::~amqp091_client() = default;

auto amqp091_client::async_connect(connection_options o) -> task<result<void>>
{
    co_return co_await impl_->connection->async_connect(std::move(o));
}

auto amqp091_client::async_connect(connection_options o, cancel_token& t)
    -> task<result<void>>
{
    co_return co_await impl_->connection->async_connect(std::move(o), t);
}

auto amqp091_client::async_open_channel()
    -> task<result<std::shared_ptr<logical_channel>>>
{
    co_return co_await impl_->connection->async_open_channel();
}

auto amqp091_client::async_run(cancel_token& t) -> task<result<void>>
{
    co_return co_await impl_->connection->async_run(t);
}

auto amqp091_client::async_recover(cancel_token& t) -> task<result<void>>
{
    co_return co_await impl_->connection->async_recover(t);
}

auto amqp091_client::async_close() -> task<result<void>>
{
    co_return co_await impl_->connection->async_close();
}

auto amqp091_client::state() const noexcept -> connection_state
{
    return impl_->connection->state();
}

auto amqp091_client::connection() const noexcept
    -> std::shared_ptr<protocol_connection>
{
    return impl_->connection;
}
} // namespace cnetmod::amqp091
