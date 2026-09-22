module cnetmod.application.json;

namespace cnetmod::application {

auto parse_offloaded(application_runtime& runtime, std::string text)
    -> task<std::expected<cnetmod::json::document, std::error_code>>
{
    try
    {
        co_return co_await runtime.offload(
            [text = std::move(text)]() mutable
                -> std::expected<cnetmod::json::document, std::error_code>
            {
                return cnetmod::json::parse_document(text);
            });
    }
    catch (const std::system_error& error)
    {
        co_return std::unexpected(error.code());
    }
    catch (const std::bad_alloc&)
    {
        co_return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
    catch (...)
    {
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    }
}

auto dump_offloaded(application_runtime& runtime, cnetmod::json::document value,
    int indentation)
    -> task<std::expected<std::string, std::error_code>>
{
    if (indentation < -1)
        co_return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        co_return co_await runtime.offload(
            [value = std::move(value), indentation]() mutable
                -> std::expected<std::string, std::error_code>
            {
                return cnetmod::json::write_document(value, indentation >= 0);
            });
    }
    catch (const std::system_error& error)
    {
        co_return std::unexpected(error.code());
    }
    catch (const std::bad_alloc&)
    {
        co_return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
    catch (...)
    {
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    }
}

} // namespace cnetmod::application
