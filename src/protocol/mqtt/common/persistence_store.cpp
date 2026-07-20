module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import nlohmann.json;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import :persistence;
import :types;
import :session;
import :retained;

namespace cnetmod::mqtt {
namespace {
using json = nlohmann::json;

auto props_to_json(const properties &props) -> json {
  json result = json::array();
  for (const auto &property : props) {
    json object;
    object["id"] = static_cast<std::uint8_t>(property.id);
    std::visit(
        [&](const auto &value) {
          using value_type = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<value_type, std::uint8_t>) {
            object["type"] = "u8";
            object["value"] = value;
          } else if constexpr (std::is_same_v<value_type, std::uint16_t>) {
            object["type"] = "u16";
            object["value"] = value;
          } else if constexpr (std::is_same_v<value_type, std::uint32_t>) {
            object["type"] = "u32";
            object["value"] = value;
          } else if constexpr (std::is_same_v<value_type, std::string>) {
            object["type"] = "str";
            object["value"] = value;
          } else {
            object["type"] = "pair";
            object["key"] = value.first;
            object["val"] = value.second;
          }
        },
        property.value);
    result.push_back(std::move(object));
  }
  return result;
}
auto props_from_json(const json &values) -> properties {
  properties result;
  if (!values.is_array())
    return result;
  for (const auto &object : values) {
    mqtt_property property;
    property.id = static_cast<property_id>(object.value("id", 0));
    const auto type = object.value("type", "");
    if (type == "u8")
      property.value = object.value("value", static_cast<std::uint8_t>(0));
    else if (type == "u16")
      property.value = object.value("value", static_cast<std::uint16_t>(0));
    else if (type == "u32")
      property.value = object.value("value", static_cast<std::uint32_t>(0));
    else if (type == "str")
      property.value = object.value("value", std::string{});
    else if (type == "pair")
      property.value = std::pair{object.value("key", std::string{}),
                                 object.value("val", std::string{})};
    result.push_back(std::move(property));
  }
  return result;
}
auto will_to_json(const will &value) -> json {
  return {{"topic", value.topic},
          {"message", value.message},
          {"qos", static_cast<std::uint8_t>(value.qos_value)},
          {"retain", value.retain},
          {"props", props_to_json(value.props)}};
}
auto will_from_json(const json &object) -> will {
  will value;
  value.topic = object.value("topic", "");
  value.message = object.value("message", "");
  value.qos_value = static_cast<qos>(object.value("qos", 0));
  value.retain = object.value("retain", false);
  if (object.contains("props"))
    value.props = props_from_json(object["props"]);
  return value;
}
auto sub_to_json(const subscribe_entry &value) -> json {
  return {{"topic_filter", value.topic_filter},
          {"max_qos", static_cast<std::uint8_t>(value.max_qos)},
          {"no_local", value.no_local},
          {"retain_as_published", value.retain_as_published},
          {"rh", static_cast<std::uint8_t>(value.rh)}};
}
auto sub_from_json(const json &object) -> subscribe_entry {
  subscribe_entry value;
  value.topic_filter = object.value("topic_filter", "");
  value.max_qos = static_cast<qos>(object.value("max_qos", 0));
  value.no_local = object.value("no_local", false);
  value.retain_as_published = object.value("retain_as_published", false);
  value.rh = static_cast<retain_handling>(object.value("rh", 0));
  return value;
}
auto publish_to_json(const publish_message &value) -> json {
  return {{"topic", value.topic},
          {"payload", value.payload.str()},
          {"qos", static_cast<std::uint8_t>(value.qos_value)},
          {"retain", value.retain},
          {"props", props_to_json(value.props)}};
}
auto publish_from_json(const json &object) -> publish_message {
  publish_message value;
  value.topic = object.value("topic", "");
  value.payload = object.value("payload", "");
  value.qos_value = static_cast<qos>(object.value("qos", 0));
  value.retain = object.value("retain", false);
  if (object.contains("props"))
    value.props = props_from_json(object["props"]);
  return value;
}
auto inflight_to_json(const inflight_message &value) -> json {
  return {{"packet_id", value.packet_id},
          {"msg", publish_to_json(value.msg)},
          {"expected_ack", static_cast<std::uint8_t>(value.expected_ack)},
          {"retry_count", value.retry_count}};
}
auto inflight_from_json(const json &object) -> inflight_message {
  inflight_message value;
  value.packet_id = object.value("packet_id", static_cast<std::uint16_t>(0));
  if (object.contains("msg"))
    value.msg = publish_from_json(object["msg"]);
  value.expected_ack = static_cast<control_packet_type>(
      object.value("expected_ack", static_cast<std::uint8_t>(0x40)));
  value.retry_count = object.value("retry_count", static_cast<std::uint8_t>(0));
  value.send_time = std::chrono::steady_clock::now();
  return value;
}
auto session_to_json(const session_state &value) -> json {
  json result{{"client_id", value.client_id},
              {"version", static_cast<std::uint8_t>(value.version)},
              {"clean_session", value.clean_session},
              {"session_expiry", value.session_expiry_interval},
              {"next_packet_id", value.next_packet_id},
              {"username", value.username}};
  json subscriptions = json::object();
  for (const auto &[filter, entry] : value.subscriptions)
    subscriptions[filter] = sub_to_json(entry);
  result["subscriptions"] = std::move(subscriptions);
  json queue = json::array();
  for (const auto &message : value.offline_queue)
    queue.push_back(publish_to_json(message));
  result["offline_queue"] = std::move(queue);
  json inflight = json::array();
  for (const auto &message : value.inflight_out)
    inflight.push_back(inflight_to_json(message));
  result["inflight_out"] = std::move(inflight);
  if (value.will_msg)
    result["will"] = will_to_json(*value.will_msg);
  return result;
}
auto session_from_json(const json &object) -> session_state {
  session_state value;
  value.client_id = object.value("client_id", "");
  value.version = static_cast<protocol_version>(object.value("version", 4));
  value.clean_session = object.value("clean_session", true);
  value.session_expiry_interval =
      object.value("session_expiry", static_cast<std::uint32_t>(0));
  value.next_packet_id =
      object.value("next_packet_id", static_cast<std::uint16_t>(1));
  value.username = object.value("username", "");
  if (object.contains("subscriptions") && object["subscriptions"].is_object())
    for (auto it = object["subscriptions"].begin();
         it != object["subscriptions"].end(); ++it)
      value.subscriptions[it.key()] = sub_from_json(it.value());
  if (object.contains("offline_queue") && object["offline_queue"].is_array())
    for (const auto &entry : object["offline_queue"])
      value.offline_queue.push_back(publish_from_json(entry));
  if (object.contains("inflight_out") && object["inflight_out"].is_array())
    for (const auto &entry : object["inflight_out"])
      value.inflight_out.push_back(inflight_from_json(entry));
  if (object.contains("will") && !object["will"].is_null())
    value.will_msg = will_from_json(object["will"]);
  value.online = false;
  return value;
}
auto retained_to_json(const retained_message &value) -> json {
  return {{"topic", value.topic},
          {"payload", value.payload},
          {"qos", static_cast<std::uint8_t>(value.qos_value)},
          {"props", props_to_json(value.props)}};
}
auto retained_from_json(const json &object) -> retained_message {
  retained_message value;
  value.topic = object.value("topic", "");
  value.payload = object.value("payload", "");
  value.qos_value = static_cast<qos>(object.value("qos", 0));
  if (object.contains("props"))
    value.props = props_from_json(object["props"]);
  return value;
}
} // namespace

