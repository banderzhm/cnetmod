export module cnetmod.protocol.http:swagger;

import std;
import :semantics;
import :router;

export namespace cnetmod::http {
struct openapi_server {
  std::string url;
  std::string description;
};
struct openapi_response {
  std::string description = "OK";
  std::string content_type = "application/json";
  std::string schema_json;
};
struct openapi_operation {
  std::vector<std::string> tags;
  std::string summary;
  std::string description;
  std::string operation_id;
  std::vector<std::string> parameters_json;
  std::string request_body_json;
  std::map<std::string, openapi_response> responses{
      {"200", openapi_response{}}};
  std::vector<std::string> security_json;
  bool deprecated = false;
};
struct openapi_document {
  std::string title = "cnetmod API";
  std::string version = "1.0.0";
  std::string description;
  std::string terms_of_service;
  std::vector<openapi_server> servers;
  std::map<std::string, std::map<std::string, openapi_operation>> paths;
  std::string components_json;
};
[[nodiscard]] auto json_schema_ref(std::string_view name) -> std::string;
[[nodiscard]] auto
json_parameter(std::string_view name, std::string_view in,
               std::string_view schema_json = R"({"type":"string"})",
               bool required = true, std::string_view description = {})
    -> std::string;
[[nodiscard]] auto
json_request_body(std::string_view schema_json,
                  std::string_view content_type = "application/json",
                  bool required = true, std::string_view description = {})
    -> std::string;
void add_operation(openapi_document &doc, http_method method, std::string path,
                   openapi_operation op);
[[nodiscard]] auto to_json(const openapi_document &doc) -> std::string;
[[nodiscard]] auto openapi_json_handler(openapi_document doc) -> handler_fn;
} // namespace cnetmod::http
