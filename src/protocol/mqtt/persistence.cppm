/// cnetmod.protocol.mqtt:persistence — MQTT persistence storage
/// Filesystem-based JSON persistence for sessions and retained messages

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:persistence;

import std;
import nlohmann.json;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import :types;
import :session;
import :retained;

namespace cnetmod::mqtt {

using json = nlohmann::json;

// =============================================================================
// Persistence configuration
// =============================================================================

export struct persistence_options {
    std::string                data_dir       = "./mqtt_data";
    std::chrono::seconds       flush_interval = std::chrono::seconds(30);
};

// =============================================================================
// JSON serialization helpers
// =============================================================================

namespace detail {

// --- properties serialization ---

inline auto props_to_json(const properties& props) -> json {
    json arr = json::array();
    for (auto& p : props) {
        json obj;
        obj["id"] = static_cast<std::uint8_t>(p.id);
        std::visit([&](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::uint8_t>) {
                obj["type"] = "u8";
                obj["value"] = val;
            } else if constexpr (std::is_same_v<T, std::uint16_t>) {
                obj["type"] = "u16";
                obj["value"] = val;
            } else if constexpr (std::is_same_v<T, std::uint32_t>) {
                obj["type"] = "u32";
                obj["value"] = val;
            } else if constexpr (std::is_same_v<T, std::string>) {
                obj["type"] = "str";
                obj["value"] = val;
            } else if constexpr (std::is_same_v<T, std::pair<std::string, std::string>>) {
                obj["type"] = "pair";
                obj["key"] = val.first;
                obj["val"] = val.second;
            }
        }, p.value);
        arr.push_back(std::move(obj));
    }
    return arr;
}

inline auto props_from_json(const json& arr) -> properties {
    properties result;
    if (!arr.is_array()) return result;
    for (auto& obj : arr) {
        mqtt_property p;
        p.id = static_cast<property_id>(obj.value("id", 0));
        auto type = obj.value("type", "");
        if (type == "u8") {
            p.value = obj.value("value", static_cast<std::uint8_t>(0));
        } else if (type == "u16") {
            p.value = obj.value("value", static_cast<std::uint16_t>(0));
        } else if (type == "u32") {
            p.value = obj.value("value", static_cast<std::uint32_t>(0));
        } else if (type == "str") {
            p.value = obj.value("value", std::string{});
        } else if (type == "pair") {
            p.value = std::pair{
                obj.value("key", std::string{}),
                obj.value("val", std::string{})};
        }
        result.push_back(std::move(p));
    }
    return result;
}

// --- will serialization ---

inline auto will_to_json(const will& w) -> json {
    json obj;
    obj["topic"]   = w.topic;
    obj["message"] = w.message;
    obj["qos"]     = static_cast<std::uint8_t>(w.qos_value);
    obj["retain"]  = w.retain;
    obj["props"]   = props_to_json(w.props);
    return obj;
}

inline auto will_from_json(const json& obj) -> will {
    will w;
    w.topic     = obj.value("topic", "");
    w.message   = obj.value("message", "");
    w.qos_value = static_cast<qos>(obj.value("qos", 0));
    w.retain    = obj.value("retain", false);
    if (obj.contains("props"))
        w.props = props_from_json(obj["props"]);
    return w;
}

// --- subscribe_entry serialization ---

inline auto sub_entry_to_json(const subscribe_entry& e) -> json {
    json obj;
    obj["topic_filter"]       = e.topic_filter;
    obj["max_qos"]            = static_cast<std::uint8_t>(e.max_qos);
    obj["no_local"]           = e.no_local;
    obj["retain_as_published"] = e.retain_as_published;
    obj["rh"]                 = static_cast<std::uint8_t>(e.rh);
    return obj;
}

inline auto sub_entry_from_json(const json& obj) -> subscribe_entry {
    subscribe_entry e;
    e.topic_filter       = obj.value("topic_filter", "");
    e.max_qos            = static_cast<qos>(obj.value("max_qos", 0));
    e.no_local           = obj.value("no_local", false);
    e.retain_as_published = obj.value("retain_as_published", false);
    e.rh = static_cast<retain_handling>(obj.value("rh", 0));
    return e;
}

// --- publish_message serialization ---

inline auto pub_msg_to_json(const publish_message& m) -> json {
    json obj;
    obj["topic"]   = m.topic;
    obj["payload"] = m.payload;
    obj["qos"]     = static_cast<std::uint8_t>(m.qos_value);
    obj["retain"]  = m.retain;
    obj["props"]   = props_to_json(m.props);
    return obj;
}

inline auto pub_msg_from_json(const json& obj) -> publish_message {
    publish_message m;
    m.topic     = obj.value("topic", "");
    m.payload   = obj.value("payload", "");
    m.qos_value = static_cast<qos>(obj.value("qos", 0));
    m.retain    = obj.value("retain", false);
    if (obj.contains("props"))
        m.props = props_from_json(obj["props"]);
    return m;
}

// --- inflight_message serialization ---

inline auto inflight_to_json(const inflight_message& im) -> json {
    json obj;
    obj["packet_id"]    = im.packet_id;
    obj["msg"]          = pub_msg_to_json(im.msg);
    obj["expected_ack"] = static_cast<std::uint8_t>(im.expected_ack);
    obj["retry_count"]  = im.retry_count;
    return obj;
}

inline auto inflight_from_json(const json& obj) -> inflight_message {
    inflight_message im;
    im.packet_id    = obj.value("packet_id", static_cast<std::uint16_t>(0));
    if (obj.contains("msg"))
        im.msg      = pub_msg_from_json(obj["msg"]);
    im.expected_ack = static_cast<control_packet_type>(
        obj.value("expected_ack", static_cast<std::uint8_t>(0x40)));
    im.retry_count  = obj.value("retry_count", static_cast<std::uint8_t>(0));
    im.send_time    = std::chrono::steady_clock::now(); // Reset on load
    return im;
}

// --- session_state serialization ---

inline auto session_to_json(const session_state& ss) -> json {
    json obj;
    obj["client_id"]       = ss.client_id;
    obj["version"]         = static_cast<std::uint8_t>(ss.version);
    obj["clean_session"]   = ss.clean_session;
    obj["session_expiry"]  = ss.session_expiry_interval;
    obj["next_packet_id"]  = ss.next_packet_id;
    obj["username"]        = ss.username;

    // subscriptions
    json subs = json::object();
    for (auto& [filter, entry] : ss.subscriptions) {
        subs[filter] = sub_entry_to_json(entry);
    }
    obj["subscriptions"] = std::move(subs);

    // offline queue
    json queue = json::array();
    for (auto& m : ss.offline_queue) {
        queue.push_back(pub_msg_to_json(m));
    }
    obj["offline_queue"] = std::move(queue);

    // inflight_out
    json inflight = json::array();
    for (auto& im : ss.inflight_out) {
        inflight.push_back(inflight_to_json(im));
    }
    obj["inflight_out"] = std::move(inflight);

    // will
    if (ss.will_msg) {
        obj["will"] = will_to_json(*ss.will_msg);
    }

    return obj;
}

inline auto session_from_json(const json& obj) -> session_state {
    session_state ss;
    ss.client_id       = obj.value("client_id", "");
    ss.version         = static_cast<protocol_version>(obj.value("version", 4));
    ss.clean_session   = obj.value("clean_session", true);
    ss.session_expiry_interval = obj.value("session_expiry", static_cast<std::uint32_t>(0));
    ss.next_packet_id  = obj.value("next_packet_id", static_cast<std::uint16_t>(1));
    ss.username        = obj.value("username", "");

    // subscriptions
    if (obj.contains("subscriptions") && obj["subscriptions"].is_object()) {
        auto& subs_obj = obj["subscriptions"];
        for (auto it = subs_obj.begin(); it != subs_obj.end(); ++it) {
            ss.subscriptions[it.key()] = sub_entry_from_json(it.value());
        }
    }

    // offline queue
    if (obj.contains("offline_queue") && obj["offline_queue"].is_array()) {
        for (auto& m : obj["offline_queue"]) {
            ss.offline_queue.push_back(pub_msg_from_json(m));
        }
    }

    // inflight_out
    if (obj.contains("inflight_out") && obj["inflight_out"].is_array()) {
        for (auto& im_obj : obj["inflight_out"]) {
            ss.inflight_out.push_back(inflight_from_json(im_obj));
        }
    }

    // will
    if (obj.contains("will") && !obj["will"].is_null()) {
        ss.will_msg = will_from_json(obj["will"]);
    }

    ss.online = false; // All sessions are offline on load
    return ss;
}

// --- retained_message serialization ---

inline auto retained_to_json(const retained_message& rm) -> json {
    json obj;
    obj["topic"]   = rm.topic;
    obj["payload"] = rm.payload;
    obj["qos"]     = static_cast<std::uint8_t>(rm.qos_value);
    obj["props"]   = props_to_json(rm.props);
    return obj;
}

inline auto retained_from_json(const json& obj) -> retained_message {
    retained_message rm;
    rm.topic     = obj.value("topic", "");
    rm.payload   = obj.value("payload", "");
    rm.qos_value = static_cast<qos>(obj.value("qos", 0));
    if (obj.contains("props"))
        rm.props = props_from_json(obj["props"]);
    return rm;
}

} // namespace detail

