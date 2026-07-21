module cnetmod.protocol.http;

import std;
import cnetmod.coro.task;
import :semantics;
import :router;
import :swagger;

namespace cnetmod::http {
namespace {
auto escape(std::string_view input) -> std::string {
  std::string out;
  out.reserve(input.size() + 8);
  for (const unsigned char c : input) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20)
        out += std::format("\\u{:04x}", static_cast<unsigned>(c));
      else
        out.push_back(static_cast<char>(c));
    }
  }
  return out;
}
void string_value(std::string &out, std::string_view value) {
  out.push_back('"');
  out += escape(value);
  out.push_back('"');
}
void field(std::string &out, std::string_view name, std::string_view value,
           bool &first) {
  if (value.empty())
    return;
  if (!first)
    out.push_back(',');
  first = false;
  string_value(out, name);
  out.push_back(':');
  string_value(out, value);
}
void raw_field(std::string &out, std::string_view name, std::string_view value,
               bool &first) {
  if (value.empty())
    return;
  if (!first)
    out.push_back(',');
  first = false;
  string_value(out, name);
  out.push_back(':');
  out += value;
}
auto method_name(http_method method) -> std::string_view {
  const auto value = method_to_string(method);
  if (value == "DELETE")
    return "delete";
  if (value == "GET")
    return "get";
  if (value == "POST")
    return "post";
  if (value == "PUT")
    return "put";
  if (value == "PATCH")
    return "patch";
  if (value == "OPTIONS")
    return "options";
  if (value == "HEAD")
    return "head";
  if (value == "TRACE")
    return "trace";
  return "get";
}
} // namespace
auto json_schema_ref(std::string_view name) -> std::string {
  return std::format(R"({{"$ref":"#/components/schemas/{}"}})", escape(name));
}
auto json_parameter(std::string_view name, std::string_view in,
                    std::string_view schema, bool required,
                    std::string_view description) -> std::string {
  std::string out = "{";
  bool first = true;
  field(out, "name", name, first);
  field(out, "in", in, first);
  out += ",\"required\":";
  out += required ? "true" : "false";
  field(out, "description", description, first);
  raw_field(out, "schema", schema, first);
  out.push_back('}');
  return out;
}
auto json_request_body(std::string_view schema, std::string_view content_type,
                       bool required, std::string_view description)
    -> std::string {
  std::string out = "{";
  bool first = true;
  field(out, "description", description, first);
  if (!first)
    out.push_back(',');
  out += std::format("\"required\":{},\"content\":{{",
                     required ? "true" : "false");
  string_value(out, content_type);
  out += ":{\"schema\":";
  out += schema.empty() ? R"({"type":"object"})" : std::string(schema);
  out += "}}}";
  return out;
}
void add_operation(openapi_document &doc, http_method method, std::string path,
                   openapi_operation op) {
  doc.paths[std::move(path)][std::string(method_name(method))] = std::move(op);
}
auto to_json(const openapi_document &doc) -> std::string {
  std::string out;
  out.reserve(4096);
  out += R"({"openapi":"3.0.3","info":{)";
  bool info = true;
  field(out, "title", doc.title, info);
  field(out, "version", doc.version, info);
  field(out, "description", doc.description, info);
  field(out, "termsOfService", doc.terms_of_service, info);
  out += "}";
  if (!doc.servers.empty()) {
    out += R"(,"servers":[)";
    bool first = true;
    for (const auto &server : doc.servers) {
      if (!first)
        out.push_back(',');
      first = false;
      out.push_back('{');
      bool item = true;
      field(out, "url", server.url, item);
      field(out, "description", server.description, item);
      out.push_back('}');
    }
    out.push_back(']');
  }
  out += R"(,"paths":{)";
  bool first_path = true;
  for (const auto &[path, methods] : doc.paths) {
    if (!first_path)
      out.push_back(',');
    first_path = false;
    string_value(out, path);
    out += ":{";
    bool first_method = true;
    for (const auto &[method, operation] : methods) {
      if (!first_method)
        out.push_back(',');
      first_method = false;
      string_value(out, method);
      out += ":{";
      bool first = true;
      if (!operation.tags.empty()) {
        out += R"("tags":[)";
        for (std::size_t i{}; i < operation.tags.size(); ++i) {
          if (i)
            out.push_back(',');
          string_value(out, operation.tags[i]);
        }
        out.push_back(']');
        first = false;
      }
      field(out, "summary", operation.summary, first);
      field(out, "description", operation.description, first);
      field(out, "operationId", operation.operation_id, first);
      if (operation.deprecated) {
        if (!first)
          out.push_back(',');
        out += R"("deprecated":true)";
        first = false;
      }
      if (!operation.parameters_json.empty()) {
        if (!first)
          out.push_back(',');
        out += R"("parameters":[)";
        for (std::size_t i{}; i < operation.parameters_json.size(); ++i) {
          if (i)
            out.push_back(',');
          out += operation.parameters_json[i];
        }
        out.push_back(']');
        first = false;
      }
      raw_field(out, "requestBody", operation.request_body_json, first);
      if (!operation.security_json.empty()) {
        if (!first)
          out.push_back(',');
        out += R"("security":[)";
        for (std::size_t i{}; i < operation.security_json.size(); ++i) {
          if (i)
            out.push_back(',');
          out += operation.security_json[i];
        }
        out.push_back(']');
        first = false;
      }
      if (!first)
        out.push_back(',');
      out += R"("responses":{)";
      bool first_response = true;
      for (const auto &[code, response] : operation.responses) {
        if (!first_response)
          out.push_back(',');
        first_response = false;
        string_value(out, code);
        out += ":{";
        bool response_field = true;
        field(out, "description", response.description, response_field);
        if (!response.schema_json.empty()) {
          if (!response_field)
            out.push_back(',');
          out += R"("content":{)";
          string_value(out, response.content_type);
          out += R"(:{"schema":)";
          out += response.schema_json;
          out += "}}";
        }
        out.push_back('}');
      }
      out += "}}";
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
auto openapi_json_handler(openapi_document doc) -> handler_fn {
  return [doc = std::move(doc)](request_context &ctx) -> task<void> {
    ctx.json(status::ok, to_json(doc));
    co_return;
  };
}
} // namespace cnetmod::http
