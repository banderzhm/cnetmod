/// cnetmod.protocol.openai:types — OpenAI API type definitions
/// Chat Completions / Embeddings / Models
/// Uses nlohmann::json for serialization/deserialization

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
    std::string arguments;          // JSON string
};

export struct tool_call {
    std::string   id;
    std::string   type = "function"; // Currently only "function"
    function_call function;
};

export struct tool {
    std::string type = "function";
    std::string function_name;
    std::string function_description;
    json        function_parameters = json::object(); // JSON Schema
};

// =============================================================================
// content_part — Multimodal content block (Vision API)
// =============================================================================

/// Image URL details
export struct image_url_detail {
    std::string url;                          // URL or base64 data URI
    std::string detail = "auto";              // "auto" | "low" | "high"
};

/// Content block: text or image
export struct content_part {
    std::string      type;                    // "text" | "image_url"
    std::string      text;                    // Valid when type="text"
    image_url_detail image_url;               // Valid when type="image_url"

    /// Create text content block
    static auto make_text(std::string_view t) -> content_part;

    /// Create image URL content block
    static auto make_image_url(std::string_view url, std::string_view detail = "auto") -> content_part;

    /// Create base64 image content block
    static auto make_image_base64(std::string_view base64_data,
                                   std::string_view media_type = "image/png",
                                   std::string_view detail = "auto") -> content_part;

    [[nodiscard]] auto to_json_object() const -> json;
};

// =============================================================================
// message — Chat message (supports multimodal)
// =============================================================================

export struct message {
    std::string              role;           // "system" | "user" | "assistant" | "tool"
    std::string              content;        // Plain text content (simple mode)
    std::vector<content_part> content_parts; // Multimodal content (Vision mode)
    std::string              name;           // Optional
    std::vector<tool_call>   tool_calls;     // Tool calls returned by assistant
    std::string              tool_call_id;   // Required when role="tool"

    /// Create simple text message
    static auto user(std::string_view text) -> message;
    static auto system(std::string_view text) -> message;
    static auto assistant(std::string_view text) -> message;

    /// Create multimodal message (Vision)
    static auto user_multimodal(std::vector<content_part> parts) -> message;

    [[nodiscard]] auto to_json_object() const -> json;
};

// =============================================================================
// chat_request — Chat Completions request
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
    std::string              stop;               // Optional stop
    int                      n                  = 1;
    std::optional<int>       seed;
    std::string              user;
    std::string              response_format;    // "" | "json_object" | "json_schema"
    std::vector<tool>        tools;
    std::string              tool_choice;        // "auto" | "none" | "required" or JSON

    [[nodiscard]] auto to_json() const -> std::string;
};

// =============================================================================
// chat_chunk — SSE streaming response chunk
// =============================================================================

export struct chat_chunk {
    std::string id;
    std::string model;
    std::string delta_role;
    std::string delta_content;
    std::string finish_reason;          // "" | "stop" | "length" | "tool_calls"
    std::vector<tool_call> delta_tool_calls;

    [[nodiscard]] static auto from_json(std::string_view text) -> chat_chunk;
};

// =============================================================================
// usage — Token usage statistics
// =============================================================================

export struct usage {
    int prompt_tokens     = 0;
    int completion_tokens = 0;
    int total_tokens      = 0;
};

// =============================================================================
// choice — Response option
// =============================================================================

export struct choice {
    int                      index = 0;
    message                  msg;               // Message content
    std::string              finish_reason;
};

// =============================================================================
// chat_response — Non-streaming complete response
// =============================================================================

export struct chat_response {
    std::string          id;
    std::string          model;
    std::vector<choice>  choices;
    usage                token_usage;

    /// Convenient access: choices[0].msg.content
    [[nodiscard]] auto content() const -> std::string_view;

    /// Parse from complete JSON body
    [[nodiscard]] static auto from_json(std::string_view text) -> chat_response;
};

// =============================================================================
// error_response — API error
// =============================================================================

export struct error_response {
    std::string message;
    std::string type;
    std::string code;

    [[nodiscard]] static auto from_json(std::string_view text) -> error_response;
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

    [[nodiscard]] auto to_json() const -> std::string;
};

export struct embedding_data {
    int                index = 0;
    std::vector<float> embedding;
};

export struct embedding_response {
    std::string                model;
    std::vector<embedding_data> data;
    usage                      token_usage;

