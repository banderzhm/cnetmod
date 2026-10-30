module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :error;
import :aws_credentials;

namespace cnetmod::mongodb {
auto validate_aws_credentials(const aws_credentials& credentials) -> result<void>
{
    const auto safe = [](std::string_view value)
    {
        return value.size() <= 16384 && value.find_first_of("\r\n\0", 0, 3) == std::string_view::npos;
    };
    if (credentials.access_key_id.empty() || credentials.secret_access_key.empty() ||
        !safe(credentials.access_key_id) || !safe(credentials.secret_access_key) || !safe(credentials.session_token))
        return std::unexpected(make_error(error_code::authentication_failed, "invalid AWS credential fields"));
    if (credentials.expires_at && *credentials.expires_at <= std::chrono::system_clock::now())
        return std::unexpected(make_error(error_code::authentication_failed, "AWS credentials expired"));
    return {};
}

auto aws_environment_credentials() -> result<std::optional<aws_credentials>>
{
    const auto access = std::getenv("AWS_ACCESS_KEY_ID");
    const auto secret = std::getenv("AWS_SECRET_ACCESS_KEY");
    const auto session = std::getenv("AWS_SESSION_TOKEN");
    if ((!access || !*access) && (!secret || !*secret))
        return std::optional<aws_credentials>{};
    aws_credentials credentials{access ? access : "", secret ? secret : "", session ? session : "", {}};
    auto valid = validate_aws_credentials(credentials);
    if (!valid)
        return std::unexpected(valid.error());
    return std::optional<aws_credentials>{std::move(credentials)};
}

aws_credential_cache::aws_credential_cache(aws_credential_callback callback, bool environment_before_first_fetch)
    : callback_(std::move(callback)), environment_before_first_fetch_(environment_before_first_fetch) {}

auto aws_credential_cache::get() -> task<result<aws_credentials>>
{
    co_await mutex_.lock();
    async_lock_guard guard{mutex_, std::adopt_lock};
    if (cached_ && (!cached_->expires_at || *cached_->expires_at > std::chrono::system_clock::now() + std::chrono::minutes{1}))
        co_return *cached_;
    if (!cached_ && environment_before_first_fetch_)
    {
        auto environment = aws_environment_credentials();
        if (!environment)
            co_return std::unexpected(environment.error());
        if (*environment)
            co_return std::move(**environment); // Read dynamically; do not pin environment credentials.
    }
    if (!callback_)
        co_return std::unexpected(make_error(error_code::authentication_failed, "missing AWS credential provider"));
    auto credentials = co_await callback_();
    if (!credentials)
        co_return std::unexpected(credentials.error());
    auto valid = validate_aws_credentials(*credentials);
    if (!valid)
        co_return std::unexpected(valid.error());
    cached_ = *credentials;
    co_return credentials;
}

auto aws_credential_cache::invalidate(std::string_view key) -> task<void>
{
    co_await mutex_.lock();
    async_lock_guard guard{mutex_, std::adopt_lock};
    if (cached_ && cached_->access_key_id == key)
        cached_.reset();
}
} // namespace cnetmod::mongodb
