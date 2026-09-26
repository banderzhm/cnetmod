#include "test_framework.hpp"

import std;
import cnetmod.json;
import cnetmod.application;
import cnetmod.core;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.protocol.http;

namespace application = cnetmod::application;

namespace {

/// Writes a JSON configuration file that is removed when the guard ends.
class temporary_configuration
{
public:
    explicit temporary_configuration(std::string_view content)
        : path_(std::filesystem::temp_directory_path() /
              std::format("cnetmod-composition-{}.json",
                  std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        write(content);
    }
    temporary_configuration(const temporary_configuration&) = delete;
    auto operator=(const temporary_configuration&) -> temporary_configuration& = delete;
    ~temporary_configuration()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    void write(std::string_view content) const
    {
        std::ofstream output{path_, std::ios::binary | std::ios::trunc};
        output << content;
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path&
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void quiet(application::application_configuration& value)
{
    value.logging.manage_lifecycle = false;
    value.management.enabled = false;
    value.install_signal_handlers = false;
    value.observability.tracing = false;
    value.observability.metrics = false;
    value.observability.logs = false;
    value.http.access_logging = false;
}

void set_variable(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void clear_variable(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

} // namespace

struct pricing_options
{
    std::string currency{"CNY"};
    int max_items = 100;
    std::vector<std::string> regions{"cn"};
    std::map<std::string, int> limits;
};

struct feature_flags
{
    bool beta = false;
};

struct nested_rule
{
    std::string name;
    bool enabled = true;
    std::vector<std::string> tags{"default"};
};

struct nested_options
{
    std::vector<nested_rule> rules;
};

namespace {

struct clock_source
{
    int value = 42;
};

struct pricing_service
{
    explicit pricing_service(clock_source& clock) : clock(&clock)
    {
    }
    clock_source* clock;
};

struct cycle_a;
struct cycle_b;
struct cycle_a
{
    cycle_b* peer = nullptr;
};
struct cycle_b
{
    cycle_a* peer = nullptr;
};

struct greeting
{
    virtual ~greeting() = default;
    [[nodiscard]] virtual auto text() const -> std::string = 0;
};

struct english_greeting final : greeting
{
    [[nodiscard]] auto text() const -> std::string override
    {
        return "hello";
    }
};

struct destruction_probe
{
    destruction_probe(std::vector<std::string>& log, std::string name)
        : log(&log), name(std::move(name))
    {
    }
    ~destruction_probe()
    {
        log->push_back(name);
    }
    std::vector<std::string>* log;
    std::string name;
};

struct first_probe : destruction_probe
{
    using destruction_probe::destruction_probe;
};

struct second_probe : destruction_probe
{
    second_probe(std::vector<std::string>& log, first_probe& first)
        : destruction_probe(log, "second"), first(&first)
    {
    }
    first_probe* first;
};

} // namespace

TEST(configuration_errors_carry_the_document_path)
{
    temporary_configuration unknown{R"({"http":{"sse":{"max_duration":5}}})"};
    auto failed = application::load_configuration(unknown.path());
    ASSERT_FALSE(failed.has_value());
    if (!failed)
    {
        ASSERT_EQ(failed.error().path, "http.sse.max_duration");
        ASSERT_EQ(failed.error().message, "unknown key");
        ASSERT_EQ(failed.error().describe(), "http.sse.max_duration: unknown key");
    }

    temporary_configuration mistyped{R"({"http":{"port":"eighty"}})"};
    auto invalid = application::load_configuration(mistyped.path());
    ASSERT_FALSE(invalid.has_value());
    if (!invalid)
        ASSERT_EQ(invalid.error().path, "http.port");

    temporary_configuration level{R"({"logging":{"level":"loud"}})"};
    auto rejected = application::load_configuration(level.path());
    ASSERT_FALSE(rejected.has_value());
    if (!rejected)
    {
        ASSERT_EQ(rejected.error().path, "logging.level");
        ASSERT_TRUE(rejected.error().message.contains("loud"));
    }

    application::application_configuration value;
    value.security.jwt.enabled = true;
    value.security.jwt.issuer = "issuer";
    value.security.jwt.secret = "short";
    auto validated = application::validate_configuration(value);
    ASSERT_FALSE(validated.has_value());
    if (!validated)
        ASSERT_EQ(validated.error().path, "security.jwt.secret");
}

TEST(environment_references_support_defaults_and_escapes)
{
    set_variable("CNETMOD_COMPOSITION_SET", "value");
    clear_variable("CNETMOD_COMPOSITION_UNSET");
    auto expanded = application::expand_environment_references(
        "a=${CNETMOD_COMPOSITION_SET} b=${CNETMOD_COMPOSITION_UNSET:-fallback} "
        "c=${CNETMOD_COMPOSITION_SET:-ignored} d=$${literal} e=$5");
    ASSERT_TRUE(expanded.has_value());
    if (expanded)
        ASSERT_EQ(*expanded,
            "a=value b=fallback c=value d=${literal} e=$5");

    auto empty_default = application::expand_environment_references(
        "${CNETMOD_COMPOSITION_UNSET:-}");
    ASSERT_TRUE(empty_default.has_value());
    if (empty_default)
        ASSERT_TRUE(empty_default->empty());

    auto missing = application::expand_environment_references(
        "${CNETMOD_COMPOSITION_UNSET}");
    ASSERT_FALSE(missing.has_value());
    if (!missing)
    {
        ASSERT_TRUE(missing.error().message.contains("CNETMOD_COMPOSITION_UNSET"));
        ASSERT_EQ(missing.error().code,
            std::make_error_code(std::errc::no_such_file_or_directory));
    }

    auto unterminated = application::expand_environment_references("${OPEN");
    ASSERT_FALSE(unterminated.has_value());
    auto invalid_name = application::expand_environment_references("${1BAD}");
    ASSERT_FALSE(invalid_name.has_value());

    temporary_configuration file{
        R"({"services":{"cache":{"type":"redis","password":"${CNETMOD_COMPOSITION_UNSET}"}}})"};
    auto located = application::load_configuration(file.path());
    ASSERT_FALSE(located.has_value());
    if (!located)
        ASSERT_EQ(located.error().path, "services.cache.password");
    clear_variable("CNETMOD_COMPOSITION_SET");
}

TEST(options_sections_apply_defaults_overrides_and_validation)
{
    temporary_configuration file{
        R"({"pricing":{"max_items":25,"limits":{"vip":3}},"features":{"beta":true}})"};
    std::shared_ptr<const pricing_options> observed;
    auto host = application::application_builder{"options"}
                    .configuration_file(file.path())
                    .configure(quiet)
                    .add_module(application::make_module("pricing",
                        {.configure_options = [](application::options_registry& options)
                            {
                                options.section<pricing_options>("pricing")
                                    .validate([](const pricing_options& value)
                                            -> std::expected<void, std::string>
                                        {
                                            if (value.max_items <= 0)
                                                return std::unexpected(
                                                    std::string{"max_items must be positive"});
                                            return {};
                                        });
                                options.section<feature_flags>("features");
                            },
                            .compose = [&observed](application::composition_context& context)
                                -> std::expected<void, std::string>
                            {
                                observed = context.components
                                               .get<application::options_monitor<pricing_options>>(
                                                   "pricing")
                                               .current();
                                return {};
                            }}))
                    .build();
    ASSERT_TRUE(host.has_value());
    ASSERT_TRUE(observed != nullptr);
    if (observed)
    {
        ASSERT_EQ(observed->currency, "CNY");
        ASSERT_EQ(observed->max_items, 25);
        ASSERT_EQ(observed->regions.size(), std::size_t{1});
        ASSERT_EQ(observed->limits.at("vip"), 3);
    }
    if (host)
        ASSERT_TRUE(host->components()
                        .get<application::options_monitor<feature_flags>>("features")
                        .current()
                        ->beta);
}

TEST(options_sections_apply_defaults_inside_array_elements)
{
    temporary_configuration file{R"({"nested":{"rules":[{"name":"alpha"}]}})"};
    auto host = application::application_builder{"nested-options"}
                    .configuration_file(file.path())
                    .configure(quiet)
                    .add_module(application::make_module("nested",
                        {.configure_options = [](application::options_registry& options)
                            { options.section<nested_options>("nested"); }}))
                    .build();
    ASSERT_TRUE(host.has_value());
    if (host)
    {
        const auto current = host->components()
                                 .get<application::options_monitor<nested_options>>(
                                     "nested")
                                 .current();
        ASSERT_EQ(current->rules.size(), std::size_t{1});
        ASSERT_TRUE(current->rules[0].enabled);
        ASSERT_TRUE(current->rules[0].tags ==
            std::vector<std::string>{"default"});
    }

    temporary_configuration unknown{
        R"({"nested":{"rules":[{"name":"alpha","unexpected":true}]}})"};
    auto rejected = application::application_builder{"nested-options-unknown"}
                        .configuration_file(unknown.path())
                        .configure(quiet)
                        .add_module(application::make_module("nested",
                            {.configure_options = [](application::options_registry& options)
                                { options.section<nested_options>("nested"); }}))
                        .build();
    ASSERT_FALSE(rejected.has_value());
}

TEST(options_sections_drive_conditional_registration)
{
    temporary_configuration file{R"({"pricing":{"regions":["cn","eu"]}})"};
    auto host = application::application_builder{"options-registration"}
                    .configuration_file(file.path())
                    .configure(quiet)
                    .add_module(application::make_module("regions",
                        {.configure_options = [](application::options_registry& options)
                            {
                                options.section<pricing_options>("pricing");
                                options.section<feature_flags>("features");
                            },
                            .register_components =
                                [](application::registration_context& context)
                                -> std::expected<void, std::string>
                            {
                                const auto pricing =
                                    context.options.current<pricing_options>("pricing");
                                for (const auto& region : pricing->regions)
                                    context.components.instance(
                                        std::make_shared<clock_source>(), region);
                                return {};
                            }}))
                    .build();
    ASSERT_TRUE(host.has_value());
    if (host)
    {
        ASSERT_TRUE(host->components().find<clock_source>("cn") != nullptr);
        ASSERT_TRUE(host->components().find<clock_source>("eu") != nullptr);
    }

    auto mistyped = application::application_builder{"options-registration-type"}
                        .configuration_file(file.path())
                        .configure(quiet)
                        .add_module(application::make_module("regions",
                            {.configure_options = [](application::options_registry& options)
                                { options.section<pricing_options>("pricing"); },
                                .register_components =
                                    [](application::registration_context& context)
                                    -> std::expected<void, std::string>
                                {
                                    (void)context.options.current<feature_flags>("pricing");
                                    return {};
                                }}))
                        .build();
    ASSERT_FALSE(mistyped.has_value());
    if (!mistyped)
        ASSERT_TRUE(mistyped.error().phase == application::build_phase::registration);

    auto undeclared = application::application_builder{"options-registration-missing"}
                          .configure(quiet)
                          .add_module(application::make_module("regions",
                              {.register_components =
                                      [](application::registration_context& context)
                                      -> std::expected<void, std::string>
                                  {
                                      (void)context.options.current<pricing_options>("pricing");
                                      return {};
                                  }}))
                          .build();
    ASSERT_FALSE(undeclared.has_value());
}

TEST(options_sections_report_unknown_keys_unclaimed_sections_and_failures)
{
    const auto build = [](std::string_view content, bool required = false)
    {
        temporary_configuration file{content};
        return application::application_builder{"options-errors"}
            .configuration_file(file.path())
            .configure(quiet)
            .add_module(application::make_module("pricing",
                {.configure_options = [required](application::options_registry& options)
                    {
                        options.section<pricing_options>("pricing", required)
                            .validate([](const pricing_options& value)
                                    -> std::expected<void, std::string>
                                {
                                    if (value.max_items <= 0)
                                        return std::unexpected(
                                            std::string{"max_items must be positive"});
                                    return {};
                                });
                    }}))
            .build();
    };

    auto unknown_key = build(R"({"pricing":{"max_itemz":3}})");
    ASSERT_FALSE(unknown_key.has_value());
    if (!unknown_key)
    {
        ASSERT_TRUE(unknown_key.error().phase == application::build_phase::options);
        ASSERT_EQ(unknown_key.error().path, "pricing.max_itemz");
    }

    auto unclaimed = build(R"({"billing":{"enabled":true}})");
    ASSERT_FALSE(unclaimed.has_value());
    if (!unclaimed)
        ASSERT_EQ(unclaimed.error().path, "billing");

    auto invalid = build(R"({"pricing":{"max_items":0}})");
    ASSERT_FALSE(invalid.has_value());
    if (!invalid)
    {
        ASSERT_EQ(invalid.error().path, "pricing");
        ASSERT_EQ(invalid.error().message, "max_items must be positive");
    }

    auto missing = build("{}", true);
    ASSERT_FALSE(missing.has_value());
    if (!missing)
        ASSERT_EQ(missing.error().path, "pricing");

    auto reserved = application::application_builder{"reserved-section"}
                        .configure([](application::application_configuration& value)
                            {
                                quiet(value);
                                value.sections.emplace("http", cnetmod::json::object());
                            })
                        .build();
    ASSERT_FALSE(reserved.has_value());
}

TEST(options_runtime_safe_sections_are_republished_on_reload)
{
    temporary_configuration file{R"({"features":{"beta":false},"pricing":{"max_items":5}})"};
    std::vector<bool> notifications;
    application::options_subscription subscription;
    auto host = application::application_builder{"options-reload"}
                    .configuration_file(file.path())
                    .configure(quiet)
                    .add_module(application::make_module("features",
                        {.configure_options = [](application::options_registry& options)
                            {
                                options.section<feature_flags>("features")
                                    .reload(application::options_reload::runtime_safe);
                                options.section<pricing_options>("pricing");
                            },
                            .compose = [&](application::composition_context& context)
                                -> std::expected<void, std::string>
                            {
                                subscription = context.components
                                                   .get<application::options_monitor<feature_flags>>(
                                                       "features")
                                                   .on_change([&notifications](const feature_flags& value)
                                                       {
                                                           notifications.push_back(value.beta);
                                                       });
                                return {};
                            }}))
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    auto& features = host->components().get<application::options_monitor<feature_flags>>(
        "features");

    file.write(R"({"features":{"beta":true},"pricing":{"max_items":5}})");
    auto published = host->reload_configuration();
    ASSERT_TRUE(published.has_value());
    if (published)
    {
        ASSERT_FALSE(published->restart_required);
        ASSERT_TRUE(std::ranges::find(published->changed, "features") !=
            published->changed.end());
    }
    ASSERT_TRUE(features.current()->beta);
    ASSERT_TRUE(notifications == std::vector<bool>({true}));

    file.write(R"({"features":{"beta":true},"pricing":{"max_items":9}})");
    auto restart = host->reload_configuration();
    ASSERT_TRUE(restart.has_value());
    if (restart)
        ASSERT_TRUE(restart->restart_required);
    ASSERT_EQ(host->components()
                  .get<application::options_monitor<pricing_options>>("pricing")
                  .current()
                  ->max_items,
        5);

    file.write(R"({"features":{"beta":"yes"},"pricing":{"max_items":9}})");
    auto rejected = host->reload_configuration();
    ASSERT_FALSE(rejected.has_value());
    if (!rejected)
        ASSERT_EQ(rejected.error().path, "features");
    ASSERT_TRUE(features.current()->beta);

    subscription.reset();
    file.write(R"({"features":{"beta":false},"pricing":{"max_items":5}})");
    ASSERT_TRUE(host->reload_configuration().has_value());
    ASSERT_EQ(notifications.size(), std::size_t{1});
}

TEST(components_resolve_dependencies_in_order_and_destroy_in_reverse)
{
    std::vector<std::string> destroyed;
    {
        application::component_collection components;
        components.singleton<second_probe>([&destroyed](application::component_resolver& resolver)
            {
                return std::make_shared<second_probe>(destroyed,
                    resolver.get<first_probe>());
            });
        components.singleton<first_probe>([&destroyed](application::component_resolver&)
            {
                return std::make_unique<first_probe>(destroyed, "first");
            });
        auto built = application::component_container::build(std::move(components));
        ASSERT_TRUE(built.has_value());
        if (built)
        {
            ASSERT_EQ((*built)->constructed(), std::size_t{2});
            ASSERT_EQ((*built)->get<second_probe>().first, &(*built)->get<first_probe>());
        }
    }
    ASSERT_TRUE(destroyed == std::vector<std::string>({"second", "first"}));
}

TEST(components_report_missing_dependencies_with_the_resolution_chain)
{
    application::component_collection components;
    components.singleton<pricing_service>([](application::component_resolver& resolver)
        {
            return std::make_shared<pricing_service>(resolver.get<clock_source>("utc"));
        });
    auto built = application::component_container::build(std::move(components));
    ASSERT_FALSE(built.has_value());
    if (!built)
    {
        ASSERT_TRUE(built.error().phase == application::build_phase::resolution);
        ASSERT_TRUE(built.error().component.contains("clock_source"));
        ASSERT_TRUE(built.error().component.contains("utc"));
        ASSERT_TRUE(built.error().message.contains("pricing_service"));
    }
}

TEST(components_detect_cycles_duplicates_and_factory_failures)
{
    application::component_collection cyclic;
    cyclic.singleton<cycle_a>([](application::component_resolver& resolver)
        {
            return cycle_a{&resolver.get<cycle_b>()};
        });
    cyclic.singleton<cycle_b>([](application::component_resolver& resolver)
        {
            return cycle_b{&resolver.get<cycle_a>()};
        });
    auto cycle = application::component_container::build(std::move(cyclic));
    ASSERT_FALSE(cycle.has_value());
    if (!cycle)
        ASSERT_TRUE(cycle.error().message.contains("dependency cycle"));

    application::component_collection duplicated;
    duplicated.singleton<clock_source>([](application::component_resolver&) { return clock_source{}; });
    duplicated.singleton<clock_source>([](application::component_resolver&) { return clock_source{}; });
    auto duplicate = application::component_container::build(std::move(duplicated));
    ASSERT_FALSE(duplicate.has_value());
    if (!duplicate)
        ASSERT_EQ(duplicate.error().code, std::make_error_code(std::errc::file_exists));

    application::component_collection throwing;
    throwing.singleton<clock_source>([](application::component_resolver&) -> clock_source
        {
            throw std::runtime_error("clock unavailable");
        });
    auto failed = application::component_container::build(std::move(throwing));
    ASSERT_FALSE(failed.has_value());
    if (!failed)
        ASSERT_TRUE(failed.error().message.contains("clock unavailable"));
}

TEST(components_support_names_aliases_borrowing_and_registry_fallback)
{
    clock_source external{7};
    application::service_registry registry;
    auto registered = registry.emplace_named<clock_source>("registry", clock_source{9});
    ASSERT_TRUE(registered.has_value());

    application::component_collection components;
    components.borrow(external, "external");
    components.singleton<english_greeting>([](application::component_resolver&)
        {
            return english_greeting{};
        });
    components.alias<greeting, english_greeting>();
    components.singleton<clock_source>("owned", [](application::component_resolver&)
        {
            return clock_source{1};
        });
    ASSERT_TRUE(components.contains<clock_source>("owned"));
    auto built = application::component_container::build(std::move(components),
        [&registry](std::type_index type, std::string_view name)
        {
            return registry.find_shared(type, name);
        });
    ASSERT_TRUE(built.has_value());
    if (!built)
        return;
    auto& container = **built;
    ASSERT_EQ(&container.get<clock_source>("external"), &external);
    ASSERT_EQ(container.get<clock_source>("owned").value, 1);
    ASSERT_EQ(container.get<clock_source>("registry").value, 9);
    ASSERT_EQ(container.get<greeting>().text(), "hello");
    ASSERT_EQ(&container.get<greeting>(),
        static_cast<greeting*>(&container.get<english_greeting>()));
    ASSERT_TRUE(container.find<clock_source>("absent") == nullptr);
    ASSERT_THROWS(container.get<clock_source>("absent"));
}

TEST(module_phases_run_in_order_around_the_service_lifecycle)
{
    cnetmod::net_init network;
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());

    std::vector<std::string> events;
    application::application_host* running = nullptr;
    const auto module_named = [&events, &running](std::string name)
    {
        return application::make_module(name,
            {.configure_options = [&events, name](application::options_registry&)
                {
                    events.push_back(name + ":options");
                },
                .register_components = [&events, name](application::registration_context&)
                    -> std::expected<void, std::string>
                {
                    events.push_back(name + ":register");
                    return {};
                },
                .compose = [&events, name](application::composition_context&)
                    -> std::expected<void, std::string>
                {
                    events.push_back(name + ":compose");
                    return {};
                },
                .on_started = [&events, &running, name](application::application_runtime&)
                    -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    events.push_back(name + ":started");
                    if (name == "second" && running != nullptr)
                        running->request_stop();
                    co_return std::expected<void, std::error_code>{};
                },
                .on_stopping = [&events, name](application::application_runtime&)
                    -> cnetmod::task<void>
                {
                    events.push_back(name + ":stopping");
                    co_return;
                }});
    };
    auto host = application::application_builder{"module-phases"}
                    .configure([&endpoint](application::application_configuration& value)
                        {
                            quiet(value);
                            value.http.address = "127.0.0.1";
                            value.http.port = endpoint->port();
                        })
                    .add_module(module_named("first"))
                    .add_module(module_named("second"))
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    ASSERT_TRUE(events == std::vector<std::string>({"first:options", "second:options",
                              "first:register", "second:register", "first:compose",
                              "second:compose"}));
    reservation->close();
    running = &*host;
    auto result = host->run();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(events == std::vector<std::string>({"first:options", "second:options",
                              "first:register", "second:register", "first:compose",
                              "second:compose", "first:started", "second:started",
                              "second:stopping", "first:stopping"}));
}

TEST(build_errors_describe_phase_component_and_path)
{
    const application::build_error error{.phase = application::build_phase::resolution,
        .component = "orders_service",
        .path = "services.primary",
        .message = "component is not registered"};
    ASSERT_EQ(error.describe(),
        "resolution [orders_service] services.primary: component is not registered");

    auto unsupported = application::application_builder{"unsupported-integration"}
                           .enable_auto_configuration()
                           .configure([](application::application_configuration& value)
                               {
                                   quiet(value);
                                   application::configured_service service{
                                       .name = "not-a-real-integration",
                                       .instance = "primary",
                                       .enabled = true};
                                   value.services.emplace("primary", std::move(service));
                               })
                           .build();
    ASSERT_FALSE(unsupported.has_value());
    if (!unsupported)
    {
        ASSERT_TRUE(unsupported.error().phase == application::build_phase::registration);
        ASSERT_EQ(unsupported.error().path, "services.primary.type");
        ASSERT_EQ(unsupported.error().code, std::make_error_code(std::errc::not_supported));
    }
}

TEST(component_container_builds_one_instance_per_event_loop)
{
    struct loop_identity_interface
    {
        virtual ~loop_identity_interface() = default;
        [[nodiscard]] virtual auto owner() const noexcept
            -> cnetmod::io_context* = 0;
    };
    struct loop_identity final : loop_identity_interface
    {
        explicit loop_identity(cnetmod::io_context& value) noexcept
            : event_loop(&value)
        {
        }

        cnetmod::io_context* event_loop;

        [[nodiscard]] auto owner() const noexcept
            -> cnetmod::io_context* override
        {
            return event_loop;
        }
    };

    auto first = cnetmod::make_io_context();
    auto second = cnetmod::make_io_context();
    std::array<cnetmod::io_context*, 2> loops{first.get(), second.get()};
    application::component_collection registrations;
    registrations.event_loop<loop_identity>(
        [](application::component_resolver&, cnetmod::io_context& event_loop)
        {
            return loop_identity{event_loop};
        });
    registrations.event_loop_alias<loop_identity_interface, loop_identity>();
    auto built = application::component_container::build(
        std::move(registrations), {}, loops);
    ASSERT_TRUE(built.has_value());
    ASSERT_EQ((*built)->constructed(), std::size_t{4});

    loop_identity* first_value = nullptr;
    loop_identity* second_value = nullptr;
    loop_identity_interface* first_alias = nullptr;
    loop_identity_interface* second_alias = nullptr;
    auto resolve = [&](cnetmod::io_context& event_loop,
                       loop_identity*& destination,
                       loop_identity_interface*& alias) -> cnetmod::task<void>
    {
        destination = (*built)->find<loop_identity>();
        alias = (*built)->find<loop_identity_interface>();
        event_loop.stop();
        co_return;
    };
    cnetmod::spawn(*first, resolve(*first, first_value, first_alias));
    first->run();
    cnetmod::spawn(*second, resolve(*second, second_value, second_alias));
    second->run();

    ASSERT_TRUE(first_value != nullptr);
    ASSERT_TRUE(second_value != nullptr);
    ASSERT_TRUE(first_value != second_value);
    ASSERT_TRUE(first_value->event_loop == first.get());
    ASSERT_TRUE(second_value->event_loop == second.get());
    ASSERT_TRUE(first_alias == first_value);
    ASSERT_TRUE(second_alias == second_value);
    ASSERT_TRUE(first_alias->owner() == first.get());
    ASSERT_TRUE(second_alias->owner() == second.get());
    ASSERT_TRUE((*built)->find<loop_identity>() == nullptr);
    ASSERT_TRUE((*built)->find<loop_identity_interface>() == nullptr);
}

RUN_TESTS()
