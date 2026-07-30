module cnetmod.protocol.mongodb;

import std;
import :error;
import :bson_document;

namespace cnetmod::mongodb {
namespace {
    template <class T>
    void append_little_endian(std::vector<std::byte>& out, T value)
    {
        using U = std::make_unsigned_t<T>;
        auto bits = static_cast<U>(value);
        for (std::size_t i{}; i < sizeof(T); ++i)
            out.push_back(static_cast<std::byte>((bits >> (i * 8)) & 0xff));
    }

    template <class T>
    auto read_little_endian(std::span<const std::byte> bytes, std::size_t& pos)
        -> std::optional<T>
    {
        if (bytes.size() - pos < sizeof(T))
            return {};
        using U = std::make_unsigned_t<T>;
        U bits{};
        for (std::size_t i{}; i < sizeof(T); ++i)
            bits |= static_cast<U>(std::to_integer<unsigned>(bytes[pos++])) << (i * 8);
        return static_cast<T>(bits);
    }

    auto cstring(std::span<const std::byte> bytes, std::size_t& pos)
        -> std::optional<std::string>
    {
        auto begin = pos;
        while (pos < bytes.size() && bytes[pos] != std::byte{})
            ++pos;
        if (pos == bytes.size())
            return {};
        std::string result(reinterpret_cast<const char*>(bytes.data() + begin),
            pos - begin);
        ++pos;
        return result;
    }

    auto encode_document(const bson_document& document, const bson_limits& limits,
        std::size_t depth) -> result<std::vector<std::byte>>;

