module;

#include <simdjson.h>

module cnetmod.json;

import std;

namespace cnetmod::json {
namespace {
    class category final : public std::error_category
    {
    public:
        [[nodiscard]] auto name() const noexcept -> const char* override
        {
            return "cnetmod.json";
        }

        [[nodiscard]] auto message(int value) const -> std::string override
        {
            switch (static_cast<errc>(value))
            {
            case errc::parse_failed:
                return "JSON parsing failed";
            case errc::serialization_failed:
                return "JSON serialization failed";
            case errc::type_mismatch:
                return "JSON value has an incompatible type";
            case errc::missing_field:
                return "JSON document is missing a required field";
            case errc::unknown_field:
                return "JSON document contains an unknown field";
            }
            return "unknown JSON error";
        }
    };

    auto from_simdjson(simdjson::dom::element source,
        parse_options options, std::size_t depth) -> std::optional<document>
    {
        using enum simdjson::dom::element_type;
        switch (source.type())
        {
        case NULL_VALUE:
            return document(nullptr);
        case BOOL:
            return document(source.get_bool().value_unsafe());
        case STRING:
            return document(source.get_string().value_unsafe());
        case INT64:
        {
            const auto value = source.get_int64().value_unsafe();
            if (value < 0)
                return document(value);
            return document(static_cast<std::uint64_t>(value));
        }
        case UINT64:
            return document(source.get_uint64().value_unsafe());
        case DOUBLE:
            return document(source.get_double().value_unsafe());
        case BIGINT:
        {
            const auto text = source.get_bigint().value_unsafe();
            const auto end = text.data() + text.size();
            double value = 0.0;
            const auto [cursor, error] = std::from_chars(text.data(), end, value);
            if (error != std::errc{} || cursor != end)
                return std::nullopt;
            return document(value);
        }
        case ARRAY:
        {
            if (depth >= options.max_depth)
                return std::nullopt;
            const auto array = source.get_array().value_unsafe();
            auto result = document::array();
            result.get_array().reserve(array.size());
            for (const auto child : array)
            {
                auto node = from_simdjson(child, options, depth + 1);
                if (!node)
                    return std::nullopt;
                result.push_back(std::move(*node));
            }
            return result;
        }
        case OBJECT:
        {
            if (depth >= options.max_depth)
                return std::nullopt;
            const auto object = source.get_object().value_unsafe();
            auto result = document::object();
            auto& entries = result.get_object();
            for (const auto field : object)
            {
                if (options.reject_duplicate_keys && entries.contains(field.key))
                    return std::nullopt;
                auto node = from_simdjson(field.value, options, depth + 1);
                if (!node)
                    return std::nullopt;
                entries.insert_or_assign(std::string{field.key}, std::move(*node));
            }
            return result;
        }
        }
        return std::nullopt;
    }

} // namespace

auto make_error_code(errc value) noexcept -> std::error_code
{
    static const category instance;
    return {static_cast<int>(value), instance};
}

auto parse_document(std::string_view input) -> std::expected<document, std::error_code>
{
    return parse_document(input, {});
}

auto parse_document(std::string_view input, parse_options options)
    -> std::expected<document, std::error_code>
{
    if (input.starts_with("\xef\xbb\xbf"))
        input.remove_prefix(3);

    simdjson::dom::parser parser;
    parser.number_as_string(true);
    const auto depth = std::min(options.max_depth, input.size());
    simdjson::dom::element root;
    if (parser.allocate(input.size(), depth + 1) ||
        parser.parse(input.data(), input.size()).get(root))
        return std::unexpected(make_error_code(errc::parse_failed));

    auto decoded = from_simdjson(root, options, 0);
    if (!decoded)
        return std::unexpected(make_error_code(errc::parse_failed));
    return std::move(*decoded);
}

namespace detail {
    namespace {
        // RFC 3629 excludes overlongs, surrogates and code points above U+10FFFF.
        [[nodiscard]] auto utf8_sequence_length(std::string_view text, std::size_t at) noexcept
            -> std::size_t
        {
            const auto byte = [&](std::size_t offset) noexcept -> unsigned
            {
                return static_cast<unsigned char>(text[at + offset]);
            };
            const auto remaining = text.size() - at;
            const auto lead = byte(0);
            const auto continuation = [&](std::size_t offset) noexcept
            {
                return (byte(offset) & 0xC0U) == 0x80U;
            };
            if (lead >= 0xC2U && lead <= 0xDFU)
                return remaining >= 2U && continuation(1) ? 2U : 0U;
            if (lead >= 0xE0U && lead <= 0xEFU)
            {
                if (remaining < 3U || !continuation(1) || !continuation(2))
                    return 0U;
                if ((lead == 0xE0U && byte(1) < 0xA0U) || (lead == 0xEDU && byte(1) >= 0xA0U))
                    return 0U;
                return 3U;
            }
            if (lead >= 0xF0U && lead <= 0xF4U)
            {
                if (remaining < 4U || !continuation(1) || !continuation(2) || !continuation(3))
                    return 0U;
                if ((lead == 0xF0U && byte(1) < 0x90U) || (lead == 0xF4U && byte(1) >= 0x90U))
                    return 0U;
                return 4U;
            }
            return 0U;
        }
    } // namespace

