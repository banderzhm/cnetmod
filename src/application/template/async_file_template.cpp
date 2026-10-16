module cnetmod.application.async_file_template;

import cnetmod.executor.async_op;

namespace cnetmod::application {

async_file_template::async_file_template(io_context& io) noexcept : io_(io) {}

auto async_file_template::event_loop() const noexcept -> io_context&
{
    if (auto* current = io_context::current())
        return *current;
    return io_;
}

auto async_file_template::open(std::filesystem::path path, open_mode mode)
    -> task<std::expected<file, std::error_code>>
{
    co_return co_await async_file_open(event_loop(), path, mode);
}

auto async_file_template::open(std::filesystem::path path, open_mode mode,
    cancel_token& cancellation)
    -> task<std::expected<file, std::error_code>>
{
    co_return co_await async_file_open(event_loop(), path, mode, cancellation);
}

auto async_file_template::read(file& source, mutable_buffer destination,
    std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_return co_await async_file_read(event_loop(), source, destination, offset);
}

auto async_file_template::read(file& source, mutable_buffer destination,
    std::uint64_t offset, cancel_token& cancellation)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_return co_await async_file_read(
        event_loop(), source, destination, offset, cancellation);
}

auto async_file_template::write(file& destination, const_buffer source,
    std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_return co_await async_file_write(event_loop(), destination, source, offset);
}

auto async_file_template::write(file& destination, const_buffer source,
    std::uint64_t offset, cancel_token& cancellation)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_return co_await async_file_write(
        event_loop(), destination, source, offset, cancellation);
}

auto async_file_template::close(file& target)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_file_close(event_loop(), target);
}

auto async_file_template::flush(file& target)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_file_flush(event_loop(), target);
}

auto async_file_template::stat(std::filesystem::path path)
    -> task<std::expected<file_stat, std::error_code>>
{
    co_return co_await async_file_stat(event_loop(), path);
}

auto async_file_template::read_all(std::filesystem::path path)
    -> task<std::expected<std::string, std::error_code>>
{
    co_return co_await async_file_read_all(event_loop(), path);
}

auto async_file_template::read_all(std::filesystem::path path,
    cancel_token& cancellation)
    -> task<std::expected<std::string, std::error_code>>
{
    co_return co_await async_file_read_all(event_loop(), path, cancellation);
}

auto async_file_template::write_all(std::filesystem::path path, std::string content)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_file_write_all(event_loop(), path, content);
}

auto async_file_template::write_all(std::filesystem::path path, std::string content,
    cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_file_write_all(event_loop(), path, content, cancellation);
}

auto async_file_template::write_all(std::filesystem::path path, std::string content,
    file_write_durability durability)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_file_write_all(event_loop(), path, content, durability);
}

auto async_file_template::write_all(std::filesystem::path path, std::string content,
    file_write_durability durability, cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_file_write_all(event_loop(), path, content, durability,
        cancellation);
}

auto async_file_template::remove(std::filesystem::path path)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_file_remove(event_loop(), path);
}

auto async_file_template::remove(std::filesystem::path path,
    cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_file_remove(event_loop(), path, cancellation);
}

} // namespace cnetmod::application