persistence::persistence(persistence_options opts) : opts_(std::move(opts)) {}
auto persistence::save_sessions(const session_store &store)
    -> std::expected<void, std::string> {
  ensure_dir();
  json root = json::array();
  store.for_each([&](const session_state &value) {
    if (!value.clean_session || !value.offline_queue.empty() ||
        !value.subscriptions.empty())
      root.push_back(session_to_json(value));
  });
  return write_file(opts_.data_dir + "/sessions.json", root.dump(2));
}
auto persistence::load_sessions() -> std::expected<session_store, std::string> {
  auto content = read_file(opts_.data_dir + "/sessions.json");
  if (!content)
    return std::unexpected(content.error());
  session_store store;
  try {
    auto root = json::parse(*content);
    if (!root.is_array())
      return std::unexpected(std::string("sessions.json: not an array"));
    for (const auto &object : root) {
      auto value = session_from_json(object);
      if (!value.client_id.empty()) {
        auto [ref, unused] = store.create_or_resume(
            value.client_id, value.clean_session, value.version);
        ref = std::move(value);
        ref.online = false;
      }
    }
  } catch (const json::exception &error) {
    return std::unexpected(std::string("sessions.json parse error: ") +
                           error.what());
  }
  return store;
}
auto persistence::save_retained(const retained_store &store)
    -> std::expected<void, std::string> {
  ensure_dir();
  json root = json::array();
  store.for_each([&](const retained_message &value) {
    root.push_back(retained_to_json(value));
  });
  return write_file(opts_.data_dir + "/retained.json", root.dump(2));
}
auto persistence::load_retained()
    -> std::expected<retained_store, std::string> {
  auto content = read_file(opts_.data_dir + "/retained.json");
  if (!content)
    return std::unexpected(content.error());
  retained_store store;
  try {
    auto root = json::parse(*content);
    if (!root.is_array())
      return std::unexpected(std::string("retained.json: not an array"));
    for (const auto &object : root) {
      auto value = retained_from_json(object);
      if (!value.topic.empty()) {
        auto topic = value.topic;
        store.store(topic, std::move(value));
      }
    }
  } catch (const json::exception &error) {
    return std::unexpected(std::string("retained.json parse error: ") +
                           error.what());
  }
  return store;
}
auto persistence::start_auto_flush(io_context &ctx, session_store &sessions,
                                   retained_store &retained) -> task<void> {
  while (true) {
    co_await async_sleep(ctx, opts_.flush_interval);
    (void)save_sessions(sessions);
    (void)save_retained(retained);
  }
}
auto persistence::options() const noexcept -> const persistence_options & {
  return opts_;
}
void persistence::ensure_dir() {
  std::filesystem::create_directories(opts_.data_dir);
}
auto persistence::write_file(const std::string &path,
                             const std::string &content)
    -> std::expected<void, std::string> {
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
  if (error) {
    std::filesystem::copy_file(
        temporary, path, std::filesystem::copy_options::overwrite_existing,
        error);
    std::filesystem::remove(temporary, error);
    if (error)
      return std::unexpected("rename failed: " + error.message());
  }
  return {};
}
auto persistence::read_file(const std::string &path)
    -> std::expected<std::string, std::string> {
  if (!std::filesystem::exists(path))
    return std::unexpected("file not found: " + path);
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::unexpected("cannot open " + path);
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}
} // namespace cnetmod::mqtt