    namespace
    {
        /// True when every byte of the word needs no escaping: not a control
        /// byte, not a quote or backslash, and plain ASCII. A word may report
        /// a false hit (borrow), never a false miss.
        [[nodiscard]] inline auto plain_word(std::uint64_t word) noexcept -> bool
        {
            constexpr std::uint64_t ones = 0x0101010101010101ULL;
            constexpr std::uint64_t highs = 0x8080808080808080ULL;
            const auto quote = word ^ (ones * '"');
            const auto backslash = word ^ (ones * '\\');
            const auto flagged = ((word - ones * 0x20U) & ~word) | (word & highs) |
                ((quote - ones) & ~quote) | ((backslash - ones) & ~backslash);
            return (flagged & highs) == 0U;
        }

        /// Whole-string check for the common case of short identifiers and
        /// labels. A tail shorter than a word is padded with spaces, which
        /// are plain, so it is tested by the same word predicate.
        [[nodiscard]] inline auto plain_ascii(std::string_view value) noexcept -> bool
        {
            std::size_t index = 0;
            for (; value.size() - index >= 8U; index += 8U)
            {
                std::uint64_t word;
                std::memcpy(&word, value.data() + index, sizeof word);
                if (!plain_word(word))
                    return false;
            }
            if (index == value.size())
                return true;
            std::uint64_t word = 0x2020202020202020ULL;
            std::memcpy(&word, value.data() + index, value.size() - index);
            return plain_word(word);
        }
    } // namespace

    auto append_string(json_writer& out, std::string_view value) -> bool
    {
        // Quotes plus the unescaped bytes; escapes reserve their own room.
        out.reserve(value.size() + 2U);
        out.put_unchecked('"');
        if (plain_ascii(value))
        {
            out.append_unchecked(value.data(), value.size());
            out.put_unchecked('"');
            return true;
        }
        std::size_t run = 0;
        std::size_t index = 0;
        const auto flush = [&]
        {
            out.append(value.data() + run, index - run);
        };
        while (index < value.size())
        {
            // Skip eight plain ASCII bytes at a time. A word is plain when no
            // byte is below 0x20, at or above 0x80, a quote or a backslash;
            // the test may report a false hit (borrow), never a false miss.
            constexpr std::uint64_t ones = 0x0101010101010101ULL;
            constexpr std::uint64_t highs = 0x8080808080808080ULL;
            while (value.size() - index >= 8U)
            {
                std::uint64_t word;
                std::memcpy(&word, value.data() + index, sizeof word);
                const auto quote = word ^ (ones * '"');
                const auto backslash = word ^ (ones * '\\');
                const auto flagged = ((word - ones * 0x20U) & ~word) | (word & highs) |
                    ((quote - ones) & ~quote) | ((backslash - ones) & ~backslash);
                if ((flagged & highs) != 0U)
                    break;
                index += 8U;
            }
            if (index == value.size())
                break;
            const auto byte = static_cast<unsigned char>(value[index]);
            if (byte >= 0x20U && byte != '"' && byte != '\\' && byte < 0x80U)
            {
                ++index;
                continue;
            }
            if (byte >= 0x80U)
            {
                const auto length = utf8_sequence_length(value, index);
                if (length == 0U)
                    return false;
                index += length;
                continue;
            }
            flush();
            switch (byte)
            {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
            {
                constexpr char hex[] = "0123456789abcdef";
                const char escaped[] = {'\\', 'u', '0', '0', hex[byte >> 4U], hex[byte & 0x0FU]};
                out.append(escaped, sizeof escaped);
            }
            }
            run = ++index;
        }
        flush();
        out.put('"');
        return true;
    }

