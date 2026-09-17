module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:client;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.executor.async_op;
import cnetmod.instrumentation.tracing;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :value;
import :routing;
import :request;
import :parser;

export namespace cnetmod::redis {
struct connect_options
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password;
    std::string username;
    std::uint32_t db = 0;
    bool resp3 = true;
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file;
    std::string tls_cert_file;
    std::string tls_key_file;
    std::string tls_sni;
};

using push_callback =
    std::function<void(std::string_view channel, std::string_view message)>;

class client
{
public:
    explicit client(io_context& ctx) noexcept;
    auto connect(connect_options opts = {})
        -> task<std::expected<void, std::string>>;
    /**
     * @brief Establishes a cancellable session with typed, payload-free errors.
     *
     * TCP attempts, TLS handshake and Redis negotiation share the caller token.
     * Blocking resolver work currently finishes before cancellation is observed.
     * A failed attempt closes the uncommitted session. Requires exclusive ownership.
     */
    [[nodiscard]] auto connect(connect_options opts, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto is_open() const noexcept -> bool;
    /**
     * @brief Reports whether the transport has no buffered unread protocol data.
     *
     * Connection pools must use this stronger predicate before reissuing a
     * command connection. An open socket alone does not prove RESP alignment.
     */
    [[nodiscard]] auto is_reusable() const noexcept -> bool;
    /**
     * @brief Releases transport and resets buffered input and negotiated state.
     *
     * Requires exclusive ownership with no pending I/O. Reconnecting invokes
     * this reset before establishing a new session; push handlers are retained.
     */
    void close() noexcept;
    /**
     * @brief Probes an exclusively owned command connection with cancellable I/O.
     *
     * Requires no outstanding commands, subscriptions, or pending push messages.
     * Only an exact PONG response succeeds. Interrupted or invalid exchanges
     * close the connection so unread bytes cannot contaminate a later command.
     * The caller supplies cancellation and, if needed, a deadline wrapper.
     */
    [[nodiscard]] auto ping(cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;
    /**
     * @brief Exchanges a bounded request batch using cancellable transport I/O.
     *
     * Requires exclusive command ownership and no buffered unsolicited input.
     * Server error replies remain response nodes; transport and parser errors
     * retain their error codes. Incomplete exchanges invalidate the connection.
     * The byte budget covers all received replies, not each pipeline element.
     */
    [[nodiscard]] auto exchange(const request& batch, cancel_token& cancellation,
        std::size_t response_byte_limit = 65536)
        -> task<std::expected<std::vector<resp3_node>, std::error_code>>;
    auto exec(const request& req)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    /**
     * @brief Observes a request batch without capturing command arguments.
     */
    auto exec(const request& req, const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto cmd(std::initializer_list<std::string_view> args)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto cmd(std::span<const std::string> args)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    /**
     * @brief Observes a command using an explicit protocol-independent parent.
     *
     * An empty sink delegates directly to the original command task. Redis
     * carries no trace headers; server error replies mark the local span failed.
     * Argument storage must remain valid until the returned task completes.
     * Parent and sink snapshots are owned by the observed task; snapshot failure
     * delegates to the original command without replacing its result.
     */
    auto cmd(std::initializer_list<std::string_view> args,
        const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto cmd(std::span<const std::string> args,
        const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto cmd_follow_redirect(std::vector<std::string> args,
        std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto pipe(std::initializer_list<std::initializer_list<std::string_view>> cmds)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto pipe(std::span<const std::vector<std::string>> cmds)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    /**
     * @brief Observes a complete pipeline, including errors in individual replies.
     *
     * A disabled sink returns the original task without copying the batch.
     */
    auto pipe(std::span<const std::vector<std::string>> cmds,
        const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto pipe(std::initializer_list<std::initializer_list<std::string_view>> cmds,
        const instrumentation::trace_context& parent,
        const instrumentation::span_exporter& on_end)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto subscribe(std::initializer_list<std::string_view> channels)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto unsubscribe(std::initializer_list<std::string_view> channels)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto psubscribe(std::initializer_list<std::string_view> patterns)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto punsubscribe(std::initializer_list<std::string_view> patterns)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto sentinel_get_master_addr_by_name(std::string_view master)
        -> task<std::expected<endpoint_info, std::string>>;
    void on_push(push_callback cb);
    auto receive_push()
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    [[nodiscard]] auto is_resp3() const noexcept -> bool;
    [[nodiscard]] static auto key_slot(std::string_view key) noexcept
        -> std::uint16_t;
    [[nodiscard]] static auto parse_redirect(const std::vector<resp3_node>& nodes)
        -> std::optional<cluster_redirect>;
    [[nodiscard]] static auto
    parse_cluster_slots(const std::vector<resp3_node>& nodes)
        -> std::expected<std::vector<cluster_slot_range>, std::string>;

private:
#ifdef CNETMOD_HAS_SSL
    struct tls_configuration_failure
    {
        std::error_code code;
        std::string_view stage;
    };

    /**
     * @brief Configures TLS identity and trust without starting network I/O.
     */
    [[nodiscard]] auto configure_tls(const connect_options& options,
        bool require_default_trust) -> std::expected<void, tls_configuration_failure>;
#endif
    auto do_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;
    auto do_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;
    auto do_auth(const connect_options& opts)
        -> task<std::expected<void, std::string>>;
    auto subscription_command(std::string_view command,
        std::initializer_list<std::string_view> names)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto parse_one_response()
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto parse_children(std::size_t count)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto fill() -> task<bool>;
    void compact_buffer();
    io_context& ctx_;
    socket sock_;
    connect_options opts_;
    std::string rbuf_;
    std::size_t rpos_ = 0;
    bool resp3_mode_ = false;
    push_callback push_cb_;
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream> ssl_;
#endif
};

/**
 * @brief Cancellation-aware Redis Cluster session with serialized operations.
 *
 * One instance owns one connection per visited node. Public command and
 * pipeline operations are coroutine-serialized so RESP exchanges cannot
 * interleave on those connections.
 */
class cluster_client
{
public:
    /**
     * @brief Creates a cluster client bound to an I/O context.
     */
    explicit cluster_client(io_context& ctx) noexcept;

    /**
     * @brief Connects to a cluster seed using a compatibility error result.
     */
    auto connect(connect_options seed) -> task<std::expected<void, std::string>>;
    /**
     * @brief Connects to a cluster seed with cancellation-aware negotiation.
     *
     * Redis Cluster supports database zero only. A non-zero database is
     * rejected before network I/O.
     */
    [[nodiscard]] auto connect(connect_options seed, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;
    /**
     * @brief Refreshes the slot cache using a compatibility error result.
     */
    auto refresh_slots() -> task<std::expected<void, std::string>>;

    /**
     * @brief Refreshes the complete slot cache with cancellation support.
     */
    [[nodiscard]] auto refresh_slots(cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Routes one command by key using a compatibility error result.
     */
    auto cmd_for_key(std::vector<std::string> args, std::string_view key,
        std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /**
     * @brief Routes one command by key and follows bounded redirections.
     */
    [[nodiscard]] auto cmd_for_key(std::vector<std::string> args,
        std::string_view key, cancel_token& cancellation,
        std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::error_code>>;
    /**
     * @brief Executes a multi-key command only when all keys share one slot.
     */
    [[nodiscard]] auto cmd_for_keys(std::vector<std::string> args,
        std::span<const std::string_view> keys, cancel_token& cancellation,
        std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::error_code>>;
    /**
     * @brief Executes an ordered pipeline using a compatibility result shape.
     */
    auto pipeline(std::span<const cluster_pipeline_item> items)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    /**
     * @brief Executes node-local batches and restores the caller's item order.
     *
     * Each outer element is exactly one command response, including its RESP
     * aggregate descendants. Transport cancellation invalidates the affected
     * node connection before it can be reused.
     */
    [[nodiscard]] auto pipeline_ordered(
        std::span<const cluster_pipeline_item> items,
        cancel_token& cancellation)
        -> task<std::expected<std::vector<std::vector<resp3_node>>,
            std::error_code>>;
    /** @brief Closes the seed and every cached node connection. */
    void close() noexcept;
    /**
     * @brief Returns the current immutable view of the local slot cache.
     */
    [[nodiscard]] auto slots() const noexcept -> const cluster_slot_cache&;

private:
    static auto endpoint_key(const endpoint_info& ep) -> std::string;
    auto connection_for(const endpoint_info& ep) -> task<client*>;
    auto connection_for(const endpoint_info& ep, cancel_token& cancellation)
        -> task<std::expected<client*, std::error_code>>;
    io_context& ctx_;
    connect_options seed_options_;
    client seed_;
    cluster_slot_cache slot_cache_;
    std::map<std::string, std::unique_ptr<client>> nodes_;
    async_mutex operation_mutex_;
};
} // namespace cnetmod::redis
