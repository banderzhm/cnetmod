module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import :session;
import :types;

namespace cnetmod::mqtt {

auto session_state::alloc_packet_id() -> std::uint16_t {
  for (std::uint32_t attempt = 0; attempt < 65535; ++attempt) {
    const auto id = next_packet_id++;
    if (next_packet_id == 0)
      next_packet_id = 1;
    if (!allocated_ids_.contains(id) && !qos2_received.contains(id)) {
      allocated_ids_.insert(id);
      return id;
    }
  }
  return next_packet_id++;
}

void session_state::release_packet_id(std::uint16_t id) {
  allocated_ids_.erase(id);
}

auto session_state::add_subscription(const subscribe_entry &entry) -> bool {
  auto [it, inserted] = subscriptions.emplace(entry.topic_filter, entry);
  if (!inserted)
    it->second = entry;
  return inserted;
}

auto session_state::remove_subscription(const std::string &topic_filter)
    -> bool {
  return subscriptions.erase(topic_filter) > 0;
}

void session_state::enqueue_offline(publish_message message) {
  if (offline_queue.size() >= max_offline_queue)
    offline_queue.erase(offline_queue.begin());
  message.enqueue_time = std::chrono::steady_clock::now();
  offline_queue.push_back(std::move(message));
}

auto session_state::check_message_expiry(publish_message &message) -> bool {
  for (auto &property : message.props) {
    if (property.id != property_id::message_expiry_interval)
      continue;
    if (auto *value = std::get_if<std::uint32_t>(&property.value)) {
      const auto elapsed = static_cast<std::uint32_t>(
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - message.enqueue_time)
              .count());
      if (elapsed >= *value)
        return true;
      *value -= elapsed;
      return false;
    }
  }
  return false;
}

void session_state::go_online() { online = true; }

void session_state::go_offline() {
  online = false;
  disconnect_time = std::chrono::steady_clock::now();
}

void session_state::clear() {
  subscriptions.clear();
  inflight_out.clear();
  allocated_ids_.clear();
  qos2_received.clear();
  qos2_pending_publish.clear();
  offline_queue.clear();
  will_msg.reset();
  next_packet_id = 1;
}

auto session_state::is_expired() const -> bool {
  if (online || session_expiry_interval == 0xFFFFFFFF)
    return false;
  if (session_expiry_interval == 0)
    return true;
  return std::chrono::steady_clock::now() - disconnect_time >=
         std::chrono::seconds(session_expiry_interval);
}

auto session_store::find(const std::string &client_id) -> session_state * {
  const auto it = sessions_.find(client_id);
  return it == sessions_.end() ? nullptr : &it->second;
}

auto session_store::find(const std::string &client_id) const
    -> const session_state * {
  const auto it = sessions_.find(client_id);
  return it == sessions_.end() ? nullptr : &it->second;
}

auto session_store::create_or_resume(const std::string &client_id,
                                     bool clean_session,
                                     protocol_version version)
    -> std::pair<session_state &, bool> {
  auto it = sessions_.find(client_id);
  if (it != sessions_.end()) {
    auto &state = it->second;
    if (clean_session)
      state.clear();
    state.version = version;
    state.clean_session = clean_session;
    state.go_online();
    return {state, !clean_session};
  }

  auto [created, unused] = sessions_.emplace(client_id, session_state{});
  (void)unused;
  auto &state = created->second;
  state.client_id = client_id;
  state.version = version;
  state.clean_session = clean_session;
  state.go_online();
  return {state, false};
}

auto session_store::remove(const std::string &client_id) -> bool {
  return sessions_.erase(client_id) > 0;
}

auto session_store::expire_sessions() -> std::size_t {
  std::size_t count = 0;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (it->second.is_expired()) {
      it = sessions_.erase(it);
      ++count;
    } else {
      ++it;
    }
  }
  return count;
}

auto session_store::size() const noexcept -> std::size_t {
  return sessions_.size();
}

auto session_store::online_count() const noexcept -> std::size_t {
  std::size_t count = 0;
  for (const auto &[client_id, state] : sessions_) {
    (void)client_id;
    if (state.online)
      ++count;
  }
  return count;
}

auto session_store::sessions() noexcept
    -> std::map<std::string, session_state> & {
  return sessions_;
}

auto session_store::sessions() const noexcept
    -> const std::map<std::string, session_state> & {
  return sessions_;
}

} // namespace cnetmod::mqtt
