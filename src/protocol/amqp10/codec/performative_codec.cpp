module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :performative_codec;
import std;
import :amqp_value_codec;
import :protocol_error;

namespace cnetmod::amqp10 {
namespace {
    auto described_list(std::uint64_t code, list fields) -> binary
    {
        while (!fields.empty() && fields.back().is_null())
            fields.pop_back();
        encoder e;
        e.write_value(
            value::described(descriptor{code}, value::make_list(std::move(fields))));
        return e.release();
    }

    auto u32(const value& v) -> std::optional<std::uint32_t>
    {
        if (auto p = std::get_if<std::uint32_t>(&v.data))
            return *p;
        if (auto p = std::get_if<std::uint8_t>(&v.data))
            return *p;
        return {};
    }

    auto boolean(const value& v, bool fallback = false) -> bool
    {
        if (auto p = std::get_if<bool>(&v.data))
            return *p;
        return fallback;
    }

    auto text(const value& v) -> std::string
    {
        if (auto p = std::get_if<std::string>(&v.data))
            return *p;
        if (auto p = std::get_if<symbol>(&v.data))
            return p->text;
        return {};
    }

    auto bin(const value& v) -> binary
    {
        if (auto p = std::get_if<binary>(&v.data))
            return *p;
        return {};
    }

    auto field(const list& v, std::size_t i) -> const value&
    {
        static const value null_value;
        return i < v.size() ? v[i] : null_value;
    }

    auto composite_value(std::uint64_t code, list fields) -> value
    {
        while (!fields.empty() && fields.back().is_null())
            fields.pop_back();
        return value::described(descriptor{code},
            value::make_list(std::move(fields)));
    }

    auto source_value(const source& s) -> value
    {
        list fields{
            s.address.empty() ? value{} : value{s.address},
            s.durable == terminus_durability::none
                ? value{}
                : value{std::uint32_t(s.durable)},
            s.expiry == expiry_policy::session_end
                ? value{}
                : value{symbol{s.expiry == expiry_policy::never
                          ? "never"
                          : s.expiry == expiry_policy::connection_close
                          ? "connection-close"
                          : "link-detach"}},
            s.timeout == 0 ? value{} : value{s.timeout},
            s.dynamic ? value{true} : value{},
        };
        if (s.distribution != distribution_mode::move || s.filter ||
            !s.outcomes.empty())
        {
            fields.resize(10);
            if (s.distribution != distribution_mode::move)
                fields[6] = value{symbol{"copy"}};
            if (s.filter)
                fields[7] = *s.filter;
            if (!s.outcomes.empty())
            {
                array outcomes;
                outcomes.reserve(s.outcomes.size());
                for (const auto& outcome : s.outcomes)
                    outcomes.emplace_back(outcome);
                fields[9] = value::make_array(std::move(outcomes));
            }
        }
        return composite_value(0x28, std::move(fields));
    }

    auto target_value(const target& t) -> value
    {
        return composite_value(
            0x29,
            {t.address.empty() ? value{} : value{t.address},
                t.durable == terminus_durability::none
                    ? value{}
                    : value{std::uint32_t(t.durable)},
                t.expiry == expiry_policy::session_end
                    ? value{}
                    : value{symbol{t.expiry == expiry_policy::never
                              ? "never"
                              : t.expiry == expiry_policy::connection_close
                              ? "connection-close"
                              : "link-detach"}},
                t.timeout == 0 ? value{} : value{t.timeout},
                t.dynamic ? value{true} : value{}});
    }

    auto outcome_value(const delivery_outcome& o) -> value
    {
        std::uint64_t code = 0x24;
        list fields;
        if (o.kind == outcome_kind::rejected)
        {
            code = 0x25;
            fields.push_back(value{});
        }
        else if (o.kind == outcome_kind::released)
            code = 0x26;
        else if (o.kind == outcome_kind::modified)
        {
            code = 0x27;
            fields = {value{o.delivery_failed}, value{o.undeliverable_here}};
        }
        else if (o.kind == outcome_kind::transactional)
        {
            code = 0x34;
            fields = {o.transaction_id ? value{*o.transaction_id} : value{},
                o.transaction_outcome ? outcome_value(*o.transaction_outcome)
                                      : value{}};
        }
        return value::described(descriptor{code},
            value::make_list(std::move(fields)));
    }

    auto unsettled_value(
        const std::vector<std::pair<binary, std::optional<delivery_outcome>>>& entries) -> value
    {
        if (entries.empty())
            return {};
        map result;
        for (const auto& [tag, state] : entries)
            result.emplace_back(value{tag}, state ? outcome_value(*state) : value{});
        return value::make_map(std::move(result));
    }

    auto descriptor_code(const value& v) -> std::optional<std::uint64_t>
    {
        auto p = std::get_if<std::shared_ptr<described_value>>(&v.data);
        if (!p || !*p)
            return {};
        return std::get_if<std::uint64_t>(&(*p)->type.value)
            ? std::optional{*std::get_if<std::uint64_t>(&(*p)->type.value)}
            : std::nullopt;
    }

