module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :protocol_error;
import std;

namespace cnetmod::amqp10 {
namespace {
    class category final : public std::error_category
    {
    public:
        auto name() const noexcept -> const char* override
        {
            return "amqp1.0";
        }

        auto message(int v) const -> std::string override
        {
            switch (static_cast<errc>(v))
            {
            case errc::invalid_field:
                return "invalid AMQP field";
            case errc::malformed_frame:
                return "malformed AMQP frame";
            case errc::unexpected_performative:
                return "unexpected AMQP performative";
            case errc::frame_size_too_small:
                return "AMQP frame is too small";
            case errc::frame_size_too_large:
                return "AMQP frame exceeds negotiated maximum";
            case errc::idle_timeout:
                return "AMQP idle timeout expired";
            case errc::link_credit_exhausted:
                return "AMQP link credit exhausted";
            case errc::delivery_rejected:
                return "AMQP delivery rejected";
            case errc::authentication_failed:
                return "AMQP SASL authentication failed";
            case errc::protocol_state:
                return "invalid AMQP protocol state";
            case errc::connection_closed:
                return "AMQP connection closed";
            case errc::cancelled:
                return "AMQP operation cancelled";
            case errc::transaction_failed:
                return "AMQP transaction failed";
            }
            return "unknown AMQP error";
        }
    };
} // namespace

auto error_category() noexcept -> const std::error_category&
{
    static category c;
    return c;
}

auto make_error_code(errc v) noexcept -> std::error_code
{
    return {static_cast<int>(v), error_category()};
}

auto make_error(error_stage s, errc c, std::string m, bool r)
    -> error
{
    return {.stage = s,
        .code = make_error_code(c),
        .message = std::move(m),
        .retryable = r};
}
} // namespace cnetmod::amqp10
