/**
 * @brief Application-managed asynchronous file operations.
 */
export module cnetmod.application.async_file_template;

import std;
import cnetmod.core.buffer;
import cnetmod.core.file;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::application {

/**
 * @brief Provides file operations bound to the host event loop.
 *
 * The template does not own its event loop. It is created by
 * application_runtime and remains valid for the host lifetime. Callers never
 * receive the underlying io_context.
 */
export class async_file_template
{
public:
    /**
     * @brief Binds file operations to an existing event loop.
     *
     * Application code normally obtains this object from
     * application_runtime::files(). Infrastructure adapters may construct one
     * when they already own an io_context with a longer lifetime.
     */
    explicit async_file_template(io_context& io) noexcept;

    async_file_template(const async_file_template&) = delete;
    auto operator=(const async_file_template&) -> async_file_template& = delete;

    /**
     * @brief Opens a file.
     */
    [[nodiscard]] auto open(std::filesystem::path path, open_mode mode)
        -> task<std::expected<file, std::error_code>>;

    /**
     * @brief Opens a file with operation-scoped cancellation.
     */
    [[nodiscard]] auto open(std::filesystem::path path, open_mode mode,
        cancel_token& cancellation)
        -> task<std::expected<file, std::error_code>>;

    /**
     * @brief Reads bytes from an open file.
     *
     * The file and destination buffer must remain valid until completion.
     */
    [[nodiscard]] auto read(file& source, mutable_buffer destination,
        std::uint64_t offset = 0)
        -> task<std::expected<std::size_t, std::error_code>>;

    /**
     * @brief Reads bytes with operation-scoped cancellation.
     */
    [[nodiscard]] auto read(file& source, mutable_buffer destination,
        std::uint64_t offset, cancel_token& cancellation)
        -> task<std::expected<std::size_t, std::error_code>>;

    /**
     * @brief Writes bytes to an open file.
     *
     * The file and source buffer must remain valid until completion.
     */
    [[nodiscard]] auto write(file& destination, const_buffer source,
        std::uint64_t offset = 0)
        -> task<std::expected<std::size_t, std::error_code>>;

    /**
     * @brief Writes bytes with operation-scoped cancellation.
     */
    [[nodiscard]] auto write(file& destination, const_buffer source,
        std::uint64_t offset, cancel_token& cancellation)
        -> task<std::expected<std::size_t, std::error_code>>;

    /**
     * @brief Closes a file asynchronously.
     */
    [[nodiscard]] auto close(file& target)
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Flushes buffered file data to durable storage.
     */
    [[nodiscard]] auto flush(file& target)
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Returns metadata for a path.
     */
    [[nodiscard]] auto stat(std::filesystem::path path)
        -> task<std::expected<file_stat, std::error_code>>;

    /**
     * @brief Reads an entire file into owned memory.
     */
    [[nodiscard]] auto read_all(std::filesystem::path path)
        -> task<std::expected<std::string, std::error_code>>;

    /**
     * @brief Reads an entire file with operation-scoped cancellation.
     */
    [[nodiscard]] auto read_all(std::filesystem::path path,
        cancel_token& cancellation)
        -> task<std::expected<std::string, std::error_code>>;

    /**
     * @brief Creates or replaces a file from owned content.
     */
    [[nodiscard]] auto write_all(std::filesystem::path path,
        std::string content)
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Creates or replaces a file with operation-scoped cancellation.
     */
    [[nodiscard]] auto write_all(std::filesystem::path path,
        std::string content, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Removes a file; a missing path is treated as success.
     */
    [[nodiscard]] auto remove(std::filesystem::path path)
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Removes a file with best-effort operation cancellation.
     */
    [[nodiscard]] auto remove(std::filesystem::path path,
        cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;

private:
    io_context& io_;
};

} // namespace cnetmod::application
