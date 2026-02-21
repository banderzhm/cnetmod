/// cnetmod.protocol.mqtt:session — MQTT 会话状态管理
/// 服务端 session 生命周期、订阅、离线消息、QoS inflight

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:session;

import std;
import :types;

namespace cnetmod::mqtt {

// =============================================================================
// Inflight 消息 (QoS 1/2 未完成确认)
// =============================================================================

export struct inflight_message {
    std::uint16_t    packet_id  = 0;
    publish_message  msg;
    control_packet_type expected_ack = control_packet_type::puback; // 下一步期待的 ACK
    std::chrono::steady_clock::time_point send_time;
    std::uint8_t     retry_count = 0;
};

// =============================================================================
// Session State — 单个客户端会话
// =============================================================================

/// MQTT 会话状态
/// 参考 MQTT v5.0 spec Section 4.1
export struct session_state {
    std::string      client_id;
    protocol_version version       = protocol_version::v3_1_1;
    bool             clean_session = true;

    // --- 订阅 ---
    std::map<std::string, subscribe_entry> subscriptions;  // topic_filter → entry

    // --- QoS 1/2 消息 ---
    std::vector<inflight_message> inflight_out;  // 发往客户端、等待 ACK
    std::set<std::uint16_t>       qos2_received; // 收到的 QoS 2 PUBLISH packet_id (等 PUBREL)
    std::map<std::uint16_t, publish_message> qos2_pending_publish; // QoS 2 入站暂存 (等 PUBREL 后转发)

    // --- 离线消息队列 ---
    std::vector<publish_message>  offline_queue;
    std::size_t                   max_offline_queue = 1000;

    // --- Will ---
    std::optional<will>           will_msg;

    // --- v5 Session Expiry ---
    std::uint32_t    session_expiry_interval = 0; // 秒, 0=立即过期, 0xFFFFFFFF=永不过期

    // --- v5 Receive Maximum 流控 ---
    std::uint16_t    receive_maximum = 65535;        // 对端允许的最大 inflight 数
    std::uint16_t    inflight_quota  = 65535;        // 当前剩余配额

    // --- QoS 重传配置 ---
    std::chrono::seconds retry_interval{20};         // 重传间隔
    std::uint8_t         max_retries = 5;            // 最大重传次数

    // --- 连接状态 ---
    bool             online = false;
    std::chrono::steady_clock::time_point disconnect_time;

    // --- 服务端 Packet ID 分配 ---
    std::uint16_t    next_packet_id = 1;
    std::set<std::uint16_t> allocated_ids_;  // O(log n) 加速查找

    // --- Keep-alive ---
    std::uint16_t    keep_alive = 0;               // 秒

    // --- 认证信息 ---
    std::string      username;

    // =========================================================================
    // 方法
    // =========================================================================

    /// 分配一个新的 packet_id（跳过仍在使用中的 ID）
    /// 使用 allocated_ids_ 集合实现 O(log n) 查找，替代线性扫描 inflight_out
    auto alloc_packet_id() -> std::uint16_t {
        for (std::uint32_t attempt = 0; attempt < 65535; ++attempt) {
            auto id = next_packet_id++;
            if (next_packet_id == 0) next_packet_id = 1;
            if (!allocated_ids_.contains(id) && !qos2_received.contains(id)) {
                allocated_ids_.insert(id);
                return id;
            }
        }
        // 极端情况：所有 ID 都在使用
        return next_packet_id++;
    }

    /// 释放 packet_id（从 inflight_out 移除时调用）
    void release_packet_id(std::uint16_t id) {
        allocated_ids_.erase(id);
    }

    /// 添加订阅，返回是否为新增（非更新）
    auto add_subscription(const subscribe_entry& entry) -> bool {
        auto [it, inserted] = subscriptions.emplace(entry.topic_filter, entry);
        if (!inserted) {
            it->second = entry; // 更新
        }
        return inserted;
    }

    /// 移除订阅，返回是否存在
    auto remove_subscription(const std::string& topic_filter) -> bool {
        return subscriptions.erase(topic_filter) > 0;
    }

    /// 将消息加入离线队列（当客户端离线时）
    void enqueue_offline(publish_message msg) {
        if (offline_queue.size() >= max_offline_queue) {
            // 丢弃最旧的
            offline_queue.erase(offline_queue.begin());
        }
        msg.enqueue_time = std::chrono::steady_clock::now();
        offline_queue.push_back(std::move(msg));
    }

