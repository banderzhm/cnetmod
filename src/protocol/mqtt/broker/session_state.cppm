module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:session;

import std;
import :types;

export namespace cnetmod::mqtt {
struct inflight_message {
  std::uint16_t packet_id = 0;
  publish_message msg;
  control_packet_type expected_ack = control_packet_type::puback;
  std::chrono::steady_clock::time_point send_time;
  std::uint8_t retry_count = 0;
};
struct session_state {
  std::string client_id;
  protocol_version version = protocol_version::v3_1_1;
  bool clean_session = true;
  std::map<std::string, subscribe_entry> subscriptions;
  std::vector<inflight_message> inflight_out;
  std::set<std::uint16_t> qos2_received;
  std::map<std::uint16_t, publish_message> qos2_pending_publish;
  std::vector<publish_message> offline_queue;
  std::size_t max_offline_queue = 1000;
  std::optional<will> will_msg;
  std::uint32_t session_expiry_interval = 0;
  std::uint16_t receive_maximum = 65535;
  std::uint16_t inflight_quota = 65535;
  std::chrono::seconds retry_interval{20};
  std::uint8_t max_retries = 5;
  bool online = false;
  std::chrono::steady_clock::time_point disconnect_time;
  std::uint16_t next_packet_id = 1;
  std::set<std::uint16_t> allocated_ids_;
  std::uint16_t keep_alive = 0;
  std::string username;
  auto alloc_packet_id() -> std::uint16_t;
  void release_packet_id(std::uint16_t id);
  auto add_subscription(const subscribe_entry &entry) -> bool;
  auto remove_subscription(const std::string &topic_filter) -> bool;
  void enqueue_offline(publish_message msg);
  static auto check_message_expiry(publish_message &msg) -> bool;
  void go_online();
  void go_offline();
  void clear();
  [[nodiscard]] auto is_expired() const -> bool;
};

class session_store {
public:
  session_store() = default;
  session_store(const session_store &) = delete;
  auto operator=(const session_store &) -> session_store & = delete;
  session_store(session_store &&) = default;
  auto operator=(session_store &&) -> session_store & = default;
  auto find(const std::string &client_id) -> session_state *;
  auto find(const std::string &client_id) const -> const session_state *;
  auto create_or_resume(const std::string &client_id, bool clean_session,
                        protocol_version ver)
      -> std::pair<session_state &, bool>;
  auto remove(const std::string &client_id) -> bool;
  template <typename Fn> void for_each(Fn &&fn) {
    for (auto &[id, state] : sessions_)
      fn(state);
  }
  template <typename Fn> void for_each(Fn &&fn) const {
    for (const auto &[id, state] : sessions_)
      fn(state);
  }
  auto expire_sessions() -> std::size_t;
  [[nodiscard]] auto size() const noexcept -> std::size_t;
  [[nodiscard]] auto online_count() const noexcept -> std::size_t;
  [[nodiscard]] auto sessions() noexcept
      -> std::map<std::string, session_state> &;
  [[nodiscard]] auto sessions() const noexcept
      -> const std::map<std::string, session_state> &;

private:
  std::map<std::string, session_state> sessions_;
};
} // namespace cnetmod::mqtt
