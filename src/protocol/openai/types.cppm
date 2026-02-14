/// cnetmod.protocol.openai:types — OpenAI API 类型定义
/// Chat Completions / Embeddings / Models
/// 使用 nlohmann::json 序列化/反序列化

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:types;

import std;
import nlohmann.json;

namespace cnetmod::openai {

using json = nlohmann::json;

// =============================================================================
// tool_call — Function Calling
// =============================================================================

export struct function_call {
    std::string name;
    std::string arguments;          // JSON 字符串
};

export struct tool_call {
    std::string   id;
    std::string   type = "function"; // 目前只有 "function"
    function_call function;
};

export struct tool {
    std::string type = "function";
    std::string function_name;
    std::string function_description;
    json        function_parameters = json::object(); // JSON Schema
};

// =============================================================================
// content_part — 多模态内容块 (Vision API)
// =============================================================================

/// 图片URL详情
export struct image_url_detail {
    std::string url;                          // URL 或 base64 data URI
    std::string detail = "auto";              // "auto" | "low" | "high"
};

/// 内容块: 文本或图片
export struct content_part {
    std::string      type;                    // "text" | "image_url"
    std::string      text;                    // type="text" 时有效
    image_url_detail image_url;               // type="image_url" 时有效

    /// 创建文本内容块
    static auto make_text(std::string_view t) -> content_part {
        return content_part{.type = "text", .text = std::string(t), .image_url = {}};
    }

    /// 创建图片URL内容块
    static auto make_image_url(std::string_view url, std::string_view detail = "auto") -> content_part {
        return content_part{.type = "image_url", .text = {},
            .image_url = {.url = std::string(url), .detail = std::string(detail)}};
    }

    /// 创建 base64 图片内容块
    static auto make_image_base64(std::string_view base64_data,
                                   std::string_view media_type = "image/png",
                                   std::string_view detail = "auto") -> content_part {
        auto url = std::format("data:{};base64,{}", media_type, base64_data);
        return make_image_url(url, detail);
    }

    [[nodiscard]] auto to_json_object() const -> json {
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
};

// =============================================================================
// message — 聊天消息 (支持多模态)
// =============================================================================

export struct message {
    std::string              role;           // "system" | "user" | "assistant" | "tool"
    std::string              content;        // 纯文本内容 (简单模式)
    std::vector<content_part> content_parts; // 多模态内容 (Vision 模式)
    std::string              name;           // 可选
    std::vector<tool_call>   tool_calls;     // assistant 返回的 tool calls
    std::string              tool_call_id;   // role="tool" 时必填

    /// 创建简单文本消息
    static auto user(std::string_view text) -> message {
        return message{.role = "user", .content = std::string(text),
            .content_parts = {}, .name = {}, .tool_calls = {}, .tool_call_id = {}};
    }

    static auto system(std::string_view text) -> message {
        return message{.role = "system", .content = std::string(text),
            .content_parts = {}, .name = {}, .tool_calls = {}, .tool_call_id = {}};
    }

    static auto assistant(std::string_view text) -> message {
        return message{.role = "assistant", .content = std::string(text),
            .content_parts = {}, .name = {}, .tool_calls = {}, .tool_call_id = {}};
    }

    /// 创建多模态消息 (Vision)
    static auto user_multimodal(std::vector<content_part> parts) -> message {
        return message{.role = "user", .content = {},
            .content_parts = std::move(parts), .name = {}, .tool_calls = {}, .tool_call_id = {}};
    }

    [[nodiscard]] auto to_json_object() const -> json {
        json j;
        j["role"] = role;

        // 多模态内容优先
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
};

// =============================================================================
// chat_request — Chat Completions 请求
// =============================================================================

export struct chat_request {
    std::string              model = "gpt-4o-mini";
    std::vector<message>     messages;
    double                   temperature       = 0.7;
    int                      max_tokens         = 4096;
    bool                     stream             = false;
    double                   top_p              = 1.0;
    double                   frequency_penalty  = 0.0;
    double                   presence_penalty   = 0.0;
    std::string              stop;               // 可选 stop
    int                      n                  = 1;
    std::optional<int>       seed;
    std::string              user;
    std::string              response_format;    // "" | "json_object" | "json_schema"
    std::vector<tool>        tools;
    std::string              tool_choice;        // "auto" | "none" | "required" 或 JSON

