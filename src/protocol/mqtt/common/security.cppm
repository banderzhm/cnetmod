module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:security;

import std;
import nlohmann.json;
import :types;

export namespace cnetmod::mqtt {
using json = nlohmann::json;
enum class auth_method { plain_password, sha256, anonymous, unauthenticated };
struct user_entry {
  auth_method method = auth_method::plain_password;
  std::string password;
  std::string digest;
  std::string salt;
  std::vector<std::string> groups;
};
enum class auth_action { allow, deny };
struct auth_rule {
  std::string topic_filter;
  auth_action pub_action = auth_action::deny;
  auth_action sub_action = auth_action::deny;
  std::set<std::string> groups;
};
using sha256_hash_fn = std::function<std::string(std::string_view)>;

class security_config {
public:
  security_config() = default;
  void set_sha256_hasher(sha256_hash_fn fn);
  void add_user(const std::string &username, const std::string &password,
                std::vector<std::string> groups = {});
  void set_anonymous_user(const std::string &username);
  void set_allow_anonymous(bool allow);
  void add_rule(auth_rule rule);
  void allow_publish(const std::string &topic_filter,
                     std::set<std::string> groups = {});
  void allow_subscribe(const std::string &topic_filter,
                       std::set<std::string> groups = {});
  void allow_all(const std::string &topic_filter,
                 std::set<std::string> groups = {});
  [[nodiscard]] auto authenticate(std::string_view username,
                                  std::string_view password) const
      -> std::optional<std::string>;
  [[nodiscard]] auto authorize_publish(const std::string &username,
                                       std::string_view topic) const -> bool;
  [[nodiscard]] auto authorize_subscribe(const std::string &username,
                                         std::string_view topic_filter) const
      -> bool;
  [[nodiscard]] auto enabled() const noexcept -> bool;
  auto load_json(const json &config) -> std::expected<void, std::string>;
  auto load_file(const std::string &path) -> std::expected<void, std::string>;

private:
  [[nodiscard]] auto get_user_groups(const std::string &username) const
      -> std::vector<std::string>;
  static auto matches_topic(std::string_view rule_filter,
                            std::string_view target) -> bool;
  static auto matches_groups(const std::set<std::string> &rule_groups,
                             const std::vector<std::string> &user_groups)
      -> bool;
  std::map<std::string, user_entry> users_;
  std::vector<auth_rule> rules_;
  std::string anonymous_user_;
  bool allow_anonymous_ = false;
  sha256_hash_fn sha256_hasher_;
};
} // namespace cnetmod::mqtt
