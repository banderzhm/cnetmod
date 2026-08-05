export module cnetmod.utils:application_result;

import std;

export namespace cnetmod::utils {

namespace detail {

template <class Result, class ErrorCode>
concept compatible_application_result =
    requires { typename std::remove_cvref_t<Result>::error_code_type; }
    && std::same_as<typename std::remove_cvref_t<Result>::error_code_type, ErrorCode>;

} // namespace detail

template <class ErrorCode>
struct application_error
{
    ErrorCode code;
    std::string message;
    std::string diagnostic;
};

/// Protocol- and transport-neutral application result.
///
/// `R` is intended for application outcomes, while transport code continues
/// to use `std::expected<T, std::error_code>`. Convert at the boundary with
/// `from_error_code`, so HTTP/database/protocol details do not leak into a
/// service's public result type.
template <class T, class ErrorCode = std::int32_t>
class R final
{
public:
    using value_type = T;
    using error_code_type = ErrorCode;

    [[nodiscard]] static auto ok(T value, std::string message = "success") -> R
    {
        return R(std::move(value), std::move(message));
    }

    [[nodiscard]] static auto error(ErrorCode code, std::string message,
        std::string diagnostic = {}) -> R
    {
        return R(application_error<ErrorCode>{
            std::move(code), std::move(message), std::move(diagnostic)});
    }

    /// Convert a transport error when this result intentionally uses
    /// std::error_code as its application error-code type.
    [[nodiscard]] static auto from_error_code(std::error_code code,
        std::string message = {}, std::string diagnostic = {}) -> R
        requires std::constructible_from<ErrorCode, std::error_code>
    {
        if (message.empty())
            message = code.message();
        return error(ErrorCode{std::move(code)}, std::move(message),
            std::move(diagnostic));
    }

    /// Convert a transport error with an application-owned mapping policy.
    template <class Mapper>
    [[nodiscard]] static auto from_error_code(std::error_code code, Mapper&& mapper,
        std::string message = {}, std::string diagnostic = {}) -> R
        requires std::invocable<Mapper, const std::error_code&>
              && std::convertible_to<std::invoke_result_t<Mapper,
                     const std::error_code&>, ErrorCode>
    {
        if (message.empty())
            message = code.message();
        return error(std::invoke(std::forward<Mapper>(mapper), code),
            std::move(message), std::move(diagnostic));
    }

    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return std::holds_alternative<T>(value_);
    }

    [[nodiscard]] auto error() const noexcept -> bool
    {
        return !ok();
    }

    [[nodiscard]] auto data() & -> T&
    {
        return std::get<T>(value_);
    }

    [[nodiscard]] auto data() const & -> const T&
    {
        return std::get<T>(value_);
    }

    /// Move the successful value out without an extra copy.
    [[nodiscard]] auto data() && -> T&&
    {
        return std::get<T>(std::move(value_));
    }

    [[nodiscard]] auto failure() & -> application_error<ErrorCode>&
    {
        return std::get<application_error<ErrorCode>>(value_);
    }

    [[nodiscard]] auto failure() const & -> const application_error<ErrorCode>&
    {
        return std::get<application_error<ErrorCode>>(value_);
    }

    [[nodiscard]] auto message() const -> const std::string&
    {
        return ok() ? success_message_ : failure().message;
    }

    template <class F>
    [[nodiscard]] auto map(F&& fn) const &
        -> R<std::remove_cvref_t<std::invoke_result_t<F, const T&>>, ErrorCode>
        requires std::invocable<F, const T&>
    {
        using mapped_type = std::remove_cvref_t<std::invoke_result_t<F, const T&>>;
        static_assert(!std::is_void_v<mapped_type>,
            "Use and_then with R<void, E> for a void-producing operation");
        if (error())
            return R<mapped_type, ErrorCode>::error(
                failure().code, failure().message, failure().diagnostic);
        return R<mapped_type, ErrorCode>::ok(
            std::invoke(std::forward<F>(fn), data()), message());
    }

