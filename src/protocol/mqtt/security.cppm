/// cnetmod.protocol.mqtt:security — MQTT Authentication and Authorization
/// Supports plain password/SHA256/anonymous authentication, topic-level authorization based on user groups

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:security;

import std;
import nlohmann.json;
import :types;
import :topic_filter;

namespace cnetmod::mqtt {

using json = nlohmann::json;

// =============================================================================
// Authentication Methods
// =============================================================================

export enum class auth_method {
    plain_password,   // Plain text password
    sha256,           // SHA256(salt + password)
    anonymous,        // Anonymous user (no password required)
    unauthenticated,  // Unauthenticated (deny all)
};

// =============================================================================
// User Entry
// =============================================================================

export struct user_entry {
    auth_method method = auth_method::plain_password;
    std::string password;          // Plain password (plain_password mode)
    std::string digest;            // Hash value (sha256 mode)
    std::string salt;              // Salt value (sha256 mode)
    std::vector<std::string> groups; // User groups
};

// =============================================================================
// Authorization Rules
// =============================================================================

export enum class auth_action {
    allow,
    deny,
};

export struct auth_rule {
    std::string  topic_filter;      // Supports +/# wildcards
    auth_action  pub_action = auth_action::deny;
    auth_action  sub_action = auth_action::deny;
    std::set<std::string> groups;   // Applicable user groups (empty means all users)
};

// =============================================================================
// Security Config — Security Configuration
// =============================================================================

/// SHA256 hash callback: accepts raw data, returns hexadecimal hash string
export using sha256_hash_fn = std::function<std::string(std::string_view)>;

export class security_config {
public:
    security_config() = default;

    // ----- SHA256 Hash Function Injection -----

    /// Set SHA256 hash function (for auth_method::sha256 verification)
    void set_sha256_hasher(sha256_hash_fn fn) { sha256_hasher_ = std::move(fn); }

    // ----- User Management -----

    /// Add plain password user
    void add_user(const std::string& username, const std::string& password,
                  std::vector<std::string> groups = {})
    {
        user_entry u;
        u.method = auth_method::plain_password;
        u.password = password;
        u.groups = std::move(groups);
        users_[username] = std::move(u);
    }

    /// Add anonymous user
    void set_anonymous_user(const std::string& username) {
        user_entry u;
        u.method = auth_method::anonymous;
        users_[username] = std::move(u);
        anonymous_user_ = username;
    }

    /// Whether to allow anonymous connections
    void set_allow_anonymous(bool allow) { allow_anonymous_ = allow; }

    // ----- Authorization Rules -----

    /// Add authorization rule
    void add_rule(auth_rule rule) {
        rules_.push_back(std::move(rule));
    }

    /// Add allow publish rule
    void allow_publish(const std::string& topic_filter,
                       std::set<std::string> groups = {})
    {
        auth_rule r;
        r.topic_filter = topic_filter;
        r.pub_action = auth_action::allow;
        r.sub_action = auth_action::deny;
        r.groups = std::move(groups);
        rules_.push_back(std::move(r));
    }

    /// Add allow subscribe rule
    void allow_subscribe(const std::string& topic_filter,
                         std::set<std::string> groups = {})
    {
        auth_rule r;
        r.topic_filter = topic_filter;
        r.pub_action = auth_action::deny;
        r.sub_action = auth_action::allow;
        r.groups = std::move(groups);
        rules_.push_back(std::move(r));
    }

    /// Add allow publish and subscribe rule
    void allow_all(const std::string& topic_filter,
                   std::set<std::string> groups = {})
    {
        auth_rule r;
        r.topic_filter = topic_filter;
        r.pub_action = auth_action::allow;
        r.sub_action = auth_action::allow;
        r.groups = std::move(groups);
        rules_.push_back(std::move(r));
    }

    // ----- Authentication -----

    /// Authenticate user
    /// Returns authenticated username (may be anonymous user), nullopt indicates authentication failure
    [[nodiscard]] auto authenticate(std::string_view username,
                                     std::string_view password) const
        -> std::optional<std::string>
    {
        if (username.empty()) {
            // Anonymous connection
            if (allow_anonymous_ && !anonymous_user_.empty()) {
                return anonymous_user_;
            }
            return std::nullopt;
        }

        auto it = users_.find(std::string(username));
        if (it == users_.end()) return std::nullopt;

        auto& u = it->second;
        switch (u.method) {
        case auth_method::plain_password:
            if (u.password == password) {
                return std::string(username);
            }
            return std::nullopt;

        case auth_method::sha256:
            if (sha256_hasher_) {
                // SHA256 verification: digest == sha256(salt + password)
                auto computed = sha256_hasher_(u.salt + std::string(password));
                if (computed == u.digest)
                    return std::string(username);
            }
            return std::nullopt;

        case auth_method::anonymous:
            return std::string(username);

        case auth_method::unauthenticated:
            return std::nullopt;
        }
        return std::nullopt;
    }

    // ----- Authorization -----

