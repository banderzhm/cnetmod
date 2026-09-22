/// cnetmod.protocol.openai client — multipart request construction

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

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
import :client;
import :audio;
import :images;
import cnetmod.json;

namespace cnetmod::openai {

[[nodiscard]] auto client::generate_boundary() -> std::string
{
    static std::atomic<std::uint64_t> counter{0};
    auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::format("----CnetmodBoundary{:016x}{:04x}", ts,
        counter.fetch_add(1));
}

void client::append_form_field(std::string& body, std::string_view boundary,
    std::string_view name, std::string_view value)
{
    body += "--";
    body += boundary;
    body += "\r\n";
    body +=
        std::format("Content-Disposition: form-data; name=\"{}\"\r\n\r\n", name);
    body += value;
    body += "\r\n";
}

void client::append_form_file(std::string& body, std::string_view boundary,
    std::string_view name, std::string_view filename,
    std::string_view content_type,
    const std::vector<std::byte>& data)
{
    body += "--";
    body += boundary;
    body += "\r\n";
    body += std::format(
        "Content-Disposition: form-data; name=\"{}\"; filename=\"{}\"\r\n", name,
        filename);
    body += std::format("Content-Type: {}\r\n\r\n", content_type);
    body.append(reinterpret_cast<const char*>(data.data()), data.size());
    body += "\r\n";
}

[[nodiscard]] auto client::get_audio_content_type(std::string_view filename)
    -> std::string
{
    if (filename.ends_with(".mp3"))
        return "audio/mpeg";
    if (filename.ends_with(".mp4"))
        return "audio/mp4";
    if (filename.ends_with(".m4a"))
        return "audio/mp4";
    if (filename.ends_with(".wav"))
        return "audio/wav";
    if (filename.ends_with(".webm"))
        return "audio/webm";
    if (filename.ends_with(".ogg"))
        return "audio/ogg";
    if (filename.ends_with(".flac"))
        return "audio/flac";
    return "application/octet-stream";
}

[[nodiscard]] auto
client::build_multipart_form(const transcription_request& req)
    -> std::pair<std::string, std::string>
{
    auto boundary = generate_boundary();
    std::string body;
    body.reserve(req.file.size() + 1024);

    append_form_file(body, boundary, "file", req.filename,
        get_audio_content_type(req.filename), req.file);

    append_form_field(body, boundary, "model", req.model);

    if (!req.language.empty())
    {
        append_form_field(body, boundary, "language", req.language);
    }
    if (!req.prompt.empty())
    {
        append_form_field(body, boundary, "prompt", req.prompt);
    }
    if (req.response_format != "json")
    {
        append_form_field(body, boundary, "response_format", req.response_format);
    }
    if (req.temperature != 0.0)
    {
        append_form_field(body, boundary, "temperature",
            std::format("{}", req.temperature));
    }

    body += "--";
    body += boundary;
    body += "--\r\n";

    return {boundary, body};
}

[[nodiscard]] auto
client::build_translation_form(const translation_request& req)
    -> std::pair<std::string, std::string>
{
    auto boundary = generate_boundary();
    std::string body;
    body.reserve(req.file.size() + 1024);

    append_form_file(body, boundary, "file", req.filename,
        get_audio_content_type(req.filename), req.file);
    append_form_field(body, boundary, "model", req.model);

    if (!req.prompt.empty())
    {
        append_form_field(body, boundary, "prompt", req.prompt);
    }
    if (req.response_format != "json")
    {
        append_form_field(body, boundary, "response_format", req.response_format);
    }
    if (req.temperature != 0.0)
    {
        append_form_field(body, boundary, "temperature",
            std::format("{}", req.temperature));
    }

    body += "--";
    body += boundary;
    body += "--\r\n";

    return {boundary, body};
}

[[nodiscard]] auto client::build_image_edit_form(const image_edit_request& req)
    -> std::pair<std::string, std::string>
{
    auto boundary = generate_boundary();
    std::string body;
    body.reserve(req.image.size() + req.mask.size() + 2048);

    append_form_file(body, boundary, "image", req.image_filename, "image/png",
        req.image);

    if (!req.mask.empty())
    {
        append_form_file(body, boundary, "mask", req.mask_filename, "image/png",
            req.mask);
    }

    append_form_field(body, boundary, "prompt", req.prompt);

    if (req.model != "dall-e-2")
    {
        append_form_field(body, boundary, "model", req.model);
    }
    if (req.n != 1)
    {
        append_form_field(body, boundary, "n", std::to_string(req.n));
    }
    if (req.size != "1024x1024")
    {
        append_form_field(body, boundary, "size", req.size);
    }
    if (req.response_format != "url")
    {
        append_form_field(body, boundary, "response_format", req.response_format);
    }
    if (!req.user.empty())
    {
        append_form_field(body, boundary, "user", req.user);
    }

    body += "--";
    body += boundary;
    body += "--\r\n";

    return {boundary, body};
}

[[nodiscard]] auto
client::build_image_variation_form(const image_variation_request& req)
    -> std::pair<std::string, std::string>
{
    auto boundary = generate_boundary();
    std::string body;
    body.reserve(req.image.size() + 1024);

    append_form_file(body, boundary, "image", req.image_filename, "image/png",
        req.image);

    if (req.model != "dall-e-2")
    {
        append_form_field(body, boundary, "model", req.model);
    }
    if (req.n != 1)
    {
        append_form_field(body, boundary, "n", std::to_string(req.n));
    }
    if (req.size != "1024x1024")
    {
        append_form_field(body, boundary, "size", req.size);
    }
    if (req.response_format != "url")
    {
        append_form_field(body, boundary, "response_format", req.response_format);
    }
    if (!req.user.empty())
    {
        append_form_field(body, boundary, "user", req.user);
    }

    body += "--";
    body += boundary;
    body += "--\r\n";

    return {boundary, body};
}

} // namespace cnetmod::openai
