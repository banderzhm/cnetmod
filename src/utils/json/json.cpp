module cnetmod.json;

import std;
import nlohmann.json;

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

    auto from_nlohmann(const nlohmann::json& source) -> document
    {
        if (source.is_null())
            return nullptr;
        if (source.is_boolean())
            return source.get<bool>();
        if (source.is_string())
            return source.get<std::string>();
        if (source.is_number_unsigned())
            return source.get<std::uint64_t>();
        if (source.is_number_integer())
            return source.get<std::int64_t>();
        if (source.is_number_float())
            return source.get<double>();
        if (source.is_array())
        {
            auto result = document::array();
            for (const auto& entry : source)
                result.push_back(from_nlohmann(entry));
            return result;
        }
        auto result = document::object();
        for (auto iterator = source.begin(); iterator != source.end(); ++iterator)
            result[iterator.key()] = from_nlohmann(iterator.value());
        return result;
    }

    auto to_nlohmann(const document& source) -> nlohmann::json
    {
        if (source.is_null())
            return nullptr;
        if (source.is_boolean())
            return source.get<bool>();
        if (source.is_string())
            return source.get<std::string>();
        if (source.is_number_unsigned())
            return source.get<std::uint64_t>();
        if (source.is_number_integer())
            return source.get<std::int64_t>();
        if (source.is_number_float())
            return source.get<double>();
        if (source.is_array())
        {
            auto result = nlohmann::json::array();
            auto& values = result.get_ref<nlohmann::json::array_t&>();
            values.reserve(source.size());
            for (const auto& entry : source)
                values.push_back(to_nlohmann(entry));
            return result;
        }
        auto result = nlohmann::json::object();
        for (const auto& [key, entry] :
            source.get_ref<const document::object_type&>())
            result[key] = to_nlohmann(entry);
        return result;
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
    try
    {
        bool violates_policy = false;
        std::unordered_map<int, std::unordered_set<std::string>> object_keys;
        const auto callback = [&](int depth, nlohmann::json::parse_event_t event,
                                  nlohmann::json& parsed) -> bool
        {
            using event_type = nlohmann::json::parse_event_t;
            if ((event == event_type::object_start || event == event_type::array_start) &&
                static_cast<std::size_t>(depth) >= options.max_depth)
                violates_policy = true;

            if (!options.reject_duplicate_keys)
                return true;
            if (event == event_type::object_start)
                object_keys[depth].clear();
            else if (event == event_type::key)
            {
                const auto& key = parsed.get_ref<const std::string&>();
                if (!object_keys[depth].emplace(key).second)
                    violates_policy = true;
            }
            else if (event == event_type::object_end)
                object_keys.erase(depth);
            return true;
        };

        auto parsed = nlohmann::json::parse(input, callback);
        if (violates_policy)
            return std::unexpected(make_error_code(errc::parse_failed));
        return from_nlohmann(parsed);
    }
    catch (const nlohmann::json::exception&)
    {
        return std::unexpected(make_error_code(errc::parse_failed));
    }
}

namespace detail {
    namespace {
        // RFC 3629 well-formedness, the same set nlohmann's UTF-8 decoder
        // accepts: no overlongs, surrogates or code points above U+10FFFF.
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
        // Floating point keeps the backend's own formatting (shortest
        // round-trip digits, "null" for non-finite values).
        try
        {
            out.append(nlohmann::json(value).dump());
            return {};
        }
        catch (const nlohmann::json::exception&)
        {
            return std::unexpected(make_error_code(errc::serialization_failed));
        }
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
    if (!prettify)
    {
        detail::json_writer out{256U};
        if (auto written = detail::append_document(out, value); !written)
            return std::unexpected(written.error());
        return std::move(out).take();
    }
    try
    {
        return to_nlohmann(value).dump(2);
    }
    catch (const nlohmann::json::exception&)
    {
        return std::unexpected(make_error_code(errc::serialization_failed));
    }
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
