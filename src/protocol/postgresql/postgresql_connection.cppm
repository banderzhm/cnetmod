module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.postgresql:connection;

import std;
import :connection_options;
import :query_result;
import :wire_protocol;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

export namespace cnetmod::postgresql {

class client
{
public:
    using copy_data_source = std::function<task<std::optional<std::vector<std::uint8_t>>>()>;
    using copy_data_sink = std::function<task<void>(std::span<const std::uint8_t>)>;
    explicit client(io_context&);
    ~client();
    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;

    auto connect(connection_options options = {}) -> task<result_set>;
    /**
     * @brief Connects and authenticates using one cancellation token.
     * The token must outlive the operation, including connection retries.
     */
    auto connect(connection_options options, cancel_token& cancellation) -> task<result_set>;
    /**
     * @brief Reconnects with the saved options after acquiring operation ownership.
     *
     * An overlapping operation is rejected without closing its transport.
     * Calls must remain on the client's owning executor.
     */
    auto reconnect() -> task<result_set>;
    auto query(std::string_view sql) -> task<result_set>;
    /**
     * @brief Executes a query with cancellable transport reads and writes.
     *
     * Cancellation during I/O discards the session; reconnect before reuse.
     * The token and SQL storage must outlive the awaited operation. Calls
     * remain serialized on the owning executor. This is transport cancellation,
     * not PostgreSQL's separate best-effort CancelRequest protocol.
     */
    auto query(std::string_view sql, cancel_token& cancellation) -> task<result_set>;
    auto execute(std::string_view sql) -> task<result_set>;
    auto execute(parameterized_query parameters) -> task<result_set>;
    auto prepare(std::string_view sql, std::string name = {})
        -> task<std::expected<prepared_statement, std::string>>;
    auto execute(const prepared_statement&, std::span<const param_value> = {})
        -> task<result_set>;
    auto close_statement(const prepared_statement&) -> task<result_set>;
    /// Streams COPY FROM STDIN without buffering the complete import.
    auto copy_from(std::string_view copy_sql, copy_data_source source)
        -> task<result_set>;
    /// Streams COPY TO STDOUT chunks to the supplied coroutine callback.
    auto copy_to(std::string_view copy_sql, copy_data_sink sink)
        -> task<result_set>;
    /// Uses a named PostgreSQL portal with Execute(max_rows) and Flush. At
    /// most batch_size rows are retained; the callback is awaited before the
    /// next Execute, providing wire-level backpressure. Callback failure
    /// closes the portal and synchronizes the connection before rethrowing.
    auto query_batches(std::string_view sql, std::size_t batch_size,
        std::function<task<void>(std::span<const row>)> consume)
        -> task<result_set>;
    /// Request cancellation through PostgreSQL's dedicated CancelRequest
    /// connection. The server may finish the operation before it receives it.
    auto cancel_current_operation() -> task<result_set>;
    auto terminate() -> task<void>;
    /**
     * @brief Terminates the session with cancellable transport shutdown.
     * Precancellation leaves an established session intact for a later retry.
     * Once shutdown starts, transport failure disconnects and preserves its code.
     */
    auto terminate(cancel_token& cancellation) -> task<std::expected<void, std::error_code>>;

    template <typename Function>
    requires std::invocable<Function> && requires(Function function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function&& function) -> task<result_set>
    {
        auto started = co_await execute("BEGIN");
        if (started.is_err())
            co_return started;
        std::string transaction_error;
        try
        {
            co_await function();
        }
        catch (const std::exception& error)
        {
            transaction_error = error.what();
        }
        catch (...)
        {
            transaction_error = "transaction failed";
        }
        if (transaction_error.empty())
            co_return co_await execute("COMMIT");
        (void)co_await execute("ROLLBACK");
        result_set result;
        result.error_msg = std::move(transaction_error);
        co_return result;
    }

