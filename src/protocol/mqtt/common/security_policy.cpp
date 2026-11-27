module;

#include <cnetmod/config.hpp>
#include "../../../utils/detail/simdjson_depth.hpp"

module cnetmod.protocol.mqtt;

import std;
import :security;
import :types;
import :topic_filter;

namespace cnetmod::mqtt {
void security_config::set_sha256_hasher(sha256_hash_fn fn)
{
    sha256_hasher_ = std::move(fn);
}

void security_config::add_user(const std::string& username,
    const std::string& password,
    std::vector<std::string> groups)
{
    users_[username] = user_entry{.method = auth_method::plain_password,
        .password = password,
        .groups = std::move(groups)};
}

void security_config::set_anonymous_user(const std::string& username)
{
    users_[username] = user_entry{.method = auth_method::anonymous};
    anonymous_user_ = username;
}

void security_config::set_allow_anonymous(bool allow)
{
    allow_anonymous_ = allow;
}

void security_config::add_rule(auth_rule rule)
{
    rules_.push_back(std::move(rule));
}

void security_config::allow_publish(const std::string& filter,
    std::set<std::string> groups)
{
    rules_.push_back(auth_rule{.topic_filter = filter,
        .pub_action = auth_action::allow,
        .sub_action = auth_action::deny,
        .groups = std::move(groups)});
}

void security_config::allow_subscribe(const std::string& filter,
    std::set<std::string> groups)
{
    rules_.push_back(auth_rule{.topic_filter = filter,
        .pub_action = auth_action::deny,
        .sub_action = auth_action::allow,
        .groups = std::move(groups)});
}

void security_config::allow_all(const std::string& filter,
    std::set<std::string> groups)
{
    rules_.push_back(auth_rule{.topic_filter = filter,
        .pub_action = auth_action::allow,
        .sub_action = auth_action::allow,
        .groups = std::move(groups)});
}

auto security_config::authenticate(std::string_view username,
    std::string_view password) const
    -> std::optional<std::string>
{
    if (username.empty())
        return allow_anonymous_ && !anonymous_user_.empty()
            ? std::optional<std::string>{anonymous_user_}
            : std::nullopt;
    const auto it = users_.find(std::string(username));
    if (it == users_.end())
        return std::nullopt;
    const auto& user = it->second;
    switch (user.method)
    {
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

auto security_config::authorize_publish(const std::string& username,
    std::string_view topic) const -> bool
{
    if (rules_.empty())
        return true;
    const auto groups = get_user_groups(username);
    bool allowed = false;
    for (const auto& rule : rules_)
        if (matches_topic(rule.topic_filter, topic) &&
            matches_groups(rule.groups, groups))
            allowed = rule.pub_action == auth_action::allow;
    return allowed;
}

auto security_config::authorize_subscribe(const std::string& username,
    std::string_view topic) const
    -> bool
{
    if (rules_.empty())
        return true;
    const auto groups = get_user_groups(username);
    bool allowed = false;
    for (const auto& rule : rules_)
        if (matches_topic(rule.topic_filter, topic) &&
            matches_groups(rule.groups, groups))
            allowed = rule.sub_action == auth_action::allow;
    return allowed;
}

auto security_config::enabled() const noexcept -> bool
{
    return !users_.empty() || !rules_.empty();
}

auto security_config::load_json(std::string_view config)
    -> std::expected<void, std::string>
{
    using cnetmod::detail::find_last_json_field;

    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (!cnetmod::detail::parse_bounded_json(parser, config, root) ||
        !root.is_object())
        return std::unexpected("JSON parse error");

    try
    {
        auto updated = *this;
        if (const auto allow = find_last_json_field(root, "allow_anonymous"); !allow.error())
            updated.allow_anonymous_ = allow.get_bool().value();
        if (const auto guest = find_last_json_field(root, "anonymous_user"); !guest.error())
            updated.set_anonymous_user(std::string{guest.get_string().value()});

        simdjson::dom::object users;
        if (!find_last_json_field(root, "users").get_object().get(users))
        {
            for (const auto field : users)
            {
                user_entry user;
                const auto object = field.value;
                std::string_view password;
                if (!find_last_json_field(object, "password").get_string().get(password))
                    user.password = password;
                if (const auto method = find_last_json_field(object, "method"); !method.error())
                {
                    const auto name = method.get_string().value();
                    if (name == "sha256")
                        user.method = auth_method::sha256;
                    else if (name == "anonymous")
                        user.method = auth_method::anonymous;
                }
                if (const auto digest = find_last_json_field(object, "digest"); !digest.error())
                    user.digest = digest.get_string().value();
                if (const auto salt = find_last_json_field(object, "salt"); !salt.error())
                    user.salt = salt.get_string().value();
                simdjson::dom::array groups;
                if (!find_last_json_field(object, "groups").get_array().get(groups))
                    for (const auto group : groups)
                        user.groups.emplace_back(group.get_string().value());
                updated.users_.insert_or_assign(std::string{field.key}, std::move(user));
            }
        }

        simdjson::dom::array rules;
        if (!find_last_json_field(root, "rules").get_array().get(rules))
        {
            for (const auto object : rules)
            {
                auth_rule rule{.topic_filter = "#"};
                std::string_view text;
                if (!find_last_json_field(object, "topic").get_string().get(text))
                    rule.topic_filter = text;
                if (!find_last_json_field(object, "pub").get_string().get(text) && text == "allow")
                    rule.pub_action = auth_action::allow;
                if (!find_last_json_field(object, "sub").get_string().get(text) && text == "allow")
                    rule.sub_action = auth_action::allow;
                simdjson::dom::array groups;
                if (!find_last_json_field(object, "groups").get_array().get(groups))
                    for (const auto group : groups)
                        rule.groups.emplace(group.get_string().value());
                updated.rules_.push_back(std::move(rule));
            }
        }
        users_ = std::move(updated.users_);
        rules_ = std::move(updated.rules_);
        anonymous_user_ = std::move(updated.anonymous_user_);
        allow_anonymous_ = updated.allow_anonymous_;
        return {};
    }
    catch (const std::exception& error)
    {
        return std::unexpected(std::string("security config parse error: ") + error.what());
    }
}

auto security_config::load_file(const std::string& path)
    -> std::expected<void, std::string>
{
    std::ifstream input(path);
    if (!input)
        return std::unexpected("cannot open: " + path);
    const std::string content{std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    return load_json(content);
}

auto security_config::get_user_groups(const std::string& username) const
    -> std::vector<std::string>
{
    const auto it = users_.find(username);
    return it == users_.end() ? std::vector<std::string>{} : it->second.groups;
}

auto security_config::matches_topic(std::string_view rule_filter,
    std::string_view target) -> bool
{
    return topic_matches(rule_filter, target);
}

auto security_config::matches_groups(const std::set<std::string>& rules,
    const std::vector<std::string>& groups)
    -> bool
{
    if (rules.empty())
        return true;
    for (const auto& group : groups)
        if (rules.count(group))
            return true;
    return false;
}
} // namespace cnetmod::mqtt
