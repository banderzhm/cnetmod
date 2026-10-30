#pragma once

namespace cnetmod::application {
class mongodb_cluster_runtime
{
public:
    mongodb_cluster_runtime(io_context& owner, mongodb_service_options configuration);
    std::size_t active_operations = 0;
    io_context& io;
    mongodb_service_options options;
    thread_pool blocking_pool{1};
    std::shared_ptr<mongodb::topology_connection_pool> pool;
    mongodb::server_selection_options selection;
    std::string database;
};

class mongodb_operation_guard
{
public:
    explicit mongodb_operation_guard(std::shared_ptr<mongodb_cluster_runtime> value);
    ~mongodb_operation_guard();

private:
    std::shared_ptr<mongodb_cluster_runtime> runtime_;
};

namespace mongodb_application_detail {
    auto lifecycle_error(mongodb::error_code code) noexcept -> std::error_code;
}
} // namespace cnetmod::application