    template <class F>
    [[nodiscard]] auto map(F&& fn) &&
        -> R<std::remove_cvref_t<std::invoke_result_t<F, T&&>>, ErrorCode>
        requires std::invocable<F, T&&>
    {
        using mapped_type = std::remove_cvref_t<std::invoke_result_t<F, T&&>>;
        static_assert(!std::is_void_v<mapped_type>,
            "Use and_then with R<void, E> for a void-producing operation");
        if (error())
        {
            auto failure_value = std::move(*this).failure();
            return R<mapped_type, ErrorCode>::error(std::move(failure_value.code),
                std::move(failure_value.message), std::move(failure_value.diagnostic));
        }
        return R<mapped_type, ErrorCode>::ok(
            std::invoke(std::forward<F>(fn), std::move(*this).data()),
            std::move(success_message_));
    }

    template <class F>
    [[nodiscard]] auto and_then(F&& fn) const & -> std::invoke_result_t<F, const T&>
        requires std::invocable<F, const T&>
              && detail::compatible_application_result<
                  std::invoke_result_t<F, const T&>, ErrorCode>
    {
        using next_type = std::invoke_result_t<F, const T&>;
        if (error())
            return next_type::error(failure().code, failure().message,
                failure().diagnostic);
        return std::invoke(std::forward<F>(fn), data());
    }

    template <class F>
    [[nodiscard]] auto and_then(F&& fn) && -> std::invoke_result_t<F, T&&>
        requires std::invocable<F, T&&>
              && detail::compatible_application_result<std::invoke_result_t<F, T&&>,
                  ErrorCode>
    {
        using next_type = std::invoke_result_t<F, T&&>;
        if (error())
        {
            auto failure_value = std::move(*this).failure();
            return next_type::error(std::move(failure_value.code),
                std::move(failure_value.message), std::move(failure_value.diagnostic));
        }
        return std::invoke(std::forward<F>(fn), std::move(*this).data());
    }

    template <class F>
    [[nodiscard]] auto map_error(F&& fn) const &
        -> R<T, std::remove_cvref_t<std::invoke_result_t<F, const ErrorCode&>>>
        requires std::invocable<F, const ErrorCode&>
    {
        using mapped_error = std::remove_cvref_t<std::invoke_result_t<F,
            const ErrorCode&>>;
        if (ok())
            return R<T, mapped_error>::ok(data(), message());
        return R<T, mapped_error>::error(
            std::invoke(std::forward<F>(fn), failure().code), failure().message,
            failure().diagnostic);
    }

    template <class F>
    [[nodiscard]] auto map_error(F&& fn) &&
        -> R<T, std::remove_cvref_t<std::invoke_result_t<F, ErrorCode&&>>>
        requires std::invocable<F, ErrorCode&&>
    {
        using mapped_error = std::remove_cvref_t<std::invoke_result_t<F, ErrorCode&&>>;
        if (ok())
            return R<T, mapped_error>::ok(std::move(*this).data(),
                std::move(success_message_));
        auto failure_value = std::move(*this).failure();
        return R<T, mapped_error>::error(
            std::invoke(std::forward<F>(fn), std::move(failure_value.code)),
            std::move(failure_value.message), std::move(failure_value.diagnostic));
    }

private:
    explicit R(T value, std::string message)
        : value_(std::move(value)), success_message_(std::move(message)) {}

    explicit R(application_error<ErrorCode> error)
        : value_(std::move(error)) {}

    std::variant<T, application_error<ErrorCode>> value_;
    std::string success_message_;
};

template <class ErrorCode>
class R<void, ErrorCode> final
{
public:
    using value_type = void;
    using error_code_type = ErrorCode;

    /// Construct a successful result without a payload. This is deliberately
    /// named `success` rather than `ok`: C++ cannot overload a static `ok()`
    /// factory and the instance predicate `result.ok()`.
    [[nodiscard]] static auto success(std::string message = "success") -> R
    {
        return R(std::move(message));
    }

    [[nodiscard]] static auto error(ErrorCode code, std::string message,
        std::string diagnostic = {}) -> R
    {
        return R(application_error<ErrorCode>{
            std::move(code), std::move(message), std::move(diagnostic)});
    }

