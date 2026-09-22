module;

#include <glaze/json/generic.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

module cnetmod.json;

import std;

namespace cnetmod::json {
namespace {
    class structural_validator
    {
    public:
        structural_validator(std::string_view input, parse_options options)
            : input_(input), options_(options)
        {
        }

        [[nodiscard]] auto validate() -> bool
        {
            skip_space();
            if (!scan_value(0)) return false;
            skip_space();
            return position_ == input_.size();
        }

    private:
        auto skip_space() -> void
        {
            while (position_ < input_.size() &&
                (input_[position_] == ' ' || input_[position_] == '\n' ||
                    input_[position_] == '\r' || input_[position_] == '\t'))
                ++position_;
        }

        [[nodiscard]] auto scan_string(std::string* decoded = nullptr) -> bool
        {
            if (position_ >= input_.size() || input_[position_] != '"') return false;
            const auto begin = position_++;
            bool escaped = false;
            while (position_ < input_.size())
            {
                const auto character = input_[position_++];
                if (escaped)
                {
                    escaped = false;
                    continue;
                }
                if (character == '\\')
                {
                    escaped = true;
                    continue;
                }
                if (character != '"') continue;
                if (decoded != nullptr)
                {
                    const auto token = input_.substr(begin, position_ - begin);
                    auto key = glz::read_json<std::string>(token);
                    if (!key) return false;
                    *decoded = std::move(*key);
                }
                return true;
            }
            return false;
        }

        [[nodiscard]] auto scan_compound(char open, char close,
            std::size_t depth) -> bool
        {
            if (depth >= options_.max_depth) return false;
            ++position_;
            skip_space();
            if (position_ < input_.size() && input_[position_] == close)
            {
                ++position_;
                return true;
            }

            std::unordered_set<std::string> keys;
            while (position_ < input_.size())
            {
                if (open == '{')
                {
                    std::string key;
                    if (!scan_string(&key)) return false;
                    if (options_.reject_duplicate_keys && !keys.emplace(std::move(key)).second)
                        return false;
                    skip_space();
                    if (position_ >= input_.size() || input_[position_++] != ':') return false;
                    skip_space();
                }
                if (!scan_value(depth + 1)) return false;
                skip_space();
                if (position_ >= input_.size()) return false;
                if (input_[position_] == close)
                {
                    ++position_;
                    return true;
                }
                if (input_[position_++] != ',') return false;
                skip_space();
            }
            return false;
        }

        [[nodiscard]] auto scan_value(std::size_t depth) -> bool
        {
            skip_space();
            if (position_ >= input_.size()) return false;
            if (input_[position_] == '{') return scan_compound('{', '}', depth);
            if (input_[position_] == '[') return scan_compound('[', ']', depth);
            if (input_[position_] == '"') return scan_string();

            const auto begin = position_;
            while (position_ < input_.size() && input_[position_] != ',' &&
                input_[position_] != ']' && input_[position_] != '}' &&
                input_[position_] != ' ' && input_[position_] != '\n' &&
                input_[position_] != '\r' && input_[position_] != '\t')
                ++position_;
            return position_ != begin;
        }

        std::string_view input_;
        parse_options options_;
        std::size_t position_ = 0;
    };

    class category final : public std::error_category
    {
    public:
        [[nodiscard]] auto name() const noexcept -> const char* override { return "cnetmod.json"; }
        [[nodiscard]] auto message(int value) const -> std::string override
        {
            switch (static_cast<errc>(value))
            {
            case errc::parse_failed: return "JSON parsing failed";
            case errc::serialization_failed: return "JSON serialization failed";
            case errc::type_mismatch: return "JSON value has an incompatible type";
            case errc::missing_field: return "JSON document is missing a required field";
            case errc::unknown_field: return "JSON document contains an unknown field";
            }
            return "unknown JSON error";
        }
    };

    auto from_glaze(const glz::generic_u64& source) -> document
    {
        if (source.is_null()) return nullptr;
        if (source.is_boolean()) return source.get<bool>();
        if (source.is_string()) return source.get<std::string>();
        if (source.is_uint64()) return source.get<std::uint64_t>();
        if (source.is_int64()) return source.get<std::int64_t>();
        if (source.is_double()) return source.get<double>();
        if (source.is_array())
        {
            auto result = document::array();
            for (const auto& entry : source.get_array()) result.push_back(from_glaze(entry));
            return result;
        }
        auto result = document::object();
        for (const auto& [key, entry] : source.get_object()) result[key] = from_glaze(entry);
        return result;
    }

    auto to_glaze(const document& source) -> glz::generic_u64
    {
        if (source.is_null()) return nullptr;
        if (source.is_boolean()) return source.get<bool>();
        if (source.is_string()) return source.get<std::string>();
        if (source.is_number_unsigned()) return source.get<std::uint64_t>();
        if (source.is_number_integer()) return source.get<std::int64_t>();
        if (source.is_number_float()) return source.get<double>();
        if (source.is_array())
        {
            glz::generic_u64 result(glz::generic_u64::array_t{});
            auto& values = result.get_array();
            values.reserve(source.size());
            for (const auto& entry : source) values.push_back(to_glaze(entry));
            return result;
        }
        glz::generic_u64 result(glz::generic_u64::object_t{});
        for (const auto& [key, entry] :
            source.get_ref<const document::object_type&>())
            result[key] = to_glaze(entry);
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
    if (!structural_validator{input, options}.validate())
        return std::unexpected(make_error_code(errc::parse_failed));
    auto parsed = glz::read_json<glz::generic_u64>(input);
    if (!parsed) return std::unexpected(make_error_code(errc::parse_failed));
    return from_glaze(*parsed);
}

auto write_document(const document& value) -> std::expected<std::string, std::error_code>
{
    auto encoded = glz::write_json(to_glaze(value));
    if (!encoded) return std::unexpected(make_error_code(errc::serialization_failed));
    return std::move(*encoded);
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
        std::istreambuf_iterator<char>{}}, callback, allow_exceptions,
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
