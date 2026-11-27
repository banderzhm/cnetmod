module;

#include <cnetmod/config.hpp>
#include "../../../utils/detail/simdjson_depth.hpp"

module cnetmod.protocol.mqtt;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import :persistence;
import :types;
import :session;
import :retained;

namespace cnetmod::mqtt {
namespace {
    using simdjson::dom::element;
    using simdjson::builder::string_builder;
    using cnetmod::detail::find_last_json_field;

    template <typename T>
    auto mqtt_field(element object, std::string_view name, T fallback) -> T
    {
        const auto field = find_last_json_field(object, name);
        if constexpr (std::same_as<T, std::string>)
        {
            std::string_view value;
            if (!field.get_string().get(value))
                return std::string{value};
        }
        else if constexpr (std::same_as<T, bool>)
        {
            bool value = false;
            if (!field.get_bool().get(value))
                return value;
        }
        else
        {
            std::int64_t signed_value = 0;
            std::uint64_t unsigned_value = 0;
            if (!field.get_int64().get(signed_value))
                return static_cast<T>(signed_value);
            if (!field.get_uint64().get(unsigned_value))
                return static_cast<T>(unsigned_value);
            double value = 0.0;
            if (!field.get_double().get(value) &&
                value >= static_cast<double>(std::numeric_limits<T>::lowest()) &&
                value <= static_cast<double>(std::numeric_limits<T>::max()))
                return static_cast<T>(value);
        }
        return fallback;
    }

    void write_properties(string_builder& out, const properties& props)
    {
        out.start_array();
        bool first = true;
        for (const auto& property : props)
        {
            if (!std::exchange(first, false))
                out.append_comma();
            out.start_object();
            out.append_key_value("id", static_cast<std::uint8_t>(property.id));
            out.append_comma();
            std::visit(
                [&](const auto& value)
                {
                    using value_type = std::decay_t<decltype(value)>;
                    if constexpr (std::same_as<value_type, std::pair<std::string, std::string>>)
                    {
                        out.append_key_value("type", "pair");
                        out.append_comma();
                        out.append_key_value("key", value.first);
                        out.append_comma();
                        out.append_key_value("val", value.second);
                    }
                    else
                    {
                        constexpr auto type = [] {
                            if constexpr (std::same_as<value_type, std::uint8_t>)
                                return "u8";
                            else if constexpr (std::same_as<value_type, std::uint16_t>)
                                return "u16";
                            else if constexpr (std::same_as<value_type, std::uint32_t>)
                                return "u32";
                            else
                                return "str";
                        }();
                        out.append_key_value("type", type);
                        out.append_comma();
                        out.append_key_value("value", value);
                    }
                },
                property.value);
            out.end_object();
        }
        out.end_array();
    }

    auto props_from_json(element values) -> properties
    {
        properties result;
        simdjson::dom::array entries;
        if (values.get_array().get(entries))
            return result;
        for (const auto object : entries)
        {
            mqtt_property property;
            property.id = static_cast<property_id>(
                mqtt_field(object, "id", 0));
            const auto type = mqtt_field(
                object, "type", std::string{});
            if (type == "u8")
                property.value = mqtt_field(
                    object, "value", static_cast<std::uint8_t>(0));
            else if (type == "u16")
                property.value = mqtt_field(
                    object, "value", static_cast<std::uint16_t>(0));
            else if (type == "u32")
                property.value = mqtt_field(
                    object, "value", static_cast<std::uint32_t>(0));
            else if (type == "str")
                property.value = mqtt_field(
                    object, "value", std::string{});
            else if (type == "pair")
                property.value = std::pair{
                    mqtt_field(object, "key", std::string{}),
                    mqtt_field(object, "val", std::string{})};
            result.push_back(std::move(property));
        }
        return result;
    }

    void write_will(string_builder& out, const will& value)
    {
        out.start_object();
        out.append_key_value("topic", value.topic);
        out.append_comma();
        out.append_key_value("message", value.message);
        out.append_comma();
        out.append_key_value("qos", static_cast<std::uint8_t>(value.qos_value));
        out.append_comma();
        out.append_key_value("retain", value.retain);
        out.append_comma();
        out.escape_and_append_with_quotes("props");
        out.append_colon();
        write_properties(out, value.props);
        out.end_object();
    }