    void append_raw_json(json_writer& out, std::string_view text)
    {
        // The output is never longer than the input.
        out.reserve(text.size());
        auto* cursor = out.cursor();
        auto* const begin = cursor;
        bool in_string = false;
        bool escaped = false;
        for (const char byte : text)
        {
            if (in_string)
            {
                *cursor++ = byte;
                if (escaped)
                    escaped = false;
                else if (byte == '\\')
                    escaped = true;
                else if (byte == '"')
                    in_string = false;
                continue;
            }
            if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r')
                continue;
            *cursor++ = byte;
            in_string = byte == '"';
        }
        out.advance(static_cast<std::size_t>(cursor - begin));
    }

    auto append_double(json_writer& out, double value)
        -> std::expected<void, std::error_code>
    {
        if (!std::isfinite(value))
        {
            out.append("null");
            return {};
        }
        std::array<char, 32> text;
        const auto [end, error] = std::to_chars(text.data(),
            text.data() + text.size(), value, std::chars_format::general);
        if (error != std::errc{})
            return std::unexpected(make_error_code(errc::serialization_failed));

        const std::string_view token{text.data(), end};
        out.append(token);
        if (token.find_first_of(".eE") == std::string_view::npos)
            out.append(".0");
        return {};
    }

    auto append_document(json_writer& out, const document& value)
        -> std::expected<void, std::error_code>
    {
        if (value.is_null())
            out.append("null");
        else if (value.is_boolean())
            out.append(value.get<bool>() ? "true" : "false");
        else if (value.is_uint64())
            append_integer(out, value.get<std::uint64_t>());
        else if (value.is_int64())
            append_integer(out, value.get<std::int64_t>());
        else if (value.is_double())
            return append_double(out, value.get<double>());
        else if (value.is_string())
        {
            if (!append_string(out, value.get_ref<const std::string&>()))
                return std::unexpected(make_error_code(errc::serialization_failed));
        }
        else if (value.is_array())
        {
            out.put('[');
            bool first = true;
            for (const auto& entry : value.get_array())
            {
                if (!std::exchange(first, false))
                    out.put(',');
                if (auto written = append_document(out, entry); !written)
                    return written;
            }
            out.put(']');
        }
        else
        {
            out.put('{');
            bool first = true;
            for (const auto& [key, entry] : value.get_object())
            {
                if (!std::exchange(first, false))
                    out.put(',');
                if (!append_string(out, key))
                    return std::unexpected(make_error_code(errc::serialization_failed));
                out.put(':');
                if (auto written = append_document(out, entry); !written)
                    return written;
            }
            out.put('}');
        }
        return {};
    }
} // namespace detail

auto write_document(const document& value, bool prettify)
    -> std::expected<std::string, std::error_code>
{
    detail::json_writer out{256U};
    if (auto written = detail::append_document(out, value); !written)
        return std::unexpected(written.error());
    auto compact = std::move(out).take();
    if (!prettify)
        return compact;

    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(compact).get(root))
        return std::unexpected(make_error_code(errc::serialization_failed));
    return simdjson::prettify(root);
}

auto document::parse(std::string_view input, std::nullptr_t,
    bool allow_exceptions, bool) -> document
{
    auto parsed = parse_document(input);
    if (parsed)
        return std::move(*parsed);
    if (allow_exceptions)
        throw exception{"invalid JSON document"};
    document result;
    result.discarded_ = true;
    return result;
}

auto document::parse(std::istream& input, std::nullptr_t callback,
    bool allow_exceptions, bool ignore_comments) -> document
{
    return parse(std::string{std::istreambuf_iterator<char>{input},
                     std::istreambuf_iterator<char>{}},
        callback, allow_exceptions,
        ignore_comments);
}

auto document::dump(int) const -> std::string
{
    auto encoded = write_document(*this);
    if (!encoded)
        throw exception{"failed to serialize JSON document"};
    return std::move(*encoded);
}

} // namespace cnetmod::json
