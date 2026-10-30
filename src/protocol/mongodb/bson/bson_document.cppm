export module cnetmod.protocol.mongodb:bson_document;

import std;
import :error;

export namespace cnetmod::mongodb {

class bson_value;
class bson_document;
using bson_array = std::vector<bson_value>;

struct bson_null
{
};

struct bson_undefined
{
};

struct bson_binary
{
    std::uint8_t subtype = 0;
    std::vector<std::byte> bytes;
};

struct bson_object_id
{
    std::array<std::byte, 12> bytes{};
};

struct bson_datetime
{
    std::int64_t milliseconds_since_epoch = 0;
};

struct bson_timestamp
{
    std::uint32_t increment = 0;
    std::uint32_t seconds = 0;
};

struct bson_regex
{
    std::string pattern;
    std::string options;
};

struct bson_javascript_code
{
    std::string source;
};

struct bson_javascript_code_with_scope
{
    std::string source;
    std::shared_ptr<bson_document> scope;
};

struct bson_symbol
{
    std::string name;
};

struct bson_db_pointer
{
    std::string name_space;
    bson_object_id object_id;
};

struct bson_decimal128
{
    // IEEE 754-2008 decimal128 payload in MongoDB wire little-endian order.
    std::array<std::byte, 16> bytes{};
};

struct bson_min_key
{
};

struct bson_max_key
{
};

class bson_value
{
public:
    using storage = std::variant<bson_null, bson_undefined, double, std::string,
        std::shared_ptr<bson_document>, std::shared_ptr<bson_array>, bson_binary,
        bson_object_id, bool, bson_datetime, bson_timestamp, bson_regex,
        bson_javascript_code, bson_javascript_code_with_scope, bson_symbol,
        bson_db_pointer, bson_decimal128,
        bson_min_key, bson_max_key, std::int32_t, std::int64_t>;

    bson_value() noexcept;
    bson_value(std::nullptr_t) noexcept;
    bson_value(bson_undefined value) noexcept;
    bson_value(double value) noexcept;
    bson_value(std::string value);
    bson_value(std::string_view value);
    bson_value(const char* value);
    bson_value(bson_document value);
    bson_value(bson_array value);
    bson_value(bson_binary value);
    bson_value(bson_object_id value) noexcept;
    bson_value(bool value) noexcept;
    bson_value(bson_datetime value) noexcept;
    bson_value(bson_timestamp value) noexcept;
    bson_value(bson_regex value);
    bson_value(bson_javascript_code value);
    bson_value(bson_javascript_code_with_scope value);
    bson_value(bson_symbol value);
    bson_value(bson_db_pointer value);
    bson_value(bson_decimal128 value) noexcept;
    bson_value(bson_min_key value) noexcept;
    bson_value(bson_max_key value) noexcept;
    bson_value(std::int32_t value) noexcept;
    bson_value(std::int64_t value) noexcept;

    [[nodiscard]] auto data() const noexcept -> const storage&;
    [[nodiscard]] auto data() noexcept -> storage&;

    template <class T>
    [[nodiscard]] auto get_if() const noexcept -> const T*
    {
        return std::get_if<T>(&data_);
    }

    [[nodiscard]] auto as_document() const noexcept -> const bson_document*;
    [[nodiscard]] auto as_array() const noexcept -> const bson_array*;

private:
    storage data_;
};

class bson_document
{
public:
    using element = std::pair<std::string, bson_value>;

    bson_document() = default;
    bson_document(std::initializer_list<element> fields);

    auto append(std::string key, bson_value value) -> bson_document&;
    auto set(std::string key, bson_value value) -> bson_document&;
    [[nodiscard]] auto find(std::string_view key) const noexcept
        -> const bson_value*;
    [[nodiscard]] auto contains(std::string_view key) const noexcept -> bool;
    [[nodiscard]] auto elements() const noexcept -> const std::vector<element>&;
    [[nodiscard]] auto size() const noexcept -> std::size_t;

private:
    std::vector<element> fields_;
};

struct bson_limits
{
    std::size_t max_document_bytes = 16 * 1024 * 1024;
    std::size_t max_nesting_depth = 100;
    std::size_t max_string_bytes = 16 * 1024 * 1024;
};

auto encode_bson_document(const bson_document& document,
    bson_limits limits = {}) -> result<std::vector<std::byte>>;
auto decode_bson_document(std::span<const std::byte> bytes,
    bson_limits limits = {}) -> result<bson_document>;

} // namespace cnetmod::mongodb
