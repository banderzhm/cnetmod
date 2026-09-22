module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import cnetmod.json;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import :persistence;
import :types;
import :session;
import :retained;

namespace cnetmod::mqtt {
namespace {
    using json = cnetmod::json::document;

    auto props_to_json(const properties& props) -> json
    {
        json result = cnetmod::json::array();
        for (const auto& property : props)
        {
            json object = cnetmod::json::object();
            object["id"] = static_cast<std::uint8_t>(property.id);
            std::visit(
                [&](const auto& value)
                {
                    using value_type = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<value_type, std::uint8_t>)
                    {
                        object["type"] = "u8";
                        object["value"] = value;
                    }
                    else if constexpr (std::is_same_v<value_type, std::uint16_t>)
                    {
                        object["type"] = "u16";
                        object["value"] = value;
                    }
                    else if constexpr (std::is_same_v<value_type, std::uint32_t>)
                    {
                        object["type"] = "u32";
                        object["value"] = value;
                    }
                    else if constexpr (std::is_same_v<value_type, std::string>)
                    {
                        object["type"] = "str";
                        object["value"] = value;
                    }
                    else
                    {
                        object["type"] = "pair";
                        object["key"] = value.first;
                        object["val"] = value.second;
                    }
                },
                property.value);
            result.get_array().push_back(std::move(object));
        }
        return result;
    }

    auto props_from_json(const json& values) -> properties
    {
        properties result;
        if (!values.is_array())
            return result;
        for (const auto& object : values.get_array())
        {
            mqtt_property property;
            property.id = static_cast<property_id>(
                cnetmod::json::value_or(object, "id", 0));
            const auto type = cnetmod::json::value_or(
                object, "type", std::string{});
            if (type == "u8")
                property.value = cnetmod::json::value_or(
                    object, "value", static_cast<std::uint8_t>(0));
            else if (type == "u16")
                property.value = cnetmod::json::value_or(
                    object, "value", static_cast<std::uint16_t>(0));
            else if (type == "u32")
                property.value = cnetmod::json::value_or(
                    object, "value", static_cast<std::uint32_t>(0));
            else if (type == "str")
                property.value = cnetmod::json::value_or(
                    object, "value", std::string{});
            else if (type == "pair")
                property.value = std::pair{
                    cnetmod::json::value_or(object, "key", std::string{}),
                    cnetmod::json::value_or(object, "val", std::string{})};
            result.push_back(std::move(property));
        }
        return result;
    }

    auto will_to_json(const will& value) -> json
    {
        auto result = cnetmod::json::object();
        result["topic"] = value.topic;
        result["message"] = value.message;
        result["qos"] = static_cast<std::uint8_t>(value.qos_value);
        result["retain"] = value.retain;
        result["props"] = props_to_json(value.props);
        return result;
    }

    auto will_from_json(const json& object) -> will
    {
        will value;
        value.topic = cnetmod::json::value_or(object, "topic", std::string{});
        value.message = cnetmod::json::value_or(object, "message", std::string{});
        value.qos_value = static_cast<qos>(cnetmod::json::value_or(object, "qos", 0));
        value.retain = cnetmod::json::value_or(object, "retain", false);
        if (object.contains("props"))
            value.props = props_from_json(object["props"]);
        return value;
    }

    auto sub_to_json(const subscribe_entry& value) -> json
    {
        auto result = cnetmod::json::object();
        result["topic_filter"] = value.topic_filter;
        result["max_qos"] = static_cast<std::uint8_t>(value.max_qos);
        result["no_local"] = value.no_local;
        result["retain_as_published"] = value.retain_as_published;
        result["rh"] = static_cast<std::uint8_t>(value.rh);
        return result;
    }

    auto sub_from_json(const json& object) -> subscribe_entry
    {
        subscribe_entry value;
        value.topic_filter = cnetmod::json::value_or(object, "topic_filter", std::string{});
        value.max_qos = static_cast<qos>(cnetmod::json::value_or(object, "max_qos", 0));
        value.no_local = cnetmod::json::value_or(object, "no_local", false);
        value.retain_as_published = cnetmod::json::value_or(object, "retain_as_published", false);
        value.rh = static_cast<retain_handling>(cnetmod::json::value_or(object, "rh", 0));
        return value;
    }

    auto publish_to_json(const publish_message& value) -> json
    {
        auto result = cnetmod::json::object();
        result["topic"] = value.topic;
        result["payload"] = value.payload.str();
        result["qos"] = static_cast<std::uint8_t>(value.qos_value);
        result["retain"] = value.retain;
        result["props"] = props_to_json(value.props);
        return result;
    }

    auto publish_from_json(const json& object) -> publish_message
    {
        publish_message value;
        value.topic = cnetmod::json::value_or(object, "topic", std::string{});
        value.payload = cnetmod::json::value_or(object, "payload", std::string{});
        value.qos_value = static_cast<qos>(cnetmod::json::value_or(object, "qos", 0));
        value.retain = cnetmod::json::value_or(object, "retain", false);
        if (object.contains("props"))
            value.props = props_from_json(object["props"]);
        return value;
    }

    auto inflight_to_json(const inflight_message& value) -> json
    {
        auto result = cnetmod::json::object();
        result["packet_id"] = value.packet_id;
        result["msg"] = publish_to_json(value.msg);
        result["expected_ack"] = static_cast<std::uint8_t>(value.expected_ack);
        result["retry_count"] = value.retry_count;
        return result;
    }

