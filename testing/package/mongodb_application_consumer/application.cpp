import std;
import cnetmod.application;
import cnetmod.protocol.mongodb;
import cnetmod.json;

int main()
{
    namespace app = cnetmod::application;
    auto host = app::application_builder{"installed-mongodb-application"}
        .enable_auto_configuration()
        .configure([](app::application_configuration& configuration) {
            configuration.logging.manage_lifecycle = false;
            configuration.management.enabled = false;
            app::configured_service service{.name = "mongodb", .enabled = true};
            service.properties = cnetmod::json::object({
                {"uri", "mongodb+srv://cluster.example.com/app?authMechanism=MONGODB-AWS&compressors=snappy"}});
            configuration.services.emplace("mongodb", std::move(service));
        }).build();
    if (!host) return 1;
    auto service = host->services().find<app::mongodb_service>();
    if (!service || service->topology_pool() || service->cleanup_required()) return 2;
    return 0;
}
