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

  enum class same_site_policy { none, lax, strict };
  std::optional<same_site_policy> same_site;

  /// Parse Set-Cookie header
  [[nodiscard]] static auto parse(std::string_view header)
      -> std::optional<cookie>;

  /// Serialize to Set-Cookie header
  [[nodiscard]] auto to_set_cookie_header() const -> std::string;

  /// Serialize to Cookie header (name=value only)
  [[nodiscard]] auto to_cookie_header() const -> std::string;

  /// Check if cookie is expired
  [[nodiscard]] auto is_expired() const -> bool;

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
  void add(const cookie &c);

  /// Add cookie from Set-Cookie header
  void add_from_header(std::string_view set_cookie_header);

  /// Get matching cookies (for request)
  [[nodiscard]] auto get_cookies(std::string_view host, std::string_view path,
                                 bool is_secure) const -> std::vector<cookie>;

  /// Generate Cookie header
  [[nodiscard]] auto to_cookie_header(std::string_view host,
                                      std::string_view path,
                                      bool is_secure) const -> std::string;

  /// Clear all cookies
  void clear();

  /// Clear expired cookies
  void clear_expired();

  /// Get all cookies
  [[nodiscard]] auto cookies() const -> const std::vector<cookie> &;

private:
  std::vector<cookie> cookies_;
};

} // namespace cnetmod::http
