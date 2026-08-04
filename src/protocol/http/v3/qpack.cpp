module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http.v3.qpack;

import std;
import cnetmod.core.buffer;
import cnetmod.protocol.http.v2.huffman;
import cnetmod.utils.flat_map;

namespace cnetmod::http::v3 {
namespace detail {
    struct static_entry
    {
        std::string_view name;
        std::string_view value;
    };

    // RFC 9204 Appendix A; wire indices are zero-based.
    constexpr std::array<static_entry, 99> static_table{{
        {":authority", ""},
        {":path", "/"},
        {"age", "0"},
        {"content-disposition", ""},
        {"content-length", "0"},
        {"cookie", ""},
        {"date", ""},
        {"etag", ""},
        {"if-modified-since", ""},
        {"if-none-match", ""},
        {"last-modified", ""},
        {"link", ""},
        {"location", ""},
        {"referer", ""},
        {"set-cookie", ""},
        {":method", "CONNECT"},
        {":method", "DELETE"},
        {":method", "GET"},
        {":method", "HEAD"},
        {":method", "OPTIONS"},
        {":method", "POST"},
        {":method", "PUT"},
        {":scheme", "http"},
        {":scheme", "https"},
        {":status", "103"},
        {":status", "200"},
        {":status", "304"},
        {":status", "404"},
        {":status", "503"},
        {"accept", "*/*"},
        {"accept", "application/dns-message"},
        {"accept-encoding", "gzip, deflate, br"},
        {"accept-ranges", "bytes"},
        {"access-control-allow-headers", "cache-control"},
        {"access-control-allow-headers", "content-type"},
        {"access-control-allow-origin", "*"},
        {"cache-control", "max-age=0"},
        {"cache-control", "max-age=2592000"},
        {"cache-control", "max-age=604800"},
        {"cache-control", "no-cache"},
        {"cache-control", "no-store"},
        {"cache-control", "public, max-age=31536000"},
        {"content-encoding", "br"},
        {"content-encoding", "gzip"},
        {"content-type", "application/dns-message"},
        {"content-type", "application/javascript"},
        {"content-type", "application/json"},
        {"content-type", "application/x-www-form-urlencoded"},
        {"content-type", "image/gif"},
        {"content-type", "image/jpeg"},
        {"content-type", "image/png"},
        {"content-type", "text/css"},
        {"content-type", "text/html; charset=utf-8"},
        {"content-type", "text/plain"},
        {"content-type", "text/plain;charset=utf-8"},
        {"range", "bytes=0-"},
        {"strict-transport-security", "max-age=31536000"},
        {"strict-transport-security", "max-age=31536000; includesubdomains"},
        {"strict-transport-security", "max-age=31536000; includesubdomains; preload"},
        {"vary", "accept-encoding"},
        {"vary", "origin"},
        {"x-content-type-options", "nosniff"},
        {"x-xss-protection", "1; mode=block"},
        {":status", "100"},
        {":status", "204"},
        {":status", "206"},
        {":status", "302"},
        {":status", "400"},
        {":status", "403"},
        {":status", "421"},
        {":status", "425"},
        {":status", "500"},
        {"accept-language", ""},
        {"access-control-allow-credentials", "FALSE"},
        {"access-control-allow-credentials", "TRUE"},
        {"access-control-allow-headers", "*"},
        {"access-control-allow-methods", "get"},
        {"access-control-allow-methods", "get, post, options"},
        {"access-control-allow-methods", "options"},
        {"access-control-expose-headers", "content-length"},
        {"access-control-request-headers", "content-type"},
        {"access-control-request-method", "get"},
        {"access-control-request-method", "post"},
        {"alt-svc", "clear"},
        {"authorization", ""},
        {"content-security-policy", "script-src 'none'; object-src 'none'; base-uri 'none'"},
        {"early-data", "1"},
        {"expect-ct", ""},
        {"forwarded", ""},
        {"if-range", ""},
        {"origin", ""},
        {"purpose", "prefetch"},
        {"server", ""},
        {"timing-allow-origin", "*"},
        {"upgrade-insecure-requests", "1"},
        {"user-agent", ""},
        {"x-forwarded-for", ""},
        {"x-frame-options", "deny"},
        {"x-frame-options", "sameorigin"},
    }};

    using static_name_index = cnetmod::flat_map<std::string_view,
        std::vector<std::uint16_t>, std::less<>>;