    auto described_fields(const value& v) -> const list*
    {
        auto p = std::get_if<std::shared_ptr<described_value>>(&v.data);
        if (!p || !*p || !(*p)->body)
            return nullptr;
        auto l = std::get_if<std::shared_ptr<list>>(&(*p)->body->data);
        return l && *l ? l->get() : nullptr;
    }

    auto outcome_from_value(const value& v) -> std::optional<delivery_outcome>
    {
        auto c = descriptor_code(v);
        auto f = described_fields(v);
        if (!c)
            return {};
        delivery_outcome result;
        switch (*c)
        {
        case 0x24:
            result.kind = outcome_kind::accepted;
            break;
        case 0x25:
            result.kind = outcome_kind::rejected;
            break;
        case 0x26:
            result.kind = outcome_kind::released;
            break;
        case 0x27:
            result.kind = outcome_kind::modified;
            if (f)
            {
                result.delivery_failed = boolean(field(*f, 0));
                result.undeliverable_here = boolean(field(*f, 1));
            }
            break;
        case 0x34:
            result.kind = outcome_kind::transactional;
            if (f)
            {
                auto id = bin(field(*f, 0));
                if (!id.empty())
                    result.transaction_id = std::move(id);
                if (auto nested = outcome_from_value(field(*f, 1)))
                    result.transaction_outcome =
                        std::make_shared<delivery_outcome>(std::move(*nested));
            }
            break;
        default:
            return {};
        }
        return result;
    }
} // namespace

auto encode_performative(const performative& p) -> binary
{
    return std::visit(
        [](const auto& x) -> binary
        {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::same_as<T, open>)
                return described_list(
                    0x10, {value{x.container_id}, x.hostname.empty() ? value{} : value{x.hostname}, value{x.max_frame_size}, value{x.channel_max}, x.idle_timeout.count() ? value{std::uint32_t(x.idle_timeout.count())} : value{}});
            else if constexpr (std::same_as<T, begin>)
                return described_list(
                    0x11, {x.remote_channel ? value{*x.remote_channel} : value{}, value{x.next_outgoing_id}, value{x.incoming_window}, value{x.outgoing_window}, value{x.handle_max}});
            else if constexpr (std::same_as<T, attach>)
                return described_list(
                    0x12,
                    {value{x.name}, value{x.handle},
                        value{x.link_role == role::receiver},
                        x.snd_settle == sender_settle_mode::mixed
                            ? value{}
                            : value{std::uint8_t(x.snd_settle)},
                        x.rcv_settle == receiver_settle_mode::first
                            ? value{}
                            : value{std::uint8_t(x.rcv_settle)},
                        x.source_terminus ? source_value(*x.source_terminus) : value{},
                        x.transaction_coordinator
                            ? value::described(descriptor{std::uint64_t{0x30}},
                                  value::make_list({}))
                            : x.target_terminus ? target_value(*x.target_terminus)
                                                : value{},
                        unsettled_value(x.unsettled), value{x.incomplete_unsettled},
                        x.initial_delivery_count ? value{*x.initial_delivery_count}
                                                 : value{}});
            else if constexpr (std::same_as<T, flow>)
                return described_list(
                    0x13,
                    {x.next_incoming_id ? value{*x.next_incoming_id} : value{},
                        value{x.incoming_window}, value{x.next_outgoing_id},
                        value{x.outgoing_window}, x.handle ? value{*x.handle} : value{},
                        x.delivery_count ? value{*x.delivery_count} : value{},
                        x.link_credit ? value{*x.link_credit} : value{}, value{},
                        value{x.drain}, value{x.echo}});
            else if constexpr (std::same_as<T, transfer>)
            {
                auto b = described_list(
                    0x14,
                    {value{x.handle}, x.delivery_id ? value{*x.delivery_id} : value{},
                        x.delivery_tag.empty() ? value{} : value{x.delivery_tag},
                        x.message_format ? value{*x.message_format} : value{},
                        value{x.settled}, value{x.more},
                        x.state ? outcome_value(*x.state) : value{}, value{x.resume},
                        value{x.aborted}, value{x.batchable}});
                b.insert(b.end(), x.payload.begin(), x.payload.end());
                return b;
            }
            else if constexpr (std::same_as<T, disposition>)
                return described_list(
                    0x15,
                    {value{x.disposition_role == role::receiver}, value{x.first},
                        x.last ? value{*x.last} : value{}, value{x.settled},
                        x.state ? outcome_value(*x.state) : value{},
                        value{x.batchable}});
            else if constexpr (std::same_as<T, detach>)
                return described_list(0x16, {value{x.handle}, value{x.closed}});
            else if constexpr (std::same_as<T, end>)
                return described_list(0x17, {});
            else if constexpr (std::same_as<T, close>)
                return described_list(0x18, {});
            else if constexpr (std::same_as<T, coordinator>)
                return described_list(0x30, {});
            else if constexpr (std::same_as<T, declare>)
                return described_list(0x31,
                    {x.global_id ? value{*x.global_id} : value{}});
            else if constexpr (std::same_as<T, discharge>)
                return described_list(0x32, {value{x.transaction_id}, value{x.fail}});
            else
                return described_list(0x33, {value{x.transaction_id}});
        },
        p);
}