    [[nodiscard]] static auto from_json(std::string_view text) -> embedding_response;
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
// TTS (Text-to-Speech) — Speech synthesis
// =============================================================================

export struct tts_request {
    std::string model = "tts-1";              // "tts-1" | "tts-1-hd"
    std::string input;                        // Text to convert (max 4096 characters)
    std::string voice = "alloy";              // "alloy"|"echo"|"fable"|"onyx"|"nova"|"shimmer"
    std::string response_format = "mp3";      // "mp3"|"opus"|"aac"|"flac"|"wav"|"pcm"
    double      speed = 1.0;                  // 0.25 ~ 4.0

    [[nodiscard]] auto to_json() const -> std::string;
};

// =============================================================================
// STT (Speech-to-Text) — Speech recognition (Whisper)
// =============================================================================

export struct transcription_request {
    std::vector<std::byte> file;              // Audio file data
    std::string            filename = "audio.mp3"; // Filename (with extension)
    std::string            model = "whisper-1";
    std::string            language;          // ISO-639-1 language code (optional)
    std::string            prompt;            // Prompt (optional)
    std::string            response_format = "json"; // "json"|"text"|"srt"|"vtt"|"verbose_json"
    double                 temperature = 0.0; // 0 ~ 1
};

export struct transcription_response {
    std::string text;                         // Transcribed text
    std::string language;                     // Detected language
    double      duration = 0.0;               // Audio duration (seconds)

    [[nodiscard]] static auto from_json(std::string_view text_data) -> transcription_response;
};

export struct translation_request {
    std::vector<std::byte> file;              // Audio file data
    std::string            filename = "audio.mp3";
    std::string            model = "whisper-1";
    std::string            prompt;            // Prompt (optional)
    std::string            response_format = "json";
    double                 temperature = 0.0;
};

// =============================================================================
// DALL-E — Image generation
// =============================================================================

export struct image_generation_request {
    std::string model = "dall-e-3";           // "dall-e-2" | "dall-e-3"
    std::string prompt;                       // Description text (max 4000 characters)
    int         n = 1;                        // Generation count (dall-e-3 only supports 1)
    std::string quality = "standard";         // "standard" | "hd" (dall-e-3 only)
    std::string response_format = "url";      // "url" | "b64_json"
    std::string size = "1024x1024";           // "256x256"|"512x512"|"1024x1024"|"1792x1024"|"1024x1792"
    std::string style = "vivid";              // "vivid" | "natural" (dall-e-3 only)
    std::string user;

    [[nodiscard]] auto to_json() const -> std::string;
};

export struct image_edit_request {
    std::vector<std::byte> image;             // Original image (PNG, <4MB, square)
    std::string            image_filename = "image.png";
    std::vector<std::byte> mask;              // Mask (optional, transparent area is edit area)
    std::string            mask_filename = "mask.png";
    std::string            prompt;            // Description text
    std::string            model = "dall-e-2"; // Currently only supports dall-e-2
    int                    n = 1;
    std::string            size = "1024x1024";
    std::string            response_format = "url";
    std::string            user;
};

export struct image_variation_request {
    std::vector<std::byte> image;             // Original image (PNG, <4MB, square)
    std::string            image_filename = "image.png";
    std::string            model = "dall-e-2";
    int                    n = 1;
    std::string            size = "1024x1024";
    std::string            response_format = "url";
    std::string            user;
};

export struct generated_image {
    std::string url;                          // Image URL (valid for 1 hour)
    std::string b64_json;                     // Base64 encoded image
    std::string revised_prompt;               // dall-e-3 revised prompt
};

export struct image_response {
    std::int64_t              created = 0;
    std::vector<generated_image> data;

    [[nodiscard]] static auto from_json(std::string_view text) -> image_response;
};

// =============================================================================
// Moderation — Content moderation
// =============================================================================

export struct moderation_request {
    std::string              model = "text-moderation-latest"; // "text-moderation-latest" | "text-moderation-stable"
    std::vector<std::string> input;

    [[nodiscard]] auto to_json() const -> std::string;
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

    [[nodiscard]] static auto from_json(std::string_view text) -> moderation_response;
};

// =============================================================================
// connect_options — Connection configuration
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
// on_chunk_fn — Streaming callback type
// =============================================================================

/// SSE streaming callback: (chunk) -> void
/// Called once for each SSE data chunk received
export using on_chunk_fn = std::function<void(const chat_chunk& chunk)>;

} // namespace cnetmod::openai
