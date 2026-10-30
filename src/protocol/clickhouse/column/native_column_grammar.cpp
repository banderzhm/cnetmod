module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;
#include "../wire/native_buffer.hpp"
#include "native_column.hpp"

namespace cnetmod::clickhouse::detail {
namespace {
    auto trim(std::string_view text) -> std::string_view
    {
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ')
            text.remove_suffix(1);
        return text;
    }

    auto arguments(std::string_view text) -> std::vector<std::string_view>
    {
        std::vector<std::string_view> result;
        std::size_t start{}, depth{};
        char quote{};
        for (std::size_t i{}; i < text.size(); ++i)
        {
            const char c = text[i];
            if (quote)
            {
                if (c == '\\')
                {
                    if (++i == text.size())
                        fail("truncated quoted type");
                }
                else if (c == quote)
                    quote = 0;
            }
            else if (c == '\'' || c == '`' || c == '"')
                quote = c;
            else if (c == '(')
                ++depth;
            else if (c == ')')
            {
                if (!depth)
                    fail("unbalanced Native type");
                --depth;
            }
            else if (c == ',' && !depth)
            {
                result.push_back(trim(text.substr(start, i - start)));
                start = i + 1;
            }
        }
        if (quote || depth)
            fail("unbalanced Native type");
        if (!text.empty())
            result.push_back(trim(text.substr(start)));
        return result;
    }

    auto number(std::string_view text) -> std::size_t
    {
        std::size_t value{};
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc{} || end != text.data() + text.size())
            fail("invalid Native type parameter");
        return value;
    }

    auto parse(std::string_view text, const limits& bounds, std::size_t depth) -> type_descriptor
    {
        if (depth > bounds.max_depth)
            fail("Native type nesting exceeds limit");
        text = trim(text);
        const auto open = text.find('(');
        const auto name = trim(text.substr(0, open));
        std::vector<std::string_view> args;
        if (open != text.npos)
        {
            if (text.back() != ')')
                fail("unbalanced Native type");
            args = arguments(text.substr(open + 1, text.size() - open - 2));
        }
        using kind = type_descriptor::kind;
        const auto leaf = [&](kind tag, std::size_t width) -> type_descriptor
        {
            return {tag, width, {}};
        };
        if (name == "String" && args.empty())
            return leaf(kind::string, 0);
        if (name == "Nothing" && args.empty())
            return leaf(kind::nothing, 0);
        if (name == "Bool" && args.empty())
            return leaf(kind::unsigned_integer, 1);
        if (name == "FixedString" && args.size() == 1)
        {
            const auto width = number(args[0]);
            if (!width || width > bounds.max_string_bytes)
                fail("invalid FixedString width");
            return leaf(kind::fixed, width);
        }
        if (args.size() > bounds.max_columns)
            fail("Native type field count exceeds limit");
        if ((name == "UUID" || name == "IPv6" || name == "IPv4" || name == "Date" || name == "Date32") && open != text.npos)
            fail("unexpected Native type parameters");
        if (name == "DateTime" && (args.size() > 1 || (args.size() == 1 && (args[0].size() < 2 || args[0].front() != '\'' || args[0].back() != '\''))))
            fail("invalid DateTime timezone");
        if (name == "UUID" || name == "IPv6")
            return leaf(kind::fixed, 16);
        if (name == "IPv4" || name == "DateTime")
            return leaf(kind::unsigned_integer, 4);
        if (name == "Date")
            return leaf(kind::unsigned_integer, 2);
        if (name == "Date32")
            return leaf(kind::signed_integer, 4);
        if (name == "DateTime64")
        {
            if (args.empty() || args.size() > 2 || number(args[0]) > 9)
                fail("invalid DateTime64 scale");
            return leaf(kind::signed_integer, 8);
        }
        if (name == "Decimal" && args.size() == 2)
        {
            auto precision = number(args[0]);
            if (!precision || precision > 76 || number(args[1]) > precision)
                fail("invalid Decimal precision/scale");
            auto width = precision <= 9 ? 4U : precision <= 18 ? 8U
                : precision <= 38                              ? 16U
                                                               : 32U;
            return leaf(width <= 8 ? kind::signed_integer : kind::fixed, width);
        }
        for (const auto prefix : {std::string_view{"UInt"}, std::string_view{"Int"}, std::string_view{"Float"}, std::string_view{"Decimal"}})
        {
            if (!name.starts_with(prefix))
                continue;
            const auto bits = number(name.substr(prefix.size()));
            const bool floating = prefix == "Float", decimal = prefix == "Decimal";
            if (floating ? (bits != 32 && bits != 64) : (bits != 8 && bits != 16 && bits != 32 && bits != 64 && bits != 128 && bits != 256))
                fail("unsupported Native numeric width");
            if (decimal && (bits < 32 || args.size() != 1 || number(args[0]) > (bits == 32 ? 9U : bits == 64 ? 18U
                                                                                       : bits == 128         ? 38U
                                                                                                             : 76U)))
                fail("invalid Decimal scale");
            if (!decimal && !args.empty())
                fail("invalid Native numeric parameters");
            return leaf(bits > 64 ? kind::fixed : floating ? kind::floating
                    : prefix == "UInt"                     ? kind::unsigned_integer
                                                           : kind::signed_integer,
                bits / 8);
        }
        if (name == "Enum8" || name == "Enum16")
        {
            if (args.empty())
                fail("empty Native enum");
            return leaf(kind::signed_integer, name == "Enum8" ? 1 : 2);
        }
        if (name == "LowCardinality" && args.size() == 1)
            return {kind::low_cardinality, 0, {parse(args[0], bounds, depth + 1)}};
        if ((name == "Nullable" || name == "Array") && args.size() == 1)
            return {name == "Nullable" ? kind::nullable : kind::array, 0, {parse(args[0], bounds, depth + 1)}};
        if (name == "Map" && args.size() == 2)
            return {kind::array, 0, {{kind::tuple, 0, {parse(args[0], bounds, depth + 2), parse(args[1], bounds, depth + 2)}}}};
        if (name == "Tuple" && !args.empty())
        {
            type_descriptor result{kind::tuple, 0, {}};
            for (auto arg : args)
            {
                // Named tuple fields: skip the quoted/bare name, retaining its type.
                auto split = arg.find(' ');
                if (!arg.empty() && (arg.front() == '`' || arg.front() == '\"'))
                {
                    const char quote = arg.front();
                    std::size_t end = 1;
                    for (; end < arg.size(); ++end)
                    {
                        if (arg[end] == '\\')
                            ++end;
                        else if (arg[end] == quote)
                            break;
                    }
                    if (end + 1 >= arg.size() || arg[end + 1] != ' ')
                        fail("invalid named Tuple field");
                    split = end + 1;
                }
                const auto parenthesis = arg.find('(');
                if (split != arg.npos && (parenthesis == arg.npos || split < parenthesis))
                    arg = trim(arg.substr(split + 1));
                result.children.push_back(parse(arg, bounds, depth + 1));
            }
            return result;
        }
        fail("unsupported Native column type: " + std::string(text));
    }
} // namespace

auto parse_type(std::string_view text, limits bounds) -> type_descriptor
{
    return parse(text, bounds, 0);
}
} // namespace cnetmod::clickhouse::detail
