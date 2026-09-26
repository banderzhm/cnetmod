/**
 * @brief Structured diagnostics for application composition failures.
 *
 * Every failure produced by application_builder::build() identifies the
 * composition phase, the component that failed, the configuration path when
 * one applies, and a human-readable message. The error code remains available
 * for programmatic classification.
 */
export module cnetmod.application.diagnostics;

import std;
import cnetmod.application.configuration;

namespace cnetmod::application {

/**
 * @brief Named composition phases, executed strictly in declaration order.
 */
export enum class build_phase
{
    /// Loading, environment expansion and validation of framework settings.
    configuration,
    /// Binding application sections to typed options.
    options,
    /// Managed services, auto-configuration and module service registration.
    registration,
    /// Managed-service dependency graph validation.
    validation,
    /// Construction of every registered component.
    resolution,
    /// Routes and middleware contributed by modules.
    composition,
};

/**
 * @brief Returns the lowercase phase name used in diagnostics.
 */
export [[nodiscard]] constexpr auto to_string(build_phase phase) noexcept
    -> std::string_view
{
    switch (phase)
    {
    case build_phase::configuration:
        return "configuration";
    case build_phase::options:
        return "options";
    case build_phase::registration:
        return "registration";
    case build_phase::validation:
        return "validation";
    case build_phase::resolution:
        return "resolution";
    case build_phase::composition:
        return "composition";
    }
    return "unknown";
}

/**
 * @brief One composition failure.
 */
export struct build_error
{
    build_phase phase = build_phase::configuration;
    /// Module, service or type that failed; empty for whole-document errors.
    std::string component;
    /// Configuration path when the failure is tied to a document location.
    std::string path;
    std::string message;
    std::error_code code = std::make_error_code(std::errc::invalid_argument);

    /**
     * @brief Formats "phase [component] path: message".
     */
    [[nodiscard]] auto describe() const -> std::string
    {
        std::string result{to_string(phase)};
        if (!component.empty())
            result += std::format(" [{}]", component);
        if (!path.empty())
            result += std::format(" {}", path);
        result += ": ";
        result += message.empty() ? code.message() : message;
        return result;
    }

    /**
     * @brief Wraps a configuration failure.
     */
    [[nodiscard]] static auto from(const configuration_error& error,
        build_phase phase = build_phase::configuration) -> build_error
    {
        return build_error{.phase = phase,
            .path = error.path,
            .message = error.message,
            .code = error.code};
    }
};

} // namespace cnetmod::application
