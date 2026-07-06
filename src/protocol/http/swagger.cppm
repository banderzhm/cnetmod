module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:swagger;

import std;
import :types;
import :router;

namespace cnetmod::http {

export struct openapi_server {
    std::string url;
    std::string description;
};

export struct openapi_response {
    std::string description = "OK";
    std::string content_type = "application/json";
    std::string schema_json;
};

export struct openapi_operation {
    std::vector<std::string> tags;
    std::string summary;
    std::string description;
    std::string operation_id;
    std::vector<std::string> parameters_json;
    std::string request_body_json;
    std::map<std::string, openapi_response> responses{
        {"200", openapi_response{}},
    };
    std::vector<std::string> security_json;
    bool deprecated = false;
};

export struct openapi_document {
    std::string title = "cnetmod API";
    std::string version = "1.0.0";
    std::string description;
    std::string terms_of_service;
    std::vector<openapi_server> servers;
    std::map<std::string, std::map<std::string, openapi_operation>> paths;
    std::string components_json;
};

namespace detail {

inline auto json_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char ch : s) {
        switch (ch) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(ch));
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }
    return out;
}

inline void append_json_string(std::string& out, std::string_view s) {
    out.push_back('"');
    out += json_escape(s);
    out.push_back('"');
}

inline void append_json_field(std::string& out,
                              std::string_view name,
                              std::string_view value,
                              bool& first) {
    if (value.empty()) return;
    if (!first) out.push_back(',');
    first = false;
    append_json_string(out, name);
    out.push_back(':');
    append_json_string(out, value);
}

inline void append_raw_json_field(std::string& out,
                                  std::string_view name,
                                  std::string_view raw_json,
                                  bool& first) {
    if (raw_json.empty()) return;
    if (!first) out.push_back(',');
    first = false;
    append_json_string(out, name);
    out.push_back(':');
    out.append(raw_json);
}

inline auto method_name(http_method method) -> std::string_view {
    auto s = method_to_string(method);
    if (s == "DELETE") return "delete";
    if (s == "GET") return "get";
    if (s == "POST") return "post";
    if (s == "PUT") return "put";
    if (s == "PATCH") return "patch";
    if (s == "OPTIONS") return "options";
    if (s == "HEAD") return "head";
    if (s == "TRACE") return "trace";
    return "get";
}

} // namespace detail

export inline auto json_schema_ref(std::string_view name) -> std::string {
    return std::format(R"({{"$ref":"#/components/schemas/{}"}})",
                       detail::json_escape(name));
}

export inline auto json_parameter(std::string_view name,
                                  std::string_view in,
                                  std::string_view schema_json = R"({"type":"string"})",
                                  bool required = true,
                                  std::string_view description = {}) -> std::string {
    std::string out = "{";
    bool first = true;
    detail::append_json_field(out, "name", name, first);
    detail::append_json_field(out, "in", in, first);
    if (!first) out.push_back(',');
    first = false;
    out += R"("required":)";
    out += required ? "true" : "false";
    detail::append_json_field(out, "description", description, first);
    detail::append_raw_json_field(out, "schema", schema_json, first);
    out.push_back('}');
    return out;
}

export inline auto json_request_body(std::string_view schema_json,
                                     std::string_view content_type = "application/json",
                                     bool required = true,
                                     std::string_view description = {}) -> std::string {
    std::string out = "{";
    bool first = true;
    detail::append_json_field(out, "description", description, first);
    if (!first) out.push_back(',');
    first = false;
    out += R"("required":)";
    out += required ? "true" : "false";
    out += R"(,"content":{)";
    detail::append_json_string(out, content_type);
    out += R"(:{"schema":)";
    out += schema_json.empty() ? R"({"type":"object"})" : std::string(schema_json);
    out += "}}";
    out.push_back('}');
    return out;
}

export inline void add_operation(openapi_document& doc,
                                 http_method method,
                                 std::string path,
                                 openapi_operation op) {
    doc.paths[std::move(path)][std::string(detail::method_name(method))] = std::move(op);
}

