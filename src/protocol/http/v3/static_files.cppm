module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http.v3.static_files;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.protocol.http.v3.session;

namespace cnetmod::http::v3 {

export struct static_file_options
{
    std::filesystem::path root;
    std::string url_prefix{"/static/"};
    std::string index_file{"index.html"};
    bool serve_precompressed{true};
    std::size_t cache_max_file_size{1024U * 1024U};
    std::chrono::milliseconds revalidate_interval{500};
};

/// A composable HTTP/3 static-file handler. `true` means the request matched
/// the configured URL prefix, including 404/405 responses; `false` lets an
/// application continue routing. Small bodies use a metadata-revalidated
/// cache; changes in the mounted directory replace or invalidate entries on
/// the next request rather than being hidden by an application startup cache.
export using static_file_handler = std::function<
    std::expected<bool, std::error_code>(http3_request&, http3_response&)>;

export auto serve_dir(static_file_options options) -> static_file_handler;

} // namespace cnetmod::http::v3