    /// 检查消息是否已过期（根据 message_expiry_interval 属性）
    /// 返回 true 表示已过期应丢弃，同时更新剩余 expiry 值
    static auto check_message_expiry(publish_message& msg) -> bool {
        for (auto it = msg.props.begin(); it != msg.props.end(); ++it) {
            if (it->id == property_id::message_expiry_interval) {
                if (auto* v = std::get_if<std::uint32_t>(&it->value)) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - msg.enqueue_time);
                    auto elapsed_sec = static_cast<std::uint32_t>(elapsed.count());
                    if (elapsed_sec >= *v) return true; // 过期
                    // 更新剩余过期时间
                    *v -= elapsed_sec;
                    return false;
                }
            }
        }
        return false; // 无 expiry 属性，不过期
    }

    /// 标记上线
    void go_online() {
        online = true;
    }

    /// 标记离线
    void go_offline() {
        online = false;
        disconnect_time = std::chrono::steady_clock::now();
    }

    /// 清空会话数据（clean session）
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

    /// 检查会话是否已过期
    [[nodiscard]] auto is_expired() const -> bool {
        if (online) return false;
        if (session_expiry_interval == 0xFFFFFFFF) return false; // 永不过期
        if (session_expiry_interval == 0) return true; // 立即过期
        auto elapsed = std::chrono::steady_clock::now() - disconnect_time;
        return elapsed >= std::chrono::seconds(session_expiry_interval);
    }
};

// =============================================================================
// Session Store — 管理所有会话
// =============================================================================

export class session_store {
public:
    session_store() = default;

    // 不可复制
    session_store(const session_store&) = delete;
    auto operator=(const session_store&) -> session_store& = delete;

    // 可移动
    session_store(session_store&&) = default;
    auto operator=(session_store&&) -> session_store& = default;

    /// 查找会话
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

    /// 创建或恢复会话
    /// 如果 clean_session=true，则清空旧会话并重新创建
    /// 如果 clean_session=false 且旧会话存在，则恢复
    /// 返回 (session_state&, session_present)
    auto create_or_resume(const std::string& client_id, bool clean_session,
                          protocol_version ver) -> std::pair<session_state&, bool>
    {
        auto it = sessions_.find(client_id);

        if (it != sessions_.end()) {
            auto& ss = it->second;
            if (clean_session) {
                // 清空并重用
                ss.clear();
                ss.version = ver;
                ss.clean_session = clean_session;
                ss.go_online();
                return {ss, false};
            }
            // 恢复旧会话
            ss.version = ver;
            ss.clean_session = clean_session;
            ss.go_online();
            return {ss, true}; // session_present = true
        }

        // 新建
        auto [new_it, _] = sessions_.emplace(client_id, session_state{});
        auto& ss = new_it->second;
        ss.client_id = client_id;
        ss.version = ver;
        ss.clean_session = clean_session;
        ss.go_online();
        return {ss, false};
    }

    /// 移除会话
    auto remove(const std::string& client_id) -> bool {
        return sessions_.erase(client_id) > 0;
    }

    /// 遍历所有会话
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

    /// 清理过期离线会话
    auto expire_sessions() -> std::size_t {
        std::size_t count = 0;
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (it->second.is_expired()) {
                // 如果有 will 且未发送，应在此处触发
                it = sessions_.erase(it);
                ++count;
            } else {
                ++it;
            }
        }
        return count;
    }

    /// 会话总数
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return sessions_.size();
    }

    /// 在线会话数
    [[nodiscard]] auto online_count() const noexcept -> std::size_t {
        std::size_t c = 0;
        for (auto& [_, ss] : sessions_) {
            if (ss.online) ++c;
        }
        return c;
    }

    /// 获取底层 map 引用（供持久化使用）
    [[nodiscard]] auto sessions() noexcept
        -> std::map<std::string, session_state>& { return sessions_; }
    [[nodiscard]] auto sessions() const noexcept
        -> const std::map<std::string, session_state>& { return sessions_; }

private:
    std::map<std::string, session_state> sessions_;
};

} // namespace cnetmod::mqtt