    auto will_from_json(element object) -> will
    {
        will value;
        value.topic = mqtt_field(object, "topic", std::string{});
        value.message = mqtt_field(object, "message", std::string{});
        value.qos_value = static_cast<qos>(mqtt_field(object, "qos", 0));
        value.retain = mqtt_field(object, "retain", false);
        if (const auto field = find_last_json_field(object, "props"); !field.error())
            value.props = props_from_json(field.value_unsafe());
        return value;
    }

    void write_sub(string_builder& out, const subscribe_entry& value)
    {
        out.start_object();
        out.append_key_value("topic_filter", value.topic_filter);
        out.append_comma();
        out.append_key_value("max_qos", static_cast<std::uint8_t>(value.max_qos));
        out.append_comma();
        out.append_key_value("no_local", value.no_local);
        out.append_comma();
        out.append_key_value("retain_as_published", value.retain_as_published);
        out.append_comma();
        out.append_key_value("rh", static_cast<std::uint8_t>(value.rh));
        out.end_object();
    }

    auto sub_from_json(element object) -> subscribe_entry
    {
        subscribe_entry value;
        value.topic_filter = mqtt_field(object, "topic_filter", std::string{});
        value.max_qos = static_cast<qos>(mqtt_field(object, "max_qos", 0));
        value.no_local = mqtt_field(object, "no_local", false);
        value.retain_as_published = mqtt_field(object, "retain_as_published", false);
        value.rh = static_cast<retain_handling>(mqtt_field(object, "rh", 0));
        return value;
    }

    void write_publish(string_builder& out, const publish_message& value)
    {
        out.start_object();
        out.append_key_value("topic", value.topic);
        out.append_comma();
        out.append_key_value("payload", value.payload.str());
        out.append_comma();
        out.append_key_value("qos", static_cast<std::uint8_t>(value.qos_value));
        out.append_comma();
        out.append_key_value("retain", value.retain);
        out.append_comma();
        out.escape_and_append_with_quotes("props");
        out.append_colon();
        write_properties(out, value.props);
        out.end_object();
    }

    auto publish_from_json(element object) -> publish_message
    {
        publish_message value;
        value.topic = mqtt_field(object, "topic", std::string{});
        value.payload = mqtt_field(object, "payload", std::string{});
        value.qos_value = static_cast<qos>(mqtt_field(object, "qos", 0));
        value.retain = mqtt_field(object, "retain", false);
        if (const auto field = find_last_json_field(object, "props"); !field.error())
            value.props = props_from_json(field.value_unsafe());
        return value;
    }

    void write_inflight(string_builder& out, const inflight_message& value)
    {
        out.start_object();
        out.append_key_value("packet_id", value.packet_id);
        out.append_comma();
        out.escape_and_append_with_quotes("msg");
        out.append_colon();
        write_publish(out, value.msg);
        out.append_comma();
        out.append_key_value("expected_ack", static_cast<std::uint8_t>(value.expected_ack));
        out.append_comma();
        out.append_key_value("retry_count", value.retry_count);
        out.end_object();
    }

    auto inflight_from_json(element object) -> inflight_message
    {
        inflight_message value;
        value.packet_id = mqtt_field(
            object, "packet_id", static_cast<std::uint16_t>(0));
        if (const auto field = find_last_json_field(object, "msg"); !field.error())
            value.msg = publish_from_json(field.value_unsafe());
        value.expected_ack = static_cast<control_packet_type>(
            mqtt_field(
                object, "expected_ack", static_cast<std::uint8_t>(0x40)));
        value.retry_count = mqtt_field(
            object, "retry_count", static_cast<std::uint8_t>(0));
        value.send_time = std::chrono::steady_clock::now();
        return value;
    }

