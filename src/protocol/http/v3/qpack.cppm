module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http.v3.qpack;

import std;
import cnetmod.core.buffer;

namespace cnetmod::http::v3 {

export struct header_field
{
    std::string name;
    std::string value;
};

/// A header block that was held until its Required Insert Count became
/// available.  It is emitted exactly once by take_completed_header_blocks().
export struct qpack_decoded_header_block
{
    std::uint64_t stream_id{};
    std::vector<header_field> headers;
};

/// RFC 9204 header-block encoder and encoder-stream producer.
export class qpack_encoder
{
public:
    explicit qpack_encoder(std::uint64_t capacity = 0) noexcept;
    ~qpack_encoder();
    qpack_encoder(qpack_encoder&&) noexcept;
    auto operator=(qpack_encoder&&) noexcept -> qpack_encoder&;
    qpack_encoder(const qpack_encoder&) = delete;
    auto operator=(const qpack_encoder&) -> qpack_encoder& = delete;

    /// Encodes one request or response header block.  The caller must send
    /// pending bytes from take_encoder_instructions() on the QPACK encoder
    /// stream before allowing the peer to consume a block using dynamic data.
    [[nodiscard]] auto encode(std::span<const header_field> headers, std::uint64_t stream_id = 0)
        -> std::expected<byte_buffer, std::error_code>;

    /// Returns and clears instructions queued for the encoder stream.
    [[nodiscard]] auto take_encoder_instructions() -> byte_buffer;

    /// Consumes decoder-stream acknowledgements, cancellations and insert
    /// count increments.
    [[nodiscard]] auto process_decoder_instructions(byte_view data)
        -> std::expected<void, std::error_code>;
    void set_max_table_capacity(std::uint64_t capacity) noexcept;
    /// Applies the peer's SETTINGS_QPACK_BLOCKED_STREAMS limit.
    void set_max_blocked_streams(std::uint64_t maximum) noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

export class qpack_decoder
{
public:
    explicit qpack_decoder(std::uint64_t initial_max_table_capacity = 0);
    ~qpack_decoder();
    qpack_decoder(qpack_decoder&&) noexcept;
    auto operator=(qpack_decoder&&) noexcept -> qpack_decoder&;
    qpack_decoder(const qpack_decoder&) = delete;
    auto operator=(const qpack_decoder&) -> qpack_decoder& = delete;

    /// Consumes RFC 9204 encoder-stream instructions before decoding header
    /// blocks that reference dynamic entries.
    [[nodiscard]] auto process_encoder_instructions(byte_view data)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto decode(byte_view encoded, std::uint64_t stream_id = 0)
        -> std::expected<std::vector<header_field>, std::error_code>;
    [[nodiscard]] auto lookup_by_name_value(std::string_view name, std::string_view value)
        -> std::optional<std::uint16_t>;
    [[nodiscard]] auto lookup_by_name(std::string_view name) -> std::vector<std::uint16_t>;
    [[nodiscard]] auto get_dynamic_table_size() const noexcept -> std::size_t;

    /// Returns and clears decoder-stream acknowledgements generated while
    /// decoding successfully processed header blocks.
    [[nodiscard]] auto take_decoder_instructions() -> byte_buffer;
    /// Retrieves header blocks whose Required Insert Count became available
    /// after process_encoder_instructions().
    [[nodiscard]] auto take_completed_header_blocks() -> std::vector<qpack_decoded_header_block>;
    /// Applies this endpoint's SETTINGS_QPACK_MAX_TABLE_CAPACITY limit to the
    /// peer encoder stream. The peer may select any current capacity up to it.
    void set_max_table_capacity(std::uint64_t capacity) noexcept;
    /// Applies this endpoint's SETTINGS_QPACK_BLOCKED_STREAMS limit.
    void set_max_blocked_streams(std::uint64_t maximum) noexcept;
    void cancel_stream(std::uint64_t stream_id);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

export auto is_sensitive_header(std::string_view name) noexcept -> bool;
export auto normalize_method(std::string_view method) -> std::string;

} // namespace cnetmod::http::v3
