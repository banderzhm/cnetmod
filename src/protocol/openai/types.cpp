/// cnetmod.protocol.openai:types — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :types;
import nlohmann.json;

namespace cnetmod::openai {

using json = nlohmann::json;

// =============================================================================
// content_part
// =============================================================================

auto content_part::make_text(std::string_view t) -> content_part {
    return content_part{.type = "text", .text = std::string(t), .image_url = {}};
}

auto content_part::make_image_url(std::string_view url, std::string_view detail) -> content_part {
    return content_part{.type = "image_url", .text = {},
        .image_url = {.url = std::string(url), .detail = std::string(detail)}};
}

auto content_part::make_image_base64(std::string_view base64_data,
                                      std::string_view media_type,
                                      std::string_view detail) -> content_part {
    auto url = std::format("data:{};base64,{}", media_type, base64_data);
    return make_image_url(url, detail);
}

auto content_part::to_json_object() const -> json {
    json j;
    j["type"] = type;
    if (type == "text") {
        j["text"] = text;
    } else if (type == "image_url") {
        j["image_url"] = {{"url", image_url.url}};
        if (image_url.detail != "auto")
            j["image_url"]["detail"] = image_url.detail;
    }
    return j;
}

// =============================================================================
// message
// =============================================================================

auto message::user(std::string_view text) -> message {
    return message{.role = "user", .content = std::string(text),
        .content_parts = {}, .name = {}, .tool_calls = {}, .tool_call_id = {}};
}

auto message::system(std::string_view text) -> message {
    return message{.role = "system", .content = std::string(text),
        .content_parts = {}, .name = {}, .tool_calls = {}, .tool_call_id = {}};
}

auto message::assistant(std::string_view text) -> message {
    return message{.role = "assistant", .content = std::string(text),
        .content_parts = {}, .name = {}, .tool_calls = {}, .tool_call_id = {}};
}

auto message::user_multimodal(std::vector<content_part> parts) -> message {
    return message{.role = "user", .content = {},
        .content_parts = std::move(parts), .name = {}, .tool_calls = {}, .tool_call_id = {}};
}

auto message::to_json_object() const -> json {
    json j;
    j["role"] = role;

    // Multimodal content takes priority
    if (!content_parts.empty()) {
        json arr = json::array();
        for (auto& p : content_parts)
            arr.push_back(p.to_json_object());
        j["content"] = std::move(arr);
    } else {
        j["content"] = content;
    }

    if (!name.empty())         j["name"] = name;
    if (!tool_call_id.empty()) j["tool_call_id"] = tool_call_id;
    if (!tool_calls.empty()) {
        json arr = json::array();
        for (auto& tc : tool_calls) {
            json o;
            o["id"]   = tc.id;
            o["type"] = tc.type;
            o["function"] = {
                {"name",      tc.function.name},
                {"arguments", tc.function.arguments}
            };
            arr.push_back(std::move(o));
        }
        j["tool_calls"] = std::move(arr);
    }
    return j;
}

// =============================================================================
// chat_request
// =============================================================================

auto chat_request::to_json() const -> std::string {
    json j;
    j["model"]       = model;
    j["temperature"] = temperature;
    j["max_tokens"]  = max_tokens;
    j["stream"]      = stream;

    // messages
    json msgs = json::array();
    for (auto& m : messages)
        msgs.push_back(m.to_json_object());
    j["messages"] = std::move(msgs);

    if (top_p != 1.0)              j["top_p"] = top_p;
    if (frequency_penalty != 0.0)  j["frequency_penalty"] = frequency_penalty;
    if (presence_penalty != 0.0)   j["presence_penalty"]  = presence_penalty;
    if (!stop.empty())             j["stop"] = stop;
    if (n != 1)                    j["n"] = n;
    if (seed.has_value())          j["seed"] = *seed;
    if (!user.empty())             j["user"] = user;

    if (!response_format.empty()) {
        j["response_format"] = {{"type", response_format}};
    }

    if (!tools.empty()) {
        json arr = json::array();
        for (auto& t : tools) {
            json fn;
            fn["name"] = t.function_name;
            if (!t.function_description.empty())
                fn["description"] = t.function_description;
            if (!t.function_parameters.empty())
                fn["parameters"] = t.function_parameters;
            arr.push_back(json{{"type", t.type}, {"function", fn}});
        }
        j["tools"] = std::move(arr);
    }
    if (!tool_choice.empty())  j["tool_choice"] = tool_choice;

    return j.dump();
}

// =============================================================================
// chat_chunk
// =============================================================================

auto chat_chunk::from_json(std::string_view text) -> chat_chunk {
    chat_chunk c;
    auto j = json::parse(text, nullptr, false);
    if (j.is_discarded()) return c;

    // Safely extract string fields (handle null)
    auto safe_string = [](const json& obj, const char* key) -> std::string {
        if (!obj.contains(key)) return {};
        auto& val = obj[key];
        if (val.is_null()) return {};
        if (val.is_string()) return val.get<std::string>();
        return {};
    };

    c.id    = safe_string(j, "id");
    c.model = safe_string(j, "model");

    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
        auto& choice = j["choices"][0];
        if (choice.contains("delta") && choice["delta"].is_object()) {
            auto& delta = choice["delta"];
            c.delta_role    = safe_string(delta, "role");
            c.delta_content = safe_string(delta, "content");
            // tool_calls delta
            if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                for (auto& tc : delta["tool_calls"]) {
                    tool_call t;
                    t.id   = safe_string(tc, "id");
                    t.type = safe_string(tc, "type");
                    if (t.type.empty()) t.type = "function";
                    if (tc.contains("function") && tc["function"].is_object()) {
                        t.function.name      = safe_string(tc["function"], "name");
                        t.function.arguments = safe_string(tc["function"], "arguments");
                    }
                    c.delta_tool_calls.push_back(std::move(t));
                }
            }
        }
        c.finish_reason = safe_string(choice, "finish_reason");
    }
    return c;
}

