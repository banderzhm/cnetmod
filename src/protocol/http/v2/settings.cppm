export module cnetmod.protocol.http.v2.settings;

import std;

export namespace cnetmod::http::v2 {

enum class setting_id : std::uint16_t {
    header_table_size = 0x1,
    enable_push = 0x2,
    max_concurrent_streams = 0x3,
    initial_window_size = 0x4,
    max_frame_size = 0x5,
    max_header_list_size = 0x6,
};

struct settings {
    std::uint32_t header_table_size = 4096;
    bool enable_push = false;
    std::uint32_t max_concurrent_streams = 100;
    std::uint32_t initial_window_size = 65'535;
    std::uint32_t max_frame_size = 16'384;
    std::uint32_t max_header_list_size = 65'536;
};

[[nodiscard]] auto decode_settings(std::span<const std::byte> payload, settings& target)
    -> std::error_code;
[[nodiscard]] auto encode_settings(const settings& value) -> std::vector<std::byte>;

} // namespace cnetmod::http::v2
