module;
#include <ctime>

module cnetmod.protocol.http;

import std;

namespace cnetmod::http {

// =============================================================================
// HTTP Date Format Helper Functions
// =============================================================================

namespace {

// Parse HTTP date format (RFC 7231)
// Supported format: Sun, 06 Nov 1994 08:49:37 GMT
auto parse_http_date(std::string_view date_str) 
    -> std::optional<std::chrono::system_clock::time_point> {
    
    // Month name mapping
    static const std::array<std::string_view, 12> months = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    
    // Trim leading/trailing spaces
    while (!date_str.empty() && date_str.front() == ' ') {
        date_str.remove_prefix(1);
    }
    while (!date_str.empty() && date_str.back() == ' ') {
        date_str.remove_suffix(1);
    }
    
    if (date_str.empty()) return std::nullopt;
    
    std::tm tm{};
    
    // Skip weekday (e.g., "Sun, ")
    auto comma_pos = date_str.find(',');
    if (comma_pos != std::string_view::npos) {
        date_str = date_str.substr(comma_pos + 1);
        while (!date_str.empty() && date_str.front() == ' ') {
            date_str.remove_prefix(1);
        }
    }
    
    // Parse date: 06 Nov 1994 08:49:37 GMT
    // or: 06-Nov-1994 08:49:37 GMT
    
    // Extract day
    auto space1 = date_str.find(' ');
    if (space1 == std::string_view::npos) return std::nullopt;
    
    auto day_str = date_str.substr(0, space1);
    date_str = date_str.substr(space1 + 1);
    
    // Parse day
    int day = 0;
    auto [ptr, ec] = std::from_chars(day_str.data(), 
        day_str.data() + day_str.size(), day);
    if (ec != std::errc{}) return std::nullopt;
    tm.tm_mday = day;
    
    // Skip possible separator
    while (!date_str.empty() && (date_str.front() == ' ' || date_str.front() == '-')) {
        date_str.remove_prefix(1);
    }
    
    // Extract month
    auto space2 = date_str.find(' ');
    if (space2 == std::string_view::npos && date_str.find('-') != std::string_view::npos) {
        space2 = date_str.find('-');
    }
    if (space2 == std::string_view::npos) return std::nullopt;
    
    auto month_str = date_str.substr(0, space2);
    date_str = date_str.substr(space2 + 1);
    
    // Find month
    bool found_month = false;
    for (std::size_t i = 0; i < months.size(); ++i) {
        if (month_str.size() >= 3 && 
            std::equal(months[i].begin(), months[i].end(), 
                      month_str.begin(), month_str.begin() + 3,
                      [](char a, char b) { 
                          return std::tolower(a) == std::tolower(b); 
                      })) {
            tm.tm_mon = static_cast<int>(i);
            found_month = true;
            break;
        }
    }
    if (!found_month) return std::nullopt;
    
    // Skip separator
    while (!date_str.empty() && (date_str.front() == ' ' || date_str.front() == '-')) {
        date_str.remove_prefix(1);
    }
    
    // Extract year
    auto space3 = date_str.find(' ');
    if (space3 == std::string_view::npos) return std::nullopt;
    
    auto year_str = date_str.substr(0, space3);
    date_str = date_str.substr(space3 + 1);
    
    int year = 0;
    auto [ptr2, ec2] = std::from_chars(year_str.data(),
        year_str.data() + year_str.size(), year);
    if (ec2 != std::errc{}) return std::nullopt;
    
    // Handle two-digit year
    if (year < 100) {
        year += (year < 70) ? 2000 : 1900;
    }
    tm.tm_year = year - 1900;
    
    // Parse time: 08:49:37
    while (!date_str.empty() && date_str.front() == ' ') {
        date_str.remove_prefix(1);
    }
    
    auto colon1 = date_str.find(':');
    if (colon1 == std::string_view::npos) return std::nullopt;
    
    auto hour_str = date_str.substr(0, colon1);
    date_str = date_str.substr(colon1 + 1);
    
    int hour = 0;
    auto [ptr3, ec3] = std::from_chars(hour_str.data(),
        hour_str.data() + hour_str.size(), hour);
    if (ec3 != std::errc{}) return std::nullopt;
    tm.tm_hour = hour;
    
    auto colon2 = date_str.find(':');
    if (colon2 == std::string_view::npos) return std::nullopt;
    
    auto min_str = date_str.substr(0, colon2);
    date_str = date_str.substr(colon2 + 1);
    
    int min = 0;
    auto [ptr4, ec4] = std::from_chars(min_str.data(),
        min_str.data() + min_str.size(), min);
    if (ec4 != std::errc{}) return std::nullopt;
    tm.tm_min = min;
    
    auto space4 = date_str.find(' ');
    auto sec_str = (space4 != std::string_view::npos) 
        ? date_str.substr(0, space4) : date_str;
    
    int sec = 0;
    auto [ptr5, ec5] = std::from_chars(sec_str.data(),
        sec_str.data() + sec_str.size(), sec);
    if (ec5 != std::errc{}) return std::nullopt;
    tm.tm_sec = sec;
    
    // Convert to time_point (assume GMT/UTC)
#ifdef _WIN32
    auto time = _mkgmtime(&tm);
#else
    auto time = timegm(&tm);
#endif
    
    if (time == -1) return std::nullopt;
    
    return std::chrono::system_clock::from_time_t(time);
}

// Format to HTTP date format (RFC 7231)
// Format: Sun, 06 Nov 1994 08:49:37 GMT
auto format_http_date(std::chrono::system_clock::time_point tp) -> std::string {
    auto time = std::chrono::system_clock::to_time_t(tp);
    
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    
    static const std::array<std::string_view, 7> weekdays = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    
    static const std::array<std::string_view, 12> months = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    
    return std::format("{}, {:02d} {} {:04d} {:02d}:{:02d}:{:02d} GMT",
        weekdays[tm.tm_wday],
        tm.tm_mday,
        months[tm.tm_mon],
        tm.tm_year + 1900,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec);
}

} // anonymous namespace

// =============================================================================
// Cookie Parsing and Serialization Implementation
// =============================================================================

auto cookie::parse(std::string_view header) -> std::optional<cookie> {
    cookie c;
    
    // Parse name=value; attr1=val1; attr2=val2
    auto semicolon = header.find(';');
    auto name_value = header.substr(0, semicolon);
    
    // Parse name=value
    auto eq = name_value.find('=');
    if (eq == std::string_view::npos) {
        return std::nullopt;
    }
    
    c.name = std::string(name_value.substr(0, eq));
    c.value = std::string(name_value.substr(eq + 1));
    
    // Trim leading/trailing spaces
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
    };
    trim(c.name);
    trim(c.value);
    
