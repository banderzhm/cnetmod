export module cnetmod.utils:application_result;

import std;

export namespace cnetmod::utils {

template <class ErrorCode>
struct application_error
{
    ErrorCode code;
    std::string message;
    std::string diagnostic;
};

/// Protocol- and transport-neutral application result.
///
/// ErrorCode is owned by the consuming application. This keeps the reusable
/// result independent from HTTP status codes, database errors, and JSON.
template <class T, class ErrorCode = std::int32_t>
class R final
{
public:
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

    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return std::holds_alternative<T>(value_);
    }

    [[nodiscard]] auto error() const noexcept -> bool
    {
        return !ok();
    }

    [[nodiscard]] auto data() const -> const T&
    {
        return std::get<T>(value_);
    }

    [[nodiscard]] auto failure() const -> const application_error<ErrorCode>&
    {
        return std::get<application_error<ErrorCode>>(value_);
    }

    [[nodiscard]] auto message() const -> const std::string&
    {
        return ok() ? success_message_ : failure().message;
    }

private:
    explicit R(T value, std::string message)
        : value_(std::move(value)), success_message_(std::move(message)) {}

    explicit R(application_error<ErrorCode> error)
        : value_(std::move(error)) {}

    std::variant<T, application_error<ErrorCode>> value_;
    std::string success_message_;
};

} // namespace cnetmod::utils
