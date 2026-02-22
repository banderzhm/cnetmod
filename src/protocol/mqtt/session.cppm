/// cnetmod.protocol.mqtt:session — MQTT session state management
/// Server session lifecycle, subscriptions, offline messages, QoS inflight

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:session;

import std;
import :types;

namespace cnetmod::mqtt {

// =============================================================================
// Inflight messages (QoS 1/2 unacknowledged)
// =============================================================================

export struct inflight_message {
    std::uint16_t    packet_id  = 0;
    publish_message  msg;
    control_packet_type expected_ack = control_packet_type::puback; // Next expected ACK
    std::chrono::steady_clock::time_point send_time;
    std::uint8_t     retry_count = 0;
};

// =============================================================================
// Session State — Single client session
// =============================================================================

/// MQTT session state
/// Reference: MQTT v5.0 spec Section 4.1
export struct session_state {
    std::string      client_id;
    protocol_version version       = protocol_version::v3_1_1;
    bool             clean_session = true;

    // --- Subscriptions ---
    std::map<std::string, subscribe_entry> subscriptions;  // topic_filter → entry

    // --- QoS 1/2 messages ---
    std::vector<inflight_message> inflight_out;  // Sent to client, awaiting ACK
    std::set<std::uint16_t>       qos2_received; // Received QoS 2 PUBLISH packet_id (awaiting PUBREL)
    std::map<std::uint16_t, publish_message> qos2_pending_publish; // QoS 2 inbound staging (forward after PUBREL)

    // --- Offline message queue ---
    std::vector<publish_message>  offline_queue;
    std::size_t                   max_offline_queue = 1000;

    // --- Will ---
    std::optional<will>           will_msg;

    // --- v5 Session Expiry ---
    std::uint32_t    session_expiry_interval = 0; // Seconds, 0=expire immediately, 0xFFFFFFFF=never expire

    // --- v5 Receive Maximum flow control ---
    std::uint16_t    receive_maximum = 65535;        // Maximum inflight allowed by peer
    std::uint16_t    inflight_quota  = 65535;        // Current remaining quota

    // --- QoS retransmission config ---
    std::chrono::seconds retry_interval{20};         // Retransmission interval
    std::uint8_t         max_retries = 5;            // Maximum retransmission count

    // --- Connection status ---
    bool             online = false;
    std::chrono::steady_clock::time_point disconnect_time;

    // --- Server Packet ID allocation ---
    std::uint16_t    next_packet_id = 1;
    std::set<std::uint16_t> allocated_ids_;  // O(log n) accelerated lookup

    // --- Keep-alive ---
    std::uint16_t    keep_alive = 0;               // Seconds

    // --- Authentication info ---
    std::string      username;

    // =========================================================================
    // Methods
    // =========================================================================

    /// Allocate a new packet_id (skip IDs still in use)
    /// Uses allocated_ids_ set for O(log n) lookup, replacing linear scan of inflight_out
    auto alloc_packet_id() -> std::uint16_t {
        for (std::uint32_t attempt = 0; attempt < 65535; ++attempt) {
            auto id = next_packet_id++;
            if (next_packet_id == 0) next_packet_id = 1;
            if (!allocated_ids_.contains(id) && !qos2_received.contains(id)) {
                allocated_ids_.insert(id);
                return id;
            }
        }
        // Extreme case: all IDs in use
        return next_packet_id++;
    }

    /// Release packet_id (called when removing from inflight_out)
    void release_packet_id(std::uint16_t id) {
        allocated_ids_.erase(id);
    }

    /// Add subscription, returns whether it's new (not an update)
    auto add_subscription(const subscribe_entry& entry) -> bool {
        auto [it, inserted] = subscriptions.emplace(entry.topic_filter, entry);
        if (!inserted) {
            it->second = entry; // Update
        }
        return inserted;
    }

    /// Remove subscription, returns whether it existed
    auto remove_subscription(const std::string& topic_filter) -> bool {
        return subscriptions.erase(topic_filter) > 0;
    }

    /// Add message to offline queue (when client is offline)
    void enqueue_offline(publish_message msg) {
        if (offline_queue.size() >= max_offline_queue) {
            // Discard oldest
            offline_queue.erase(offline_queue.begin());
        }
        msg.enqueue_time = std::chrono::steady_clock::now();
        offline_queue.push_back(std::move(msg));
    }

