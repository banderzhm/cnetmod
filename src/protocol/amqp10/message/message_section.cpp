module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :message_section;
import std;
import :amqp_value_codec;
import :protocol_error;

namespace cnetmod::amqp10 {
namespace {
    void section(encoder& e, std::uint64_t code, value body)
    {
        e.write_value(value::described(descriptor{code}, std::move(body)));
    }

    auto amap(const annotations& m) -> value
    {
        map v;
        for (const auto& [k, x] : m)
            v.emplace_back(value{k}, x);
        return value::make_map(std::move(v));
    }

    auto pmap(const application_properties& m) -> value
    {
        map v;
        for (const auto& [k, x] : m)
            v.emplace_back(value{k}, x);
        return value::make_map(std::move(v));
    }

    auto fields(const value& v) -> const list*
    {
        auto d = std::get_if<std::shared_ptr<described_value>>(&v.data);
        if (!d || !*d || !(*d)->body)
            return nullptr;
        auto l = std::get_if<std::shared_ptr<list>>(&(*d)->body->data);
        return l && *l ? l->get() : nullptr;
    }

    auto code(const value& v) -> std::optional<std::uint64_t>
    {
        auto d = std::get_if<std::shared_ptr<described_value>>(&v.data);
        if (!d || !*d)
            return {};
        if (auto p = std::get_if<std::uint64_t>(&(*d)->type.value))
            return *p;
        return {};
    }

    auto boolean(const value& v, bool fallback = false) -> bool
    {
        if (auto p = std::get_if<bool>(&v.data))
            return *p;
        return fallback;
    }

    auto number(const value& v) -> std::optional<std::uint32_t>
    {
        if (auto p = std::get_if<std::uint32_t>(&v.data))
            return *p;
        if (auto p = std::get_if<std::uint8_t>(&v.data))
            return *p;
        return {};
    }

    auto string_value(const value& v) -> std::string
    {
        if (auto p = std::get_if<std::string>(&v.data))
            return *p;
        if (auto p = std::get_if<symbol>(&v.data))
            return p->text;
        return {};
    }

    auto at(const list& v, std::size_t i) -> const value&
    {
        static value empty;
        return i < v.size() ? v[i] : empty;
    }