    // Parse attributes
    if (semicolon != std::string_view::npos) {
        auto attrs = header.substr(semicolon + 1);
        
        while (!attrs.empty()) {
            // Skip spaces
            while (!attrs.empty() && (attrs[0] == ' ' || attrs[0] == '\t')) {
                attrs = attrs.substr(1);
            }
            
            if (attrs.empty()) break;
            
            // Find next semicolon
            auto next_semi = attrs.find(';');
            auto attr = attrs.substr(0, next_semi);
            
            // Parse attribute
            auto attr_eq = attr.find('=');
            std::string attr_name;
            std::string attr_value;
            
            if (attr_eq != std::string_view::npos) {
                attr_name = std::string(attr.substr(0, attr_eq));
                attr_value = std::string(attr.substr(attr_eq + 1));
            } else {
                attr_name = std::string(attr);
            }
            
            trim(attr_name);
            trim(attr_value);
            
            // Convert to lowercase for comparison
            std::string attr_lower = attr_name;
            std::ranges::transform(attr_lower, attr_lower.begin(),
                [](unsigned char ch) { return std::tolower(ch); });
            
            if (attr_lower == "domain") {
                c.domain = attr_value;
            } else if (attr_lower == "path") {
                c.path = attr_value;
            } else if (attr_lower == "expires") {
                // Parse HTTP date format (RFC 7231)
                // Supported format: Sun, 06 Nov 1994 08:49:37 GMT
                c.expires = parse_http_date(attr_value);
            } else if (attr_lower == "max-age") {
                try {
                    auto seconds = std::stoll(attr_value);
                    c.max_age = std::chrono::seconds(seconds);
                    c.expires = std::chrono::system_clock::now() + *c.max_age;
                } catch (...) {
                    // Ignore parse errors
                }
            } else if (attr_lower == "secure") {
                c.secure = true;
            } else if (attr_lower == "httponly") {
                c.http_only = true;
            } else if (attr_lower == "samesite") {
                std::string value_lower = attr_value;
                std::ranges::transform(value_lower, value_lower.begin(),
                    [](unsigned char ch) { return std::tolower(ch); });
                
                if (value_lower == "strict") {
                    c.same_site = same_site_policy::strict;
                } else if (value_lower == "lax") {
                    c.same_site = same_site_policy::lax;
                } else if (value_lower == "none") {
                    c.same_site = same_site_policy::none;
                }
            }
            
            // Move to next attribute
            if (next_semi == std::string_view::npos) {
                break;
            }
            attrs = attrs.substr(next_semi + 1);
        }
    }
    
