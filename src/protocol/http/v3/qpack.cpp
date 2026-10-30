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

    // Open-addressed name index over the static table. Every encoded and
    // decoded field probes it, so it replaces an ordered map (a string
    // comparison per binary-search step) with one hash and usually one probe.
    // Each distinct name owns a contiguous run of its wire indices, kept in
    // ascending order so the first entry is the canonical name reference.
    struct static_name_index
    {
        static constexpr std::size_t slot_count = 256U;
        static constexpr std::uint8_t empty = 0xffU;

        struct name_run
        {
            std::string_view name;
            std::uint8_t first{};
            std::uint8_t count{};
        };

        std::array<std::uint8_t, slot_count> slots{};
        std::array<name_run, static_table.size()> runs{};
        std::array<std::uint8_t, static_table.size()> wire_indices{};
        std::size_t run_count{};

        [[nodiscard]] static constexpr auto hash(std::string_view text) noexcept -> std::size_t
        {
            std::uint32_t value = 2166136261U;
            for (const auto character : text)
                value = (value ^ static_cast<unsigned char>(character)) * 16777619U;
            return value;
        }

        [[nodiscard]] auto find(std::string_view name) const noexcept -> const name_run*
        {
            for (auto slot = hash(name) & (slot_count - 1U);; slot = (slot + 1U) & (slot_count - 1U))
            {
                const auto run = slots[slot];
                if (run == empty)
                    return nullptr;
                if (runs[run].name == name)
                    return &runs[run];
            }
        }

        [[nodiscard]] auto indices(const name_run& run) const noexcept
            -> std::span<const std::uint8_t>
        {
            return {wire_indices.data() + run.first, run.count};
        }
    };

    [[nodiscard]] auto static_entries_by_name() -> const static_name_index&
    {
        static const auto index = []
        {
            static_name_index result;
            result.slots.fill(static_name_index::empty);
            std::size_t next_index{};
            for (std::uint16_t wire_index{}; wire_index < static_table.size(); ++wire_index)
            {
                const auto name = static_table[wire_index].name;
                if (result.find(name) != nullptr)
                    continue;
                auto& run = result.runs[result.run_count];
                run.name = name;
                run.first = static_cast<std::uint8_t>(next_index);
                for (std::uint16_t candidate = wire_index; candidate < static_table.size(); ++candidate)
                    if (static_table[candidate].name == name)
                        result.wire_indices[next_index++] = static_cast<std::uint8_t>(candidate);
                run.count = static_cast<std::uint8_t>(next_index - run.first);
                auto slot = static_name_index::hash(name) & (static_name_index::slot_count - 1U);
                while (result.slots[slot] != static_name_index::empty)
                    slot = (slot + 1U) & (static_name_index::slot_count - 1U);
                result.slots[slot] = static_cast<std::uint8_t>(result.run_count++);
            }
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

    auto append_string(byte_buffer& out, std::string_view s,
        std::vector<std::byte>& scratch) -> void
    {
        // The caller owns a reusable scratch buffer: huffman_encode() clears
        // and reserves it, so a repeated header value no longer allocates a
        // temporary vector for every field.
        cnetmod::http::v2::huffman_encode(s, scratch);
        if (scratch.size() < s.size())
        {
            append_int(out, 0x80U, 7, scratch.size());
            out.insert(out.end(), scratch.begin(), scratch.end());
            return;
        }
        append_int(out, 0, 7, s.size());
        out.insert(out.end(),
            reinterpret_cast<const std::byte*>(s.data()),
            reinterpret_cast<const std::byte*>(s.data()) + s.size());
    }

    /// Huffman-decodes into a reused scratch string and returns an exactly
    /// sized copy. Decoding straight into the result reserves twice the
    /// encoded length, which pushes typical short header values out of the
    /// small-string buffer and onto the heap.
    auto huffman_string(byte_view encoded) -> std::expected<std::string, std::error_code>
    {
        thread_local std::string scratch;
        if (auto decoded = cnetmod::http::v2::huffman_decode(encoded, scratch); !decoded)
            return std::unexpected(decoded.error());
        return std::string{scratch};
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
            auto result = huffman_string(in.first(*n));
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

    /// Static-table references for one field, from a single index probe.
    struct static_match
    {
        std::optional<std::uint16_t> exact;
        std::optional<std::uint16_t> name;
    };

    auto match_static(std::string_view n, std::string_view v) -> static_match
    {
        const auto& index = static_entries_by_name();
        const auto* run = index.find(n);
        if (run == nullptr)
            return {};
        static_match result{.name = index.indices(*run).front()};
        for (const auto wire_index : index.indices(*run))
            if (static_table[wire_index].value == v)
            {
                result.exact = wire_index;
                break;
            }
        return result;
    }

    auto exact(std::string_view n, std::string_view v) -> std::optional<std::uint16_t>
    {
        return match_static(n, v).exact;
    }

    auto name(std::string_view n) -> std::optional<std::uint16_t>
    {
        const auto& index = static_entries_by_name();
        const auto* run = index.find(n);
        return run == nullptr ? std::nullopt
                              : std::optional<std::uint16_t>{index.indices(*run).front()};
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

        [[nodiscard]] auto can_insert(std::size_t entry_size,
            std::uint64_t known_received_count) const -> bool
        {
            if (entry_size > capacity)
                return false;
            auto remaining = size;
            for (auto entry = entries.rbegin(); remaining + entry_size > capacity;
                ++entry)
            {
                if (entry == entries.rend() || entry->absolute > known_received_count)
                    return false;
                remaining -= entry->size;
            }
            return true;
        }

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
    std::uint64_t local_table_limit{std::numeric_limits<std::uint64_t>::max()};
    byte_buffer pending_encoder_instructions;
    detail::dynamic_table dynamic_table;
    bool capacity_update_pending{true};
    std::uint64_t acknowledged_insert_count{};
    std::uint64_t max_blocked_streams{std::numeric_limits<std::uint64_t>::max()};
    // All referenced sections stay pinned until their own Section ACK,
    // including sections whose entries are already known to the decoder.
    cnetmod::flat_map<std::uint64_t, std::deque<std::uint64_t>> outstanding_streams;
    byte_buffer pending_decoder_stream_input;
    // Reused by append_string() so header values do not allocate a
    // temporary Huffman buffer per field.
    std::vector<std::byte> string_scratch;
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
    capacity = std::min(capacity, impl_->local_table_limit);
    impl_->max_table_capacity = capacity;
    impl_->dynamic_table.set_capacity(capacity);
    impl_->capacity_update_pending = true;
}

void qpack_encoder::set_local_table_limit(std::uint64_t capacity) noexcept
{
    impl_->local_table_limit = capacity;
    if (impl_->max_table_capacity > capacity)
        set_max_table_capacity(capacity);
}

void qpack_encoder::set_max_blocked_streams(std::uint64_t maximum) noexcept
{
    impl_->max_blocked_streams = maximum;
}

namespace detail {
    /// Appends a field section that references the static table only. This is
    /// the complete encoding whenever the dynamic table is disabled: Required
    /// Insert Count and Delta Base are zero and each field costs one index probe.
    void append_static_section(byte_buffer& out, std::span<const header_field> headers,
        std::vector<std::byte>& scratch)
    {
        out.reserve(out.size() + 2U + headers.size() * 16U);
        append_int(out, 0, 8, 0U);
        append_int(out, 0, 7, 0U);
        for (const auto& h : headers)
        {
            const auto match = match_static(h.name, h.value);
            if (match.exact)
                append_int(out, 0xc0U, 6, *match.exact);
            else if (match.name)
            {
                append_int(out, 0x50U, 4, *match.name);
                append_string(out, h.value, scratch);
            }
            else
            {
                append_int(out, 0x20U, 3, h.name.size());
                for (const auto c : h.name)
                    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
                append_string(out, h.value, scratch);
            }
        }
    }
} // namespace detail

auto qpack_encoder::encode_into(std::span<const header_field> headers,
    std::uint64_t stream_id, byte_buffer& out) -> std::expected<void, std::error_code>
{
    if (impl_->max_table_capacity == 0U)
    {
        if (impl_->capacity_update_pending)
        {
            detail::append_int(impl_->pending_encoder_instructions, 0x20U, 5, 0U);
            impl_->capacity_update_pending = false;
        }
        detail::append_static_section(out, headers, impl_->string_scratch);
        return {};
    }
    auto encoded = encode(headers, stream_id);
    if (!encoded)
        return std::unexpected(encoded.error());
    out.insert(out.end(), encoded->begin(), encoded->end());
    return {};
}

auto qpack_encoder::encode(std::span<const header_field> headers, std::uint64_t stream_id)
    -> std::expected<byte_buffer, std::error_code>
{
    if (impl_->capacity_update_pending)
    {
        detail::append_int(impl_->pending_encoder_instructions, 0x20U, 5, impl_->max_table_capacity);
        impl_->capacity_update_pending = false;
    }
    if (impl_->max_table_capacity == 0U)
    {
        byte_buffer out;
        detail::append_static_section(out, headers, impl_->string_scratch);
        return out;
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
        if (impl_->max_table_capacity != 0U && impl_->outstanding_streams.empty() &&
            impl_->dynamic_table.can_insert(h.name.size() + h.value.size() + 32U,
                impl_->acknowledged_insert_count) &&
            !is_sensitive_header(h.name) && !detail::exact(h.name, h.value))
        {
            if (const auto static_name = detail::name(h.name))
            {
                detail::append_int(impl_->pending_encoder_instructions, 0xc0U, 6, *static_name);
                detail::append_string(impl_->pending_encoder_instructions, h.value,
                    impl_->string_scratch);
            }
            else
            {
                detail::append_int(impl_->pending_encoder_instructions, 0x40U, 5, h.name.size());
                for (const auto c : h.name)
                    impl_->pending_encoder_instructions.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
                detail::append_string(impl_->pending_encoder_instructions, h.value,
                    impl_->string_scratch);
            }
            (void)impl_->dynamic_table.insert(h);
        }
    }
    // Finish insertions before retaining table-entry pointers: a later field
    // can evict an earlier field in this same section when capacity is small.
    const bool dynamic_enabled = impl_->max_table_capacity != 0U;
    std::uint64_t required{};
    for (const auto& h : headers)
    {
        const bool sensitive = is_sensitive_header(h.name);
        // With no dynamic table capacity every lookup would hash a name that
        // can never be present, so the default static-only configuration
        // skips them entirely.
        representations.push_back({&h,
            (dynamic_enabled && !sensitive) ? impl_->dynamic_table.exact(h.name, h.value) : nullptr,
            (dynamic_enabled && !sensitive) ? impl_->dynamic_table.by_name(h.name) : nullptr});
        const auto& representation = representations.back();
        if (representation.exact_dynamic)
            required = std::max(required, representation.exact_dynamic->absolute);
        else if (!detail::exact(h.name, h.value) && representation.name_dynamic)
            required = std::max(required, representation.name_dynamic->absolute);
    }
    bool may_reference_dynamic = required <= impl_->acknowledged_insert_count;
    if (!may_reference_dynamic)
    {
        const auto blocked_streams = std::ranges::count_if(impl_->outstanding_streams, [this](const auto& stream)
            {
                return std::ranges::any_of(stream.second, [this](auto required)
                    {
                        return required > impl_->acknowledged_insert_count;
                    });
            });
        may_reference_dynamic = std::cmp_less(blocked_streams, impl_->max_blocked_streams);
    }
    if (!may_reference_dynamic)
        required = 0U;
    byte_buffer out;
    out.reserve(32U + headers.size() * 16U);
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
            detail::append_string(out, h.value, impl_->string_scratch);
        }
        else if (const auto i = detail::name(h.name))
        {
            detail::append_int(out, 0x50U, 4, *i);
            detail::append_string(out, h.value, impl_->string_scratch);
        }
        else
        {
            detail::append_int(out, 0x20U, 3, h.name.size());
            for (auto c : h.name)
                out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
            detail::append_string(out, h.value, impl_->string_scratch);
        }
    }
    if (required != 0U)
        impl_->outstanding_streams[stream_id].push_back(required);
    return out;
}

qpack_static_response_headers::qpack_static_response_headers(
    int status, byte_buffer encoded) noexcept
    : status_(status), encoded_(std::move(encoded)) {}

auto qpack_static_response_headers::encode(int status,
    std::span<const header_field> fields)
    -> std::expected<qpack_static_response_headers, std::error_code>
{
    std::vector<header_field> response_fields;
    response_fields.reserve(fields.size() + 1U);
    response_fields.push_back({":status", std::to_string(status)});
    response_fields.insert(response_fields.end(), fields.begin(), fields.end());
    qpack_encoder encoder;
    auto section = encoder.encode(response_fields);
    if (!section)
        return std::unexpected(section.error());
    return qpack_static_response_headers{status, std::move(*section)};
}

auto qpack_static_response_headers::status() const noexcept -> int
{
    return status_;
}

auto qpack_static_response_headers::encoded() const noexcept -> byte_view
{
    return encoded_.view();
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
            const auto stream = impl_->outstanding_streams.find(*id);
            if (stream == impl_->outstanding_streams.end() || stream->second.empty())
                return std::unexpected(detail::error());
            impl_->acknowledged_insert_count = std::max(
                impl_->acknowledged_insert_count, stream->second.front());
            stream->second.pop_front();
            if (stream->second.empty())
                impl_->outstanding_streams.erase(stream);
        }
        else if ((first & 0xc0U) == 0x40U)
        {
            auto id = detail::read_int(cursor, 6);
            if (!id)
                return std::unexpected(id.error());
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
    // One unfinished insertion can retain only a table-sized field. Huffman
    // codes use at most 30 bits per decoded byte; 4x the allowed table size
    // plus integer/string prefixes is a conservative encoded upper bound.
    // Apply it after consuming complete instructions, so a large batch of
    // individually valid insertions does not trip a cumulative traffic cap.
    constexpr auto storage_max = std::numeric_limits<std::size_t>::max();
    const auto unfinished_limit = impl_->max_table_capacity > (storage_max - 64U) / 4U
        ? storage_max
        : static_cast<std::size_t>(impl_->max_table_capacity * 4U + 64U);
    if (impl_->pending_encoder_stream_input.size() > unfinished_limit)
    {
        impl_->pending_encoder_stream_input.clear();
        return std::unexpected(detail::error());
    }
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
    std::vector<header_field> fields;
    struct owning_sink_state
    {
        std::vector<header_field>& fields;
        std::size_t initial_capacity;
    } state{fields, std::min<std::size_t>(in.size(), 8U)};
    const auto decoded = decode_each(in,
        qpack_header_sink{
            .context = &state,
            .consume = [](void* context, header_field&& field)
                -> std::expected<void, std::error_code>
            {
                auto& state = *static_cast<owning_sink_state*>(context);
                if (state.fields.empty())
                    state.fields.reserve(state.initial_capacity);
                state.fields.push_back(std::move(field));
                return {};
            },
        },
        stream_id);
    if (!decoded)
        return std::unexpected(decoded.error());
    return fields;
}

auto qpack_decoder::decode_each(byte_view in, qpack_header_sink sink,
    std::uint64_t stream_id) -> std::expected<void, std::error_code>
{
    if (sink.consume == nullptr)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
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
    const auto emit = [&sink](header_field field)
        -> std::expected<void, std::error_code>
    {
        return sink.consume(sink.context, std::move(field));
    };
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
                const auto& field = detail::static_table[*i];
                const auto emitted = sink.consume_static
                    ? sink.consume_static(sink.context, field.name, field.value)
                    : emit({std::string{field.name}, std::string{field.value}});
                if (!emitted)
                    return std::unexpected(emitted.error());
            }
            else
            {
                if (*i > base)
                    return std::unexpected(detail::error());
                const auto* entry = impl_->dynamic_table.by_absolute(base - *i);
                if (entry == nullptr)
                    return std::unexpected(detail::error());
                if (auto emitted = emit(entry->field); !emitted)
                    return std::unexpected(emitted.error());
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
                const auto name = detail::static_table[*i].name;
                const auto emitted = sink.consume_static_name
                    ? sink.consume_static_name(sink.context, name, std::move(*v))
                    : emit({std::string{name}, std::move(*v)});
                if (!emitted)
                    return std::unexpected(emitted.error());
            }
            else
            {
                if (*i > base)
                    return std::unexpected(detail::error());
                const auto* entry = impl_->dynamic_table.by_absolute(base - *i);
                if (entry == nullptr)
                    return std::unexpected(detail::error());
                if (auto emitted = emit({entry->field.name, std::move(*v)}); !emitted)
                    return std::unexpected(emitted.error());
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
                auto decoded = detail::huffman_string(in.first(*n));
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
            if (auto emitted = emit({std::move(name), std::move(*v)}); !emitted)
                return std::unexpected(emitted.error());
        }
        else if ((f & 0xf0U) == 0x10U)
        {
            auto index = detail::read_int(in, 4);
            if (!index || base + *index + 1U > impl_->dynamic_table.insert_count)
                return std::unexpected(detail::error());
            const auto* entry = impl_->dynamic_table.by_absolute(base + *index + 1U);
            if (entry == nullptr)
                return std::unexpected(detail::error());
            if (auto emitted = emit(entry->field); !emitted)
                return std::unexpected(emitted.error());
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
            if (auto emitted = emit({entry->field.name, std::move(*value)}); !emitted)
                return std::unexpected(emitted.error());
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
    return {};
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

auto qpack_decoder::has_pending_decoder_instructions() const noexcept -> bool
{
    return !impl_->pending_decoder_instructions.empty();
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

auto qpack_decoder::cancel_blocked_stream(std::uint64_t stream_id) -> bool
{
    if (!impl_->blocked_header_blocks.contains(stream_id))
        return false;
    cancel_stream(stream_id);
    return true;
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