// =============================================================================
// Persistence — Persistence manager
// =============================================================================

export class persistence {
public:
    explicit persistence(persistence_options opts = {})
        : opts_(std::move(opts)) {}

    // =========================================================================
    // Session persistence
    // =========================================================================

    /// Save all sessions to file
    auto save_sessions(const session_store& store)
        -> std::expected<void, std::string>
    {
        ensure_dir();

        json root = json::array();
        store.for_each([&](const session_state& ss) {
            // Only persist non-clean-session or sessions with offline data
            if (!ss.clean_session || !ss.offline_queue.empty() ||
                !ss.subscriptions.empty()) {
                root.push_back(detail::session_to_json(ss));
            }
        });

        auto path = opts_.data_dir + "/sessions.json";
        return write_file(path, root.dump(2));
    }

    /// Load sessions from file
    auto load_sessions() -> std::expected<session_store, std::string> {
        auto path = opts_.data_dir + "/sessions.json";
        auto content = read_file(path);
        if (!content) return std::unexpected(content.error());

        session_store store;
        try {
            auto root = json::parse(*content);
            if (!root.is_array())
                return std::unexpected(std::string("sessions.json: not an array"));

            for (auto& obj : root) {
                auto ss = detail::session_from_json(obj);
                if (!ss.client_id.empty()) {
                    auto [ref, _] = store.create_or_resume(
                        ss.client_id, ss.clean_session, ss.version);
                    ref = std::move(ss);
                    ref.online = false;
                }
            }
        } catch (const json::exception& e) {
            return std::unexpected(std::string("sessions.json parse error: ") + e.what());
        }

        return store;
    }

