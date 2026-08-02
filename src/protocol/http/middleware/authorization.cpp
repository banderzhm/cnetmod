module cnetmod.protocol.http.middleware.authorization;

import std;

namespace cnetmod::http {
namespace {

    auto matches_any(const std::vector<std::string>& granted,
        const std::vector<std::string>& required) noexcept -> bool
    {
        return std::ranges::any_of(required, [&granted](const std::string& wanted)
            {
                return std::ranges::any_of(granted, [&wanted](const std::string& candidate)
                    {
                        return permission_matches(candidate, wanted);
                    });
            });
    }

    auto matches_all(const std::vector<std::string>& granted,
        const std::vector<std::string>& required) noexcept -> bool
    {
        return std::ranges::all_of(required, [&granted](const std::string& wanted)
            {
                return std::ranges::any_of(granted, [&wanted](const std::string& candidate)
                    {
                        return permission_matches(candidate, wanted);
                    });
            });
    }

} // namespace

auto permission_matches(const std::string_view granted,
    const std::string_view required) noexcept -> bool
{
    if (granted == required)
    {
        return true;
    }

    auto granted_cursor = std::size_t{};
    auto required_cursor = std::size_t{};
    while (true)
    {
        const auto granted_end = granted.find(':', granted_cursor);
        const auto required_end = required.find(':', required_cursor);
        const auto granted_segment = granted.substr(
            granted_cursor, granted_end == std::string_view::npos ? std::string_view::npos : granted_end - granted_cursor);
        const auto required_segment = required.substr(
            required_cursor, required_end == std::string_view::npos ? std::string_view::npos : required_end - required_cursor);
        if (granted_segment != "*" && granted_segment != required_segment)
        {
            return false;
        }
        if (granted_end == std::string_view::npos ||
            required_end == std::string_view::npos)
        {
            return granted_end == required_end;
        }
        granted_cursor = granted_end + 1;
        required_cursor = required_end + 1;
    }
}

auto is_authorized(const authorization_principal& principal,
    const authorization_requirement& requirement) noexcept -> bool
{
    return matches_all(principal.permissions, requirement.all_of) &&
        (requirement.any_of.empty() ||
            matches_any(principal.permissions, requirement.any_of));
}

auto authorize(authorization_options options) -> middleware_fn
{
    return [options = std::move(options)](request_context& context, next_fn next) -> task<void>
    {
        if (options.skip && options.skip(context))
        {
            co_await next();
            co_return;
        }
        if (!options.authenticate || !options.requirement_for)
        {
            context.json(status::internal_server_error,
                R"({"code":"AUTHORIZATION_NOT_CONFIGURED"})");
            co_return;
        }

        auto principal = options.authenticate(context);
        if (!principal)
        {
            context.json(status::unauthorized, R"({"code":"UNAUTHENTICATED"})");
            co_return;
        }
        if (options.on_authenticated)
        {
            options.on_authenticated(context, *principal);
        }
        const auto requirement = options.requirement_for(context);
        if (requirement && !is_authorized(*principal, *requirement))
        {
            context.json(status::forbidden, R"({"code":"FORBIDDEN"})");
            co_return;
        }
        co_await next();
    };
}

} // namespace cnetmod::http
