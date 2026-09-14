module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.client_facade;
import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.client_options;
import cnetmod.protocol.kafka.request_header;
import cnetmod.protocol.kafka.broker_request_codec;
import cnetmod.protocol.kafka.broker_connection;
import cnetmod.protocol.kafka.broker_metadata;
import cnetmod.protocol.kafka.kafka_producer;
import cnetmod.protocol.kafka.partitioner;
import cnetmod.protocol.kafka.kafka_consumer;

export namespace cnetmod::kafka {
class client_facade
{
public:
    client_facade(io_context&, client_options);
    ~client_facade();
    client_facade(client_facade&&) noexcept;
    auto operator=(client_facade&&) noexcept -> client_facade&;
    /**
     * @brief Connects, creating a fresh runtime generation after explicit close.
     * Existing handles retain their stopped generation and are not rebound.
     */
    auto connect(cancel_token* = nullptr) -> task<result<void>>;
    auto refresh_metadata(std::vector<std::string> = {},
        cancel_token* = nullptr) -> task<result<void>>;
    [[nodiscard]] auto metadata() const -> std::shared_ptr<metadata_cache>;
    [[nodiscard]] auto api_versions() const
        -> std::span<const protocol::api_version>;
    auto make_producer(producer_options = {}, std::unique_ptr<partitioner> = {})
        -> result<producer>;
    auto make_consumer(consumer_options) -> result<consumer>;
    void add_connection_observer(std::weak_ptr<connection_observer>);
    /**
     * @brief Seals runtime factories and metadata reconnect before closing transports.
     * This synchronous operation does not join consumer maintenance or in-flight I/O.
     * Access and lifecycle transitions must remain on the owning executor.
     */
    void close() noexcept;
    /**
     * @brief Cancels and joins registered consumer maintenance before closing transports.
     * Failed cleanup retains its runtime for retry. Already running user operations
     * must be settled by their caller; cancellation is forwarded to group cleanup.
     */
    auto async_close(cancel_token* = nullptr) -> task<result<void>>;
    /**
     * @brief Reports whether consumer registrations still require a completion join.
     */
    [[nodiscard]] auto requires_async_close() const noexcept -> bool;
    /**
     * @brief Returns the first terminal consumer maintenance error without allocation.
     * Running tasks and successfully completed tasks do not report an error.
     * This does not consume failures or restart maintenance.
     */
    [[nodiscard]] auto background_error() const noexcept -> std::error_code;
    /**
     * @brief Restarts only terminally failed maintenance on still-open consumers.
     * Replacement ownership is registered before dispatch. Running tasks and
     * closed consumers are never restarted. Errors remain retryable by the owner.
     */
    [[nodiscard]] auto restart_failed_maintenance() -> std::expected<void, std::error_code>;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::kafka
