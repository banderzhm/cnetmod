export module cnetmod.protocol.http.v2.header_compression;

import std;

export namespace cnetmod::http::v2 {

struct header_field {
    std::string name;
    std::string value;
    bool sensitive = false;
};

/// RFC 7541 HPACK codec.  One instance is owned by each HTTP/2 direction.
class header_compression {
public:
    explicit header_compression(std::size_t dynamic_table_limit = 4096);

    [[nodiscard]] auto decode(std::span<const std::byte> block)
        -> std::expected<std::vector<header_field>, std::error_code>;
    [[nodiscard]] auto encode(std::span<const header_field> fields)
        -> std::expected<std::vector<std::byte>, std::error_code>;

    void set_dynamic_table_limit(std::size_t bytes);
    [[nodiscard]] auto dynamic_table_limit() const noexcept -> std::size_t;

private:
    struct entry { std::string name; std::string value; };
    void evict();
    void insert(entry item);
    [[nodiscard]] auto at(std::uint32_t index) const -> std::optional<entry>;
    [[nodiscard]] auto find(std::string_view name, std::string_view value,
                            bool exact) const -> std::uint32_t;
    std::deque<entry> dynamic_table_;
    std::size_t dynamic_table_bytes_ = 0;
    std::size_t dynamic_table_limit_ = 4096;
};

} // namespace cnetmod::http::v2
