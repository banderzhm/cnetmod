export module cnetmod.protocol.redis:redis_template;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.instrumentation.tracing;
import cnetmod.json;
import :client;
import :pool;
import :request;
import :value;

export namespace cnetmod::redis {

using ttl_seconds = std::chrono::seconds;
using reply = std::vector<resp3_node>;
using pipeline_reply = std::expected<reply, std::error_code>;

/**
 * @brief Adds a stable application namespace to every logical Redis key.
 */
struct key_namespace
{
    std::string prefix;

    /**
     * @brief Returns the physical key formed from the configured prefix and suffix.
     */
    [[nodiscard]] auto key(std::string_view suffix) const -> std::string;
};

/**
 * @brief Bounds Redis template behavior independently of protocol transport settings.
 */
struct template_options
{
    key_namespace ns;
    std::chrono::steady_clock::duration default_ttl{};
    std::size_t scan_page = 128;
    std::size_t scan_limit = 100000;
    std::size_t response_byte_limit = 4U * 1024U * 1024U;
};

/**
 * @brief Glaze-only JSON codec used by typed Redis template operations.
 */
struct json_codec
{
    template <typename T>
    [[nodiscard]] static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        return cnetmod::json::parse<T>(input);
    }

    template <typename T>
    [[nodiscard]] static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
    {
        return cnetmod::json::write(value);
    }
};

/**
 * @brief Builds one ordered Redis pipeline without owning a connection.
 */
class pipeline_builder
{
public:
    pipeline_builder() = default;

    /**
     * @brief Appends an advanced command containing already-physical keys.
     *
     * Prefer the typed methods below so logical keys always receive the
     * configured namespace.
     */
    auto raw_command(std::span<const std::string> arguments)
        -> pipeline_builder&;
    auto get(std::string_view key) -> pipeline_builder&;
    auto set(std::string_view key, std::string_view value,
        ttl_seconds ttl = {}) -> pipeline_builder&;
    auto del(std::string_view key) -> pipeline_builder&;
    auto exists(std::string_view key) -> pipeline_builder&;
    auto incr(std::string_view key, std::int64_t by = 1) -> pipeline_builder&;
    auto expire(std::string_view key, ttl_seconds ttl) -> pipeline_builder&;
    auto hset(std::string_view key, std::string_view field,
        std::string_view value, ttl_seconds ttl = {}) -> pipeline_builder&;
    auto hdel(std::string_view key, std::string_view field)
        -> pipeline_builder&;
    auto sadd(std::string_view key, std::string_view member,
        ttl_seconds ttl = {}) -> pipeline_builder&;
    auto srem(std::string_view key, std::string_view member)
        -> pipeline_builder&;
    auto sismember(std::string_view key, std::string_view member)
        -> pipeline_builder&;

    /**
     * @brief Removes every queued command while retaining allocated storage.
     */
    void clear();

    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto size() const noexcept -> std::size_t;

private:
    friend class redis_template;
    key_namespace namespace_;
    ttl_seconds default_ttl_{};
    request batch_;
    std::vector<std::string> operations_;
    bool valid_ = true;

    pipeline_builder(key_namespace keys, ttl_seconds default_ttl);
    [[nodiscard]] auto physical_key(std::string_view key) const -> std::string;
    [[nodiscard]] auto effective_ttl(ttl_seconds ttl) const noexcept
        -> ttl_seconds;
    auto append(std::vector<std::string> arguments) -> pipeline_builder&;
};

/**
 * @brief High-level, cancellable Redis operations backed by a connection pool.
 *
 * The template never owns a connection or an execution domain. Each operation
 * borrows one pooled connection, uses client::exchange(), and relies on the
 * pool's is_reusable() check before the lease can be issued again.
 */
class redis_template
{
public:
    explicit redis_template(connection_pool& pool,
        template_options options = {},
        instrumentation::trace_context parent = {},
        instrumentation::span_exporter spans = {});

    /**
     * @brief Reads one value while preserving the distinction between nil and failure.
     */
    [[nodiscard]] auto get(std::string_view key)
        -> task<std::expected<std::optional<std::string>, std::error_code>>;
    /**
     * @brief Reads one value and forwards caller cancellation to pool and transport.
     */
    [[nodiscard]] auto get(std::string_view key, cancel_token& cancellation)
        -> task<std::expected<std::optional<std::string>, std::error_code>>;