    return c;
}

auto cookie::to_set_cookie_header() const -> std::string {
    std::string result = name + "=" + value;
    
    if (!domain.empty()) {
        result += "; Domain=" + domain;
    }
    
    if (!path.empty()) {
        result += "; Path=" + path;
    }
    
    if (max_age) {
        result += "; Max-Age=" + std::to_string(max_age->count());
    } else if (expires) {
        // Format to HTTP date format (RFC 7231)
        result += "; Expires=" + format_http_date(*expires);
    }
    
    if (secure) {
        result += "; Secure";
    }
    
    if (http_only) {
        result += "; HttpOnly";
    }
    
    if (same_site) {
        result += "; SameSite=";
        switch (*same_site) {
        case same_site_policy::strict:
            result += "Strict";
            break;
        case same_site_policy::lax:
            result += "Lax";
            break;
        case same_site_policy::none:
            result += "None";
            break;
        }
    }
    
    return result;
}

auto cookie::matches_domain(std::string_view host) const -> bool {
    if (domain.empty()) {
        return true;  // No domain specified, match all
    }
    
    // Domain matching rules:
    // 1. Exact match
    if (host == domain) {
        return true;
    }
    
    // 2. Subdomain match (if domain starts with .)
    if (domain.starts_with('.')) {
        if (host.ends_with(domain)) {
            return true;
        }
        // Remove leading dot and match again
        auto domain_without_dot = domain.substr(1);
        if (host == domain_without_dot || host.ends_with("." + std::string(domain_without_dot))) {
            return true;
        }
    } else {
        // domain doesn't start with ., check if it's a subdomain
        if (host.ends_with("." + domain)) {
            return true;
        }
    }
    
    return false;
}

auto cookie::matches_path(std::string_view request_path) const -> bool {
    if (path.empty() || path == "/") {
        return true;  // Default path matches all
    }
    
    // Path matching rules:
    // 1. Exact match
    if (request_path == path) {
        return true;
    }
    
    // 2. Prefix match (cookie path is prefix of request path)
    if (request_path.starts_with(path)) {
        // Ensure complete path segment match
        if (path.ends_with('/') || 
            request_path.size() == path.size() ||
            request_path[path.size()] == '/') {
            return true;
        }
    }
    
    return false;
}

} // namespace cnetmod::http
