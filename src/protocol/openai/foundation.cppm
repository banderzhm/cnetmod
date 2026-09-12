/// cnetmod.protocol.openai:foundation — Shared protocol foundations

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:foundation;

import std;
import nlohmann.json;

namespace cnetmod::openai {

export using json = nlohmann::json;

export struct usage
{
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
};

export struct error_response
{
    std::string message;
    std::string type;
    std::string code;

    [[nodiscard]] static auto from_json(std::string_view text) -> error_response;
};

export struct model_info
{
    std::string id;
    std::string owned_by;
    int created = 0;
};

export struct connect_options
{
    std::string api_base = "https://api.openai.com/v1";
    std::string api_key;
    bool tls_verify = true;
    std::string tls_ca_file;
    int timeout_seconds = 120;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};

} // namespace cnetmod::openai