auto decode_performative(std::span<const std::byte> b)
    -> std::expected<performative, std::error_code>
{
    decoder d(b);
    auto root = d.read_value();
    if (!root)
        return std::unexpected(root.error());
    auto code = descriptor_code(*root);
    auto f = described_fields(*root);
    if (!code || !f)
        return std::unexpected(make_error_code(errc::unexpected_performative));
    switch (*code)
    {
    case 0x10:
    {
        open x;
        x.container_id = text(field(*f, 0));
        x.hostname = text(field(*f, 1));
        x.max_frame_size = u32(field(*f, 2)).value_or(262144);
        x.channel_max =
            static_cast<std::uint16_t>(u32(field(*f, 3)).value_or(65535));
        x.idle_timeout = std::chrono::milliseconds(u32(field(*f, 4)).value_or(0));
        return performative{std::move(x)};
    }
    case 0x11:
    {
        begin x;
        if (auto v = u32(field(*f, 0)))
            x.remote_channel = static_cast<std::uint16_t>(*v);
        x.next_outgoing_id = u32(field(*f, 1)).value_or(1);
        x.incoming_window = u32(field(*f, 2)).value_or(0);
        x.outgoing_window = u32(field(*f, 3)).value_or(0);
        x.handle_max = u32(field(*f, 4)).value_or(65535);
        return performative{x};
    }
    case 0x12:
    {
        attach x;
        x.name = text(field(*f, 0));
        x.handle = u32(field(*f, 1)).value_or(0);
        x.link_role = boolean(field(*f, 2)) ? role::receiver : role::sender;
        x.snd_settle =
            static_cast<sender_settle_mode>(u32(field(*f, 3)).value_or(2));
        x.rcv_settle =
            static_cast<receiver_settle_mode>(u32(field(*f, 4)).value_or(0));
        if (auto entries = std::get_if<std::shared_ptr<map>>(&field(*f, 7).data);
            entries && *entries)
            for (const auto& [key, state] : **entries)
                if (auto tag = std::get_if<binary>(&key.data))
                    x.unsettled.emplace_back(*tag, outcome_from_value(state));
        x.incomplete_unsettled = boolean(field(*f, 8));
        if (auto count = u32(field(*f, 9)))
            x.initial_delivery_count = *count;
        return performative{std::move(x)};
    }
    case 0x13:
    {
        flow x;
        if (auto v = u32(field(*f, 0)))
            x.next_incoming_id = *v;
        x.incoming_window = u32(field(*f, 1)).value_or(0);
        x.next_outgoing_id = u32(field(*f, 2)).value_or(0);
        x.outgoing_window = u32(field(*f, 3)).value_or(0);
        if (auto v = u32(field(*f, 4)))
            x.handle = *v;
        if (auto v = u32(field(*f, 5)))
            x.delivery_count = *v;
        if (auto v = u32(field(*f, 6)))
            x.link_credit = *v;
        x.drain = boolean(field(*f, 8));
        x.echo = boolean(field(*f, 9));
        return performative{x};
    }
    case 0x14:
    {
        transfer x;
        x.handle = u32(field(*f, 0)).value_or(0);
        if (auto v = u32(field(*f, 1)))
            x.delivery_id = *v;
        x.delivery_tag = bin(field(*f, 2));
        if (auto v = u32(field(*f, 3)))
            x.message_format = *v;
        x.settled = boolean(field(*f, 4));
        x.more = boolean(field(*f, 5));
        x.state = outcome_from_value(field(*f, 6));
        x.resume = boolean(field(*f, 7));
        x.aborted = boolean(field(*f, 8));
        x.batchable = boolean(field(*f, 9));
        auto consumed = b.size() - d.remaining();
        x.payload.assign(b.begin() + static_cast<std::ptrdiff_t>(consumed),
            b.end());
        return performative{std::move(x)};
    }
    case 0x15:
    {
        disposition x;
        x.disposition_role = boolean(field(*f, 0)) ? role::receiver : role::sender;
        x.first = u32(field(*f, 1)).value_or(0);
        if (auto v = u32(field(*f, 2)))
            x.last = *v;
        x.settled = boolean(field(*f, 3));
        x.state = outcome_from_value(field(*f, 4));
        x.batchable = boolean(field(*f, 5));
        return performative{x};
    }
    case 0x16:
        return performative{
            detach{u32(field(*f, 0)).value_or(0), boolean(field(*f, 1)), {}}};
    case 0x17:
        return performative{end{}};
    case 0x18:
        return performative{close{}};
    case 0x30:
        return performative{coordinator{}};
    case 0x31:
    {
        declare x;
        if (!field(*f, 0).is_null())
            x.global_id = bin(field(*f, 0));
        return performative{x};
    }
    case 0x32:
        return performative{discharge{bin(field(*f, 0)), boolean(field(*f, 1))}};
    case 0x33:
        return performative{declared{bin(field(*f, 0))}};
    default:
        return std::unexpected(make_error_code(errc::unexpected_performative));
    }
}
} // namespace cnetmod::amqp10
