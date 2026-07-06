#include "test_framework.hpp"

import cnetmod.protocol.http;

TEST(openapi_json_contains_registered_operation) {
    cnetmod::http::openapi_document doc;
    doc.title = "Demo API";
    doc.version = "2.1.0";
    doc.description = "demo";
    doc.servers.push_back({.url = "http://127.0.0.1:8080"});
    doc.components_json = R"({"schemas":{"User":{"type":"object"}}})";

    cnetmod::http::openapi_operation op;
    op.tags = {"users"};
    op.summary = "Get user";
    op.operation_id = "getUser";
    op.parameters_json.push_back(cnetmod::http::json_parameter(
        "id", "path", R"({"type":"string"})", true));
    op.responses = {
        {"200", cnetmod::http::openapi_response{
            .description = "User",
            .schema_json = cnetmod::http::json_schema_ref("User"),
        }},
        {"404", cnetmod::http::openapi_response{.description = "Not found"}},
    };
    cnetmod::http::add_operation(doc, cnetmod::http::http_method::GET, "/users/{id}", std::move(op));

    auto json = cnetmod::http::to_json(doc);
    ASSERT_TRUE(json.find(R"("openapi":"3.0.3")") != std::string::npos);
    ASSERT_TRUE(json.find(R"("title":"Demo API")") != std::string::npos);
    ASSERT_TRUE(json.find(R"("/users/{id}")") != std::string::npos);
    ASSERT_TRUE(json.find(R"("get":)") != std::string::npos);
    ASSERT_TRUE(json.find(R"("#/components/schemas/User")") != std::string::npos);
}

TEST(swagger_ui_references_openapi_url) {
    auto html = cnetmod::http::swagger_ui_html("/docs/openapi.json", "Docs");
    ASSERT_TRUE(html.find("swagger-ui") != std::string::npos);
    ASSERT_TRUE(html.find("/docs/openapi.json") != std::string::npos);
}

RUN_TESTS()
