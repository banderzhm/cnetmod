module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import nlohmann.json;
import :security;
import :types;
import :topic_filter;

namespace cnetmod::mqtt {
void security_config::set_sha256_hasher(sha256_hash_fn fn) {
  sha256_hasher_ = std::move(fn);
}
void security_config::add_user(const std::string &username,
                               const std::string &password,
                               std::vector<std::string> groups) {
  users_[username] = user_entry{.method = auth_method::plain_password,
                                .password = password,
                                .groups = std::move(groups)};
}
void security_config::set_anonymous_user(const std::string &username) {
  users_[username] = user_entry{.method = auth_method::anonymous};
  anonymous_user_ = username;
}
void security_config::set_allow_anonymous(bool allow) {
  allow_anonymous_ = allow;
}
void security_config::add_rule(auth_rule rule) {
  rules_.push_back(std::move(rule));
}
void security_config::allow_publish(const std::string &filter,
                                    std::set<std::string> groups) {
  rules_.push_back(auth_rule{.topic_filter = filter,
                             .pub_action = auth_action::allow,
                             .sub_action = auth_action::deny,
                             .groups = std::move(groups)});
}
void security_config::allow_subscribe(const std::string &filter,
                                      std::set<std::string> groups) {
  rules_.push_back(auth_rule{.topic_filter = filter,
                             .pub_action = auth_action::deny,
                             .sub_action = auth_action::allow,
                             .groups = std::move(groups)});
}
void security_config::allow_all(const std::string &filter,
                                std::set<std::string> groups) {
  rules_.push_back(auth_rule{.topic_filter = filter,
                             .pub_action = auth_action::allow,
                             .sub_action = auth_action::allow,
                             .groups = std::move(groups)});
}
auto security_config::authenticate(std::string_view username,
                                   std::string_view password) const
    -> std::optional<std::string> {
  if (username.empty())
    return allow_anonymous_ && !anonymous_user_.empty()
               ? std::optional<std::string>{anonymous_user_}
               : std::nullopt;
  const auto it = users_.find(std::string(username));
  if (it == users_.end())
    return std::nullopt;
  const auto &user = it->second;
  switch (user.method) {
  case auth_method::plain_password:
    return user.password == password
               ? std::optional<std::string>{std::string(username)}
               : std::nullopt;
  case auth_method::sha256:
    if (sha256_hasher_ &&
        sha256_hasher_(user.salt + std::string(password)) == user.digest)
      return std::string(username);
    return std::nullopt;
  case auth_method::anonymous:
    return std::string(username);
  case auth_method::unauthenticated:
    return std::nullopt;
  }
  return std::nullopt;
}
auto security_config::authorize_publish(const std::string &username,
                                        std::string_view topic) const -> bool {
  if (rules_.empty())
    return true;
  const auto groups = get_user_groups(username);
  bool allowed = false;
  for (const auto &rule : rules_)
    if (matches_topic(rule.topic_filter, topic) &&
        matches_groups(rule.groups, groups))
      allowed = rule.pub_action == auth_action::allow;
  return allowed;
}
auto security_config::authorize_subscribe(const std::string &username,
                                          std::string_view topic) const
    -> bool {
  if (rules_.empty())
    return true;
  const auto groups = get_user_groups(username);
  bool allowed = false;
  for (const auto &rule : rules_)
    if (matches_topic(rule.topic_filter, topic) &&
        matches_groups(rule.groups, groups))
      allowed = rule.sub_action == auth_action::allow;
  return allowed;
}
auto security_config::enabled() const noexcept -> bool {
  return !users_.empty() || !rules_.empty();
}
auto security_config::load_json(const json &config)
    -> std::expected<void, std::string> {
  try {
    if (config.contains("allow_anonymous"))
      allow_anonymous_ = config["allow_anonymous"].get<bool>();
    if (config.contains("anonymous_user"))
      set_anonymous_user(config["anonymous_user"].get<std::string>());
    if (config.contains("users") && config["users"].is_object())
      for (auto it = config["users"].begin(); it != config["users"].end();
           ++it) {
        const auto &object = it.value();
        user_entry user;
        user.method = auth_method::plain_password;
        user.password = object.value("password", "");
        if (object.contains("method")) {
          const auto method = object["method"].get<std::string>();
          if (method == "sha256")
            user.method = auth_method::sha256;
          else if (method == "anonymous")
            user.method = auth_method::anonymous;
        }
        if (object.contains("digest"))
          user.digest = object["digest"].get<std::string>();
        if (object.contains("salt"))
          user.salt = object["salt"].get<std::string>();
        if (object.contains("groups") && object["groups"].is_array())
          for (const auto &group : object["groups"])
            user.groups.push_back(group.get<std::string>());
        users_[it.key()] = std::move(user);
      }
    if (config.contains("rules") && config["rules"].is_array())
      for (const auto &object : config["rules"]) {
        auth_rule rule;
        rule.topic_filter = object.value("topic", "#");
        rule.pub_action = object.value("pub", "deny") == "allow"
                              ? auth_action::allow
                              : auth_action::deny;
        rule.sub_action = object.value("sub", "deny") == "allow"
                              ? auth_action::allow
                              : auth_action::deny;
        if (object.contains("groups") && object["groups"].is_array())
          for (const auto &group : object["groups"])
            rule.groups.insert(group.get<std::string>());
        rules_.push_back(std::move(rule));
      }
    return {};
  } catch (const json::exception &error) {
    return std::unexpected(std::string("security config parse error: ") +
                           error.what());
  }
}
auto security_config::load_file(const std::string &path)
    -> std::expected<void, std::string> {
  std::ifstream input(path);
  if (!input)
    return std::unexpected("cannot open: " + path);
  try {
    return load_json(json::parse(input));
  } catch (const json::exception &error) {
    return std::unexpected(std::string("JSON parse error: ") + error.what());
  }
}
auto security_config::get_user_groups(const std::string &username) const
    -> std::vector<std::string> {
  const auto it = users_.find(username);
  return it == users_.end() ? std::vector<std::string>{} : it->second.groups;
}
auto security_config::matches_topic(std::string_view rule_filter,
                                    std::string_view target) -> bool {
  return topic_matches(rule_filter, target);
}
auto security_config::matches_groups(const std::set<std::string> &rules,
                                     const std::vector<std::string> &groups)
    -> bool {
  if (rules.empty())
    return true;
  for (const auto &group : groups)
    if (rules.count(group))
      return true;
  return false;
}
} // namespace cnetmod::mqtt