    template <typename Function>
    auto transaction(Function&& function, isolation_level level) -> task<result_set>
    {
        std::string_view isolation = "READ COMMITTED";
        if (level == isolation_level::read_uncommitted)
            isolation = "READ UNCOMMITTED";
        else if (level == isolation_level::repeatable_read)
            isolation = "REPEATABLE READ";
        else if (level == isolation_level::serializable)
            isolation = "SERIALIZABLE";
        auto started = co_await execute(std::format("BEGIN ISOLATION LEVEL {}", isolation));
        if (started.is_err())
            co_return started;
        std::exception_ptr transaction_error;
        try
        {
            co_await function();
        }
        catch (...)
        {
            transaction_error = std::current_exception();
        }
        if (!transaction_error)
            co_return co_await execute("COMMIT");
        (void)co_await execute("ROLLBACK");
        std::rethrow_exception(transaction_error);
    }

    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto secure_channel() const noexcept -> bool;
    [[nodiscard]] auto last_error() const noexcept -> std::error_code;
    [[nodiscard]] auto current_format_opts() const noexcept -> const format_options&;
    [[nodiscard]] auto server_parameters() const noexcept
        -> const std::unordered_map<std::string, std::string>&;
    [[nodiscard]] auto backend_process_id() const noexcept -> std::uint32_t;
    [[nodiscard]] auto backend_secret_key() const noexcept -> std::uint32_t;
    /// ORM dialect hook: retrieves generated identity values using RETURNING.
    void append_insert_returning(std::string& sql, std::string_view column) const;

private:
    struct streaming_portal_state;
    enum class streaming_portal_action : std::uint8_t;
    template <typename Cancellation = std::nullptr_t>
    auto read_exact(std::uint8_t*, std::size_t, Cancellation cancellation = nullptr) -> task<bool>;
    template <typename Cancellation = std::nullptr_t>
    auto read_message(Cancellation cancellation = nullptr) -> task<std::expected<detail::backend_message, std::string>>;
    template <typename Cancellation = std::nullptr_t>
    auto write_all(std::span<const std::uint8_t>, Cancellation cancellation = nullptr) -> task<bool>;
    template <typename Cancellation>
    auto connect_impl(connection_options, Cancellation cancellation) -> task<result_set>;
    template <typename Cancellation>
    auto authenticate(Cancellation cancellation) -> task<result_set>;
    template <typename Cancellation = std::nullptr_t>
    auto collect_results(Cancellation cancellation = nullptr) -> task<result_set>;
    auto drain_until_ready() -> task<bool>;
    auto abort_streaming_portal(std::string_view portal) -> task<void>;
    auto deliver_batch(std::vector<row>& batch,
        const std::function<task<void>(std::span<const row>)>& consume)
        -> task<std::exception_ptr>;
    auto parse_streaming_row_description(std::span<const std::uint8_t> payload,
        result_set& result, std::vector<std::uint32_t>& oids) const -> std::optional<std::string>;
    auto parse_streaming_data_row(std::span<const std::uint8_t> payload,
        const std::vector<std::uint32_t>& oids) const -> std::expected<row, std::string>;
    auto run_streaming_portal(std::string_view portal, streaming_portal_state& state) -> task<void>;
    auto advance_streaming_portal_state(streaming_portal_state& state,
        const detail::backend_message& message) const -> streaming_portal_action;
    static void parse_streaming_command_complete(std::span<const std::uint8_t>, result_set&);
    void disconnect(std::error_code = {}) noexcept;

    io_context& context_;
    socket socket_;
    connection_options options_;
    format_options format_options_{};
    bool connected_{};
    bool secure_{};
    bool transaction_failed_{};
    std::error_code last_error_{};
    std::unordered_map<std::string, std::string> parameters_;
    std::uint32_t process_id_{};
    std::uint32_t secret_key_{};
    std::uint64_t statement_counter_{};
    std::atomic_flag operation_in_progress_{};
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_context_;
    std::unique_ptr<ssl_stream> ssl_stream_;
#endif
};

} // namespace cnetmod::postgresql
