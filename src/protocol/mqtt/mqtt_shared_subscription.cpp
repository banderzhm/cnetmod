module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import :shared_sub;

namespace cnetmod::mqtt {

auto parse_shared_subscription(std::string_view whole_filter)
    -> std::optional<share_name_topic_filter> {
  constexpr std::string_view prefix = "$share/";
  if (!whole_filter.starts_with(prefix)) {
    return share_name_topic_filter{{}, std::string(whole_filter)};
  }

  const auto rest = whole_filter.substr(prefix.size());
  const auto slash_pos = rest.find('/');
  if (slash_pos == std::string_view::npos)
    return std::nullopt;

  const auto share_name = rest.substr(0, slash_pos);
  const auto topic_filter = rest.substr(slash_pos + 1);
  if (share_name.empty() || topic_filter.empty())
    return std::nullopt;

  return share_name_topic_filter{std::string(share_name),
                                 std::string(topic_filter)};
}

auto is_shared_subscription(std::string_view filter) noexcept -> bool {
  return filter.starts_with("$share/");
}

auto extract_topic_filter(std::string_view filter) -> std::string {
  auto parsed = parse_shared_subscription(filter);
  return parsed ? std::move(parsed->topic_filter) : std::string(filter);
}

auto shared_target_store::make_key(std::string_view share_name,
                                   std::string_view filter) -> std::string {
  std::string key;
  key.reserve(share_name.size() + 1 + filter.size());
  key.append(share_name);
  key.push_back('/');
  key.append(filter);
  return key;
}

void shared_target_store::add_member(std::string_view share_name,
                                     std::string_view filter,
                                     const std::string &client_id) {
  auto &group = groups_[make_key(share_name, filter)];
  for (const auto &member : group.members) {
    if (member.client_id == client_id)
      return;
  }
  group.members.push_back(shared_member{client_id, std::string(filter)});
}

void shared_target_store::remove_member(std::string_view share_name,
                                        std::string_view filter,
                                        const std::string &client_id) {
  auto it = groups_.find(make_key(share_name, filter));
  if (it == groups_.end())
    return;

  std::erase_if(it->second.members, [&client_id](const shared_member &member) {
    return member.client_id == client_id;
  });
  if (it->second.members.empty())
    groups_.erase(it);
}

void shared_target_store::remove_client(const std::string &client_id) {
  for (auto it = groups_.begin(); it != groups_.end();) {
    std::erase_if(it->second.members,
                  [&client_id](const shared_member &member) {
                    return member.client_id == client_id;
                  });
    if (it->second.members.empty()) {
      it = groups_.erase(it);
    } else {
      ++it;
    }
  }
}

auto shared_target_store::select_target(std::string_view share_name,
                                        std::string_view filter)
    -> std::string {
  auto it = groups_.find(make_key(share_name, filter));
  if (it == groups_.end() || it->second.members.empty())
    return {};

  auto &group = it->second;
  const auto index = group.rr_index++ % group.members.size();
  return group.members[index].client_id;
}

auto shared_target_store::group_count() const noexcept -> std::size_t {
  return groups_.size();
}

void shared_target_store::clear() { groups_.clear(); }

} // namespace cnetmod::mqtt