    void write_session(string_builder& out, const session_state& value)
    {
        out.start_object();
        out.append_key_value("client_id", value.client_id);
        out.append_comma();
        out.append_key_value("version", static_cast<std::uint8_t>(value.version));
        out.append_comma();
        out.append_key_value("clean_session", value.clean_session);
        out.append_comma();
        out.append_key_value("session_expiry", value.session_expiry_interval);
        out.append_comma();
        out.append_key_value("next_packet_id", value.next_packet_id);
        out.append_comma();
        out.append_key_value("username", value.username);
        out.append_comma();
        out.escape_and_append_with_quotes("subscriptions");
        out.append_colon();
        out.start_object();
        bool first = true;
        for (const auto& [filter, entry] : value.subscriptions)
        {
            if (!std::exchange(first, false))
                out.append_comma();
            out.escape_and_append_with_quotes(filter);
            out.append_colon();
            write_sub(out, entry);
        }
        out.end_object();
        out.append_comma();
        out.escape_and_append_with_quotes("offline_queue");
        out.append_colon();
        out.start_array();
        first = true;
        for (const auto& message : value.offline_queue)
        {
            if (!std::exchange(first, false))
                out.append_comma();
            write_publish(out, message);
        }
        out.end_array();
        out.append_comma();
        out.escape_and_append_with_quotes("inflight_out");
        out.append_colon();
        out.start_array();
        first = true;
        for (const auto& message : value.inflight_out)
        {
            if (!std::exchange(first, false))
                out.append_comma();
            write_inflight(out, message);
        }
        out.end_array();
        if (value.will_msg)
        {
            out.append_comma();
            out.escape_and_append_with_quotes("will");
            out.append_colon();
            write_will(out, *value.will_msg);
        }
        out.end_object();
    }

    auto session_from_json(element object) -> session_state
    {
        session_state value;
        value.client_id = mqtt_field(object, "client_id", std::string{});
        value.version = static_cast<protocol_version>(mqtt_field(object, "version", 4));
        value.clean_session = mqtt_field(object, "clean_session", true);
        value.session_expiry_interval =
            mqtt_field(object, "session_expiry", static_cast<std::uint32_t>(0));
        value.next_packet_id =
            mqtt_field(object, "next_packet_id", static_cast<std::uint16_t>(1));
        value.username = mqtt_field(object, "username", std::string{});
        simdjson::dom::object subscriptions;
        if (!find_last_json_field(object, "subscriptions").get_object().get(subscriptions))
            for (const auto field : subscriptions)
                value.subscriptions[std::string{field.key}] = sub_from_json(field.value);
        simdjson::dom::array queue;
        if (!find_last_json_field(object, "offline_queue").get_array().get(queue))
            for (const auto entry : queue)
                value.offline_queue.push_back(publish_from_json(entry));
        simdjson::dom::array inflight;
        if (!find_last_json_field(object, "inflight_out").get_array().get(inflight))
            for (const auto entry : inflight)
                value.inflight_out.push_back(inflight_from_json(entry));
        if (const auto field = find_last_json_field(object, "will");
            !field.error() && !field.value_unsafe().is_null())
            value.will_msg = will_from_json(field.value_unsafe());
        value.online = false;
        return value;
    }

    void write_retained(string_builder& out, const retained_message& value)
    {
        out.start_object();
        out.append_key_value("topic", value.topic);
        out.append_comma();
        out.append_key_value("payload", value.payload);
        out.append_comma();
        out.append_key_value("qos", static_cast<std::uint8_t>(value.qos_value));
        out.append_comma();
        out.escape_and_append_with_quotes("props");
        out.append_colon();
        write_properties(out, value.props);
        out.end_object();
    }