    [[nodiscard]] auto static_entries_by_name() -> const static_name_index&
    {
        static const auto index = []
        {
            static_name_index result;
            result.reserve(static_table.size());
            for (std::uint16_t wire_index{};
                wire_index < static_table.size(); ++wire_index)
                result[static_table[wire_index].name].push_back(wire_index);
            return result;
        }();
        return index;
    }

    auto error() -> std::error_code
    {
        return std::make_error_code(std::errc::bad_message);
    }

    auto append_int(byte_buffer& out, std::uint8_t prefix, std::uint8_t bits, std::uint64_t value) -> void
    {
        const auto lim = (std::uint64_t{1} << bits) - 1U;
        if (value < lim)
        {
            out.push_back(static_cast<std::byte>(prefix | value));
            return;
        }
        out.push_back(static_cast<std::byte>(prefix | lim));
        value -= lim;
        while (value >= 128U)
        {
            out.push_back(static_cast<std::byte>((value & 0x7fU) | 0x80U));
            value >>= 7U;
        }
        out.push_back(static_cast<std::byte>(value));
    }

    auto read_int(byte_view& in, std::uint8_t bits) -> std::expected<std::uint64_t, std::error_code>
    {
        if (in.empty())
            return std::unexpected(error());
        const auto lim = (std::uint8_t{1} << bits) - 1U;
        std::uint64_t v = std::to_integer<std::uint8_t>(in.front()) & lim;
        in = in.subspan(1);
        if (v < lim)
            return v;
        std::uint32_t shift{};
        for (;;)
        {
            if (in.empty() || shift >= 63U)
                return std::unexpected(error());
            auto b = std::to_integer<std::uint8_t>(in.front());
            in = in.subspan(1);
            v += static_cast<std::uint64_t>(b & 0x7fU) << shift;
            if (!(b & 0x80U))
                return v;
            shift += 7U;
        }
    }

    auto append_string(byte_buffer& out, std::string_view s) -> void
    {
        auto huffman = cnetmod::http::v2::huffman_encode(s);
        if (huffman.size() < s.size())
        {
            append_int(out, 0x80U, 7, huffman.size());
            out.insert(out.end(), huffman.begin(), huffman.end());
            return;
        }
        append_int(out, 0, 7, s.size());
        for (const auto c : s)
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }

    auto read_string(byte_view& in) -> std::expected<std::string, std::error_code>
    {
        if (in.empty())
            return std::unexpected(error());
        const bool huffman = (std::to_integer<std::uint8_t>(in.front()) & 0x80U) != 0U;
        auto n = read_int(in, 7);
        if (!n || *n > in.size())
            return std::unexpected(error());
        if (huffman)
        {
            auto result = cnetmod::http::v2::huffman_decode(in.first(*n));
            in = in.subspan(*n);
            return result;
        }
        std::string s;
        s.reserve(*n);
        for (std::size_t i{}; i < *n; ++i)
            s.push_back(static_cast<char>(std::to_integer<unsigned char>(in[i])));
        in = in.subspan(*n);
        return s;
    }

    auto prefixed_integer_size(byte_view input,
        std::uint8_t prefix_bits)
        -> std::expected<std::size_t, std::error_code>
    {
        if (input.empty())
            return std::unexpected(
                std::make_error_code(std::errc::message_size));
        const auto prefix_max = (std::uint8_t{1} << prefix_bits) - 1U;
        if ((std::to_integer<std::uint8_t>(input.front()) & prefix_max) <
            prefix_max)
            return 1U;

        std::size_t offset = 1U;
        std::uint32_t shift{};
        while (offset < input.size())
        {
            const auto byte = std::to_integer<std::uint8_t>(input[offset++]);
            if ((byte & 0x80U) == 0U)
                return offset;
            shift += 7U;
            if (shift >= 63U)
                return std::unexpected(error());
        }
        return std::unexpected(std::make_error_code(std::errc::message_size));
    }

    auto string_literal_size(byte_view input,
        std::uint8_t prefix_bits = 7U)
        -> std::expected<std::size_t, std::error_code>
    {
        auto integer_size = prefixed_integer_size(input, prefix_bits);
        if (!integer_size)
            return std::unexpected(integer_size.error());
        auto copy = input;
        auto length = read_int(copy, prefix_bits);
        if (!length)
            return std::unexpected(length.error());
        if (*length > copy.size())
            return std::unexpected(
                std::make_error_code(std::errc::message_size));
        return *integer_size + static_cast<std::size_t>(*length);
    }

