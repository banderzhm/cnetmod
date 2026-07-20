export module cnetmod.protocol.http:multipart_url;
import std;
export namespace cnetmod::http {
[[nodiscard]] auto url_decode(std::string_view input, bool plus_as_space = true)
    -> std::string;
[[nodiscard]] auto url_encode(std::string_view input) -> std::string;
} // namespace cnetmod::http