    // =========================================================================
    // Retained Messages persistence
    // =========================================================================

    /// Save retained messages
    auto save_retained(const retained_store& store)
        -> std::expected<void, std::string>
    {
        ensure_dir();

        json root = json::array();
        store.for_each([&](const retained_message& rm) {
            root.push_back(detail::retained_to_json(rm));
        });

        auto path = opts_.data_dir + "/retained.json";
        return write_file(path, root.dump(2));
    }

    /// Load retained messages
    auto load_retained() -> std::expected<retained_store, std::string> {
        auto path = opts_.data_dir + "/retained.json";
        auto content = read_file(path);
        if (!content) return std::unexpected(content.error());

        retained_store store;
        try {
            auto root = json::parse(*content);
            if (!root.is_array())
                return std::unexpected(std::string("retained.json: not an array"));

            for (auto& obj : root) {
                auto rm = detail::retained_from_json(obj);
                if (!rm.topic.empty()) {
                    store.store(rm.topic, std::move(rm));
                }
            }
        } catch (const json::exception& e) {
            return std::unexpected(std::string("retained.json parse error: ") + e.what());
        }

        return store;
    }

    // =========================================================================
    // Auto-flush
    // =========================================================================

    /// Start timed auto-flush coroutine
    auto start_auto_flush(io_context& ctx,
                           session_store& sessions,
                           retained_store& retained)
        -> task<void>
    {
        while (true) {
            co_await async_sleep(ctx, opts_.flush_interval);

            // Save sessions
            auto sr = save_sessions(sessions);
            if (!sr) {
                // Save failed, silently continue (can add logging)
            }

            // Save retained
            auto rr = save_retained(retained);
            if (!rr) {
                // Save failed, silently continue
            }
        }
    }

    /// Get configuration
    [[nodiscard]] auto options() const noexcept -> const persistence_options& {
        return opts_;
    }

private:
    // =========================================================================
    // File operation helpers
    // =========================================================================

    void ensure_dir() {
        std::filesystem::create_directories(opts_.data_dir);
    }

    static auto write_file(const std::string& path, const std::string& content)
        -> std::expected<void, std::string>
    {
        // Atomic write: write to temp file first, then rename
        auto tmp_path = path + ".tmp";
        {
            std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
            if (!ofs) return std::unexpected("cannot open " + tmp_path);
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!ofs) return std::unexpected("write failed: " + tmp_path);
        }

        std::error_code ec;
        std::filesystem::rename(tmp_path, path, ec);
        if (ec) {
            // If rename fails, try copy + remove
            std::filesystem::copy_file(tmp_path, path,
                std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(tmp_path, ec);
            if (ec) return std::unexpected("rename failed: " + ec.message());
        }
        return {};
    }

    static auto read_file(const std::string& path)
        -> std::expected<std::string, std::string>
    {
        if (!std::filesystem::exists(path)) {
            return std::unexpected("file not found: " + path);
        }

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return std::unexpected("cannot open " + path);

        std::string content(
            (std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>());
        return content;
    }

    persistence_options opts_;
};

} // namespace cnetmod::mqtt