    auto retained_from_json(element object) -> retained_message
    {
        retained_message value;
        value.topic = mqtt_field(object, "topic", std::string{});
        value.payload = mqtt_field(object, "payload", std::string{});
        value.qos_value = static_cast<qos>(mqtt_field(object, "qos", 0));
        if (const auto field = find_last_json_field(object, "props"); !field.error())
            value.props = props_from_json(field.value_unsafe());
        return value;
    }
} // namespace

persistence::persistence(persistence_options opts)
    : opts_(std::move(opts)) {}

auto persistence::save_sessions(const session_store& store)
    -> std::expected<void, std::string>
{
    ensure_dir();
    string_builder out;
    out.start_array();
    bool first = true;
    store.for_each([&](const session_state& value)
        {
            if (value.clean_session && value.offline_queue.empty() &&
                value.subscriptions.empty())
                return;
            if (!std::exchange(first, false))
                out.append_comma();
            write_session(out, value);
        });
    out.end_array();
    std::string_view encoded;
    if (!out.validate_unicode() || out.view().get(encoded))
        return std::unexpected("failed to encode sessions JSON");
    return write_file(opts_.data_dir + "/sessions.json", encoded);
}

auto persistence::load_sessions() -> std::expected<session_store, std::string>
{
    auto content = read_file(opts_.data_dir + "/sessions.json");
    if (!content)
        return std::unexpected(content.error());
    session_store store;
    {
        simdjson::dom::parser parser;
        element root;
        if (!cnetmod::detail::parse_bounded_json(parser, *content, root))
            return std::unexpected(std::string("sessions.json parse error"));
        simdjson::dom::array rows;
        if (root.get_array().get(rows))
            return std::unexpected(std::string("sessions.json: not an array"));
        for (const auto object : rows)
        {
            auto value = session_from_json(object);
            if (!value.client_id.empty())
            {
                auto [ref, unused] = store.create_or_resume(
                    value.client_id, value.clean_session, value.version);
                ref = std::move(value);
                ref.online = false;
            }
        }
    }
    return store;
}

auto persistence::save_retained(const retained_store& store)
    -> std::expected<void, std::string>
{
    ensure_dir();
    string_builder out;
    out.start_array();
    bool first = true;
    store.for_each([&](const retained_message& value)
        {
            if (!std::exchange(first, false))
                out.append_comma();
            write_retained(out, value);
        });
    out.end_array();
    std::string_view encoded;
    if (!out.validate_unicode() || out.view().get(encoded))
        return std::unexpected("failed to encode retained JSON");
    return write_file(opts_.data_dir + "/retained.json", encoded);
}

auto persistence::load_retained()
    -> std::expected<retained_store, std::string>
{
    auto content = read_file(opts_.data_dir + "/retained.json");
    if (!content)
        return std::unexpected(content.error());
    retained_store store;
    {
        simdjson::dom::parser parser;
        element root;
        if (!cnetmod::detail::parse_bounded_json(parser, *content, root))
            return std::unexpected(std::string("retained.json parse error"));
        simdjson::dom::array rows;
        if (root.get_array().get(rows))
            return std::unexpected(std::string("retained.json: not an array"));
        for (const auto object : rows)
        {
            auto value = retained_from_json(object);
            if (!value.topic.empty())
            {
                auto topic = value.topic;
                store.store(topic, std::move(value));
            }
        }
    }
    return store;
}

auto persistence::start_auto_flush(io_context& ctx, session_store& sessions,
    retained_store& retained) -> task<void>
{
    while (true)
    {
        co_await async_sleep(ctx, opts_.flush_interval);
        (void)save_sessions(sessions);
        (void)save_retained(retained);
    }
}

auto persistence::options() const noexcept -> const persistence_options&
{
    return opts_;
}

void persistence::ensure_dir()
{
    std::filesystem::create_directories(opts_.data_dir);
}

auto persistence::write_file(const std::string& path,
    std::string_view content)
    -> std::expected<void, std::string>
{
    auto temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return std::unexpected("cannot open " + temporary);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output)
            return std::unexpected("write failed: " + temporary);
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::filesystem::copy_file(
            temporary, path, std::filesystem::copy_options::overwrite_existing,
            error);
        std::filesystem::remove(temporary, error);
        if (error)
            return std::unexpected("rename failed: " + error.message());
    }
    return {};
}

auto persistence::read_file(const std::string& path)
    -> std::expected<std::string, std::string>
{
    if (!std::filesystem::exists(path))
        return std::unexpected("file not found: " + path);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::unexpected("cannot open " + path);
    return std::string((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
}
} // namespace cnetmod::mqtt
