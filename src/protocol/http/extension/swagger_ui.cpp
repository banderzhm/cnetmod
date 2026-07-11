module cnetmod.protocol.http;

import std;
import :swagger_ui;
import :router;

namespace cnetmod::http {

namespace {
auto escape_html_json(std::string_view value) -> std::string {
    std::string output;
    output.reserve(value.size() + 8);
    for (const auto ch : value) {
        if (ch == '\\' || ch == '"') output.push_back('\\');
        output.push_back(ch);
    }
    return output;
}
}

auto swagger_ui_html(std::string_view openapi_url, std::string_view title) -> std::string {
    return std::format(R"(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{}</title><link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5/swagger-ui.css"></head>
<body><div id="swagger-ui"></div><script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
<script>window.ui=SwaggerUIBundle({{url:"{}",dom_id:"#swagger-ui",deepLinking:true,presets:[SwaggerUIBundle.presets.apis],layout:"BaseLayout"}});</script>
</body></html>)", escape_html_json(title), escape_html_json(openapi_url));
}

auto swagger_ui_handler(std::string openapi_url, std::string title) -> handler_fn {
    return [openapi_url = std::move(openapi_url), title = std::move(title)](request_context& ctx) -> task<void> {
        ctx.html(status::ok, swagger_ui_html(openapi_url, title));
        co_return;
    };
}

} // namespace cnetmod::http
