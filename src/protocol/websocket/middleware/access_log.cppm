/**
 * @brief WebSocket access logging middleware.
 */
export module cnetmod.protocol.websocket:access_log;

import std;
import cnetmod.core.log;
import :server;

export namespace cnetmod {

[[nodiscard]] auto
ws_access_log(ws::ws_handler_fn handler, logger::level lv = logger::level::info,
    std::source_location loc = std::source_location::current())
    -> ws::ws_handler_fn;

} // namespace cnetmod
