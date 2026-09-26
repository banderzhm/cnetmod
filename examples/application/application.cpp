#include <cnetmod/config.hpp>

import std;
import cnetmod.application;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace application = cnetmod::application;
namespace http = cnetmod::http;

namespace {

/// Typed configuration section `orders` in application.yaml.
struct order_options
{
    int page_size = 20;
};

/// Business component: constructed once at build time, injected by type.
class order_catalog
{
public:
    explicit order_catalog(const order_options& options) : page_size_(options.page_size)
    {
    }

    [[nodiscard]] auto list() const -> std::string
    {
        return std::format(R"({{"orders":[],"page_size":{}}})", page_size_);
    }

private:
    int page_size_;
};

/// One feature module: declares its configuration, registers its components
/// and contributes routes with their access policy.
class order_module final : public application::application_module
{
public:
    [[nodiscard]] auto name() const -> std::string_view override
    {
        return "orders";
    }

    void configure_options(application::options_registry& options) override
    {
        options.section<order_options>("orders").validate(
            [](const order_options& value) -> std::expected<void, std::string>
            {
                if (value.page_size <= 0 || value.page_size > 500)
                    return std::unexpected(std::string{"page_size must be in [1, 500]"});
                return {};
            });
    }

    [[nodiscard]] auto register_components(application::registration_context& context)
        -> std::expected<void, std::string> override
    {
        context.components.singleton<order_catalog>(
            [](application::component_resolver& resolver)
            {
                return std::make_shared<order_catalog>(*resolver
                        .get<application::options_monitor<order_options>>("orders")
                        .current());
            });
        return {};
    }

    [[nodiscard]] auto compose(application::composition_context& context)
        -> std::expected<void, std::string> override
    {
        auto& catalog = context.components.get<order_catalog>();
        context.routes.get("/orders",
            [&catalog](http::request_context& request) -> cnetmod::task<void>
            {
                request.json(http::status::ok, catalog.list());
                co_return;
            },
            http::endpoint_metadata{http::allow_anonymous{},
                http::endpoint_name{"orders.list"}});
        return {};
    }
};

} // namespace

auto main() -> int
{
    auto host = application::application_builder{"order-service"}
                    .configuration_file("application.yaml")
                    .enable_auto_configuration()
                    .add_module<order_module>()
                    .build();
    if (!host)
    {
        logger::critical{"order-service cannot start: {}", host.error().describe()};
        return 1;
    }
    return host->run() ? 0 : 1;
}