    auto append_element(std::vector<std::byte>& out, std::string_view key,
        const bson_value& value, const bson_limits& limits, std::size_t depth)
        -> result<void>
    {
        if (key.find('\0') != std::string_view::npos)
            return std::unexpected(make_error(error_code::invalid_bson,
                "BSON element name contains NUL"));
        auto add_key = [&]
        {
            out.insert(out.end(), reinterpret_cast<const std::byte*>(key.data()),
                reinterpret_cast<const std::byte*>(key.data() + key.size()));
            out.push_back(std::byte{});
        };
        auto add_nested = [&](std::uint8_t tag, const bson_document& nested)
            -> result<void>
        {
            auto encoded = encode_document(nested, limits, depth + 1);
            if (!encoded)
                return std::unexpected(encoded.error());
            out.push_back(static_cast<std::byte>(tag));
            add_key();
            out.insert(out.end(), encoded->begin(), encoded->end());
            return {};
        };
        return std::visit([&](const auto& item) -> result<void>
            {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::same_as<T, bson_null>)
                {
                    out.push_back(std::byte{0x0a});
                    add_key();
                }
                else if constexpr (std::same_as<T, bson_undefined>)
                {
                    out.push_back(std::byte{0x06});
                    add_key();
                }
                else if constexpr (std::same_as<T, double>)
                {
                    out.push_back(std::byte{0x01});
                    add_key();
                    append_little_endian(out, std::bit_cast<std::uint64_t>(item));
                }
                else if constexpr (std::same_as<T, std::string>)
                {
                    if (item.size() > limits.max_string_bytes ||
                        item.size() > static_cast<std::size_t>(
                                          std::numeric_limits<std::int32_t>::max() - 1))
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "BSON string exceeds configured size limit"));
                    out.push_back(std::byte{0x02});
                    add_key();
                    append_little_endian(out, static_cast<std::int32_t>(item.size() + 1));
                    out.insert(out.end(), reinterpret_cast<const std::byte*>(item.data()),
                        reinterpret_cast<const std::byte*>(item.data() + item.size()));
                    out.push_back(std::byte{});
                }
                else if constexpr (std::same_as<T, std::shared_ptr<bson_document>>)
                {
                    if (!item)
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "null BSON document pointer"));
                    return add_nested(0x03, *item);
                }
                else if constexpr (std::same_as<T, std::shared_ptr<bson_array>>)
                {
                    if (!item)
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "null BSON array pointer"));
                    bson_document array_document;
                    for (std::size_t i{}; i < item->size(); ++i)
                        array_document.append(std::to_string(i), (*item)[i]);
                    return add_nested(0x04, array_document);
                }
                else if constexpr (std::same_as<T, bson_binary>)
                {
                    if (item.bytes.size() > static_cast<std::size_t>(
                                                std::numeric_limits<std::int32_t>::max()))
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "BSON binary exceeds wire limit"));
                    out.push_back(std::byte{0x05});
                    add_key();
                    const auto old_binary_extra = item.subtype == 0x02 ? std::size_t{4} : 0;
                    if (item.bytes.size() + old_binary_extra > static_cast<std::size_t>(
                                                                   std::numeric_limits<std::int32_t>::max()))
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "BSON binary exceeds wire limit"));
                    append_little_endian(out, static_cast<std::int32_t>(item.bytes.size() + old_binary_extra));
                    out.push_back(static_cast<std::byte>(item.subtype));
                    if (item.subtype == 0x02)
                        append_little_endian(out, static_cast<std::int32_t>(item.bytes.size()));
                    out.insert(out.end(), item.bytes.begin(), item.bytes.end());
                }
                else if constexpr (std::same_as<T, bson_object_id>)
                {
                    out.push_back(std::byte{0x07});
                    add_key();
                    out.insert(out.end(), item.bytes.begin(), item.bytes.end());
                }
                else if constexpr (std::same_as<T, bool>)
                {
                    out.push_back(std::byte{0x08});
                    add_key();
                    out.push_back(item ? std::byte{1} : std::byte{});
                }
                else if constexpr (std::same_as<T, bson_datetime>)
                {
                    out.push_back(std::byte{0x09});
                    add_key();
                    append_little_endian(out, item.milliseconds_since_epoch);
                }
                else if constexpr (std::same_as<T, bson_regex>)
                {
                    if (item.pattern.find('\0') != std::string::npos ||
                        item.options.find('\0') != std::string::npos)
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "BSON regex contains NUL"));
                    out.push_back(std::byte{0x0b});
                    add_key();
                    for (auto text : {std::string_view(item.pattern), std::string_view(item.options)})
                    {
                        out.insert(out.end(), reinterpret_cast<const std::byte*>(text.data()),
                            reinterpret_cast<const std::byte*>(text.data() + text.size()));
                        out.push_back(std::byte{});
                    }
                }
                else if constexpr (std::same_as<T, bson_javascript_code> ||
                    std::same_as<T, bson_symbol>)
                {
                    const auto& text = [&]() -> const std::string&
                    {
                        if constexpr (std::same_as<T, bson_javascript_code>)
                            return item.source;
                        else
                            return item.name;
                    }();
                    if (text.size() > limits.max_string_bytes ||
                        text.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - 1))
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "BSON code or symbol exceeds configured size limit"));
                    out.push_back(std::byte{std::same_as<T, bson_javascript_code> ? 0x0d : 0x0e});
                    add_key();
                    append_little_endian(out, static_cast<std::int32_t>(text.size() + 1));
                    out.insert(out.end(), reinterpret_cast<const std::byte*>(text.data()),
                        reinterpret_cast<const std::byte*>(text.data() + text.size()));
                    out.push_back(std::byte{});
                }
                else if constexpr (std::same_as<T, bson_db_pointer>)
                {
                    if (item.name_space.size() > limits.max_string_bytes ||
                        item.name_space.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - 1))
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "BSON DBPointer namespace exceeds configured size limit"));
                    out.push_back(std::byte{0x0c});
                    add_key();
                    append_little_endian(out, static_cast<std::int32_t>(item.name_space.size() + 1));
                    out.insert(out.end(), reinterpret_cast<const std::byte*>(item.name_space.data()),
                        reinterpret_cast<const std::byte*>(item.name_space.data() + item.name_space.size()));
                    out.push_back(std::byte{});
                    out.insert(out.end(), item.object_id.bytes.begin(), item.object_id.bytes.end());
                }
                else if constexpr (std::same_as<T, bson_javascript_code_with_scope>)
                {
                    if (!item.scope || item.source.size() > limits.max_string_bytes ||
                        item.source.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - 1))
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "invalid BSON JavaScript-with-scope value"));
                    auto scope = encode_document(*item.scope, limits, depth + 1);
                    if (!scope)
                        return std::unexpected(scope.error());
                    const auto total = std::size_t{4 + 4 + 1} + item.source.size() + scope->size();
                    if (total > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
                        return std::unexpected(make_error(error_code::invalid_bson,
                            "BSON JavaScript-with-scope exceeds wire limit"));
                    out.push_back(std::byte{0x0f});
                    add_key();
                    append_little_endian(out, static_cast<std::int32_t>(total));
                    append_little_endian(out, static_cast<std::int32_t>(item.source.size() + 1));
                    out.insert(out.end(), reinterpret_cast<const std::byte*>(item.source.data()),
                        reinterpret_cast<const std::byte*>(item.source.data() + item.source.size()));
                    out.push_back(std::byte{});
                    out.insert(out.end(), scope->begin(), scope->end());
                }
                else if constexpr (std::same_as<T, bson_decimal128>)
                {
                    out.push_back(std::byte{0x13});
                    add_key();
                    out.insert(out.end(), item.bytes.begin(), item.bytes.end());
                }
                else if constexpr (std::same_as<T, bson_min_key>)
                {
                    out.push_back(std::byte{0xff});
                    add_key();
                }
                else if constexpr (std::same_as<T, bson_max_key>)
                {
                    out.push_back(std::byte{0x7f});
                    add_key();
                }
                else if constexpr (std::same_as<T, bson_timestamp>)
                {
                    out.push_back(std::byte{0x11});
                    add_key();
                    append_little_endian(out, item.increment);
                    append_little_endian(out, item.seconds);
                }
                else if constexpr (std::same_as<T, std::int32_t>)
                {
                    out.push_back(std::byte{0x10});
                    add_key();
                    append_little_endian(out, item);
                }
                else if constexpr (std::same_as<T, std::int64_t>)
                {
                    out.push_back(std::byte{0x12});
                    add_key();
                    append_little_endian(out, item);
                }
                return {};
            },
            value.data());
    }

    auto encode_document(const bson_document& document, const bson_limits& limits,
        std::size_t depth) -> result<std::vector<std::byte>>
    {
        if (depth > limits.max_nesting_depth)
            return std::unexpected(make_error(error_code::invalid_bson,
                "BSON nesting depth exceeded"));
        std::vector<std::byte> out(4);
        for (const auto& [key, value] : document.elements())
        {
            auto appended = append_element(out, key, value, limits, depth);
            if (!appended)
                return std::unexpected(appended.error());
            if (out.size() + 1 > limits.max_document_bytes)
                return std::unexpected(make_error(error_code::invalid_bson,
                    "BSON document exceeds configured size limit"));
        }
        out.push_back(std::byte{});
        if (out.size() > static_cast<std::size_t>(
                             std::numeric_limits<std::int32_t>::max()))
            return std::unexpected(make_error(error_code::invalid_bson,
                "BSON document exceeds wire limit"));
        auto length = static_cast<std::uint32_t>(out.size());
        for (std::size_t i{}; i < 4; ++i)
            out[i] = static_cast<std::byte>((length >> (i * 8)) & 0xff);
        return out;
    }

    auto decode_document(std::span<const std::byte> input,
        const bson_limits& limits, std::size_t depth) -> result<bson_document>
    {
        if (depth > limits.max_nesting_depth || input.size() < 5)
            return std::unexpected(make_error(error_code::invalid_bson,
                "invalid BSON document depth or length"));
        std::size_t pos{};
        auto length = read_little_endian<std::int32_t>(input, pos);
        if (!length || *length < 5 || static_cast<std::size_t>(*length) != input.size() ||
            input.size() > limits.max_document_bytes || input.back() != std::byte{})
            return std::unexpected(make_error(error_code::invalid_bson,
                "invalid BSON document length"));
        bson_document document;
        // The trailing NUL is part of the BSON document and must be consumed.
        // Stopping at size - 1 leaves that terminator unread and incorrectly
        // reports every non-empty valid document as having trailing bytes.
        while (pos < input.size())
        {
            auto tag = std::to_integer<std::uint8_t>(input[pos++]);
            if (tag == 0)
                break;
            auto key = cstring(input, pos);
            if (!key)
                return std::unexpected(make_error(error_code::invalid_bson,
                    "unterminated BSON element name"));
            bson_value value;
            switch (tag)
            {
            case 0x01:
            {
                auto bits = read_little_endian<std::uint64_t>(input, pos);
                if (!bits)
                    goto truncated;
                value = std::bit_cast<double>(*bits);
                break;
            }
            case 0x02:
            {
                auto n = read_little_endian<std::int32_t>(input, pos);
                if (!n || *n <= 0 || static_cast<std::size_t>(*n) > input.size() - pos ||
                    static_cast<std::size_t>(*n - 1) > limits.max_string_bytes ||
                    input[pos + *n - 1] != std::byte{})
                    goto truncated;
                value = std::string(reinterpret_cast<const char*>(input.data() + pos), *n - 1);
                pos += *n;
                break;
            }
            case 0x03:
            case 0x04:
            {
                auto nested_start = pos;
                auto nested_length = read_little_endian<std::int32_t>(input, pos);
                pos = nested_start;
                if (!nested_length || *nested_length < 5 ||
                    static_cast<std::size_t>(*nested_length) > input.size() - pos)
                    goto truncated;
                auto nested = decode_document(input.subspan(pos, *nested_length), limits, depth + 1);
                if (!nested)
                    return std::unexpected(nested.error());
                pos += *nested_length;
                if (tag == 0x03)
                    value = std::move(*nested);
                else
                {
                    bson_array array;
                    array.reserve(nested->size());
                    for (std::size_t i{}; i < nested->size(); ++i)
                    {
                        auto entry = nested->find(std::to_string(i));
                        if (!entry)
                            return std::unexpected(make_error(error_code::invalid_bson,
                                "BSON array keys are not consecutive"));
                        array.push_back(*entry);
                    }
                    value = std::move(array);
                }
                break;
            }
            case 0x05:
            {
                auto n = read_little_endian<std::int32_t>(input, pos);
                if (!n || *n < 0 || pos >= input.size() ||
                    static_cast<std::size_t>(*n) > input.size() - pos - 1)
                    goto truncated;
                auto subtype = std::to_integer<std::uint8_t>(input[pos++]);
                bson_binary binary{.subtype = subtype};
                if (subtype == 0x02)
                {
                    auto old_length = read_little_endian<std::int32_t>(input, pos);
                    if (!old_length || *old_length < 0 || *n != *old_length + 4 ||
                        static_cast<std::size_t>(*old_length) > input.size() - pos)
                        goto truncated;
                    binary.bytes.assign(input.begin() + pos, input.begin() + pos + *old_length);
                    pos += *old_length;
                }
                else
                {
                    binary.bytes.assign(input.begin() + pos, input.begin() + pos + *n);
                    pos += *n;
                }
                value = std::move(binary);
                break;
            }
            case 0x06:
                value = bson_undefined{};
                break;
            case 0x07:
            {
                if (input.size() - pos < 12)
                    goto truncated;
                bson_object_id id;
                std::copy_n(input.begin() + pos, 12, id.bytes.begin());
                pos += 12;
                value = id;
                break;
            }
            case 0x08:
                if (pos >= input.size() || (input[pos] != std::byte{} && input[pos] != std::byte{1}))
                    goto truncated;
                value = input[pos++] == std::byte{1};
                break;
            case 0x09:
            {
                auto v = read_little_endian<std::int64_t>(input, pos);
                if (!v)
                    goto truncated;
                value = bson_datetime{*v};
                break;
            }
            case 0x0a:
                value = nullptr;
                break;
            case 0x0b:
            {
                auto pattern = cstring(input, pos);
                auto options = cstring(input, pos);
                if (!pattern || !options)
                    goto truncated;
                value = bson_regex{std::move(*pattern), std::move(*options)};
                break;
            }
            case 0x0c:
            {
                auto n = read_little_endian<std::int32_t>(input, pos);
                if (!n || *n <= 0 || static_cast<std::size_t>(*n) > input.size() - pos ||
                    static_cast<std::size_t>(*n - 1) > limits.max_string_bytes ||
                    input[pos + *n - 1] != std::byte{} ||
                    input.size() - (pos + *n) < 12)
                    goto truncated;
                bson_db_pointer pointer;
                pointer.name_space.assign(reinterpret_cast<const char*>(input.data() + pos), *n - 1);
                pos += *n;
                std::copy_n(input.begin() + pos, 12, pointer.object_id.bytes.begin());
                pos += 12;
                value = std::move(pointer);
                break;
            }
            case 0x0d:
            case 0x0e:
            {
                auto n = read_little_endian<std::int32_t>(input, pos);
                if (!n || *n <= 0 || static_cast<std::size_t>(*n) > input.size() - pos ||
                    static_cast<std::size_t>(*n - 1) > limits.max_string_bytes ||
                    input[pos + *n - 1] != std::byte{})
                    goto truncated;
                std::string text(reinterpret_cast<const char*>(input.data() + pos), *n - 1);
                pos += *n;
                if (tag == 0x0d)
                    value = bson_javascript_code{std::move(text)};
                else
                    value = bson_symbol{std::move(text)};
                break;
            }
            case 0x0f:
            {
                auto total = read_little_endian<std::int32_t>(input, pos);
                const auto value_start = pos - 4;
                auto code_length = read_little_endian<std::int32_t>(input, pos);
                if (!total || *total < 14 || !code_length || *code_length <= 0 ||
                    static_cast<std::size_t>(*total) > input.size() - value_start ||
                    static_cast<std::size_t>(*code_length) > input.size() - pos ||
                    input[pos + *code_length - 1] != std::byte{})
                    goto truncated;
                std::string source(reinterpret_cast<const char*>(input.data() + pos), *code_length - 1);
                pos += *code_length;
                if (pos - value_start > static_cast<std::size_t>(*total))
                    goto truncated;
                const auto scope_size = static_cast<std::size_t>(*total) - (pos - value_start);
                auto scope = decode_document(input.subspan(pos, scope_size), limits, depth + 1);
                if (!scope)
                    return std::unexpected(scope.error());
                pos += scope_size;
                value = bson_javascript_code_with_scope{std::move(source),
                    std::make_shared<bson_document>(std::move(*scope))};
                break;
            }
            case 0x10:
            {
                auto v = read_little_endian<std::int32_t>(input, pos);
                if (!v)
                    goto truncated;
                value = *v;
                break;
            }
            case 0x11:
            {
                auto i = read_little_endian<std::uint32_t>(input, pos);
                auto s = read_little_endian<std::uint32_t>(input, pos);
                if (!i || !s)
                    goto truncated;
                value = bson_timestamp{*i, *s};
                break;
            }
            case 0x12:
            {
                auto v = read_little_endian<std::int64_t>(input, pos);
                if (!v)
                    goto truncated;
                value = *v;
                break;
            }
            case 0x13:
            {
                if (input.size() - pos < 16)
                    goto truncated;
                bson_decimal128 decimal;
                std::copy_n(input.begin() + pos, 16, decimal.bytes.begin());
                pos += 16;
                value = decimal;
                break;
            }
            case 0x7f:
                value = bson_max_key{};
                break;
            case 0xff:
                value = bson_min_key{};
                break;
            default:
                return std::unexpected(make_error(error_code::invalid_bson,
                    "unsupported BSON element type " + std::to_string(tag)));
            }
            document.append(std::move(*key), std::move(value));
            continue;
        truncated:
            return std::unexpected(make_error(error_code::invalid_bson,
                "truncated or invalid BSON element"));
        }
        if (pos != input.size())
            return std::unexpected(make_error(error_code::invalid_bson,
                "BSON document has trailing bytes"));
        return document;
    }
} // namespace

