export module cnetmod.protocol.http.middleware.authorization;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

export namespace cnetmod::http {

enum class authorization_error_code : std::uint8_t
{
    unauthenticated,
    invalid_credential,
    verifier_failure,
};

struct authorization_error
{
    authorization_error_code code;
    std::string message;
};

// This is deliberately application-neutral.  The application decides how a
// principal is stored after authentication and may add its own claims there.
struct authorization_principal
{
    std::string subject;
    std::string tenant_id;
    std::vector<std::string> permissions;
};

// all_of must all match; any_of is optional and requires at least one match
// when non-empty.  Permission segments support `*`, e.g. iot:device:*.
struct authorization_requirement
{
    std::vector<std::string> all_of;
    std::vector<std::string> any_of;
};

using principal_authenticator = std::function<
    std::expected<authorization_principal, authorization_error>(request_context&)>;
using authorization_requirement_resolver = std::function<
    std::optional<authorization_requirement>(const request_context&)>;
using authenticated_principal_sink = std::function<void(
    request_context&, const authorization_principal&)>;

struct authorization_options
{
    principal_authenticator authenticate;
    authorization_requirement_resolver requirement_for;
    authenticated_principal_sink on_authenticated;
    std::function<bool(const request_context&)> skip;
};

[[nodiscard]] auto permission_matches(std::string_view granted,
    std::string_view required) noexcept -> bool;
[[nodiscard]] auto is_authorized(const authorization_principal& principal,
    const authorization_requirement& requirement) noexcept -> bool;

auto authorize(authorization_options options) -> middleware_fn;

} // namespace cnetmod::http