    /**
     * @brief Stores one value using an explicit or configured default TTL.
     */
    [[nodiscard]] auto set(std::string_view key, std::string_view value,
        ttl_seconds ttl = {}) -> task<std::expected<void, std::error_code>>;
    /**
     * @brief Stores one value with cancellation-aware connection and wire I/O.
     */
    [[nodiscard]] auto set(std::string_view key, std::string_view value,
        ttl_seconds ttl, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Deletes one key and reports whether Redis removed it.
     */
    [[nodiscard]] auto del(std::string_view key)
        -> task<std::expected<bool, std::error_code>>;
    [[nodiscard]] auto del(std::string_view key, cancel_token& cancellation)
        -> task<std::expected<bool, std::error_code>>;

    /**
     * @brief Tests whether one logical key exists.
     */
    [[nodiscard]] auto exists(std::string_view key)
        -> task<std::expected<bool, std::error_code>>;
    [[nodiscard]] auto exists(std::string_view key, cancel_token& cancellation)
        -> task<std::expected<bool, std::error_code>>;

    /**
     * @brief Atomically increments an integer value and returns the new value.
     */
    [[nodiscard]] auto incr(std::string_view key, std::int64_t by = 1)
        -> task<std::expected<std::int64_t, std::error_code>>;
    [[nodiscard]] auto incr(std::string_view key, std::int64_t by,
        cancel_token& cancellation)
        -> task<std::expected<std::int64_t, std::error_code>>;

    /**
     * @brief Applies a key expiration and reports whether the key existed.
     */
    [[nodiscard]] auto expire(std::string_view key, ttl_seconds ttl)
        -> task<std::expected<bool, std::error_code>>;
    [[nodiscard]] auto expire(std::string_view key, ttl_seconds ttl,
        cancel_token& cancellation)
        -> task<std::expected<bool, std::error_code>>;

    /**
     * @brief Returns nullopt for a missing key and -1ms for a persistent key.
     */
    [[nodiscard]] auto pttl(std::string_view key)
        -> task<std::expected<std::optional<std::chrono::milliseconds>,
            std::error_code>>;
    [[nodiscard]] auto pttl(std::string_view key, cancel_token& cancellation)
        -> task<std::expected<std::optional<std::chrono::milliseconds>,
            std::error_code>>;

    /**
     * @brief Reads multiple values with output positions matching input positions.
     */
    [[nodiscard]] auto mget(std::span<const std::string> keys)
        -> task<std::expected<std::vector<std::optional<std::string>>,
            std::error_code>>;
    [[nodiscard]] auto mget(std::span<const std::string> keys,
        cancel_token& cancellation)
        -> task<std::expected<std::vector<std::optional<std::string>>,
            std::error_code>>;

    /**
     * @brief Stores one hash field and reports whether it was newly inserted.
     */
    [[nodiscard]] auto hset(std::string_view key, std::string_view field,
        std::string_view value, ttl_seconds ttl = {})
        -> task<std::expected<bool, std::error_code>>;
    [[nodiscard]] auto hset(std::string_view key, std::string_view field,
        std::string_view value, ttl_seconds ttl, cancel_token& cancellation)
        -> task<std::expected<bool, std::error_code>>;
    /**
     * @brief Reads one hash field while preserving Redis nil.
     */
    [[nodiscard]] auto hget(std::string_view key, std::string_view field)
        -> task<std::expected<std::optional<std::string>, std::error_code>>;
    [[nodiscard]] auto hget(std::string_view key, std::string_view field,
        cancel_token& cancellation)
        -> task<std::expected<std::optional<std::string>, std::error_code>>;
    /**
     * @brief Normalizes RESP2 arrays and RESP3 maps into ordered field-value pairs.
     */
    [[nodiscard]] auto hgetall(std::string_view key)
        -> task<std::expected<std::vector<std::pair<std::string, std::string>>,
            std::error_code>>;
    [[nodiscard]] auto hgetall(std::string_view key,
        cancel_token& cancellation)
        -> task<std::expected<std::vector<std::pair<std::string, std::string>>,
            std::error_code>>;
    /**
     * @brief Deletes one hash field and reports whether Redis removed it.
     */
    [[nodiscard]] auto hdel(std::string_view key, std::string_view field)
        -> task<std::expected<bool, std::error_code>>;
    [[nodiscard]] auto hdel(std::string_view key, std::string_view field,
        cancel_token& cancellation)
        -> task<std::expected<bool, std::error_code>>;

    /**
     * @brief Adds one set member and reports whether it was newly inserted.
     */
    [[nodiscard]] auto sadd(std::string_view key, std::string_view member,
        ttl_seconds ttl = {}) -> task<std::expected<bool, std::error_code>>;
    [[nodiscard]] auto sadd(std::string_view key, std::string_view member,
        ttl_seconds ttl, cancel_token& cancellation)
        -> task<std::expected<bool, std::error_code>>;
    /**
     * @brief Removes one set member and reports whether Redis removed it.
     */
    [[nodiscard]] auto srem(std::string_view key, std::string_view member)
        -> task<std::expected<bool, std::error_code>>;
    [[nodiscard]] auto srem(std::string_view key, std::string_view member,
        cancel_token& cancellation)
        -> task<std::expected<bool, std::error_code>>;
    /**
     * @brief Tests membership without exposing RESP numeric conversion.
     */
    [[nodiscard]] auto sismember(std::string_view key,
        std::string_view member) -> task<std::expected<bool, std::error_code>>;
    [[nodiscard]] auto sismember(std::string_view key,
        std::string_view member, cancel_token& cancellation)
        -> task<std::expected<bool, std::error_code>>;
    /**
     * @brief Scans a complete set with cursor looping, deduplication, and a hard limit.
     */
    [[nodiscard]] auto sscan_all(std::string_view key)
        -> task<std::expected<std::vector<std::string>, std::error_code>>;
    [[nodiscard]] auto sscan_all(std::string_view key,
        cancel_token& cancellation)
        -> task<std::expected<std::vector<std::string>, std::error_code>>;

    /**
     * @brief Creates an independent pipeline builder safe for concurrent callers.
     */
    [[nodiscard]] auto pipeline() const -> pipeline_builder;
    /**
     * @brief Executes one wire exchange and preserves each command's result.
     */
    [[nodiscard]] auto execute(pipeline_builder& builder)
        -> task<std::expected<std::vector<pipeline_reply>, std::error_code>>;
    [[nodiscard]] auto execute(pipeline_builder& builder,
        cancel_token& cancellation)
        -> task<std::expected<std::vector<pipeline_reply>, std::error_code>>;

    template <typename T, typename Codec = json_codec>
    [[nodiscard]] auto get_as(std::string_view key)
        -> task<std::expected<std::optional<T>, std::error_code>>
    {
        cancel_token cancellation;
        co_return co_await get_as<T, Codec>(key, cancellation);
    }

    template <typename T, typename Codec = json_codec>
    [[nodiscard]] auto get_as(std::string_view key,
        cancel_token& cancellation)
        -> task<std::expected<std::optional<T>, std::error_code>>
    {
        auto encoded = co_await get(key, cancellation);
        if (!encoded)
            co_return std::unexpected(encoded.error());
        if (!*encoded)
            co_return std::optional<T>{};
        auto decoded = Codec::template decode<T>(**encoded);
        if (!decoded)
            co_return std::unexpected(decoded.error());
        co_return std::optional<T>{std::move(*decoded)};
    }

    template <typename T, typename Codec = json_codec>
    [[nodiscard]] auto set_as(std::string_view key, const T& value,
        ttl_seconds ttl = {}) -> task<std::expected<void, std::error_code>>
    {
        cancel_token cancellation;
        co_return co_await set_as<T, Codec>(key, value, ttl, cancellation);
    }

    template <typename T, typename Codec = json_codec>
    [[nodiscard]] auto set_as(std::string_view key, const T& value,
        ttl_seconds ttl, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto encoded = Codec::template encode<T>(value);
        if (!encoded)
            co_return std::unexpected(encoded.error());
        co_return co_await set(key, *encoded, ttl, cancellation);
    }

private:
    connection_pool& pool_;
    template_options options_;
    instrumentation::trace_context parent_;
    instrumentation::span_exporter spans_;

    [[nodiscard]] auto physical_key(std::string_view key) const -> std::string;
    [[nodiscard]] auto effective_ttl(ttl_seconds ttl) const noexcept
        -> ttl_seconds;
    [[nodiscard]] auto execute_one(std::vector<std::string> arguments,
        cancel_token& cancellation)
        -> task<std::expected<reply, std::error_code>>;
};

} // namespace cnetmod::redis
