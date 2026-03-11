module;

export module cnetmod.protocol.http:cookie;

import std;

namespace cnetmod::http {

// =============================================================================
// cookie — HTTP Cookie
// =============================================================================

export struct cookie {
    std::string name;
    std::string value;
    
    // Cookie attributes
    std::string domain;
    std::string path = "/";
    std::optional<std::chrono::system_clock::time_point> expires;
    std::optional<std::chrono::seconds> max_age;
    bool secure = false;
    bool http_only = false;
    
    enum class same_site_policy {
        none,
        lax,
        strict
    };
    std::optional<same_site_policy> same_site;
    
    /// Parse Set-Cookie header
    [[nodiscard]] static auto parse(std::string_view header) 
        -> std::optional<cookie>;
    
    /// Serialize to Set-Cookie header
    [[nodiscard]] auto to_set_cookie_header() const -> std::string;
    
    /// Serialize to Cookie header (name=value only)
    [[nodiscard]] auto to_cookie_header() const -> std::string {
        return name + "=" + value;
    }
    
    /// Check if cookie is expired
    [[nodiscard]] auto is_expired() const -> bool {
        if (!expires) return false;
        return std::chrono::system_clock::now() > *expires;
    }
    
    /// Check if cookie matches domain
    [[nodiscard]] auto matches_domain(std::string_view host) const -> bool;
    
    /// Check if cookie matches path
    [[nodiscard]] auto matches_path(std::string_view request_path) const -> bool;
};

// =============================================================================
// cookie_jar — Cookie Storage and Management
// =============================================================================

export class cookie_jar {
public:
    /// Add cookie (from Set-Cookie header)
    void add(const cookie& c) {
        // Remove existing cookie with same name
        std::erase_if(cookies_, [&](const cookie& existing) {
            return existing.name == c.name && 
                   existing.domain == c.domain && 
                   existing.path == c.path;
        });
        
        // Add new cookie
        if (!c.is_expired()) {
            cookies_.push_back(c);
        }
    }
    
    /// Add cookie from Set-Cookie header
    void add_from_header(std::string_view set_cookie_header) {
        if (auto c = cookie::parse(set_cookie_header)) {
            add(*c);
        }
    }
    
    /// Get matching cookies (for request)
    [[nodiscard]] auto get_cookies(std::string_view host, 
                                   std::string_view path,
                                   bool is_secure) const 
        -> std::vector<cookie> {
        std::vector<cookie> result;
        
        for (const auto& c : cookies_) {
            if (c.is_expired()) continue;
            if (c.secure && !is_secure) continue;
            if (!c.matches_domain(host)) continue;
            if (!c.matches_path(path)) continue;
            
            result.push_back(c);
        }
        
        return result;
    }
    
    /// Generate Cookie header
    [[nodiscard]] auto to_cookie_header(std::string_view host,
                                       std::string_view path,
                                       bool is_secure) const 
        -> std::string {
        auto cookies = get_cookies(host, path, is_secure);
        if (cookies.empty()) return {};
        
        std::string result;
        for (std::size_t i = 0; i < cookies.size(); ++i) {
            if (i > 0) result += "; ";
            result += cookies[i].to_cookie_header();
        }
        return result;
    }
    
    /// Clear all cookies
    void clear() {
        cookies_.clear();
    }
    
    /// Clear expired cookies
    void clear_expired() {
        std::erase_if(cookies_, [](const cookie& c) {
            return c.is_expired();
        });
    }
    
    /// Get all cookies
    [[nodiscard]] auto cookies() const -> const std::vector<cookie>& {
        return cookies_;
    }

private:
    std::vector<cookie> cookies_;
};

} // namespace cnetmod::http