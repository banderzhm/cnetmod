module cnetmod.protocol.websocket;

import std;
import :access_log;
import cnetmod.core.log;
import cnetmod.coro.task;

namespace cnetmod {

auto ws_access_log(ws::ws_handler_fn handler, logger::level lv,
    std::source_location loc) -> ws::ws_handler_fn
{
    return [handler = std::move(handler), lv,
               loc](ws::ws_context& ctx) -> task<void>
    {
        const auto path = std::string(ctx.path());
        logger::detail::write_log(lv, std::format("WS+ {}", path), loc);
        const auto start = std::chrono::steady_clock::now();
        try
        {
            co_await handler(ctx);
        }
        catch (...)
        {
            const auto ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start)
                                .count();
            logger::detail::write_log(logger::level::error,
                std::format("WS! {} {:.2f}ms (exception)", path, ms), loc);
            throw;
        }
        const auto ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start)
                            .count();
        logger::detail::write_log(
            lv, std::format("WS- {} {:.2f}ms", path, ms), loc);
    };
}

} // namespace cnetmod
