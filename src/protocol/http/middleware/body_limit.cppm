/**
 * @file body_limit.cppm
 * @brief Request body size limit middleware — returns 413 Payload Too Large when exceeded
 *
 * Checks Content-Length and actual body size, rejects oversized requests before handler execution to prevent OOM.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.body_limit;
 *
 *   svr.use(body_limit());                     // Default 1MB
 *   svr.use(body_limit(8 * 1024 * 1024));      // 8MB
 */
export module cnetmod.protocol.http.middleware.body_limit;

import std;

import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.protocol.http.semantics;

namespace cnetmod {

// =============================================================================
// body_limit — Request body size limit middleware
// =============================================================================
//
// Check order:
//   1. Content-Length header (if exists) > max_bytes → 413
//   2. Actual body.size() > max_bytes → 413 (fallback, handles chunked transfer)
//   3. Pass → call next()

export auto body_limit(std::size_t max_bytes = 1024 * 1024)
    -> http::middleware_fn;

} // namespace cnetmod
