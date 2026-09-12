/// cnetmod.protocol.openai:structured — Strongly typed structured responses

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:structured;

import std;
import cnetmod.coro.task;
import :foundation;
import :chat;
import :model;
import :prompt;
import :service;

namespace cnetmod::openai {

/// Explicit schema and decoder for a C++ domain object.
/// This avoids reflection macros while keeping wire validation at the boundary.
export template <typename T>
struct structured_output_contract
{
    std::string schema_name = "response";
    json schema = json::object();
    std::function<std::expected<T, std::string>(const json&)> decode;
    bool strict = true;
};

export template <typename T>
struct structured_service_result
{
    T value;
    ai_service_result raw;
};

/// Typed facade over ai_service. The provider is instructed with JSON Schema,
/// then the response is validated and decoded into the requested domain type.
export template <typename T>
class structured_service
{
public:
    structured_service(ai_service& service,
        structured_output_contract<T> contract)
        : service_(service), contract_(std::move(contract))
    {
    }

    auto invoke(std::string input, std::string session_id = {},
        chat_request defaults = {}, const run_config& config = {})
        -> task<std::expected<structured_service_result<T>, std::string>>
    {
        if (!contract_.decode)
            co_return std::unexpected("structured output decoder is not configured");
        if (!contract_.schema.is_object() || contract_.schema.empty())
            co_return std::unexpected("structured output schema is not configured");

        defaults.response_format = "json_schema";
        defaults.response_schema_name = contract_.schema_name;
        defaults.response_schema = contract_.schema;
        defaults.response_schema_strict = contract_.strict;

        auto result = co_await service_.invoke(std::move(input),
            std::move(session_id), std::move(defaults), config);
        if (!result)
            co_return std::unexpected(result.error());

        auto value = json::parse(result->output.content, nullptr, false);
        if (value.is_discarded())
            co_return std::unexpected("structured output is not valid JSON");
        auto valid = validate_json_schema(value, contract_.schema);
        if (!valid)
            co_return std::unexpected("structured output validation failed: " +
                valid.error());
        auto decoded = contract_.decode(value);
        if (!decoded)
            co_return std::unexpected("structured output decoding failed: " +
                decoded.error());
        co_return structured_service_result<T>{
            std::move(*decoded), std::move(*result)};
    }

private:
    ai_service& service_;
    structured_output_contract<T> contract_;
};

} // namespace cnetmod::openai
