module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;
import cnetmod.core.socket;
import cnetmod.coro.timer;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
#include "native_session.hpp"

namespace cnetmod::clickhouse {
auto client::cancel_current_query(cancel_token* cancellation) -> task<std::expected<void, error>>
{
    cancel_token local;
    auto& token = cancellation ? *cancellation : local;
    if (!impl_->busy || !impl_->querying)
        co_return std::unexpected(error{.kind = error_kind::invalid_argument, .message = "no active ClickHouse SELECT"});
    if (impl_->cancelling)
        co_return std::unexpected(error{.kind = error_kind::busy, .message = "ClickHouse cancellation is already in progress"});

    struct guard
    {
        bool& active;

        ~guard()
        {
            active = false;
        }
    } ownership{impl_->cancelling};

    impl_->cancelling = true;
    try
    {
        // Only the result-reading phase admits this write; no other writer can
        // interleave Query/INSERT bytes with the one-byte Native Cancel packet.
        const std::array packet{std::byte{3}};
        co_await impl_->write(packet, token);
        if (impl_->querying)
            impl_->cancel_requested = true;
        co_return std::expected<void, error>{};
    }
    catch (detail::failure& failure)
    {
        co_return std::unexpected(std::move(failure.value));
    }
}
} // namespace cnetmod::clickhouse
