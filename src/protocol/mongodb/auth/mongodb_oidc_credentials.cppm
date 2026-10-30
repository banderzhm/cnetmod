export module cnetmod.protocol.mongodb:oidc_credentials;
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.io.io_context;
import :error;

export namespace cnetmod::mongodb {
enum class oidc_environment
{
    none,
    azure,
    gcp,
    kubernetes,
    test
};

struct oidc_callback_parameters
{
    std::string username;
    std::chrono::milliseconds timeout{60000};
    int version = 1;
};

struct oidc_access_token
{
    std::string access_token;
    std::optional<std::chrono::seconds> expires_in;
};

struct cached_oidc_token
{
    std::string access_token;
    bool from_cache = false;
};

using oidc_token_callback = std::function<task<result<oidc_access_token>>(oidc_callback_parameters)>;

/// One cache per client/pool, owned by one event loop. Provider fetches are
/// single-flight; SASL never blocks cache readers or conditional invalidation.
class oidc_token_cache
{
public:
    oidc_token_cache(io_context& io, oidc_token_callback callback, oidc_callback_parameters parameters = {});
    oidc_token_cache(const oidc_token_cache&) = delete;
    auto operator=(const oidc_token_cache&) -> oidc_token_cache& = delete;
    auto get(std::chrono::milliseconds timeout = std::chrono::milliseconds{60000}) -> task<result<cached_oidc_token>>;
    auto peek() -> task<std::optional<std::string>>;
    auto invalidate(std::string_view token) -> task<void>;

private:
    io_context& io_;
    oidc_token_callback callback_;
    oidc_callback_parameters parameters_;

    struct fetch_waiter
    {
        cancel_token wake;
    };

    bool fetching_ = false;
    std::vector<std::weak_ptr<fetch_waiter>> waiting_;
    void release_fetch() noexcept;
    std::optional<std::string> cached_;
    std::optional<std::chrono::steady_clock::time_point> expires_;
    std::chrono::steady_clock::time_point last_callback_{};
};
} // namespace cnetmod::mongodb