    /// Check if user has permission to publish to specified topic
    [[nodiscard]] auto authorize_publish(const std::string& username,
                                          std::string_view topic) const -> bool
    {
        if (rules_.empty()) return true; // Default allow when no rules

        auto user_groups = get_user_groups(username);

        // Iterate through rules, last match takes effect (later defined takes priority)
        bool allowed = false;
        for (auto& rule : rules_) {
            if (!matches_topic(rule.topic_filter, topic)) continue;
            if (!matches_groups(rule.groups, user_groups)) continue;
            allowed = (rule.pub_action == auth_action::allow);
        }
        return allowed;
    }

    /// Check if user has permission to subscribe to specified topic filter
    [[nodiscard]] auto authorize_subscribe(const std::string& username,
                                            std::string_view topic_filter_str) const -> bool
    {
        if (rules_.empty()) return true;

        auto user_groups = get_user_groups(username);

        bool allowed = false;
        for (auto& rule : rules_) {
            // Subscribe authorization: rule filter needs to cover subscription filter
            if (!matches_topic(rule.topic_filter, topic_filter_str)) continue;
            if (!matches_groups(rule.groups, user_groups)) continue;
            allowed = (rule.sub_action == auth_action::allow);
        }
        return allowed;
    }

    /// Whether security is enabled (has users or rules)
    [[nodiscard]] auto enabled() const noexcept -> bool {
        return !users_.empty() || !rules_.empty();
    }

    // ----- JSON Configuration Loading -----

    /// Load configuration from JSON
    /// Format:
    /// {
    ///   "allow_anonymous": false,
    ///   "users": { "user1": { "password": "pass1", "groups": ["admin"] }, ... },
    ///   "anonymous_user": "guest",
    ///   "rules": [
    ///     { "topic": "sensor/#", "pub": "allow", "sub": "allow", "groups": ["admin"] },
    ///     ...
    ///   ]
    /// }
    auto load_json(const json& config) -> std::expected<void, std::string> {
        try {
            if (config.contains("allow_anonymous")) {
                allow_anonymous_ = config["allow_anonymous"].get<bool>();
            }

            if (config.contains("anonymous_user")) {
                set_anonymous_user(config["anonymous_user"].get<std::string>());
            }

            if (config.contains("users") && config["users"].is_object()) {
                for (auto it = config["users"].begin(); it != config["users"].end(); ++it) {
                    auto& uobj = it.value();
                    user_entry u;
                    u.method = auth_method::plain_password;
                    u.password = uobj.value("password", "");

                    if (uobj.contains("method")) {
                        auto m = uobj["method"].get<std::string>();
                        if (m == "sha256") u.method = auth_method::sha256;
                        else if (m == "anonymous") u.method = auth_method::anonymous;
                    }
                    if (uobj.contains("digest")) u.digest = uobj["digest"].get<std::string>();
                    if (uobj.contains("salt")) u.salt = uobj["salt"].get<std::string>();

                    if (uobj.contains("groups") && uobj["groups"].is_array()) {
                        for (auto& g : uobj["groups"]) {
                            u.groups.push_back(g.get<std::string>());
                        }
                    }
                    users_[it.key()] = std::move(u);
                }
            }

            if (config.contains("rules") && config["rules"].is_array()) {
                for (auto& robj : config["rules"]) {
                    auth_rule r;
                    r.topic_filter = robj.value("topic", "#");

                    auto pub_str = robj.value("pub", "deny");
                    auto sub_str = robj.value("sub", "deny");
                    r.pub_action = (pub_str == "allow") ? auth_action::allow : auth_action::deny;
                    r.sub_action = (sub_str == "allow") ? auth_action::allow : auth_action::deny;

                    if (robj.contains("groups") && robj["groups"].is_array()) {
                        for (auto& g : robj["groups"]) {
                            r.groups.insert(g.get<std::string>());
                        }
                    }
                    rules_.push_back(std::move(r));
                }
            }

            return {};
        } catch (const json::exception& e) {
            return std::unexpected(std::string("security config parse error: ") + e.what());
        }
    }

    /// Load from JSON file
    auto load_file(const std::string& path) -> std::expected<void, std::string> {
        std::ifstream ifs(path);
        if (!ifs) return std::unexpected("cannot open: " + path);
        try {
            auto config = json::parse(ifs);
            return load_json(config);
        } catch (const json::exception& e) {
            return std::unexpected(std::string("JSON parse error: ") + e.what());
        }
    }

private:
    [[nodiscard]] auto get_user_groups(const std::string& username) const
        -> std::vector<std::string>
    {
        auto it = users_.find(username);
        if (it != users_.end()) return it->second.groups;
        return {};
    }

    /// Check rule topic filter matches target
    static auto matches_topic(std::string_view rule_filter,
                               std::string_view target) -> bool
    {
        return topic_matches(rule_filter, target);
    }

    /// Check if rule groups match user groups
    static auto matches_groups(const std::set<std::string>& rule_groups,
                                const std::vector<std::string>& user_groups) -> bool
    {
        if (rule_groups.empty()) return true; // Empty means all users
        for (auto& ug : user_groups) {
            if (rule_groups.count(ug)) return true;
        }
        return false;
    }

    std::map<std::string, user_entry> users_;
    std::vector<auth_rule>            rules_;
    std::string                       anonymous_user_;
    bool                              allow_anonymous_ = false;
    sha256_hash_fn                    sha256_hasher_;   // Optional SHA256 hash callback
};

} // namespace cnetmod::mqtt