    [[nodiscard]] auto to_json() const -> std::string {
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
};

// =============================================================================
// chat_chunk — SSE 流式响应块
// =============================================================================

export struct chat_chunk {
    std::string id;
    std::string model;
    std::string delta_role;
    std::string delta_content;
    std::string finish_reason;          // "" | "stop" | "length" | "tool_calls"
    std::vector<tool_call> delta_tool_calls;

    [[nodiscard]] static auto from_json(std::string_view text) -> chat_chunk {
        chat_chunk c;
        auto j = json::parse(text, nullptr, false);
        if (j.is_discarded()) return c;

        // 安全提取字符串字段 (处理 null)
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
};

// =============================================================================
// usage — token 使用统计
// =============================================================================

export struct usage {
    int prompt_tokens     = 0;
    int completion_tokens = 0;
    int total_tokens      = 0;
};

// =============================================================================
// choice — 响应选项
// =============================================================================

export struct choice {
    int                      index = 0;
    message                  msg;               // message 内容
    std::string              finish_reason;
};

// =============================================================================
// chat_response — 非流式完整响应
// =============================================================================

export struct chat_response {
    std::string          id;
    std::string          model;
    std::vector<choice>  choices;
    usage                token_usage;

    /// 便捷访问: choices[0].msg.content
    [[nodiscard]] auto content() const -> std::string_view {
        if (choices.empty()) return {};
        return choices[0].msg.content;
    }

    /// 从完整 JSON body 解析
    [[nodiscard]] static auto from_json(std::string_view text) -> chat_response {
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
};

// =============================================================================
// error_response — API 错误
// =============================================================================

export struct error_response {
    std::string message;
    std::string type;
    std::string code;

    [[nodiscard]] static auto from_json(std::string_view text) -> error_response {
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
};

// =============================================================================
// embedding_request / embedding_response
// =============================================================================

export struct embedding_request {
    std::string              model = "text-embedding-3-small";
    std::vector<std::string> input;
    std::string              encoding_format;   // "float" | "base64"
    std::optional<int>       dimensions;
    std::string              user;

    [[nodiscard]] auto to_json() const -> std::string {
        json j;
        j["model"] = model;
        if (input.size() == 1) j["input"] = input[0];
        else                   j["input"] = input;
        if (!encoding_format.empty()) j["encoding_format"] = encoding_format;
        if (dimensions.has_value())   j["dimensions"]      = *dimensions;
        if (!user.empty())            j["user"]            = user;
        return j.dump();
    }
};

export struct embedding_data {
    int                index = 0;
    std::vector<float> embedding;
};

export struct embedding_response {
    std::string                model;
    std::vector<embedding_data> data;
    usage                      token_usage;

    [[nodiscard]] static auto from_json(std::string_view text) -> embedding_response {
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
};

// =============================================================================
// model_info — Models API
// =============================================================================

export struct model_info {
    std::string id;
    std::string owned_by;
    int         created = 0;
};

// =============================================================================
// TTS (Text-to-Speech) — 语音合成
// =============================================================================

export struct tts_request {
    std::string model = "tts-1";              // "tts-1" | "tts-1-hd"
    std::string input;                        // 要转换的文本 (最大 4096 字符)
    std::string voice = "alloy";              // "alloy"|"echo"|"fable"|"onyx"|"nova"|"shimmer"
    std::string response_format = "mp3";      // "mp3"|"opus"|"aac"|"flac"|"wav"|"pcm"
    double      speed = 1.0;                  // 0.25 ~ 4.0

    [[nodiscard]] auto to_json() const -> std::string {
        json j;
        j["model"] = model;
        j["input"] = input;
        j["voice"] = voice;
        if (response_format != "mp3") j["response_format"] = response_format;
        if (speed != 1.0)             j["speed"] = speed;
        return j.dump();
    }
};

// =============================================================================
// STT (Speech-to-Text) — 语音识别 (Whisper)
// =============================================================================

export struct transcription_request {
    std::vector<std::byte> file;              // 音频文件数据
    std::string            filename = "audio.mp3"; // 文件名 (含扩展名)
    std::string            model = "whisper-1";
    std::string            language;          // ISO-639-1 语言代码 (可选)
    std::string            prompt;            // 提示词 (可选)
    std::string            response_format = "json"; // "json"|"text"|"srt"|"vtt"|"verbose_json"
    double                 temperature = 0.0; // 0 ~ 1
};

export struct transcription_response {
    std::string text;                         // 转录文本
    std::string language;                     // 检测到的语言
    double      duration = 0.0;               // 音频时长 (秒)

