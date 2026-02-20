/// cnetmod.protocol.mqtt:security — MQTT 认证与授权
/// 支持明文密码/SHA256/匿名认证，基于用户组的 topic 级授权

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
// 认证方式
// =============================================================================

export enum class auth_method {
    plain_password,   // 明文密码
    sha256,           // SHA256(salt + password)
    anonymous,        // 匿名用户（无需密码）
    unauthenticated,  // 未认证（拒绝所有）
};

// =============================================================================
// 用户条目
// =============================================================================

export struct user_entry {
    auth_method method = auth_method::plain_password;
    std::string password;          // 明文密码（plain_password 模式）
    std::string digest;            // 哈希值（sha256 模式）
    std::string salt;              // 盐值（sha256 模式）
    std::vector<std::string> groups; // 所属用户组
};

// =============================================================================
// 授权规则
// =============================================================================

export enum class auth_action {
    allow,
    deny,
};

export struct auth_rule {
    std::string  topic_filter;      // 支持 +/# 通配符
    auth_action  pub_action = auth_action::deny;
    auth_action  sub_action = auth_action::deny;
    std::set<std::string> groups;   // 适用的用户组（空表示所有用户）
};

// =============================================================================
// Security Config — 安全配置
// =============================================================================

/// SHA256 哈希回调：接受原始数据，返回十六进制哈希字符串
export using sha256_hash_fn = std::function<std::string(std::string_view)>;

export class security_config {
public:
    security_config() = default;

    // ----- SHA256 哈希函数注入 -----

    /// 设置 SHA256 哈希函数（用于 auth_method::sha256 验证）
    void set_sha256_hasher(sha256_hash_fn fn) { sha256_hasher_ = std::move(fn); }

    // ----- 用户管理 -----

    /// 添加明文密码用户
    void add_user(const std::string& username, const std::string& password,
                  std::vector<std::string> groups = {})
    {
        user_entry u;
        u.method = auth_method::plain_password;
        u.password = password;
        u.groups = std::move(groups);
        users_[username] = std::move(u);
    }

    /// 添加匿名用户
    void set_anonymous_user(const std::string& username) {
        user_entry u;
        u.method = auth_method::anonymous;
        users_[username] = std::move(u);
        anonymous_user_ = username;
    }

    /// 是否允许匿名连接
    void set_allow_anonymous(bool allow) { allow_anonymous_ = allow; }

    // ----- 授权规则 -----

    /// 添加授权规则
    void add_rule(auth_rule rule) {
        rules_.push_back(std::move(rule));
    }

    /// 添加允许发布规则
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

    /// 添加允许订阅规则
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

    /// 添加允许发布和订阅规则
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

    // ----- 认证 -----

    /// 认证用户
    /// 返回认证后的用户名（可能是匿名用户），nullopt 表示认证失败
    [[nodiscard]] auto authenticate(std::string_view username,
                                     std::string_view password) const
        -> std::optional<std::string>
    {
        if (username.empty()) {
            // 匿名连接
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
                // SHA256 验证：digest == sha256(salt + password)
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

    // ----- 授权 -----

    /// 检查用户是否有权发布到指定 topic
    [[nodiscard]] auto authorize_publish(const std::string& username,
                                          std::string_view topic) const -> bool
    {
        if (rules_.empty()) return true; // 无规则时默认允许

        auto user_groups = get_user_groups(username);

        // 遍历规则，最后匹配的生效（后定义优先）
        bool allowed = false;
        for (auto& rule : rules_) {
            if (!matches_topic(rule.topic_filter, topic)) continue;
            if (!matches_groups(rule.groups, user_groups)) continue;
            allowed = (rule.pub_action == auth_action::allow);
        }
        return allowed;
    }

    /// 检查用户是否有权订阅指定 topic filter
    [[nodiscard]] auto authorize_subscribe(const std::string& username,
                                            std::string_view topic_filter_str) const -> bool
    {
        if (rules_.empty()) return true;

        auto user_groups = get_user_groups(username);

        bool allowed = false;
        for (auto& rule : rules_) {
            // 订阅授权：规则的 filter 需要覆盖订阅的 filter
            if (!matches_topic(rule.topic_filter, topic_filter_str)) continue;
            if (!matches_groups(rule.groups, user_groups)) continue;
            allowed = (rule.sub_action == auth_action::allow);
        }
        return allowed;
    }

    /// 是否启用安全（有用户或规则）
    [[nodiscard]] auto enabled() const noexcept -> bool {
        return !users_.empty() || !rules_.empty();
    }

    // ----- JSON 配置加载 -----

    /// 从 JSON 加载配置
    /// 格式:
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

    /// 从 JSON 文件加载
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

    /// 检查 rule topic filter 是否匹配 target
    static auto matches_topic(std::string_view rule_filter,
                               std::string_view target) -> bool
    {
        return topic_matches(rule_filter, target);
    }

    /// 检查 rule groups 是否匹配 user groups
    static auto matches_groups(const std::set<std::string>& rule_groups,
                                const std::vector<std::string>& user_groups) -> bool
    {
        if (rule_groups.empty()) return true; // 空表示所有用户
        for (auto& ug : user_groups) {
            if (rule_groups.count(ug)) return true;
        }
        return false;
    }

    std::map<std::string, user_entry> users_;
    std::vector<auth_rule>            rules_;
    std::string                       anonymous_user_;
    bool                              allow_anonymous_ = false;
    sha256_hash_fn                    sha256_hasher_;   // 可选 SHA256 哈希回调
};

} // namespace cnetmod::mqtt
