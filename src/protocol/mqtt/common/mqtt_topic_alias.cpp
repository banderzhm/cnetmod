module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import :topic_alias;

namespace cnetmod::mqtt {

topic_alias_send::topic_alias_send(std::uint16_t max_alias) noexcept
    : max_(max_alias) {}

void topic_alias_send::set_max(std::uint16_t max_alias) noexcept {
  max_ = max_alias;
  clear();
}

auto topic_alias_send::enabled() const noexcept -> bool { return max_ > 0; }

auto topic_alias_send::max() const noexcept -> std::uint16_t { return max_; }

auto topic_alias_send::find_by_alias(std::uint16_t alias) const -> std::string {
  const auto iterator = alias_to_topic_.find(alias);
  return iterator == alias_to_topic_.end() ? std::string{} : iterator->second;
}

auto topic_alias_send::find_by_topic(std::string_view topic) const
    -> std::optional<std::uint16_t> {
  const auto iterator = topic_to_alias_.find(std::string(topic));
  return iterator == topic_to_alias_.end()
             ? std::nullopt
             : std::optional<std::uint16_t>{iterator->second};
}

void topic_alias_send::insert_or_update(std::string_view topic,
                                        std::uint16_t alias) {
  if (alias == 0 || alias > max_)
    return;

  const auto topic_copy = std::string(topic);
  const auto alias_iterator = alias_to_topic_.find(alias);
  if (alias_iterator != alias_to_topic_.end()) {
    topic_to_alias_.erase(alias_iterator->second);
    alias_iterator->second = topic_copy;
  } else {
    alias_to_topic_[alias] = topic_copy;
  }
  topic_to_alias_[topic_copy] = alias;
  lru_time_[alias] = ++lru_clock_;
}

auto topic_alias_send::get_lru_alias() const -> std::uint16_t {
  if (max_ == 0)
    return 0;

  for (std::uint16_t alias = 1; alias <= max_; ++alias) {
    if (!alias_to_topic_.contains(alias))
      return alias;
  }

  std::uint16_t oldest_alias = 1;
  std::uint64_t oldest_time = std::numeric_limits<std::uint64_t>::max();
  for (const auto &[alias, time] : lru_time_) {
    if (time < oldest_time) {
      oldest_time = time;
      oldest_alias = alias;
    }
  }
  return oldest_alias;
}

auto topic_alias_send::allocate(std::string_view topic)
    -> std::pair<std::uint16_t, bool> {
  if (!enabled())
    return {0, false};

  if (const auto existing = find_by_topic(topic)) {
    lru_time_[*existing] = ++lru_clock_;
    return {*existing, false};
  }

  const auto alias = get_lru_alias();
  insert_or_update(topic, alias);
  return {alias, true};
}

void topic_alias_send::clear() {
  alias_to_topic_.clear();
  topic_to_alias_.clear();
  lru_time_.clear();
  lru_clock_ = 0;
}

topic_alias_recv::topic_alias_recv(std::uint16_t max_alias) noexcept
    : max_(max_alias) {}

void topic_alias_recv::set_max(std::uint16_t max_alias) noexcept {
  max_ = max_alias;
  clear();
}

auto topic_alias_recv::enabled() const noexcept -> bool { return max_ > 0; }

auto topic_alias_recv::max() const noexcept -> std::uint16_t { return max_; }

void topic_alias_recv::insert_or_update(std::string_view topic,
                                        std::uint16_t alias) {
  if (alias != 0 && alias <= max_)
    aliases_[alias] = std::string(topic);
}

auto topic_alias_recv::find(std::uint16_t alias) const -> std::string {
  const auto iterator = aliases_.find(alias);
  return iterator == aliases_.end() ? std::string{} : iterator->second;
}

auto topic_alias_recv::resolve(std::string_view topic, std::uint16_t alias)
    -> std::string {
  if (alias == 0)
    return std::string(topic);
  if (!topic.empty()) {
    insert_or_update(topic, alias);
    return std::string(topic);
  }
  return find(alias);
}

void topic_alias_recv::clear() { aliases_.clear(); }

} // namespace cnetmod::mqtt