bson_value::bson_value() noexcept : data_(bson_null{}) {}

bson_value::bson_value(std::nullptr_t) noexcept : data_(bson_null{}) {}

bson_value::bson_value(bson_undefined value) noexcept : data_(value) {}

bson_value::bson_value(double value) noexcept : data_(value) {}

bson_value::bson_value(std::string value) : data_(std::move(value)) {}

bson_value::bson_value(std::string_view value) : data_(std::string(value)) {}

bson_value::bson_value(const char* value) : data_(std::string(value ? value : "")) {}

bson_value::bson_value(bson_document value) : data_(std::make_shared<bson_document>(std::move(value))) {}

bson_value::bson_value(bson_array value) : data_(std::make_shared<bson_array>(std::move(value))) {}

bson_value::bson_value(bson_binary value) : data_(std::move(value)) {}

bson_value::bson_value(bson_object_id value) noexcept : data_(value) {}

bson_value::bson_value(bool value) noexcept : data_(value) {}

bson_value::bson_value(bson_datetime value) noexcept : data_(value) {}

bson_value::bson_value(bson_timestamp value) noexcept : data_(value) {}

bson_value::bson_value(bson_regex value) : data_(std::move(value)) {}

bson_value::bson_value(bson_javascript_code value) : data_(std::move(value)) {}

