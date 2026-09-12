/// cnetmod.protocol.openai:messages — Text, multimodal and tool messages

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:messages;

import std;
import :foundation;
import :tool_contracts;

namespace cnetmod::openai {

export struct image_url_detail
{
    std::string url;
    std::string detail = "auto";
};

export struct content_part
{
    std::string type;
    std::string text;
    image_url_detail image_url;

    static auto make_text(std::string_view text) -> content_part;
    static auto make_image_url(std::string_view url,
        std::string_view detail = "auto") -> content_part;
    static auto make_image_base64(std::string_view base64_data,
        std::string_view media_type = "image/png",
        std::string_view detail = "auto") -> content_part;
    [[nodiscard]] auto to_json_object() const -> json;
};

export struct message
{
    std::string role;
    std::string content;
    std::vector<content_part> content_parts;
    std::string name;
    std::vector<tool_call> tool_calls;
    std::string tool_call_id;

    /// Constructs end-user input (wire role: user).
    static auto user(std::string_view text) -> message;
    /// Constructs a system-level instruction (wire role: system).
    static auto system(std::string_view text) -> message;
    /// Constructs model-generated output (wire role: assistant).
    static auto model_output(std::string_view text) -> message;
    /// Constructs an application-level instruction (wire role: developer).
    static auto developer(std::string_view text) -> message;
    /// Constructs a tool execution result correlated by tool-call identifier.
    static auto tool_result(std::string_view tool_call_id,
        std::string_view content, std::string_view name = {}) -> message;
    /// Constructs a model tool-invocation request (wire role: assistant).
    static auto tool_call_request(std::vector<tool_call> calls,
        std::string_view content = {}) -> message;
    /// Constructs multimodal end-user input.
    static auto user_multimodal(std::vector<content_part> parts) -> message;
    [[nodiscard]] static auto from_json_object(const json& value)
        -> std::expected<message, std::string>;
    [[nodiscard]] auto to_json_object() const -> json;
};

} // namespace cnetmod::openai
