module cnetmod.application.managed_service;

namespace cnetmod::application {

auto managed_service::cleanup_required() const noexcept -> bool
{
    return false;
}

auto managed_service::shutdown_required() const noexcept -> bool
{
    return cleanup_required();
}

} // namespace cnetmod::application