    auto decoder_instruction_size(byte_view input)
        -> std::expected<std::size_t, std::error_code>
    {
        if (input.empty())
            return std::unexpected(
                std::make_error_code(std::errc::message_size));
        const auto first = std::to_integer<std::uint8_t>(input.front());
        return prefixed_integer_size(input,
            (first & 0x80U) != 0U ? 7U : 6U);
    }

    auto encoder_instruction_size(byte_view input)
        -> std::expected<std::size_t, std::error_code>
    {
        if (input.empty())
            return std::unexpected(
                std::make_error_code(std::errc::message_size));
        const auto first = std::to_integer<std::uint8_t>(input.front());
        if ((first & 0x80U) != 0U)
        {
            auto name_index_size = prefixed_integer_size(input, 6U);
            if (!name_index_size)
                return std::unexpected(name_index_size.error());
            auto value_size = string_literal_size(input.subspan(*name_index_size));
            if (!value_size)
                return std::unexpected(value_size.error());
            return *name_index_size + *value_size;
        }
        if ((first & 0xe0U) == 0x20U)
            return prefixed_integer_size(input, 5U);
        if ((first & 0xc0U) == 0x40U)
        {
            auto name_size_field = prefixed_integer_size(input, 5U);
            if (!name_size_field)
                return std::unexpected(name_size_field.error());
            auto copy = input;
            auto name_size = read_int(copy, 5U);
            if (!name_size)
                return std::unexpected(name_size.error());
            if (*name_size > copy.size())
                return std::unexpected(
                    std::make_error_code(std::errc::message_size));
            const auto value_offset = *name_size_field +
                static_cast<std::size_t>(*name_size);
            auto value_size = string_literal_size(input.subspan(value_offset));
            if (!value_size)
                return std::unexpected(value_size.error());
            return value_offset + *value_size;
        }
        if ((first & 0xe0U) == 0x00U)
            return prefixed_integer_size(input, 5U);
        return std::unexpected(error());
    }

    auto exact(std::string_view n, std::string_view v) -> std::optional<std::uint16_t>
    {
        const auto& index = static_entries_by_name();
        const auto entries = index.find(n);
        if (entries == index.end())
            return std::nullopt;
        for (const auto wire_index : entries->second)
            if (static_table[wire_index].value == v)
                return wire_index;
        return std::nullopt;
    }

    auto name(std::string_view n) -> std::optional<std::uint16_t>
    {
        const auto& index = static_entries_by_name();
        const auto entries = index.find(n);
        return entries == index.end() || entries->second.empty()
            ? std::nullopt
            : std::optional{entries->second.front()};
    }

    struct dynamic_entry
    {
        std::uint64_t absolute;
        header_field field;
        std::size_t size;
    };

    struct dynamic_table
    {
        std::uint64_t capacity{};
        std::size_t size{};
        std::uint64_t insert_count{};
        std::deque<dynamic_entry> entries;

        auto set_capacity(std::uint64_t value) -> bool
        {
            capacity = value;
            while (size > capacity && !entries.empty())
            {
                size -= entries.back().size;
                entries.pop_back();
            }
            return size <= capacity;
        }

        auto insert(header_field field) -> bool
        {
            const auto entry_size = field.name.size() + field.value.size() + 32U;
            if (entry_size > capacity)
            {
                entries.clear();
                size = 0;
                return false;
            }
            while (size + entry_size > capacity && !entries.empty())
            {
                size -= entries.back().size;
                entries.pop_back();
            }
            entries.push_front({++insert_count, std::move(field), entry_size});
            size += entry_size;
            return true;
        }

        [[nodiscard]] auto by_absolute(std::uint64_t absolute) const -> const dynamic_entry*
        {
            for (const auto& entry : entries)
                if (entry.absolute == absolute)
                    return &entry;
            return nullptr;
        }

        [[nodiscard]] auto exact(std::string_view name, std::string_view value) const -> const dynamic_entry*
        {
            for (const auto& entry : entries)
                if (entry.field.name == name && entry.field.value == value)
                    return &entry;
            return nullptr;
        }

        [[nodiscard]] auto by_name(std::string_view name) const -> const dynamic_entry*
        {
            for (const auto& entry : entries)
                if (entry.field.name == name)
                    return &entry;
            return nullptr;
        }
    };