// =============================================================================
// chat_response
// =============================================================================

auto chat_response::content() const -> std::string_view {
    if (choices.empty()) return {};
    return choices[0].msg.content;
}

auto chat_response::from_json(std::string_view text) -> chat_response {
    chat_response r;
    auto j = json::parse(text, nullptr, false);
    if (j.is_discarded()) return r;

    r.id    = j.value("id", "");
    r.model = j.value("model", "");

    // choices
    if (j.contains("choices") && j["choices"].is_array()) {
        for (auto& cj : j["choices"]) {
            choice ch;
            ch.index         = cj.value("index", 0);
            ch.finish_reason = cj.value("finish_reason", "");
            if (cj.contains("message") && cj["message"].is_object()) {
                auto& mj = cj["message"];
                ch.msg.role    = mj.value("role", "");
                ch.msg.content = mj.value("content", "");
                // tool_calls
                if (mj.contains("tool_calls") && mj["tool_calls"].is_array()) {
                    for (auto& tc : mj["tool_calls"]) {
                        tool_call t;
                        t.id   = tc.value("id", "");
                        t.type = tc.value("type", "function");
                        if (tc.contains("function") && tc["function"].is_object()) {
                            t.function.name      = tc["function"].value("name", "");
                            t.function.arguments = tc["function"].value("arguments", "");
                        }
                        ch.msg.tool_calls.push_back(std::move(t));
                    }
                }
            }
            r.choices.push_back(std::move(ch));
        }
    }

    // usage
    if (j.contains("usage") && j["usage"].is_object()) {
        auto& u = j["usage"];
        r.token_usage.prompt_tokens     = u.value("prompt_tokens", 0);
        r.token_usage.completion_tokens = u.value("completion_tokens", 0);
        r.token_usage.total_tokens      = u.value("total_tokens", 0);
    }

    return r;
}

// =============================================================================
// error_response
// =============================================================================

auto error_response::from_json(std::string_view text) -> error_response {
    error_response e;
    auto j = json::parse(text, nullptr, false);
    if (j.is_discarded()) { e.message = std::string(text); return e; }
    if (j.contains("error") && j["error"].is_object()) {
        auto& err = j["error"];
        e.message = err.value("message", "");
        e.type    = err.value("type", "");
        e.code    = err.value("code", "");
    } else {
        e.message = j.value("message", std::string(text));
    }
    return e;
}

// =============================================================================
// embedding_request
// =============================================================================

auto embedding_request::to_json() const -> std::string {
    json j;
    j["model"] = model;
    if (input.size() == 1) j["input"] = input[0];
    else                   j["input"] = input;
    if (!encoding_format.empty()) j["encoding_format"] = encoding_format;
    if (dimensions.has_value())   j["dimensions"]      = *dimensions;
    if (!user.empty())            j["user"]            = user;
    return j.dump();
}

// =============================================================================
// embedding_response
// =============================================================================