export inline auto to_json(const openapi_document& doc) -> std::string {
    std::string out;
    out.reserve(4096);
    out += R"({"openapi":"3.0.3","info":{)";
    bool first_info = true;
    detail::append_json_field(out, "title", doc.title, first_info);
    detail::append_json_field(out, "version", doc.version, first_info);
    detail::append_json_field(out, "description", doc.description, first_info);
    detail::append_json_field(out, "termsOfService", doc.terms_of_service, first_info);
    out += "}";

    if (!doc.servers.empty()) {
        out += R"(,"servers":[)";
        bool first_server = true;
        for (const auto& server : doc.servers) {
            if (!first_server) out.push_back(',');
            first_server = false;
            out.push_back('{');
            bool first = true;
            detail::append_json_field(out, "url", server.url, first);
            detail::append_json_field(out, "description", server.description, first);
            out.push_back('}');
        }
        out.push_back(']');
    }

    out += R"(,"paths":{)";
    bool first_path = true;
    for (const auto& [path, methods] : doc.paths) {
        if (!first_path) out.push_back(',');
        first_path = false;
        detail::append_json_string(out, path);
        out += ":{";
        bool first_method = true;
        for (const auto& [method, op] : methods) {
            if (!first_method) out.push_back(',');
            first_method = false;
            detail::append_json_string(out, method);
            out += ":{";
            bool first = true;
            if (!op.tags.empty()) {
                if (!first) out.push_back(',');
                first = false;
                out += R"("tags":[)";
                for (std::size_t i = 0; i < op.tags.size(); ++i) {
                    if (i != 0) out.push_back(',');
                    detail::append_json_string(out, op.tags[i]);
                }
                out.push_back(']');
            }
            detail::append_json_field(out, "summary", op.summary, first);
            detail::append_json_field(out, "description", op.description, first);
            detail::append_json_field(out, "operationId", op.operation_id, first);
            if (op.deprecated) {
                if (!first) out.push_back(',');
                first = false;
                out += R"("deprecated":true)";
            }
            if (!op.parameters_json.empty()) {
                if (!first) out.push_back(',');
                first = false;
                out += R"("parameters":[)";
                for (std::size_t i = 0; i < op.parameters_json.size(); ++i) {
                    if (i != 0) out.push_back(',');
                    out += op.parameters_json[i];
                }
                out.push_back(']');
            }
            detail::append_raw_json_field(out, "requestBody", op.request_body_json, first);
            if (!op.security_json.empty()) {
                if (!first) out.push_back(',');
                first = false;
                out += R"("security":[)";
                for (std::size_t i = 0; i < op.security_json.size(); ++i) {
                    if (i != 0) out.push_back(',');
                    out += op.security_json[i];
                }
                out.push_back(']');
            }
            if (!first) out.push_back(',');
            out += R"("responses":{)";
            bool first_resp = true;
            for (const auto& [status_code, resp] : op.responses) {
                if (!first_resp) out.push_back(',');
                first_resp = false;
                detail::append_json_string(out, status_code);
                out += ":{";
                bool first_resp_field = true;
                detail::append_json_field(out, "description", resp.description, first_resp_field);
                if (!resp.schema_json.empty()) {
                    if (!first_resp_field) out.push_back(',');
                    out += R"("content":{)";
                    detail::append_json_string(out, resp.content_type);
                    out += R"(:{"schema":)";
                    out += resp.schema_json;
                    out += "}}";
                }
                out.push_back('}');
            }
            out.push_back('}');
            out.push_back('}');
        }
        out.push_back('}');
    }
    out.push_back('}');
    if (!doc.components_json.empty()) {
        out += R"(,"components":)";
        out += doc.components_json;
    }
    out.push_back('}');
    return out;
}

export inline auto openapi_json_handler(openapi_document doc) -> handler_fn {
    return [doc = std::move(doc)](request_context& ctx) -> task<void> {
        ctx.json(status::ok, to_json(doc));
        co_return;
    };
}

export inline auto swagger_ui_html(std::string_view openapi_url = "/openapi.json",
                                   std::string_view title = "API Docs") -> std::string {
    return std::format(R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>{}</title>
  <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5/swagger-ui.css">
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
  <script>
    window.ui = SwaggerUIBundle({{
      url: "{}",
      dom_id: "#swagger-ui",
      deepLinking: true,
      presets: [SwaggerUIBundle.presets.apis],
      layout: "BaseLayout"
    }});
  </script>
</body>
</html>)", detail::json_escape(title), detail::json_escape(openapi_url));
}

export inline auto swagger_ui_handler(std::string openapi_url = "/openapi.json",
                                      std::string title = "API Docs") -> handler_fn {
    return [openapi_url = std::move(openapi_url),
            title = std::move(title)](request_context& ctx) -> task<void> {
        ctx.html(status::ok, swagger_ui_html(openapi_url, title));
        co_return;
    };
}

} // namespace cnetmod::http
