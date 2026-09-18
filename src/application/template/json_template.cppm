/**
 * @brief Application-managed typed JSON parsing and serialization.
 */
export module cnetmod.application.json_template;

import std;
import cnetmod.json;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.executor.pool;
import cnetmod.io.io_context;
import cnetmod.protocol.http;

export namespace cnetmod::application {

/**
 * @brief Offloads typed JSON work to the application-managed CPU pool.
 *
 * Codec policies are selected per call and default to Glaze. Completion is
 * always resumed on the application event loop.
 */
class json_template
{
public:
    /**
     * @brief Binds JSON operations to application-owned execution resources.
     */
    json_template(io_context& io, thread_pool& cpu_pool) noexcept
        : io_(io), cpu_pool_(cpu_pool) {}

    /**
     * @brief Parses an owned JSON document without blocking the event loop.
     */
    template <typename T, typename Codec = json::glaze_codec>
    requires json::codec_for<Codec, T>
    [[nodiscard]] auto parse(std::string input,
        cancel_token* cancellation = nullptr)
        -> task<std::expected<T, std::error_code>>
    {
        if (cancellation && cancellation->is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        auto result = co_await blocking_invoke(cpu_pool_, io_,
            [input = std::move(input)]
            {
                return json::parse<T, Codec>(input);
            });
        if (cancellation && cancellation->is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        co_return result;
    }

    /**
     * @brief Serializes an owned value without blocking the event loop.
     */
    template <typename T, typename Codec = json::glaze_codec>
    requires json::codec_for<Codec, T>
    [[nodiscard]] auto write(T value, cancel_token* cancellation = nullptr)
        -> task<std::expected<std::string, std::error_code>>
    {
        if (cancellation && cancellation->is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        auto result = co_await blocking_invoke(cpu_pool_, io_,
            [value = std::move(value)]
            {
                return json::write<T, Codec>(value);
            });
        if (cancellation && cancellation->is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        co_return result;
    }

    /**
     * @brief Reads and parses the complete HTTP request body.
     */
    template <typename T, typename Codec = json::glaze_codec>
    requires json::codec_for<Codec, T>
    [[nodiscard]] auto body(http::request_context& request)
        -> task<std::expected<T, std::error_code>>
    {
        const auto body = co_await request.read_full_body();
        co_return co_await parse<T, Codec>(std::string{body},
            &request.cancellation_token());
    }

    /**
     * @brief Serializes a value and writes an HTTP JSON response.
     */
    template <typename T, typename Codec = json::glaze_codec>
    requires json::codec_for<Codec, T>
    [[nodiscard]] auto respond(http::request_context& request,
        int status_code, T value) -> task<std::expected<void, std::error_code>>
    {
        auto encoded = co_await write<T, Codec>(std::move(value),
            &request.cancellation_token());
        if (!encoded)
            co_return std::unexpected(encoded.error());
        request.json(status_code, *encoded);
        co_return std::expected<void, std::error_code>{};
    }

private:
    io_context& io_;
    thread_pool& cpu_pool_;
};

} // namespace cnetmod::application
