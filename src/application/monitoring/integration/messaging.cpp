module;

#include <cnetmod/config.hpp>

module cnetmod.observability.messaging;

import std;

namespace cnetmod::observability::messaging {
namespace {

    /**
     * @brief Prepares trace map nodes without copying unrelated metadata.
     *
     * Allocation failures leave the original carrier untouched. This helper is
     * entered only when propagation is explicitly requested.
     */
    template <typename Metadata, typename Update>
    void update_trace_map(Metadata& destination, Update update) noexcept
    {
        static_assert(std::same_as<typename Metadata::key_type, std::string>);
        static_assert(std::same_as<typename Metadata::key_compare, std::less<>>);
        static_assert(noexcept(std::declval<typename Metadata::key_compare>()(
            std::declval<const std::string&>(), std::declval<const std::string&>())));
        try
        {
            Metadata replacement{destination.key_comp(), destination.get_allocator()};
            update(replacement);
            const auto parent = destination.find("traceparent");
            const auto state = destination.find("tracestate");
            if (parent != destination.end())
                destination.erase(parent);
            if (state != destination.end())
                destination.erase(state);
            // Matching allocators and nonthrowing comparison allow node transfer.
            destination.merge(replacement);
        }
        catch (...)
        {
            // Optional propagation must not prevent delivery of a message.
        }
    }

    auto parse(std::string_view traceparent, std::string_view tracestate)
        -> std::optional<http::tracing::trace_context>
    {
        return http::tracing::parse_traceparent(traceparent, tracestate);
    }

} // namespace

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
/**
 * @brief Prepares trace headers before a nonthrowing in-place metadata commit.
 *
 * Existing business header buffers retain ownership throughout the update.
 * Every potentially allocating step completes before removing old trace keys.
 */
void inject(kafka::record& destination,
    const http::tracing::trace_context& context) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<kafka::header>);
    static_assert(std::is_nothrow_move_assignable_v<kafka::header>);
    try
    {
        const auto prepare = [](std::string key, std::string_view value)
            -> kafka::header
        {
            kafka::bytes bytes(value.size());
            if (!value.empty())
                std::memcpy(bytes.data(), value.data(), value.size());
            return {std::move(key), std::move(bytes)};
        };
        std::array<kafka::header, 2> prepared{
            prepare("traceparent", http::tracing::format_traceparent(context)),
            prepare("tracestate", context.tracestate)};
        const std::size_t count = context.tracestate.empty() ? 1 : 2;
        auto& headers = destination.headers;
        if (headers.max_size() - headers.size() < count)
            return;
        headers.reserve(headers.size() + count);
        // Everything after reserve only compares keys or moves owned buffers.
        std::erase_if(headers, [](const kafka::header& header) noexcept
            {
                return header.key == "traceparent" || header.key == "tracestate";
            });
        for (std::size_t index = 0; index < count; ++index)
            headers.push_back(std::move(prepared[index]));
    }
    catch (...)
    {
        // Preparation failed before any original metadata was removed.
    }
}

auto extract(const kafka::consumed_record& source)
    -> std::optional<http::tracing::trace_context>
{
    std::string_view traceparent;
    std::string_view tracestate;
    for (const auto& header : source.headers)
    {
        if (header.key != "traceparent" && header.key != "tracestate")
            continue;
        const auto value = header.value.empty() ? std::string_view{} : std::string_view{reinterpret_cast<const char*>(header.value.data()), header.value.size()};
        if (header.key == "traceparent")
            traceparent = value;
        if (header.key == "tracestate")
            tracestate = value;
    }
    return parse(traceparent, tracestate);
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_MQTT
/**
 * @brief Commits prepared trace properties without copying business strings.
 *
 * Reservation is the final allocating step before the nonthrowing update.
 */
void inject(mqtt::properties& destination,
    const http::tracing::trace_context& context) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<mqtt::mqtt_property>);
    static_assert(std::is_nothrow_move_assignable_v<mqtt::mqtt_property>);
    try
    {
        std::array<mqtt::mqtt_property, 2> prepared{
            mqtt::mqtt_property::string_pair_prop(mqtt::property_id::user_property,
                "traceparent", http::tracing::format_traceparent(context)),
            mqtt::mqtt_property::string_pair_prop(mqtt::property_id::user_property,
                "tracestate", context.tracestate)};
        const std::size_t count = context.tracestate.empty() ? 1 : 2;
        if (destination.max_size() - destination.size() < count)
            return;
        destination.reserve(destination.size() + count);
        std::erase_if(destination, [](const mqtt::mqtt_property& property) noexcept
            {
                if (property.id != mqtt::property_id::user_property)
                    return false;
                const auto* pair = std::get_if<std::pair<std::string, std::string>>(&property.value);
                return pair && (pair->first == "traceparent" || pair->first == "tracestate");
            });
        for (std::size_t index = 0; index < count; ++index)
            destination.push_back(std::move(prepared[index]));
    }
    catch (...)
    {
        // Preparation failed before any original metadata was removed.
    }
}

auto extract(const mqtt::properties& source)
    -> std::optional<http::tracing::trace_context>
{
    std::string_view traceparent;
    std::string_view tracestate;
    for (const auto& property : source)
    {
        if (property.id != mqtt::property_id::user_property)
            continue;
        const auto* pair = std::get_if<std::pair<std::string, std::string>>(
            &property.value);
        if (!pair)
            continue;
        if (pair->first == "traceparent")
            traceparent = pair->second;
        if (pair->first == "tracestate")
            tracestate = pair->second;
    }
    return parse(traceparent, tracestate);
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
void inject(amqp091::message& destination,
    const http::tracing::trace_context& context) noexcept
{
    update_trace_map(destination.headers, [&](auto& headers)
        {
            headers.insert_or_assign("traceparent",
                http::tracing::format_traceparent(context));
            if (!context.tracestate.empty())
                headers.insert_or_assign("tracestate", context.tracestate);
        });
}

auto extract(const amqp091::message& source)
    -> std::optional<http::tracing::trace_context>
{
    const auto parent = source.headers.find("traceparent");
    const auto state = source.headers.find("tracestate");
    return parse(parent == source.headers.end() ? std::string_view{}
                                                : parent->second,
        state == source.headers.end() ? std::string_view{} : state->second);
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
void inject(amqp10::message& destination,
    const http::tracing::trace_context& context) noexcept
{
    update_trace_map(destination.application, [&](auto& properties)
        {
            properties.insert_or_assign("traceparent",
                amqp10::value{http::tracing::format_traceparent(context)});
            if (!context.tracestate.empty())
                properties.insert_or_assign("tracestate",
                    amqp10::value{context.tracestate});
        });
}

auto extract(const amqp10::message& source)
    -> std::optional<http::tracing::trace_context>
{
    const auto read = [&source](std::string_view key) -> std::string_view
    {
        const auto found = source.application.find(key);
        if (found == source.application.end())
            return {};
        const auto* text = std::get_if<std::string>(&found->second.data);
        return text ? std::string_view{*text} : std::string_view{};
    };
    return parse(read("traceparent"), read("tracestate"));
}
#endif

} // namespace cnetmod::observability::messaging
