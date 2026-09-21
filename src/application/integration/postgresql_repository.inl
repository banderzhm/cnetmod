template <orm::Model T>
[[nodiscard]] auto make_postgresql_repository_handle(
    postgresql_service& service,
    orm::automatic_interceptor_options interceptors)
    -> std::expected<postgresql_repository_handle<T>, std::error_code>
{
    auto gateway = make_postgresql_session_gateway(service);
    try
    {
        auto owned_gateway = std::make_shared<postgresql_session_gateway>(
            std::move(gateway));
        return make_orm_repository_handle<T, postgresql_session_gateway>(
            std::move(owned_gateway),
            interceptors);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
}
