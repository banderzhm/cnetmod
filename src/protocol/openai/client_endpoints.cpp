/// cnetmod.protocol.openai client — models, embeddings, audio, image and moderation endpoints

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
import :foundation;
import :embeddings;
import :audio;
import :images;
import :moderation;
import nlohmann.json;

namespace cnetmod::openai {

auto client::list_models()
    -> task<std::expected<std::vector<model_info>, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    http::request http_req(http::http_method::GET, build_path("/models"));
    apply_common_headers(http_req, "application/json");

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    std::vector<model_info> models;
    auto j = nlohmann::json::parse(resp_body, nullptr, false);
    if (!j.is_discarded() && j.contains("data") && j["data"].is_array())
    {
        for (auto& m : j["data"])
        {
            model_info info;
            info.id = m.value("id", "");
            info.owned_by = m.value("owned_by", "");
            info.created = m.value("created", 0);
            models.push_back(std::move(info));
        }
    }

    co_return models;
}

auto client::embeddings(embedding_request req)
    -> task<std::expected<embedding_response, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    auto body = req.to_json();

    http::request http_req(http::http_method::POST, build_path("/embeddings"));
    apply_common_headers(http_req, "application/json");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return embedding_response::from_json(resp_body);
}

auto client::text_to_speech(tts_request req)
    -> task<std::expected<std::vector<std::byte>, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    auto body = req.to_json();

    http::request http_req(http::http_method::POST, build_path("/audio/speech"));
    apply_common_headers(http_req, "audio/mpeg");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_binary_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, audio_data] = *resp_r;
    if (status != 200)
    {
        std::string err_text(reinterpret_cast<const char*>(audio_data.data()),
            std::min(audio_data.size(), std::size_t{1024}));
        auto err = error_response::from_json(err_text);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return audio_data;
}

auto client::transcribe(transcription_request req)
    -> task<std::expected<transcription_response, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    auto [boundary, body] = build_multipart_form(req);

    http::request http_req(http::http_method::POST,
        build_path("/audio/transcriptions"));
    http_req.set_header("Host", url_.host);
    http_req.set_header("Authorization", "Bearer " + opts_.api_key);
    http_req.set_header("Content-Type",
        "multipart/form-data; boundary=" + boundary);
    http_req.set_header("Connection", "keep-alive");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return transcription_response::from_json(resp_body);
}

auto client::translate(translation_request req)
    -> task<std::expected<transcription_response, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    auto [boundary, body] = build_translation_form(req);

    http::request http_req(http::http_method::POST,
        build_path("/audio/translations"));
    http_req.set_header("Host", url_.host);
    http_req.set_header("Authorization", "Bearer " + opts_.api_key);
    http_req.set_header("Content-Type",
        "multipart/form-data; boundary=" + boundary);
    http_req.set_header("Connection", "keep-alive");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return transcription_response::from_json(resp_body);
}

auto client::create_image(image_generation_request req)
    -> task<std::expected<image_response, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    auto body = req.to_json();

    http::request http_req(http::http_method::POST,
        build_path("/images/generations"));
    apply_common_headers(http_req, "application/json");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return image_response::from_json(resp_body);
}

auto client::edit_image(image_edit_request req)
    -> task<std::expected<image_response, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    auto [boundary, body] = build_image_edit_form(req);

    http::request http_req(http::http_method::POST, build_path("/images/edits"));
    http_req.set_header("Host", url_.host);
    http_req.set_header("Authorization", "Bearer " + opts_.api_key);
    http_req.set_header("Content-Type",
        "multipart/form-data; boundary=" + boundary);
    http_req.set_header("Connection", "keep-alive");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return image_response::from_json(resp_body);
}

auto client::create_image_variation(image_variation_request req)
    -> task<std::expected<image_response, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    auto [boundary, body] = build_image_variation_form(req);

    http::request http_req(http::http_method::POST,
        build_path("/images/variations"));
    http_req.set_header("Host", url_.host);
    http_req.set_header("Authorization", "Bearer " + opts_.api_key);
    http_req.set_header("Content-Type",
        "multipart/form-data; boundary=" + boundary);
    http_req.set_header("Connection", "keep-alive");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return image_response::from_json(resp_body);
}

auto client::moderate(moderation_request req)
    -> task<std::expected<moderation_response, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    auto body = req.to_json();

    http::request http_req(http::http_method::POST, build_path("/moderations"));
    apply_common_headers(http_req, "application/json");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return moderation_response::from_json(resp_body);
}

} // namespace cnetmod::openai
