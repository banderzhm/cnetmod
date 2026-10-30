module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;

namespace cnetmod::clickhouse {
struct connection_pool_state
{
    io_context& io;
    connection_pool_options options;
    std::vector<std::unique_ptr<client>> idle;
    std::size_t active{};
    bool stopping{};

    connection_pool_state(io_context& context, connection_pool_options input)
        : io(context), options(std::move(input))
    {
        if (!options.maximum_connections || options.maximum_connections > 4096 || options.minimum_connections > options.maximum_connections)
            throw std::invalid_argument("invalid ClickHouse pool capacity");
        idle.reserve(options.maximum_connections);
    }
};

pooled_connection::pooled_connection() noexcept = default;

pooled_connection::pooled_connection(std::shared_ptr<connection_pool_state> state, std::unique_ptr<client> value)
    : state_(std::move(state)), client_(std::move(value)) {}

pooled_connection::pooled_connection(pooled_connection&&) noexcept = default;

auto pooled_connection::operator=(pooled_connection&& other) noexcept -> pooled_connection&
{
    if (this != &other)
    {
        release();
        state_ = std::move(other.state_);
        client_ = std::move(other.client_);
    }
    return *this;
}

pooled_connection::~pooled_connection()
{
    release();
}

void pooled_connection::release() noexcept
{
    if (!client_)
        return;
    --state_->active;
    if (!state_->stopping && client_->is_open())
        state_->idle.push_back(std::move(client_));
    else
        client_.reset();
    state_.reset();
}

auto pooled_connection::get() const -> client&
{
    if (!client_)
        throw std::logic_error("empty ClickHouse pool lease");
    return *client_;
}

auto pooled_connection::operator->() const -> client*
{
    return &get();
}

connection_pool::connection_pool(io_context& context, connection_pool_options options)
    : state_(std::make_shared<connection_pool_state>(context, std::move(options))) {}

connection_pool::~connection_pool()
{
    (void)close();
}

auto connection_pool::size() const noexcept -> std::size_t
{
    return state_->idle.size() + state_->active;
}

auto connection_pool::idle_count() const noexcept -> std::size_t
{
    return state_->idle.size();
}

auto connection_pool::checked_out_count() const noexcept -> std::size_t
{
    return state_->active;
}

auto connection_pool::close() -> std::expected<void, std::error_code>
{
    state_->stopping = true;
    state_->idle.clear();
    if (state_->active)
        return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
    return {};
}

auto connection_pool::acquire(cancel_token* cancellation) -> task<std::expected<pooled_connection, error>>
{
    auto state = state_; // Retain ownership across suspended handshakes/close.
    if (cancellation && cancellation->is_cancelled())
        co_return std::unexpected(error{.kind = error_kind::cancelled, .cause = std::make_error_code(std::errc::operation_canceled), .message = "ClickHouse pool acquisition cancelled"});
    if (state->stopping)
        co_return std::unexpected(error{.kind = error_kind::transport, .cause = std::make_error_code(std::errc::not_connected), .message = "ClickHouse pool is stopped"});
    if (state->active >= state->options.maximum_connections)
        co_return std::unexpected(error{.kind = error_kind::busy, .cause = std::make_error_code(std::errc::resource_unavailable_try_again), .message = "ClickHouse pool is exhausted"});
    ++state->active;

    struct permit
    {
        connection_pool_state& state;
        bool transferred{};

        ~permit()
        {
            if (!transferred)
                --state.active;
        }
    } reservation{*state};

    std::unique_ptr<client> connection;
    try
    {
        if (!state->idle.empty())
        {
            connection = std::move(state->idle.back());
            state->idle.pop_back();
        }
        else
        {
            connection = std::make_unique<client>(state->io);
            auto connected = co_await connection->connect(state->options.connection, cancellation);
            if (!connected)
            {
                co_return std::unexpected(std::move(connected.error()));
            }
        }
        if (state->stopping)
        {
            co_return std::unexpected(error{.kind = error_kind::transport, .cause = std::make_error_code(std::errc::not_connected), .message = "ClickHouse pool closed during acquisition"});
        }
        reservation.transferred = true;
        co_return pooled_connection{std::move(state), std::move(connection)};
    }
    catch (...)
    {
        throw;
    }
}

auto connection_pool::warm_up(cancel_token* cancellation) -> task<std::expected<void, error>>
{
    std::vector<pooled_connection> leases;
    for (std::size_t i{}; i < state_->options.minimum_connections; ++i)
    {
        auto lease = co_await acquire(cancellation);
        if (!lease)
            co_return std::unexpected(std::move(lease.error()));
        leases.push_back(std::move(*lease));
    }
    co_return std::expected<void, error>{};
}

auto connection_pool::health_check(cancel_token* cancellation) -> task<std::expected<void, error>>
{
    auto lease = co_await acquire(cancellation);
    if (!lease)
        co_return std::unexpected(std::move(lease.error()));
    co_return co_await (*lease)->ping(cancellation);
}
} // namespace cnetmod::clickhouse
