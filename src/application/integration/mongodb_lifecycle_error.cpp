module cnetmod.application.mongodb;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;

namespace cnetmod::application {
namespace mongodb_application_detail {
    /**
     * Preserves MongoDB failure identity without publishing server diagnostics.
     * Values are offset by one because the protocol enum has no success entry.
     */
    class mongodb_category final : public std::error_category
    {
    public:
        auto name() const noexcept -> const char* override
        {
            return "cnetmod.mongodb";
        }

        auto message(int value) const -> std::string override
        {
            return std::format("MongoDB error {}", value);
        }

        auto default_error_condition(int value) const noexcept -> std::error_condition override
        {
            if (value <= 0 || value > static_cast<int>(mongodb::error_code::connection_closed) + 1)
                return {value, *this};
            switch (static_cast<mongodb::error_code>(value - 1))
            {
            case mongodb::error_code::invalid_bson:
            case mongodb::error_code::protocol_error:
                return std::make_error_condition(std::errc::protocol_error);
            case mongodb::error_code::message_too_large:
                return std::make_error_condition(std::errc::message_size);
            case mongodb::error_code::authentication_failed:
                return std::make_error_condition(std::errc::permission_denied);
            case mongodb::error_code::connection_failed:
            case mongodb::error_code::tls_failed:
                return std::make_error_condition(std::errc::io_error);
            case mongodb::error_code::connection_closed:
                return std::make_error_condition(std::errc::not_connected);
            case mongodb::error_code::pool_exhausted:
                return std::make_error_condition(std::errc::resource_unavailable_try_again);
            default:
                return {value, *this};
            }
        }
    };

    auto lifecycle_error(mongodb::error_code code) noexcept -> std::error_code
    {
        if (code == mongodb::error_code::operation_cancelled)
            return std::make_error_code(std::errc::operation_canceled);
        if (code == mongodb::error_code::operation_timed_out)
            return std::make_error_code(std::errc::timed_out);
        static const mongodb_category category;
        return {static_cast<int>(code) + 1, category};
    }

} // namespace mongodb_application_detail
} // namespace cnetmod::application
#endif