bson_value::bson_value(bson_javascript_code_with_scope value) : data_(std::move(value)) {}

bson_value::bson_value(bson_symbol value) : data_(std::move(value)) {}

bson_value::bson_value(bson_db_pointer value) : data_(std::move(value)) {}

bson_value::bson_value(bson_decimal128 value) noexcept : data_(value) {}

bson_value::bson_value(bson_min_key value) noexcept : data_(value) {}

bson_value::bson_value(bson_max_key value) noexcept : data_(value) {}

bson_value::bson_value(std::int32_t value) noexcept : data_(value) {}

bson_value::bson_value(std::int64_t value) noexcept : data_(value) {}

auto bson_value::data() const noexcept -> const storage&
{
    return data_;
}

auto bson_value::data() noexcept -> storage&
{
    return data_;
}

auto bson_value::as_document() const noexcept -> const bson_document*
{
    auto p = std::get_if<std::shared_ptr<bson_document>>(&data_);
    return p && *p ? p->get() : nullptr;
}

auto bson_value::as_array() const noexcept -> const bson_array*
{
    auto p = std::get_if<std::shared_ptr<bson_array>>(&data_);
    return p && *p ? p->get() : nullptr;
}

bson_document::bson_document(std::initializer_list<element> fields) : fields_(fields) {}

auto bson_document::append(std::string key, bson_value value) -> bson_document&
{
    fields_.emplace_back(std::move(key), std::move(value));
    return *this;
}

auto bson_document::set(std::string key, bson_value value) -> bson_document&
{
    for (auto& [existing, current] : fields_)
        if (existing == key)
        {
            current = std::move(value);
            return *this;
        }
    return append(std::move(key), std::move(value));
}

auto bson_document::find(std::string_view key) const noexcept -> const bson_value*
{
    for (const auto& [existing, value] : fields_)
        if (existing == key)
            return &value;
    return nullptr;
}

auto bson_document::contains(std::string_view key) const noexcept -> bool
{
    return find(key) != nullptr;
}

auto bson_document::elements() const noexcept -> const std::vector<element>&
{
    return fields_;
}

auto bson_document::size() const noexcept -> std::size_t
{
    return fields_.size();
}

auto encode_bson_document(const bson_document& document, bson_limits limits)
    -> result<std::vector<std::byte>>
{
    return encode_document(document, limits, 0);
}

auto decode_bson_document(std::span<const std::byte> bytes, bson_limits limits)
    -> result<bson_document>
{
    return decode_document(bytes, limits, 0);
}

} // namespace cnetmod::mongodb