    auto inflight_from_json(const json& object) -> inflight_message
    {
        inflight_message value;
        value.packet_id = cnetmod::json::value_or(
            object, "packet_id", static_cast<std::uint16_t>(0));
        if (object.contains("msg"))
            value.msg = publish_from_json(object["msg"]);
        value.expected_ack = static_cast<control_packet_type>(
            cnetmod::json::value_or(
                object, "expected_ack", static_cast<std::uint8_t>(0x40)));
        value.retry_count = cnetmod::json::value_or(
            object, "retry_count", static_cast<std::uint8_t>(0));
        value.send_time = std::chrono::steady_clock::now();
        return value;
    }

    auto session_to_json(const session_state& value) -> json
    {
        auto result = cnetmod::json::object();
        result["client_id"] = value.client_id;
        result["version"] = static_cast<std::uint8_t>(value.version);
        result["clean_session"] = value.clean_session;
        result["session_expiry"] = value.session_expiry_interval;
        result["next_packet_id"] = value.next_packet_id;
        result["username"] = value.username;
        json subscriptions = cnetmod::json::object();
        for (const auto& [filter, entry] : value.subscriptions)
            subscriptions[filter] = sub_to_json(entry);
        result["subscriptions"] = std::move(subscriptions);
        json queue = cnetmod::json::array();
        for (const auto& message : value.offline_queue)
            queue.get_array().push_back(publish_to_json(message));
        result["offline_queue"] = std::move(queue);
        json inflight = cnetmod::json::array();
        for (const auto& message : value.inflight_out)
            inflight.get_array().push_back(inflight_to_json(message));
        result["inflight_out"] = std::move(inflight);
        if (value.will_msg)
            result["will"] = will_to_json(*value.will_msg);
        return result;
    }

    auto session_from_json(const json& object) -> session_state
    {
        session_state value;
        value.client_id = cnetmod::json::value_or(object, "client_id", std::string{});
        value.version = static_cast<protocol_version>(cnetmod::json::value_or(object, "version", 4));
        value.clean_session = cnetmod::json::value_or(object, "clean_session", true);
        value.session_expiry_interval =
            cnetmod::json::value_or(object, "session_expiry", static_cast<std::uint32_t>(0));
        value.next_packet_id =
            cnetmod::json::value_or(object, "next_packet_id", static_cast<std::uint16_t>(1));
        value.username = cnetmod::json::value_or(object, "username", std::string{});
        if (object.contains("subscriptions") && object["subscriptions"].is_object())
            for (const auto& [key, entry] : object["subscriptions"].get_object())
                value.subscriptions[key] = sub_from_json(entry);
        if (object.contains("offline_queue") && object["offline_queue"].is_array())
            for (const auto& entry : object["offline_queue"].get_array())
                value.offline_queue.push_back(publish_from_json(entry));
        if (object.contains("inflight_out") && object["inflight_out"].is_array())
            for (const auto& entry : object["inflight_out"].get_array())
                value.inflight_out.push_back(inflight_from_json(entry));
        if (object.contains("will") && !object["will"].is_null())
            value.will_msg = will_from_json(object["will"]);
        value.online = false;
        return value;
    }

    auto retained_to_json(const retained_message& value) -> json
    {
        auto result = cnetmod::json::object();
        result["topic"] = value.topic;
        result["payload"] = value.payload;
        result["qos"] = static_cast<std::uint8_t>(value.qos_value);
        result["props"] = props_to_json(value.props);
        return result;
    }

    auto retained_from_json(const json& object) -> retained_message
    {
        retained_message value;
        value.topic = cnetmod::json::value_or(object, "topic", std::string{});
        value.payload = cnetmod::json::value_or(object, "payload", std::string{});
        value.qos_value = static_cast<qos>(cnetmod::json::value_or(object, "qos", 0));
        if (object.contains("props"))
            value.props = props_from_json(object["props"]);
        return value;
    }
} // namespace

persistence::persistence(persistence_options opts)
    : opts_(std::move(opts)) {}

auto persistence::save_sessions(const session_store& store)
    -> std::expected<void, std::string>
{
    ensure_dir();
    json root = cnetmod::json::array();
    store.for_each([&](const session_state& value)
        {
            if (!value.clean_session || !value.offline_queue.empty() ||
                !value.subscriptions.empty())
                root.get_array().push_back(session_to_json(value));
        });
    const auto encoded = cnetmod::json::write_document(root, true);
    return encoded ? write_file(opts_.data_dir + "/sessions.json", *encoded)
                   : std::unexpected("failed to encode sessions JSON");
}

auto persistence::load_sessions() -> std::expected<session_store, std::string>
{
    auto content = read_file(opts_.data_dir + "/sessions.json");
    if (!content)
        return std::unexpected(content.error());
    session_store store;
    {
        auto root = cnetmod::json::parse_document(*content);
        if (!root)
            return std::unexpected(std::string("sessions.json parse error"));
        if (!root->is_array())
            return std::unexpected(std::string("sessions.json: not an array"));
        for (const auto& object : root->get_array())
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
    json root = cnetmod::json::array();
    store.for_each([&](const retained_message& value)
        {
            root.get_array().push_back(retained_to_json(value));
        });
    const auto encoded = cnetmod::json::write_document(root, true);
    return encoded ? write_file(opts_.data_dir + "/retained.json", *encoded)
                   : std::unexpected("failed to encode retained JSON");
}

auto persistence::load_retained()
    -> std::expected<retained_store, std::string>
{
    auto content = read_file(opts_.data_dir + "/retained.json");
    if (!content)
        return std::unexpected(content.error());
    retained_store store;
    {
        auto root = cnetmod::json::parse_document(*content);
        if (!root)
            return std::unexpected(std::string("retained.json parse error"));
        if (!root->is_array())
            return std::unexpected(std::string("retained.json: not an array"));
        for (const auto& object : root->get_array())
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
    const std::string& content)
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