    [[nodiscard]] static auto from_json(std::string_view text_data) -> transcription_response {
        transcription_response r;
        auto j = json::parse(text_data, nullptr, false);
        if (j.is_discarded()) {
            // 可能是纯文本格式
            r.text = std::string(text_data);
            return r;
        }
        r.text     = j.value("text", "");
        r.language = j.value("language", "");
        r.duration = j.value("duration", 0.0);
        return r;
    }
};

export struct translation_request {
    std::vector<std::byte> file;              // 音频文件数据
    std::string            filename = "audio.mp3";
    std::string            model = "whisper-1";
    std::string            prompt;            // 提示词 (可选)
    std::string            response_format = "json";
    double                 temperature = 0.0;
};

// =============================================================================
// DALL-E — 图像生成
// =============================================================================

export struct image_generation_request {
    std::string model = "dall-e-3";           // "dall-e-2" | "dall-e-3"
    std::string prompt;                       // 描述文本 (最大 4000 字符)
    int         n = 1;                        // 生成数量 (dall-e-3 只支持 1)
    std::string quality = "standard";         // "standard" | "hd" (dall-e-3 only)
    std::string response_format = "url";      // "url" | "b64_json"
    std::string size = "1024x1024";           // "256x256"|"512x512"|"1024x1024"|"1792x1024"|"1024x1792"
    std::string style = "vivid";              // "vivid" | "natural" (dall-e-3 only)
    std::string user;

    [[nodiscard]] auto to_json() const -> std::string {
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
};

export struct image_edit_request {
    std::vector<std::byte> image;             // 原图 (PNG, <4MB, 正方形)
    std::string            image_filename = "image.png";
    std::vector<std::byte> mask;              // 蒙版 (可选, 透明区域为编辑区)
    std::string            mask_filename = "mask.png";
    std::string            prompt;            // 描述文本
    std::string            model = "dall-e-2"; // 目前只支持 dall-e-2
    int                    n = 1;
    std::string            size = "1024x1024";
    std::string            response_format = "url";
    std::string            user;
};

export struct image_variation_request {
    std::vector<std::byte> image;             // 原图 (PNG, <4MB, 正方形)
    std::string            image_filename = "image.png";
    std::string            model = "dall-e-2";
    int                    n = 1;
    std::string            size = "1024x1024";
    std::string            response_format = "url";
    std::string            user;
};

export struct generated_image {
    std::string url;                          // 图片 URL (1小时有效)
    std::string b64_json;                     // base64 编码的图片
    std::string revised_prompt;               // dall-e-3 修改后的 prompt
};

export struct image_response {
    std::int64_t              created = 0;
    std::vector<generated_image> data;

    [[nodiscard]] static auto from_json(std::string_view text) -> image_response {
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
};

// =============================================================================
// Moderation — 内容审核
// =============================================================================

export struct moderation_request {
    std::string              model = "text-moderation-latest"; // "text-moderation-latest" | "text-moderation-stable"
    std::vector<std::string> input;

    [[nodiscard]] auto to_json() const -> std::string {
        json j;
        j["model"] = model;
        if (input.size() == 1) j["input"] = input[0];
        else                   j["input"] = input;
        return j.dump();
    }
};

export struct moderation_categories {
    bool hate = false;
    bool hate_threatening = false;
    bool harassment = false;
    bool harassment_threatening = false;
    bool self_harm = false;
    bool self_harm_intent = false;
    bool self_harm_instructions = false;
    bool sexual = false;
    bool sexual_minors = false;
    bool violence = false;
    bool violence_graphic = false;
};

export struct moderation_result {
    bool                  flagged = false;
    moderation_categories categories;
};

export struct moderation_response {
    std::string                   id;
    std::string                   model;
    std::vector<moderation_result> results;

    [[nodiscard]] static auto from_json(std::string_view text) -> moderation_response {
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
};

// =============================================================================
// connect_options — 连接配置
// =============================================================================

export struct connect_options {
    std::string   api_base = "https://api.openai.com/v1";
    std::string   api_key;
    bool          tls_verify = true;
    std::string   tls_ca_file;
    int           timeout_seconds = 120;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};

// =============================================================================
// on_chunk_fn — 流式回调类型
// =============================================================================

/// SSE 流式回调: (chunk) -> void
/// 每收到一个 SSE 数据块就调用一次
export using on_chunk_fn = std::function<void(const chat_chunk& chunk)>;

} // namespace cnetmod::openai
