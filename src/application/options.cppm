/**
 * @brief Typed application configuration sections (Options pattern).
 *
 * Modules declare the top-level sections they own. At build time every
 * section of the configuration document must be claimed by exactly one
 * declaration; each claimed section is decoded into its C++ type with the
 * type's default member values applied first, unknown keys rejected with their
 * document path, and the optional validator applied.
 *
 * Sections declared runtime_safe are republished atomically on configuration
 * reload and notify subscribers; restart_required sections report a pending
 * restart instead of changing the published value.
 */
export module cnetmod.application.options;

import std;
import cnetmod.json;
import cnetmod.application.configuration;
import cnetmod.application.components;
import cnetmod.application.diagnostics;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::application {

/**
 * @brief Reload behavior of one options section.
 */
export enum class options_reload
{
    restart_required,
    runtime_safe,
};

/**
 * @brief Validation hook: returns a human-readable message on failure.
 */
export template <class T>
using options_validator = std::function<std::expected<void, std::string>(const T&)>;

/**
 * @brief Keeps a change subscription alive; destroying it unsubscribes.
 */
export class options_subscription
{
public:
    options_subscription() noexcept = default;
    explicit options_subscription(std::function<void()> cancel) noexcept
        : cancel_(std::move(cancel))
    {
    }
    options_subscription(options_subscription&& other) noexcept
        : cancel_(std::exchange(other.cancel_, {}))
    {
    }
    auto operator=(options_subscription&& other) noexcept -> options_subscription&
    {
        if (this != &other)
        {
            reset();
            cancel_ = std::exchange(other.cancel_, {});
        }
        return *this;
    }
    options_subscription(const options_subscription&) = delete;
    auto operator=(const options_subscription&) -> options_subscription& = delete;
    ~options_subscription()
    {
        reset();
    }

    void reset() noexcept
    {
        if (auto cancel = std::exchange(cancel_, {}))
        {
            try
            {
                cancel();
            }
            catch (...)
            {
            }
        }
    }

private:
    std::function<void()> cancel_;
};

/**
 * @brief Published snapshot of one options section.
 *
 * current() is safe to call from any thread. Change callbacks run on the
 * thread that performs the reload, after the new snapshot is published.
 */
export template <class T>
class options_monitor
{
public:
    explicit options_monitor(std::shared_ptr<const T> initial)
        : state_(std::make_shared<state>())
    {
        state_->current = std::move(initial);
    }

    /**
     * @brief Returns the current immutable snapshot.
     */
    [[nodiscard]] auto current() const -> std::shared_ptr<const T>
    {
        concurrent_containers::shared_latch_guard guard{state_->latch};
        return state_->current;
    }

    /**
     * @brief Registers a callback for subsequent publications.
     */
    [[nodiscard]] auto on_change(std::function<void(const T&)> callback)
        -> options_subscription
    {
        if (!callback)
            throw std::invalid_argument("options change callback must not be empty");
        auto shared_callback =
            std::make_shared<const std::function<void(const T&)>>(std::move(callback));
        std::uint64_t id = 0;
        {
            concurrent_containers::exclusive_latch_guard guard{state_->latch};
            id = state_->next_id++;
            state_->subscribers.emplace_back(id, std::move(shared_callback));
        }
        return options_subscription{[weak = std::weak_ptr<state>(state_), id]
            {
                if (auto owner = weak.lock())
                {
                    concurrent_containers::exclusive_latch_guard guard{owner->latch};
                    std::erase_if(owner->subscribers,
                        [id](const auto& entry) { return entry.first == id; });
                }
            }};
    }

    /**
     * @brief Publishes a new snapshot and notifies subscribers. Host-internal.
     *
     * Callbacks run outside the latch, so they may read current() or cancel
     * their own subscription.
     */
    void publish(std::shared_ptr<const T> next)
    {
        subscriber_list callbacks;
        {
            concurrent_containers::exclusive_latch_guard guard{state_->latch};
            state_->current = next;
            callbacks = state_->subscribers;
        }
        for (const auto& [id, callback] : callbacks)
        {
            (void)id;
            try
            {
                (*callback)(*next);
            }
            catch (...)
            {
                // A failing observer must not block publication to others.
            }
        }
    }

private:
    using subscriber_list = std::vector<std::pair<std::uint64_t,
        std::shared_ptr<const std::function<void(const T&)>>>>;

    /**
     * Guarded by the cnetmod reader/writer latch; held only for pointer and
     * vector bookkeeping, never across a callback or suspension point.
     */
    struct state
    {
        mutable concurrent_containers::atomic_rw_latch latch;
        std::shared_ptr<const T> current;
        subscriber_list subscribers;
        std::uint64_t next_id = 1;
    };

    std::shared_ptr<state> state_;
};

namespace detail {

    /**
     * @brief Rejects keys that do not exist in the defaults document.
     *
     * Empty objects in the defaults represent maps and accept any key.
     */
    auto reject_unknown_keys(const cnetmod::json::document& provided,
        const cnetmod::json::document& defaults, const std::string& path)
        -> std::expected<void, configuration_error>;

    /**
     * @brief Overlays provided values onto defaults; nested mappings merge.
     */
    void overlay(cnetmod::json::document& target,
        const cnetmod::json::document& provided);

    /**
     * @brief Decodes a section with defaults and strict key checking.
     */
    template <class T>
    [[nodiscard]] auto decode_section(std::string_view name,
        const cnetmod::json::document* provided,
        const options_validator<T>& validator)
        -> std::expected<std::shared_ptr<const T>, configuration_error>
    {
        const std::string path{name};
        auto defaults = cnetmod::json::to_document(T{}, true);
        if (!defaults)
            return std::unexpected(configuration_error{
                .code = defaults.error(),
                .path = path,
                .message = "section type cannot be serialized"});
        auto merged = *defaults;
        if (provided != nullptr)
        {
            if (!provided->is_object())
                return std::unexpected(
                    configuration_error{.path = path, .message = "expected a mapping"});
            if (auto known = reject_unknown_keys(*provided, *defaults, path); !known)
                return std::unexpected(known.error());
            overlay(merged, *provided);
        }
        auto decoded = cnetmod::json::from_document<T>(merged);
        if (!decoded)
            return std::unexpected(configuration_error{
                .code = decoded.error(),
                .path = path,
                .message = std::format("invalid section: {}", decoded.error().message())});
        if (validator)
        {
            if (auto valid = validator(*decoded); !valid)
                return std::unexpected(configuration_error{
                    .path = path, .message = std::move(valid.error())});
        }
        return std::make_shared<const T>(std::move(*decoded));
    }

} // namespace detail

/**
 * @brief Type-erased section declaration owned by the registry.
 */
class options_section
{
public:
    virtual ~options_section() = default;
    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
    [[nodiscard]] virtual auto reload_policy() const noexcept -> options_reload = 0;
    /// Decodes the initial value; `provided` is null when the section is absent.
    [[nodiscard]] virtual auto bind(const cnetmod::json::document* provided)
        -> std::expected<void, configuration_error> = 0;
    /// Validates a reload candidate and returns the publication step.
    [[nodiscard]] virtual auto stage(const cnetmod::json::document* provided)
        -> std::expected<std::function<void()>, configuration_error> = 0;
    virtual void register_components(component_collection& components) = 0;
};

template <class T>
class typed_options_section final : public options_section
{
public:
    typed_options_section(std::string name, bool required)
        : name_(std::move(name)), required_(required)
    {
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override
    {
        return name_;
    }

    [[nodiscard]] auto reload_policy() const noexcept -> options_reload override
    {
        return reload_;
    }

    [[nodiscard]] auto bind(const cnetmod::json::document* provided)
        -> std::expected<void, configuration_error> override
    {
        if (provided == nullptr && required_)
            return std::unexpected(configuration_error{
                .code = std::make_error_code(std::errc::no_such_file_or_directory),
                .path = name_,
                .message = "required section is missing"});
        auto decoded = detail::decode_section<T>(name_, provided, validator_);
        if (!decoded)
            return std::unexpected(decoded.error());
        monitor_ = std::make_shared<options_monitor<T>>(std::move(*decoded));
        return {};
    }

    [[nodiscard]] auto stage(const cnetmod::json::document* provided)
        -> std::expected<std::function<void()>, configuration_error> override
    {
        if (provided == nullptr && required_)
            return std::unexpected(configuration_error{
                .code = std::make_error_code(std::errc::no_such_file_or_directory),
                .path = name_,
                .message = "required section is missing"});
        auto decoded = detail::decode_section<T>(name_, provided, validator_);
        if (!decoded)
            return std::unexpected(decoded.error());
        return [monitor = monitor_, next = std::move(*decoded)]() mutable
        {
            monitor->publish(std::move(next));
        };
    }

    void register_components(component_collection& components) override
    {
        components.instance(monitor_, name_);
    }

    /**
     * @brief Bound monitor; null until bind() succeeded.
     */
    [[nodiscard]] auto monitor() const noexcept
        -> const std::shared_ptr<options_monitor<T>>&
    {
        return monitor_;
    }

    options_validator<T> validator_;
    options_reload reload_ = options_reload::restart_required;

private:
    std::string name_;
    bool required_;
    std::shared_ptr<options_monitor<T>> monitor_;
};

/**
 * @brief Fluent declaration returned by options_registry::section().
 */
export template <class T>
class options_section_builder
{
public:
    explicit options_section_builder(typed_options_section<T>& section) noexcept
        : section_(&section)
    {
    }

    /**
     * @brief Adds cross-field validation run after decoding.
     */
    auto validate(options_validator<T> validator) -> options_section_builder&
    {
        section_->validator_ = std::move(validator);
        return *this;
    }

    /**
     * @brief Declares whether changes may be applied without restart.
     */
    auto reload(options_reload policy) noexcept -> options_section_builder&
    {
        section_->reload_ = policy;
        return *this;
    }

private:
    typed_options_section<T>* section_;
};

/**
 * @brief Result of applying a configuration reload to options sections.
 */
export struct options_reload_outcome
{
    std::vector<std::string> published;
    std::vector<std::string> restart_required;
};

/**
 * @brief Declares, binds and republishes typed configuration sections.
 */
export class options_registry
{
public:
    /**
     * @brief Declares a section. Sections are optional unless `required`.
     *
     * T must be default-constructible and JSON-mapped through cnetmod.json;
     * absent keys take T's default member values.
     */
    template <class T>
    requires std::default_initializable<T>
    auto section(std::string name, bool required = false)
        -> options_section_builder<T>
    {
        if (name.empty())
            throw std::invalid_argument("options section name must not be empty");
        if (std::ranges::any_of(sections_,
                [&name](const auto& item) { return item->name() == name; }))
            throw std::logic_error(std::format(
                "options section '{}' is declared more than once", name));
        auto declared = std::make_unique<typed_options_section<T>>(
            std::move(name), required);
        auto& reference = *declared;
        sections_.push_back(std::move(declared));
        return options_section_builder<T>{reference};
    }

    /**
     * @brief Returns the current value of a bound section.
     *
     * Intended for the registration phase, where modules register components
     * conditionally on configuration. Components that must observe reloads
     * resolve options_monitor<T> instead of keeping this snapshot.
     *
     * @throws std::out_of_range when the section is not declared
     * @throws std::logic_error when T differs from the declared type or the
     *         section is not bound yet
     */
    template <class T>
    [[nodiscard]] auto current(std::string_view name) const
        -> std::shared_ptr<const T>
    {
        for (const auto& section : sections_)
        {
            if (section->name() != name)
                continue;
            const auto* typed =
                dynamic_cast<const typed_options_section<T>*>(section.get());
            if (typed == nullptr)
                throw std::logic_error(std::format(
                    "options section '{}' is declared with a different type", name));
            if (!typed->monitor())
                throw std::logic_error(
                    std::format("options section '{}' is not bound yet", name));
            return typed->monitor()->current();
        }
        throw std::out_of_range(
            std::format("options section '{}' is not declared", name));
    }

    /**
     * @brief Decodes every declared section and rejects unclaimed sections.
     */
    [[nodiscard]] auto bind(const application_configuration& configuration)
        -> std::expected<void, build_error>;

    /**
     * @brief Registers each section's options_monitor<T>, named by section.
     */
    void register_components(component_collection& components);

    /**
     * @brief Validates and applies changed sections from a reload candidate.
     *
     * Validation of every changed section completes before any publication.
     */
    [[nodiscard]] auto reload(const application_configuration& candidate,
        std::span<const std::string> changed)
        -> std::expected<options_reload_outcome, configuration_error>;

private:
    std::vector<std::unique_ptr<options_section>> sections_;
};

} // namespace cnetmod::application
