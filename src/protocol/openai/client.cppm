/// cnetmod.protocol.openai:client — Async OpenAI Chat Completions client
/// Supports SSL/TLS, streaming SSE, connection reuse
/// Reuses cnetmod's ssl_context / ssl_stream / http::request / http::response_parser

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:client;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.protocol.http;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :types;
import nlohmann.json;

namespace cnetmod::openai {

// =============================================================================
// client — Async OpenAI client
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx) noexcept;
    ~client();

    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;

    // ── Connection ──────────────────────────────────────────────────

    /// Connect to OpenAI API endpoint (TCP + TLS handshake)
    auto connect(connect_options opts) -> task<std::expected<void, std::string>>;

    /// Check if connected
    [[nodiscard]] auto is_connected() const noexcept -> bool;

    /// Close connection
    void close() noexcept;

    // ── Non-streaming chat ────────────────────────────────────────────

    /// Non-streaming Chat Completions — Send request, wait for complete response
    auto chat(chat_request req)
        -> task<std::expected<chat_response, std::string>>;

    // ── Streaming chat (SSE) ───────────────────────────────────────

    /// Streaming Chat Completions — SSE chunk-by-chunk callback, returns complete concatenated result
    auto chat_stream(chat_request req, on_chunk_fn on_chunk)
        -> task<std::expected<std::string, std::string>>;

    // ── Streaming chat (async callback) ──────────────────────────

    /// Async callback type: (chunk) -> task<bool>, return false to abort stream
    using async_chunk_fn = std::function<task<bool>(const chat_chunk&)>;

    /// Streaming Chat Completions — Async callback version, supports co_await in callback
    auto chat_stream_async(chat_request req, async_chunk_fn on_chunk)
        -> task<std::expected<std::string, std::string>>;

    // ── Models API ─────────────────────────────────────

    /// List available models
    auto list_models()
        -> task<std::expected<std::vector<model_info>, std::string>>;

    // ── Embeddings API ─────────────────────────────────────

    /// Create embedding vectors
    auto embeddings(embedding_request req)
        -> task<std::expected<embedding_response, std::string>>;

    // ── TTS (Text-to-Speech) API ────────────────────────────

    /// Text to speech — Returns audio binary data
    auto text_to_speech(tts_request req)
        -> task<std::expected<std::vector<std::byte>, std::string>>;

    // ── STT (Speech-to-Text / Whisper) API ────────────────

    /// Speech to text (Transcription)
    auto transcribe(transcription_request req)
        -> task<std::expected<transcription_response, std::string>>;

    /// Speech translation to English (Translation)
    auto translate(translation_request req)
        -> task<std::expected<transcription_response, std::string>>;

    // ── DALL-E (Image Generation) API ─────────────────────

    /// Generate image (DALL-E 2/3)
    auto create_image(image_generation_request req)
        -> task<std::expected<image_response, std::string>>;

    /// Edit image (DALL-E 2)
    auto edit_image(image_edit_request req)
        -> task<std::expected<image_response, std::string>>;

    /// Image variation (DALL-E 2)
    auto create_image_variation(image_variation_request req)
        -> task<std::expected<image_response, std::string>>;

    // ── Moderation API ───────────────────────────────────

    /// Content moderation
    auto moderate(moderation_request req)
        -> task<std::expected<moderation_response, std::string>>;

private:
    // ── Transport layer ──

    auto do_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;

    auto do_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;

    auto do_read_some() -> task<std::optional<std::string>>;

    // ── Path / Common headers ──

    [[nodiscard]] auto build_path(std::string_view suffix) const -> std::string;

    void apply_common_headers(http::request& req, std::string_view accept);

    // ── Auto reconnect ──

    auto ensure_connected() -> task<std::expected<void, std::string>>;

    // ── HTTP request/response helpers ──

    auto send_http_request(const http::request& req)
        -> task<std::expected<void, std::string>>;

    /// Read complete HTTP response (non-streaming)
    auto read_full_response()
        -> task<std::expected<std::pair<int, std::string>, std::string>>;

    /// Read HTTP response header (for streaming - only read until header ends)
    auto read_response_header()
        -> task<std::expected<std::pair<int, std::string>, std::string>>;

    /// Read remaining body (for error responses)
    auto read_remaining_body() -> task<std::string>;

    /// Read binary response (for TTS)
    auto read_binary_response()
        -> task<std::expected<std::pair<int, std::vector<std::byte>>, std::string>>;

    // ── Multipart form building helpers ──

    [[nodiscard]] static auto generate_boundary() -> std::string;

    static void append_form_field(std::string& body, std::string_view boundary,
                                  std::string_view name, std::string_view value);

    static void append_form_file(std::string& body, std::string_view boundary,
                                  std::string_view name, std::string_view filename,
                                  std::string_view content_type, const std::vector<std::byte>& data);

    [[nodiscard]] static auto get_audio_content_type(std::string_view filename) -> std::string;

    [[nodiscard]] auto build_multipart_form(const transcription_request& req)
        -> std::pair<std::string, std::string>;

    [[nodiscard]] auto build_translation_form(const translation_request& req)
        -> std::pair<std::string, std::string>;

    [[nodiscard]] auto build_image_edit_form(const image_edit_request& req)
        -> std::pair<std::string, std::string>;

    [[nodiscard]] auto build_image_variation_form(const image_variation_request& req)
        -> std::pair<std::string, std::string>;

    // ── Members ──

    io_context&     ctx_;
    socket          sock_;
    connect_options opts_;
    http::url       url_;
    std::string     rbuf_;          // Read buffer
    bool            connected_ = false;

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream>  ssl_;
#endif
};

} // namespace cnetmod::openai
