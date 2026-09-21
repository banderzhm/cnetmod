template <orm::Model T>
[[nodiscard]] auto make_mysql_repository_handle(mysql_service& service,
    orm::automatic_interceptor_options interceptors)
    -> std::expected<mysql_repository_handle<T>, std::error_code>
{
    auto gateway = make_mysql_session_gateway(service);
    try
    {
        auto owned_gateway = std::make_shared<mysql_session_gateway>(
            std::move(gateway));
        return make_orm_repository_handle<T, mysql_session_gateway,
            orm::mysql_stream_strategy>(std::move(owned_gateway),
            interceptors);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
}
