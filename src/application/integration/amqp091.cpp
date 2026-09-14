module cnetmod.application.amqp091;
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import std;
import cnetmod.application.task_supervisor;
import cnetmod.coro.timer;
import cnetmod.coro.wait_group;

namespace cnetmod::application {
namespace {

    struct cancellation_link
    {
        cancel_token& parent;
        cancel_token& child;

        cancellation_link(cancel_token& parent, cancel_token& child) noexcept : parent(parent), child(child)
        {
            if (!parent.register_callback(this, [](void* raw) noexcept
                    {
                        static_cast<cancellation_link*>(raw)->child.cancel();
                    }))
                child.cancel();
        }

        ~cancellation_link()
        {
            (void)parent.complete_callback(this);
            parent.finish_callback(this);
        }
    };

    auto connect_client(amqp091::amqp091_client& client,
        const amqp091::connection_options& options, cancel_token& token)
        -> task<std::expected<void, std::error_code>>
    {
        auto result = co_await client.async_connect(options, token);
        if (token.is_cancelled())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        if (!result)
        {
            if (result.error().code == amqp091::error_code::not_enough_memory)
                co_return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
            co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
        }
        co_return {};
    }

} // namespace

/**
 * @brief Shares startup completion without borrowing the initiating coroutine.
 */
struct amqp091_service::session_startup
{
    async_wait_group completed;
    cancel_token cancellation;
    std::exception_ptr failure;
    std::error_code error;
    bool ready = false;
    bool notified = false;

    session_startup()
    {
        completed.add();
    }

    void notify() noexcept
    {
        if (!std::exchange(notified, true))
            completed.done();
    }
};

auto amqp091_service::run_session(std::shared_ptr<session_startup> startup, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    cancellation_link cancellation{token, startup->cancellation};

    struct completion_guard
    {
        session_startup& startup;

        ~completion_guard()
        {
            startup.ready = false;
            startup.notify();
        }
    } completion{*startup};

    try
    {
        auto result = co_await client_.connection()->async_run_session(startup->cancellation, [startup]
            {
                startup->ready = true;
                startup->notify();
            });
        if (!result && !token.is_cancelled())
        {
            startup->error = std::make_error_code(result.error().code == amqp091::error_code::not_enough_memory
                    ? std::errc::not_enough_memory
                    : std::errc::connection_aborted);
            co_return std::unexpected(startup->error);
        }
        co_return {};
    }
    catch (...)
    {
        startup->failure = std::current_exception();
        throw;
    }
}

auto amqp091_service::wait_for_startup(std::shared_ptr<session_startup> startup, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    cancellation_link cancellation{token, startup->cancellation};
    co_await startup->completed.wait();
    if (startup->failure)
        std::rethrow_exception(startup->failure);
    if (token.is_cancelled())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (!startup->ready)
        co_return std::unexpected(startup->error ? startup->error : std::make_error_code(std::errc::connection_aborted));
    co_return {};
}

amqp091_service::amqp091_service(io_context& io, amqp091::connection_options options,
    std::string instance, service_requirement requirement, recovery_policy recovery)
    : options_(std::move(options)), client_(io), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery) {}

auto amqp091_service::client() noexcept -> amqp091::amqp091_client&
{
    return client_;
}

auto amqp091_service::key() const -> service_key
{
    return {"amqp091", instance_};
}

auto amqp091_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto amqp091_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto amqp091_service::shutdown_required() const noexcept -> bool
{
    return client_.state() != amqp091::connection_state::disconnected;
}

auto amqp091_service::start(service_context& context) -> task<std::expected<void, std::error_code>>
{
    if (context.cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (context.operation_deadline.expired())
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    const auto task_name = std::format("amqp091-pump:{}", instance_);
    if (client_.state() == amqp091::connection_state::open)
    {
        const auto pump = context.supervisor.state(task_name);
        if (pump && *pump != supervised_task_state::failed && *pump != supervised_task_state::stopped)
        {
            if (!startup_)
                co_return std::unexpected(std::make_error_code(std::errc::file_exists));
            co_return co_await with_deadline(context.io, context.operation_deadline,
                wait_for_startup(startup_, context.cancellation), context.cancellation);
        }
    }
    else
    {
        auto connected = co_await with_deadline(context.io, context.operation_deadline,
            connect_client(client_, options_, context.cancellation), context.cancellation);
        if (!connected)
            co_return std::unexpected(connected.error());
    }
    /**
     * @brief Delegates recovery and required-service escalation to the lifecycle.
     *
     * This task owns one transport session only. Retrying a disconnected frame
     * pump cannot reconnect it and would create a second recovery budget.
     */
    auto pump_policy = recovery_;
    pump_policy.budget = std::chrono::milliseconds::zero();
    auto startup = std::make_shared<session_startup>();
    startup_ = startup;
    auto supervised = context.supervisor.supervise(task_name, [this, startup](cancel_token& token) -> task<std::expected<void, std::error_code>>
        {
            try
            {
                return run_session(startup, token);
            }
            catch (...)
            {
                startup->failure = std::current_exception();
                startup->notify();
                throw;
            }
        },
        pump_policy, false);
    if (!supervised)
        co_return std::unexpected(supervised.error());
    if (context.supervisor.state(task_name) == supervised_task_state::failed)
        co_return std::unexpected(context.supervisor.last_error(task_name));
    co_return co_await with_deadline(context.io, context.operation_deadline,
        wait_for_startup(startup, context.cancellation), context.cancellation);
}

auto amqp091_service::stop(service_context&) -> task<std::expected<void, std::error_code>>
{
    auto closed = co_await client_.async_close();
    if (!closed)
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    co_return {};
}

auto amqp091_service::probe(service_context& context) -> task<health_report>
{
    const auto task_name = std::format("amqp091-pump:{}", instance_);
    const auto pump = context.supervisor.state(task_name);
    const auto up = startup_ && startup_->ready && client_.state() == amqp091::connection_state::open && pump &&
        (*pump == supervised_task_state::starting || *pump == supervised_task_state::running);
    co_return health_report{.status = up ? service_health::up : service_health::down,
        .message = up ? "amqp 0-9-1 connected" : "amqp 0-9-1 session unavailable",
        .error = up ? std::error_code{} : context.supervisor.last_error(task_name)};
}

auto auto_configure_amqp091(const configured_service& configuration,
    auto_configuration_context& context) -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"host", "port", "username", "password", "virtual_host"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        amqp091::connection_options options;
        options.endpoint.host = configuration.properties.value("host", options.endpoint.host);
        options.endpoint.port = configuration.properties.value("port", options.endpoint.port);
        options.credentials.username = configuration.properties.value("username", std::string{});
        options.credentials.password = configuration.properties.value("password", std::string{});
        options.virtual_host = configuration.properties.value("virtual_host", options.virtual_host);
        auto service = std::make_shared<amqp091_service>(context.io, std::move(options),
            configuration.instance, configuration.requirement, configuration.recovery);
        return context.services.add_managed_named<amqp091_service>(
            configuration.instance, std::move(service));
    }
    catch (...)
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
}
} // namespace cnetmod::application
#endif
