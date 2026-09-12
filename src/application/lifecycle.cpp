module cnetmod.application.lifecycle;

import std;

namespace cnetmod::application {

void application_lifecycle::on_start(lifecycle_action action)
{
    if (!action)
        throw std::invalid_argument("startup action cannot be empty");
    startup_.push_back(std::move(action));
}

void application_lifecycle::on_stop(lifecycle_action action)
{
    if (!action)
        throw std::invalid_argument("shutdown action cannot be empty");
    shutdown_.push_back(std::move(action));
}

auto application_lifecycle::start()
    -> task<std::expected<void, std::error_code>>
{
    for (auto& action : startup_)
    {
        try
        {
            auto result = co_await action();
            if (!result)
                co_return std::unexpected(result.error());
        }
        catch (...)
        {
            co_return std::unexpected(
                std::make_error_code(std::errc::io_error));
        }
    }
    co_return {};
}

auto application_lifecycle::stop()
    -> task<std::expected<void, std::error_code>>
{
    std::optional<std::error_code> first_error;
    for (auto action = shutdown_.rbegin(); action != shutdown_.rend(); ++action)
    {
        try
        {
            auto result = co_await (*action)();
            if (!result && !first_error)
                first_error = result.error();
        }
        catch (...)
        {
            if (!first_error)
                first_error = std::make_error_code(std::errc::io_error);
        }
    }
    if (first_error)
        co_return std::unexpected(*first_error);
    co_return {};
}

} // namespace cnetmod::application