    [[nodiscard]] static auto from_error_code(std::error_code code,
        std::string message = {}, std::string diagnostic = {}) -> R
        requires std::constructible_from<ErrorCode, std::error_code>
    {
        if (message.empty())
            message = code.message();
        return error(ErrorCode{std::move(code)}, std::move(message),
            std::move(diagnostic));
    }

    template <class Mapper>
    [[nodiscard]] static auto from_error_code(std::error_code code, Mapper&& mapper,
        std::string message = {}, std::string diagnostic = {}) -> R
        requires std::invocable<Mapper, const std::error_code&>
              && std::convertible_to<std::invoke_result_t<Mapper,
                     const std::error_code&>, ErrorCode>
    {
        if (message.empty())
            message = code.message();
        return error(std::invoke(std::forward<Mapper>(mapper), code),
            std::move(message), std::move(diagnostic));
    }

    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return std::holds_alternative<std::monostate>(value_);
    }

    [[nodiscard]] auto error() const noexcept -> bool
    {
        return !ok();
    }

    [[nodiscard]] auto failure() & -> application_error<ErrorCode>&
    {
        return std::get<application_error<ErrorCode>>(value_);
    }

    [[nodiscard]] auto failure() const & -> const application_error<ErrorCode>&
    {
        return std::get<application_error<ErrorCode>>(value_);
    }

    [[nodiscard]] auto message() const -> const std::string&
    {
        return ok() ? success_message_ : failure().message;
    }

    template <class F>
    [[nodiscard]] auto map(F&& fn) const &
        -> R<std::remove_cvref_t<std::invoke_result_t<F>>, ErrorCode>
        requires std::invocable<F>
    {
        using mapped_type = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(!std::is_void_v<mapped_type>,
            "Use and_then with R<void, E> for a void-producing operation");
        if (error())
            return R<mapped_type, ErrorCode>::error(
                failure().code, failure().message, failure().diagnostic);
        return R<mapped_type, ErrorCode>::ok(std::invoke(std::forward<F>(fn)),
            message());
    }

    template <class F>
    [[nodiscard]] auto and_then(F&& fn) const & -> std::invoke_result_t<F>
        requires std::invocable<F>
              && detail::compatible_application_result<std::invoke_result_t<F>, ErrorCode>
    {
        using next_type = std::invoke_result_t<F>;
        if (error())
            return next_type::error(failure().code, failure().message,
                failure().diagnostic);
        return std::invoke(std::forward<F>(fn));
    }

    template <class F>
    [[nodiscard]] auto map_error(F&& fn) const &
        -> R<void, std::remove_cvref_t<std::invoke_result_t<F,
            const ErrorCode&>>>
        requires std::invocable<F, const ErrorCode&>
    {
        using mapped_error = std::remove_cvref_t<std::invoke_result_t<F,
            const ErrorCode&>>;
        if (ok())
            return R<void, mapped_error>::success(message());
        return R<void, mapped_error>::error(
            std::invoke(std::forward<F>(fn), failure().code), failure().message,
            failure().diagnostic);
    }

    template <class F>
    [[nodiscard]] auto map_error(F&& fn) &&
        -> R<void, std::remove_cvref_t<std::invoke_result_t<F, ErrorCode&&>>>
        requires std::invocable<F, ErrorCode&&>
    {
        using mapped_error = std::remove_cvref_t<std::invoke_result_t<F, ErrorCode&&>>;
        if (ok())
            return R<void, mapped_error>::success(std::move(success_message_));
        auto failure_value = std::move(*this).failure();
        return R<void, mapped_error>::error(
            std::invoke(std::forward<F>(fn), std::move(failure_value.code)),
            std::move(failure_value.message), std::move(failure_value.diagnostic));
    }

private:
    explicit R(std::string message)
        : value_(std::monostate{}), success_message_(std::move(message)) {}

    explicit R(application_error<ErrorCode> error)
        : value_(std::move(error)) {}

    std::variant<std::monostate, application_error<ErrorCode>> value_;
    std::string success_message_;
};

} // namespace cnetmod::utils
