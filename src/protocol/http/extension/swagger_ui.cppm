export module cnetmod.protocol.http:swagger_ui;

import std;
import :semantics;
import :router;

export namespace cnetmod::http {
[[nodiscard]] auto
swagger_ui_html(std::string_view openapi_url = "/openapi.json",
                std::string_view title = "API Docs") -> std::string;

[[nodiscard]] auto swagger_ui_handler(std::string openapi_url = "/openapi.json",
                                      std::string title = "API Docs")
    -> handler_fn;
} // namespace cnetmod::http