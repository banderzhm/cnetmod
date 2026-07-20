export module cnetmod.protocol.http.v2.huffman;

import std;

export namespace cnetmod::http::v2 {

[[nodiscard]] auto huffman_decode(std::span<const std::byte> input)
    -> std::expected<std::string, std::error_code>;

[[nodiscard]] auto huffman_encode(std::string_view input)
    -> std::vector<std::byte>;

} // namespace cnetmod::http::v2