    /// Check if message has expired (based on message_expiry_interval property)
    /// Returns true if expired and should be discarded, also updates remaining expiry value
    static auto check_message_expiry(publish_message& msg) -> bool {
        for (auto it = msg.props.begin(); it != msg.props.end(); ++it) {
            if (it->id == property_id::message_expiry_interval) {
                if (auto* v = std::get_if<std::uint32_t>(&it->value)) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - msg.enqueue_time);
                    auto elapsed_sec = static_cast<std::uint32_t>(elapsed.count());
                    if (elapsed_sec >= *v) return true; // Expired
                    // Update remaining expiry time
                    *v -= elapsed_sec;
                    return false;
                }
            }
        }
        return false; // No expiry property, doesn't expire
    }

    /// Mark as online
    void go_online() {
        online = true;
    }

    /// Mark as offline
    void go_offline() {
        online = false;
        disconnect_time = std::chrono::steady_clock::now();
    }

    /// Clear session data (clean session)
    void clear() {
        subscriptions.clear();
        inflight_out.clear();
        allocated_ids_.clear();
        qos2_received.clear();
        qos2_pending_publish.clear();
        offline_queue.clear();
        will_msg.reset();
        next_packet_id = 1;
    }

    /// Check if session has expired
    [[nodiscard]] auto is_expired() const -> bool {
        if (online) return false;
        if (session_expiry_interval == 0xFFFFFFFF) return false; // Never expire
        if (session_expiry_interval == 0) return true; // Expire immediately
        auto elapsed = std::chrono::steady_clock::now() - disconnect_time;
        return elapsed >= std::chrono::seconds(session_expiry_interval);
    }
};

// =============================================================================
// Session Store — Manage all sessions
// =============================================================================

export class session_store {
public:
    session_store() = default;

    // Non-copyable
    session_store(const session_store&) = delete;
    auto operator=(const session_store&) -> session_store& = delete;

    // Movable
    session_store(session_store&&) = default;
    auto operator=(session_store&&) -> session_store& = default;

    /// Find session
    auto find(const std::string& client_id) -> session_state* {
        auto it = sessions_.find(client_id);
        if (it != sessions_.end()) return &it->second;
        return nullptr;
    }

    auto find(const std::string& client_id) const -> const session_state* {
        auto it = sessions_.find(client_id);
        if (it != sessions_.end()) return &it->second;
        return nullptr;
    }

    /// Create or resume session
    /// If clean_session=true, clear old session and recreate
    /// If clean_session=false and old session exists, resume it
    /// Returns (session_state&, session_present)
    auto create_or_resume(const std::string& client_id, bool clean_session,
                          protocol_version ver) -> std::pair<session_state&, bool>
    {
        auto it = sessions_.find(client_id);

        if (it != sessions_.end()) {
            auto& ss = it->second;
            if (clean_session) {
                // Clear and reuse
                ss.clear();
                ss.version = ver;
                ss.clean_session = clean_session;
                ss.go_online();
                return {ss, false};
            }
            // Resume old session
            ss.version = ver;
            ss.clean_session = clean_session;
            ss.go_online();
            return {ss, true}; // session_present = true
        }

        // Create new session
        auto [new_it, _] = sessions_.emplace(client_id, session_state{});
        auto& ss = new_it->second;
        ss.client_id = client_id;
        ss.version = ver;
        ss.clean_session = clean_session;
        ss.go_online();
        return {ss, false};
    }

    /// Remove session
    auto remove(const std::string& client_id) -> bool {
        return sessions_.erase(client_id) > 0;
    }

    /// Iterate over all sessions
    template <typename Fn>
    void for_each(Fn&& fn) {
        for (auto& [id, ss] : sessions_) {
            fn(ss);
        }
    }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (auto& [id, ss] : sessions_) {
            fn(ss);
        }
    }

    /// Clean up expired offline sessions
    auto expire_sessions() -> std::size_t {
        std::size_t count = 0;
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (it->second.is_expired()) {
                // If will message exists and not sent, should trigger here
                it = sessions_.erase(it);
                ++count;
            } else {
                ++it;
            }
        }
        return count;
    }

    /// Total session count
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return sessions_.size();
    }

    /// Online session count
    [[nodiscard]] auto online_count() const noexcept -> std::size_t {
        std::size_t c = 0;
        for (auto& [_, ss] : sessions_) {
            if (ss.online) ++c;
        }
        return c;
    }

    /// Get underlying map reference (for persistence use)
    [[nodiscard]] auto sessions() noexcept
        -> std::map<std::string, session_state>& { return sessions_; }
    [[nodiscard]] auto sessions() const noexcept
        -> const std::map<std::string, session_state>& { return sessions_; }

private:
    std::map<std::string, session_state> sessions_;
};

} // namespace cnetmod::mqtt
