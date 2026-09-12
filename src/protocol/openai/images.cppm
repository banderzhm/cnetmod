/// cnetmod.protocol.openai:images — Image generation and editing contracts

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:images;

import std;
import :foundation;

namespace cnetmod::openai {

export struct image_generation_request
{
    std::string model = "dall-e-3";
    std::string prompt;
    int n = 1;
    std::string quality = "standard";
    std::string response_format = "url";
    std::string size = "1024x1024";
    std::string style = "vivid";
    std::string user;

    [[nodiscard]] auto to_json() const -> std::string;
};

export struct image_edit_request
{
    std::vector<std::byte> image;
    std::string image_filename = "image.png";
    std::vector<std::byte> mask;
    std::string mask_filename = "mask.png";
    std::string prompt;
    std::string model = "dall-e-2";
    int n = 1;
    std::string size = "1024x1024";
    std::string response_format = "url";
    std::string user;
};

export struct image_variation_request
{
    std::vector<std::byte> image;
    std::string image_filename = "image.png";
    std::string model = "dall-e-2";
    int n = 1;
    std::string size = "1024x1024";
    std::string response_format = "url";
    std::string user;
};

export struct generated_image
{
    std::string url;
    std::string b64_json;
    std::string revised_prompt;
};

export struct image_response
{
    std::int64_t created = 0;
    std::vector<generated_image> data;

    [[nodiscard]] static auto from_json(std::string_view text) -> image_response;
};

} // namespace cnetmod::openai