auto embedding_response::from_json(std::string_view text) -> embedding_response {
    embedding_response r;
    auto j = json::parse(text, nullptr, false);
    if (j.is_discarded()) return r;
    r.model = j.value("model", "");
    if (j.contains("data") && j["data"].is_array()) {
        for (auto& d : j["data"]) {
            embedding_data ed;
            ed.index = d.value("index", 0);
            if (d.contains("embedding") && d["embedding"].is_array()) {
                for (auto& v : d["embedding"])
                    ed.embedding.push_back(v.get<float>());
            }
            r.data.push_back(std::move(ed));
        }
    }
    if (j.contains("usage") && j["usage"].is_object()) {
        auto& u = j["usage"];
        r.token_usage.prompt_tokens = u.value("prompt_tokens", 0);
        r.token_usage.total_tokens  = u.value("total_tokens", 0);
    }
    return r;
}

// =============================================================================
// tts_request
// =============================================================================

auto tts_request::to_json() const -> std::string {
    json j;
    j["model"] = model;
    j["input"] = input;
    j["voice"] = voice;
    if (response_format != "mp3") j["response_format"] = response_format;
    if (speed != 1.0)             j["speed"] = speed;
    return j.dump();
}

// =============================================================================
// transcription_response
// =============================================================================

auto transcription_response::from_json(std::string_view text_data) -> transcription_response {
    transcription_response r;
    auto j = json::parse(text_data, nullptr, false);
    if (j.is_discarded()) {
        // Might be plain text format
        r.text = std::string(text_data);
        return r;
    }
    r.text     = j.value("text", "");
    r.language = j.value("language", "");
    r.duration = j.value("duration", 0.0);
    return r;
}

// =============================================================================
// image_generation_request
// =============================================================================

auto image_generation_request::to_json() const -> std::string {
    json j;
    j["model"]  = model;
    j["prompt"] = prompt;
    if (n != 1)                      j["n"] = n;
    if (quality != "standard")       j["quality"] = quality;
    if (response_format != "url")    j["response_format"] = response_format;
    if (size != "1024x1024")         j["size"] = size;
    if (style != "vivid")            j["style"] = style;
    if (!user.empty())               j["user"] = user;
    return j.dump();
}

// =============================================================================
// image_response
// =============================================================================

auto image_response::from_json(std::string_view text) -> image_response {
    image_response r;
    auto j = json::parse(text, nullptr, false);
    if (j.is_discarded()) return r;
    r.created = j.value("created", std::int64_t{0});
    if (j.contains("data") && j["data"].is_array()) {
        for (auto& d : j["data"]) {
            generated_image img;
            img.url            = d.value("url", "");
            img.b64_json       = d.value("b64_json", "");
            img.revised_prompt = d.value("revised_prompt", "");
            r.data.push_back(std::move(img));
        }
    }
    return r;
}

// =============================================================================
// moderation_request
// =============================================================================

auto moderation_request::to_json() const -> std::string {
    json j;
    j["model"] = model;
    if (input.size() == 1) j["input"] = input[0];
    else                   j["input"] = input;
    return j.dump();
}

// =============================================================================
// moderation_response
// =============================================================================

auto moderation_response::from_json(std::string_view text) -> moderation_response {
    moderation_response r;
    auto j = json::parse(text, nullptr, false);
    if (j.is_discarded()) return r;
    r.id    = j.value("id", "");
    r.model = j.value("model", "");
    if (j.contains("results") && j["results"].is_array()) {
        for (auto& res : j["results"]) {
            moderation_result mr;
            mr.flagged = res.value("flagged", false);
            if (res.contains("categories") && res["categories"].is_object()) {
                auto& c = res["categories"];
                mr.categories.hate                    = c.value("hate", false);
                mr.categories.hate_threatening        = c.value("hate/threatening", false);
                mr.categories.harassment              = c.value("harassment", false);
                mr.categories.harassment_threatening  = c.value("harassment/threatening", false);
                mr.categories.self_harm               = c.value("self-harm", false);
                mr.categories.self_harm_intent        = c.value("self-harm/intent", false);
                mr.categories.self_harm_instructions  = c.value("self-harm/instructions", false);
                mr.categories.sexual                  = c.value("sexual", false);
                mr.categories.sexual_minors           = c.value("sexual/minors", false);
                mr.categories.violence                = c.value("violence", false);
                mr.categories.violence_graphic        = c.value("violence/graphic", false);
            }
            r.results.push_back(std::move(mr));
        }
    }
    return r;
}

} // namespace cnetmod::openai
