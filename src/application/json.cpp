module cnetmod.application.json;

namespace cnetmod::application {

auto parse_offloaded(application_runtime& runtime, std::string text)
    -> task<std::expected<nlohmann::json, std::error_code>>
{
    try
    {
        co_return co_await runtime.offload(
            [text = std::move(text)]() mutable
                -> std::expected<nlohmann::json, std::error_code>
            {
                try
                {
                    return nlohmann::json::parse(text);
                }
                catch (const nlohmann::json::parse_error&)
                {
                    return std::unexpected(
                        std::make_error_code(std::errc::invalid_argument));
                }
                catch (const std::bad_alloc&)
                {
                    return std::unexpected(
                        std::make_error_code(std::errc::not_enough_memory));
                }
                catch (...)
                {
                    return std::unexpected(
                        std::make_error_code(std::errc::io_error));
                }
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

auto dump_offloaded(application_runtime& runtime, nlohmann::json value,
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
                try
                {
                    return value.dump(indentation);
                }
                catch (const std::bad_alloc&)
                {
                    return std::unexpected(
                        std::make_error_code(std::errc::not_enough_memory));
                }
                catch (...)
                {
                    return std::unexpected(
                        std::make_error_code(std::errc::invalid_argument));
                }
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
