export module cnetmod.application.orm_repository;

import std;
import cnetmod.coro.task;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.model_metadata;
import cnetmod.orm.repository;
import cnetmod.orm.repository_impl;

namespace cnetmod::application {

/**
 * @brief Owns a gateway-backed repository without exposing database details.
 */
export template <orm::Model T, typename Gateway,
    typename StreamStrategy = orm::session_stream_strategy>
class orm_repository_handle
{
public:
    orm_repository_handle(std::shared_ptr<Gateway> gateway,
        orm::automatic_interceptor_options interceptors = {})
        : gateway_(std::move(gateway)),
          repository_(std::make_unique<orm::repository<T, Gateway,
              StreamStrategy>>(*gateway_, interceptors))
    {
    }

    [[nodiscard]] auto get() noexcept
        -> orm::repository<T, Gateway, StreamStrategy>&
    {
        return *repository_;
    }

    [[nodiscard]] auto get() const noexcept
        -> const orm::repository<T, Gateway, StreamStrategy>&
    {
        return *repository_;
    }

    [[nodiscard]] auto operator->() noexcept
        -> orm::repository<T, Gateway, StreamStrategy>*
    {
        return repository_.get();
    }

    [[nodiscard]] auto operator->() const noexcept
        -> const orm::repository<T, Gateway, StreamStrategy>*
    {
        return repository_.get();
    }

private:
    std::shared_ptr<Gateway> gateway_;
    std::unique_ptr<orm::repository<T, Gateway, StreamStrategy>> repository_;
};

/**
 * @brief Creates a repository handle around an already-bound gateway.
 */
export template <orm::Model T, typename Gateway,
    typename StreamStrategy = orm::session_stream_strategy>
[[nodiscard]] auto make_orm_repository_handle(
    std::shared_ptr<Gateway> gateway,
    orm::automatic_interceptor_options interceptors = {})
    -> std::expected<orm_repository_handle<T, Gateway, StreamStrategy>,
        std::error_code>
{
    if (!gateway)
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        return orm_repository_handle<T, Gateway, StreamStrategy>{
            std::move(gateway), interceptors};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
}

} // namespace cnetmod::application