    void read_annotations(const value& body, annotations& out)
    {
        auto p = std::get_if<std::shared_ptr<map>>(&body.data);
        if (!p || !*p)
            return;
        for (const auto& [k, v] : **p)
            if (auto key = std::get_if<symbol>(&k.data))
                out[*key] = v;
    }
} // namespace

auto encode_message(const message& m) -> binary
{
    encoder e;
    if (m.header)
    {
        const auto& h = *m.header;
        section(e, 0x70,
            value::make_list(
                {value{h.durable}, value{h.priority},
                    h.ttl ? value{std::uint32_t(h.ttl->count())} : value{},
                    value{h.first_acquirer}, value{h.delivery_count}}));
    }
    if (!m.delivery_annotations.empty())
        section(e, 0x71, amap(m.delivery_annotations));
    if (!m.message_annotations.empty())
        section(e, 0x72, amap(m.message_annotations));
    if (m.properties)
    {
        const auto& p = *m.properties;
        section(
            e, 0x73,
            value::make_list(
                {p.message_id ? *p.message_id : value{},
                    p.user_id.empty() ? value{} : value{p.user_id},
                    p.to.empty() ? value{} : value{p.to},
                    p.subject.empty() ? value{} : value{p.subject},
                    p.reply_to.empty() ? value{} : value{p.reply_to},
                    p.correlation_id ? *p.correlation_id : value{},
                    p.content_type.empty() ? value{} : value{symbol{p.content_type}},
                    p.content_encoding.empty() ? value{}
                                               : value{symbol{p.content_encoding}},
                    p.absolute_expiry_time ? value{*p.absolute_expiry_time} : value{},
                    p.creation_time ? value{*p.creation_time} : value{},
                    p.group_id.empty() ? value{} : value{p.group_id},
                    p.group_sequence ? value{*p.group_sequence} : value{},
                    p.reply_to_group_id.empty() ? value{}
                                                : value{p.reply_to_group_id}}));
    }
    if (!m.application.empty())
        section(e, 0x74, pmap(m.application));
    std::visit(
        [&](const auto& body)
        {
            using T = std::remove_cvref_t<decltype(body)>;
            if constexpr (std::same_as<T, binary>)
                section(e, 0x75, value{body});
            else if constexpr (std::same_as<T, value>)
                section(e, 0x77, body);
            else
                for (const auto& seq : body)
                    section(e, 0x76, value::make_list(seq));
        },
        m.body);
    if (!m.footer.empty())
        section(e, 0x78, amap(m.footer));
    return e.release();
}

auto decode_message(std::span<const std::byte> b)
    -> std::expected<message, std::error_code>
{
    decoder d(b);
    message result;
    std::vector<list> sequences;
    while (d.remaining())
    {
        auto v = d.read_value();
        if (!v)
            return std::unexpected(v.error());
        auto c = code(*v);
        if (!c)
            return std::unexpected(make_error_code(errc::malformed_frame));
        auto described = std::get<std::shared_ptr<described_value>>(v->data);
        switch (*c)
        {
        case 0x70:
        {
            auto f = fields(*v);
            if (!f)
                return std::unexpected(make_error_code(errc::invalid_field));
            header_section h;
            h.durable = boolean(at(*f, 0));
            h.priority = static_cast<std::uint8_t>(number(at(*f, 1)).value_or(4));
            if (auto n = number(at(*f, 2)))
                h.ttl = std::chrono::milliseconds(*n);
            h.first_acquirer = boolean(at(*f, 3));
            h.delivery_count = number(at(*f, 4)).value_or(0);
            result.header = std::move(h);
            break;
        }
        case 0x71:
            read_annotations(*described->body, result.delivery_annotations);
            break;
        case 0x72:
            read_annotations(*described->body, result.message_annotations);
            break;
        case 0x73:
        {
            auto f = fields(*v);
            if (!f)
                return std::unexpected(make_error_code(errc::invalid_field));
            properties_section p;
            if (!at(*f, 0).is_null())
                p.message_id = at(*f, 0);
            if (auto x = std::get_if<binary>(&at(*f, 1).data))
                p.user_id = *x;
            p.to = string_value(at(*f, 2));
            p.subject = string_value(at(*f, 3));
            p.reply_to = string_value(at(*f, 4));
            if (!at(*f, 5).is_null())
                p.correlation_id = at(*f, 5);
            p.content_type = string_value(at(*f, 6));
            p.content_encoding = string_value(at(*f, 7));
            if (auto x = std::get_if<timestamp>(&at(*f, 8).data))
                p.absolute_expiry_time = *x;
            if (auto x = std::get_if<timestamp>(&at(*f, 9).data))
                p.creation_time = *x;
            p.group_id = string_value(at(*f, 10));
            p.group_sequence = number(at(*f, 11));
            p.reply_to_group_id = string_value(at(*f, 12));
            result.properties = std::move(p);
            break;
        }
        case 0x74:
        {
            auto entries = std::get_if<std::shared_ptr<map>>(&described->body->data);
            if (entries && *entries)
                for (const auto& [k, x] : **entries)
                    if (auto key = std::get_if<std::string>(&k.data))
                        result.application[*key] = x;
            break;
        }
        case 0x75:
            if (auto p = std::get_if<binary>(&described->body->data))
                result.body = *p;
            else
                return std::unexpected(make_error_code(errc::invalid_field));
            break;
        case 0x76:
        {
            auto f = fields(*v);
            if (!f)
                return std::unexpected(make_error_code(errc::invalid_field));
            sequences.push_back(*f);
            result.body = sequences;
            break;
        }
        case 0x77:
            result.body = *described->body;
            break;
        case 0x78:
            read_annotations(*described->body, result.footer);
            break;
        default:
            return std::unexpected(make_error_code(errc::invalid_field));
        }
    }
    return result;
}
} // namespace cnetmod::amqp10
