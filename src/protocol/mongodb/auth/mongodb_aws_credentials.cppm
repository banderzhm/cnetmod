export module cnetmod.protocol.mongodb:aws_credentials;
import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :error;

export namespace cnetmod::mongodb {
struct aws_credentials
{
    std::string access_key_id;
    std::string secret_access_key;
    std::string session_token;
    std::optional<std::chrono::system_clock::time_point> expires_at;
};

using aws_credential_callback = std::function<task<result<aws_credentials>>()>;
/// Reads environment dynamically and never prints credentials in an error.
auto aws_environment_credentials() -> result<std::optional<aws_credentials>>;
auto validate_aws_credentials(const aws_credentials& credentials) -> result<void>;

class aws_credential_cache
{
public:
    explicit aws_credential_cache(aws_credential_callback callback, bool environment_before_first_fetch = false);
    auto get() -> task<result<aws_credentials>>;
    auto invalidate(std::string_view access_key_id) -> task<void>;

private:
    aws_credential_callback callback_;
    bool environment_before_first_fetch_ = false;
    async_mutex mutex_;
    std::optional<aws_credentials> cached_;
};
} // namespace cnetmod::mongodb