    auto encode_required_insert_count(std::uint64_t required, std::uint64_t capacity) -> std::uint64_t
    {
        if (required == 0U)
            return 0U;
        const auto max_entries = capacity / 32U;
        return max_entries == 0U ? 0U : (required % (2U * max_entries)) + 1U;
    }

    auto decode_required_insert_count(std::uint64_t encoded, std::uint64_t insert_count, std::uint64_t capacity)
        -> std::expected<std::uint64_t, std::error_code>
    {
        if (encoded == 0U)
            return 0U;
        const auto max_entries = capacity / 32U;
        if (max_entries == 0U || encoded > 2U * max_entries)
            return std::unexpected(error());
        const auto full_range = 2U * max_entries;
        const auto max_value = insert_count + max_entries;
        auto required = encoded - 1U + full_range * (max_value / full_range);
        if (required > max_value)
            required -= full_range;
        if (required == 0U)
            return std::unexpected(error());
        return required;
    }
} // namespace detail

struct qpack_encoder::impl
{
    std::uint64_t max_table_capacity{};
    byte_buffer pending_encoder_instructions;
    detail::dynamic_table dynamic_table;
    bool capacity_update_pending{true};
    std::uint64_t acknowledged_insert_count{};
    std::uint64_t max_blocked_streams{std::numeric_limits<std::uint64_t>::max()};
    std::unordered_set<std::uint64_t> cancelled_streams;
    cnetmod::flat_map<std::uint64_t, std::uint64_t> outstanding_streams;
    byte_buffer pending_decoder_stream_input;
};

struct qpack_decoder::impl
{
    std::uint64_t max_table_capacity{};
    byte_buffer pending_decoder_instructions;
    detail::dynamic_table dynamic_table;
    std::uint64_t max_blocked_streams{std::numeric_limits<std::uint64_t>::max()};
    cnetmod::flat_map<std::uint64_t, byte_buffer> blocked_header_blocks;
    std::vector<qpack_decoded_header_block> completed_header_blocks;
    byte_buffer pending_encoder_stream_input;
};

qpack_encoder::qpack_encoder(std::uint64_t capacity) noexcept
    : impl_(std::make_unique<impl>())
{
    impl_->max_table_capacity = capacity;
    impl_->dynamic_table.set_capacity(capacity);
}

qpack_encoder::~qpack_encoder() = default;
qpack_encoder::qpack_encoder(qpack_encoder&&) noexcept = default;
auto qpack_encoder::operator=(qpack_encoder&&) noexcept -> qpack_encoder& = default;

void qpack_encoder::set_max_table_capacity(std::uint64_t capacity) noexcept
{
    impl_->max_table_capacity = capacity;
    impl_->dynamic_table.set_capacity(capacity);
    impl_->capacity_update_pending = true;
}

void qpack_encoder::set_max_blocked_streams(std::uint64_t maximum) noexcept
{
    impl_->max_blocked_streams = maximum;
}

auto qpack_encoder::encode(std::span<const header_field> headers, std::uint64_t stream_id)
    -> std::expected<byte_buffer, std::error_code>
{
    if (impl_->capacity_update_pending)
    {
        detail::append_int(impl_->pending_encoder_instructions, 0x20U, 5, impl_->max_table_capacity);
        impl_->capacity_update_pending = false;
    }

    struct representation
    {
        const header_field* field;
        const detail::dynamic_entry* exact_dynamic{};
        const detail::dynamic_entry* name_dynamic{};
    };

    std::vector<representation> representations;
    representations.reserve(headers.size());
    for (const auto& h : headers)
    {
        if (impl_->max_table_capacity != 0U && impl_->outstanding_streams.empty() && h.name.size() + h.value.size() + 32U <= impl_->max_table_capacity && !is_sensitive_header(h.name) && !detail::exact(h.name, h.value))
        {
            if (const auto static_name = detail::name(h.name))
            {
                detail::append_int(impl_->pending_encoder_instructions, 0xc0U, 6, *static_name);
                detail::append_string(impl_->pending_encoder_instructions, h.value);
            }
            else
            {
                detail::append_int(impl_->pending_encoder_instructions, 0x40U, 5, h.name.size());
                for (const auto c : h.name)
                    impl_->pending_encoder_instructions.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
                detail::append_string(impl_->pending_encoder_instructions, h.value);
            }
            (void)impl_->dynamic_table.insert(h);
        }
        const bool sensitive = is_sensitive_header(h.name);
        representations.push_back({&h, sensitive ? nullptr : impl_->dynamic_table.exact(h.name, h.value), sensitive ? nullptr : impl_->dynamic_table.by_name(h.name)});
    }
    auto required = impl_->dynamic_table.insert_count;
    const auto blocked_streams = std::ranges::count_if(impl_->outstanding_streams, [this](const auto& stream)
        {
            return stream.second > impl_->acknowledged_insert_count;
        });
    const bool may_reference_dynamic = required <= impl_->acknowledged_insert_count || std::cmp_less(blocked_streams, impl_->max_blocked_streams);
    if (!may_reference_dynamic)
        required = 0U;
    byte_buffer out;
    detail::append_int(out, 0, 8, detail::encode_required_insert_count(required, impl_->max_table_capacity));
    detail::append_int(out, 0, 7, 0U);
    for (const auto& representation : representations)
    {
        const auto& h = *representation.field;
        if (required != 0U && representation.exact_dynamic != nullptr)
        {
            detail::append_int(out, 0x80U, 6, required - representation.exact_dynamic->absolute);
        }
        else if (const auto i = detail::exact(h.name, h.value))
            detail::append_int(out, 0xc0U, 6, *i);
        else if (required != 0U && representation.name_dynamic != nullptr)
        {
            detail::append_int(out, 0x40U, 4, required - representation.name_dynamic->absolute);
            detail::append_string(out, h.value);
        }
        else if (const auto i = detail::name(h.name))
        {
            detail::append_int(out, 0x50U, 4, *i);
            detail::append_string(out, h.value);
        }
        else
        {
            detail::append_int(out, 0x20U, 3, h.name.size());
            for (auto c : h.name)
                out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
            detail::append_string(out, h.value);
        }
    }
    if (required > impl_->acknowledged_insert_count)
        impl_->outstanding_streams.insert_or_assign(stream_id, required);
    return out;
}

auto qpack_encoder::take_encoder_instructions() -> byte_buffer
{
    return std::exchange(impl_->pending_encoder_instructions, {});
}

auto qpack_encoder::process_decoder_instructions(byte_view data)
    -> std::expected<void, std::error_code>
{
    impl_->pending_decoder_stream_input.insert(
        impl_->pending_decoder_stream_input.end(), data.begin(), data.end());
    std::size_t consumed{};
    while (consumed < impl_->pending_decoder_stream_input.size())
    {
        auto remaining = impl_->pending_decoder_stream_input.view().subspan(consumed);
        auto instruction_size = detail::decoder_instruction_size(remaining);
        if (!instruction_size)
        {
            if (instruction_size.error() ==
                std::make_error_code(std::errc::message_size))
                break;
            return std::unexpected(instruction_size.error());
        }
        auto instruction = remaining.first(*instruction_size);
        auto cursor = instruction;
        const auto first = std::to_integer<std::uint8_t>(cursor.front());
        if (first & 0x80U)
        {
            auto id = detail::read_int(cursor, 7);
            if (!id)
                return std::unexpected(id.error());
            impl_->outstanding_streams.erase(*id);
        }
        else if ((first & 0xc0U) == 0x40U)
        {
            auto id = detail::read_int(cursor, 6);
            if (!id)
                return std::unexpected(id.error());
            impl_->cancelled_streams.insert(*id);
            impl_->outstanding_streams.erase(*id);
        }
        else if ((first & 0xc0U) == 0x00U)
        {
            auto increment = detail::read_int(cursor, 6);
            if (!increment || *increment == 0U || *increment > impl_->dynamic_table.insert_count - impl_->acknowledged_insert_count)
                return std::unexpected(detail::error());
            impl_->acknowledged_insert_count += *increment;
        }
        else
            return std::unexpected(detail::error());
        consumed += *instruction_size;
    }
    impl_->pending_decoder_stream_input.erase(
        impl_->pending_decoder_stream_input.begin(),
        impl_->pending_decoder_stream_input.begin() +
            static_cast<std::ptrdiff_t>(consumed));
    return {};
}

qpack_decoder::qpack_decoder(std::uint64_t capacity)
    : impl_(std::make_unique<impl>())
{
    impl_->max_table_capacity = capacity;
    impl_->dynamic_table.set_capacity(capacity);
}

qpack_decoder::~qpack_decoder() = default;
qpack_decoder::qpack_decoder(qpack_decoder&&) noexcept = default;
auto qpack_decoder::operator=(qpack_decoder&&) noexcept -> qpack_decoder& = default;

auto qpack_decoder::process_encoder_instructions(byte_view data) -> std::expected<void, std::error_code>
{
    impl_->pending_encoder_stream_input.insert(
        impl_->pending_encoder_stream_input.end(), data.begin(), data.end());
    std::size_t consumed{};
    while (consumed < impl_->pending_encoder_stream_input.size())
    {
        auto remaining = impl_->pending_encoder_stream_input.view().subspan(consumed);
        auto instruction_size = detail::encoder_instruction_size(remaining);
        if (!instruction_size)
        {
            if (instruction_size.error() ==
                std::make_error_code(std::errc::message_size))
                break;
            return std::unexpected(instruction_size.error());
        }
        auto instruction = remaining.first(*instruction_size);
        auto cursor = instruction;
        const auto first = std::to_integer<std::uint8_t>(cursor.front());
        if (first & 0x80U)
        {
            const bool is_static = (first & 0x40U) != 0U;
            auto index = detail::read_int(cursor, 6);
            if (!index)
                return std::unexpected(index.error());
            std::string name;
            if (is_static)
            {
                if (*index >= detail::static_table.size())
                    return std::unexpected(detail::error());
                name = detail::static_table[*index].name;
            }
            else
            {
                if (*index >= impl_->dynamic_table.insert_count)
                    return std::unexpected(detail::error());
                const auto* entry = impl_->dynamic_table.by_absolute(impl_->dynamic_table.insert_count - *index);
                if (entry == nullptr)
                    return std::unexpected(detail::error());
                name = entry->field.name;
            }
            auto value = detail::read_string(cursor);
            if (!value)
                return std::unexpected(value.error());
            if (impl_->dynamic_table.insert({std::move(name), std::move(*value)}))
                detail::append_int(impl_->pending_decoder_instructions, 0x00U, 6, 1U);
        }
        else if ((first & 0xe0U) == 0x20U)
        {
            auto capacity = detail::read_int(cursor, 5);
            if (!capacity || *capacity > impl_->max_table_capacity)
            {
                // A semantic instruction error is terminal for the bytes
                // supplied in this call. Do not retain them as if they were
                // merely an incomplete fragmented instruction; doing so
                // would duplicate the instruction if a caller retries after
                // updating its local SETTINGS-derived limit.
                impl_->pending_encoder_stream_input.clear();
                return std::unexpected(detail::error());
            }
            impl_->dynamic_table.set_capacity(*capacity);
        }
        else if ((first & 0xc0U) == 0x40U)
        {
            const bool huffman = (first & 0x20U) != 0U;
            auto length = detail::read_int(cursor, 5);
            if (!length || *length > cursor.size())
                return std::unexpected(detail::error());
            std::string name;
            if (huffman)
            {
                auto decoded = cnetmod::http::v2::huffman_decode(cursor.first(*length));
                if (!decoded)
                    return std::unexpected(decoded.error());
                name = std::move(*decoded);
            }
            else
            {
                name.reserve(*length);
                for (std::size_t i{}; i < *length; ++i)
                    name.push_back(static_cast<char>(std::to_integer<unsigned char>(cursor[i])));
            }
            cursor = cursor.subspan(*length);
            auto value = detail::read_string(cursor);
            if (!value)
                return std::unexpected(value.error());
            if (impl_->dynamic_table.insert({std::move(name), std::move(*value)}))
                detail::append_int(impl_->pending_decoder_instructions, 0x00U, 6, 1U);
        }
        else if ((first & 0xe0U) == 0x00U)
        {
            auto index = detail::read_int(cursor, 5);
            if (!index || *index >= impl_->dynamic_table.insert_count)
                return std::unexpected(detail::error());
            const auto* entry = impl_->dynamic_table.by_absolute(impl_->dynamic_table.insert_count - *index);
            if (entry == nullptr)
                return std::unexpected(detail::error());
            if (impl_->dynamic_table.insert(entry->field))
                detail::append_int(impl_->pending_decoder_instructions, 0x00U, 6, 1U);
        }
        else
            return std::unexpected(detail::error());
        consumed += *instruction_size;
    }
    impl_->pending_encoder_stream_input.erase(
        impl_->pending_encoder_stream_input.begin(),
        impl_->pending_encoder_stream_input.begin() +
            static_cast<std::ptrdiff_t>(consumed));
    // Encoder instructions are ordered.  Once their inserts are committed,
    // every retained block is retried in stream-id order; a still-unavailable
    // block remains retained without producing a duplicate acknowledgement.
    std::vector<std::uint64_t> ready;
    ready.reserve(impl_->blocked_header_blocks.size());
    for (const auto& [stream_id, block] : impl_->blocked_header_blocks)
    {
        auto probe = block.view();
        auto encoded_ric = detail::read_int(probe, 8);
        if (!encoded_ric)
            return std::unexpected(encoded_ric.error());
        auto required = detail::decode_required_insert_count(*encoded_ric,
            impl_->dynamic_table.insert_count, impl_->max_table_capacity);
        if (!required)
            return std::unexpected(required.error());
        if (*required <= impl_->dynamic_table.insert_count)
            ready.push_back(stream_id);
    }
    std::ranges::sort(ready);
    for (const auto stream_id : ready)
    {
        auto block = std::move(impl_->blocked_header_blocks.at(stream_id));
        impl_->blocked_header_blocks.erase(stream_id);
        auto headers = decode(block, stream_id);
        if (!headers)
        {
            if (headers.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
                continue;
            return std::unexpected(headers.error());
        }
        impl_->completed_header_blocks.push_back({stream_id, std::move(*headers)});
    }
    return {};
}

auto qpack_decoder::decode(byte_view in, std::uint64_t stream_id)
    -> std::expected<std::vector<header_field>, std::error_code>
{
    const auto encoded = in;
    auto encoded_ric = detail::read_int(in, 8);
    if (in.empty())
        return std::unexpected(detail::error());
    const bool negative = (std::to_integer<std::uint8_t>(in.front()) & 0x80U) != 0U;
    auto delta_base = detail::read_int(in, 7);
    if (!encoded_ric || !delta_base)
        return std::unexpected(detail::error());
    auto required = detail::decode_required_insert_count(*encoded_ric, impl_->dynamic_table.insert_count, impl_->max_table_capacity);
    if (!required)
        return std::unexpected(required.error());
    const auto base = negative ? (*required > *delta_base + 1U ? *required - *delta_base - 1U : 0U) : *required + *delta_base;
    if (negative && *required <= *delta_base)
        return std::unexpected(detail::error());
    if (*required > impl_->dynamic_table.insert_count)
    {
        if (impl_->blocked_header_blocks.contains(stream_id) ||
            impl_->blocked_header_blocks.size() >= impl_->max_blocked_streams)
            return std::unexpected(detail::error());
        impl_->blocked_header_blocks.emplace(stream_id, byte_buffer{encoded.begin(), encoded.end()});
        return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    }
    std::vector<header_field> out;
    while (!in.empty())
    {
        auto f = std::to_integer<std::uint8_t>(in.front());
        if (f & 0x80U)
        {
            const bool st = (f & 0x40U) != 0U;
            auto i = detail::read_int(in, 6);
            if (!i)
                return std::unexpected(detail::error());
            if (st)
            {
                if (*i >= detail::static_table.size())
                    return std::unexpected(detail::error());
                out.push_back({std::string{detail::static_table[*i].name}, std::string{detail::static_table[*i].value}});
            }
            else
            {
                if (*i > base)
                    return std::unexpected(detail::error());
                const auto* entry = impl_->dynamic_table.by_absolute(base - *i);
                if (entry == nullptr)
                    return std::unexpected(detail::error());
                out.push_back(entry->field);
            }
        }
        else if ((f & 0xc0U) == 0x40U)
        {
            const bool st = (f & 0x10U) != 0U;
            auto i = detail::read_int(in, 4);
            auto v = detail::read_string(in);
            if (!i || !v)
                return std::unexpected(detail::error());
            if (st)
            {
                if (*i >= detail::static_table.size())
                    return std::unexpected(detail::error());
                out.push_back({std::string{detail::static_table[*i].name}, std::move(*v)});
            }
            else
            {
                if (*i > base)
                    return std::unexpected(detail::error());
                const auto* entry = impl_->dynamic_table.by_absolute(base - *i);
                if (entry == nullptr)
                    return std::unexpected(detail::error());
                out.push_back({entry->field.name, std::move(*v)});
            }
        }
        else if ((f & 0xe0U) == 0x20U)
        {
            const bool huffman = (f & 0x08U) != 0U;
            auto n = detail::read_int(in, 3);
            if (!n || *n > in.size())
                return std::unexpected(detail::error());
            std::string name;
            if (huffman)
            {
                auto decoded = cnetmod::http::v2::huffman_decode(in.first(*n));
                if (!decoded)
                    return std::unexpected(decoded.error());
                name = std::move(*decoded);
            }
            else
            {
                name.reserve(*n);
                for (std::size_t i{}; i < *n; ++i)
                    name.push_back(static_cast<char>(std::to_integer<unsigned char>(in[i])));
            }
            in = in.subspan(*n);
            auto v = detail::read_string(in);
            if (!v)
                return std::unexpected(v.error());
            out.push_back({std::move(name), std::move(*v)});
        }
        else if ((f & 0xf0U) == 0x10U)
        {
            auto index = detail::read_int(in, 4);
            if (!index || base + *index + 1U > impl_->dynamic_table.insert_count)
                return std::unexpected(detail::error());
            const auto* entry = impl_->dynamic_table.by_absolute(base + *index + 1U);
            if (entry == nullptr)
                return std::unexpected(detail::error());
            out.push_back(entry->field);
        }
        else if ((f & 0xf0U) == 0x00U)
        {
            auto index = detail::read_int(in, 3);
            auto value = detail::read_string(in);
            if (!index || !value || base + *index + 1U > impl_->dynamic_table.insert_count)
                return std::unexpected(detail::error());
            const auto* entry = impl_->dynamic_table.by_absolute(base + *index + 1U);
            if (entry == nullptr)
                return std::unexpected(detail::error());
            out.push_back({entry->field.name, std::move(*value)});
        }
        else
            return std::unexpected(detail::error());
    }
    // A header acknowledgement is meaningful only for a block that references
    // dynamic-table state. Static/literal-only blocks have Required Insert
    // Count zero; acknowledging them makes compliant encoders reject an
    // acknowledgement for a stream with no outstanding dynamic reference.
    if (*required != 0U)
        detail::append_int(impl_->pending_decoder_instructions, 0x80U, 7, stream_id);
    return out;
}

auto qpack_decoder::lookup_by_name_value(std::string_view n, std::string_view v) -> std::optional<std::uint16_t>
{
    return detail::exact(n, v);
}

auto qpack_decoder::lookup_by_name(std::string_view n) -> std::vector<std::uint16_t>
{
    std::vector<std::uint16_t> r;
    for (std::uint16_t i{}; i < detail::static_table.size(); ++i)
        if (detail::static_table[i].name == n)
            r.push_back(i);
    return r;
}

auto qpack_decoder::get_dynamic_table_size() const noexcept -> std::size_t
{
    return impl_->dynamic_table.entries.size();
}

auto qpack_decoder::take_decoder_instructions() -> byte_buffer
{
    return std::exchange(impl_->pending_decoder_instructions, {});
}

auto qpack_decoder::take_completed_header_blocks() -> std::vector<qpack_decoded_header_block>
{
    return std::exchange(impl_->completed_header_blocks, {});
}

void qpack_decoder::set_max_table_capacity(std::uint64_t capacity) noexcept
{
    impl_->max_table_capacity = capacity;
    if (impl_->dynamic_table.capacity > capacity)
        impl_->dynamic_table.set_capacity(capacity);
}

void qpack_decoder::set_max_blocked_streams(std::uint64_t maximum) noexcept
{
    impl_->max_blocked_streams = maximum;
}

void qpack_decoder::cancel_stream(std::uint64_t stream_id)
{
    impl_->blocked_header_blocks.erase(stream_id);
    detail::append_int(impl_->pending_decoder_instructions, 0x40U, 6, stream_id);
}

auto is_sensitive_header(std::string_view n) noexcept -> bool
{
    return n == "authorization" || n == "cookie" || n == "x-cnetmod-auth" || n.rfind("proxy-", 0) == 0;
}

auto normalize_method(std::string_view m) -> std::string
{
    std::string r{m};
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c)
        {
            return static_cast<char>(c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c);
        });
    return r;
}
} // namespace cnetmod::http::v3
