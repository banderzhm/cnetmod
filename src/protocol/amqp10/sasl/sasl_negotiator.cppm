module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:sasl_negotiator;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;
import :primitive_value;

export namespace cnetmod::amqp10 {
enum class sasl_code : std::uint8_t
{
    ok = 0,
    auth = 1,
    sys = 2,
    sys_permanent = 3,
    sys_temporary = 4
};

struct sasl_mechanisms
{
    std::vector<symbol> mechanisms;
};

struct sasl_init
{
    symbol mechanism;
    binary initial_response;
    std::string hostname;
};

struct sasl_challenge
{
    binary challenge;
};

struct sasl_response
{
    binary response;
};

struct sasl_outcome
{
    sasl_code code = sasl_code::sys;
    binary additional_data;
};

using sasl_performative =
    std::variant<sasl_mechanisms, sasl_init, sasl_challenge, sasl_response,
        sasl_outcome>;

class sasl_negotiator
{
public:
    explicit sasl_negotiator(credentials credentials);
    ~sasl_negotiator();
    sasl_negotiator(sasl_negotiator&&) noexcept;
    auto operator=(sasl_negotiator&&) noexcept -> sasl_negotiator&;
    sasl_negotiator(const sasl_negotiator&) = delete;
    auto operator=(const sasl_negotiator&) -> sasl_negotiator& = delete;
    [[nodiscard]] auto select(std::span<const symbol> offered,
        std::string_view hostname)
        -> std::expected<sasl_init, error>;
    [[nodiscard]] auto respond(std::span<const std::byte> challenge)
        -> std::expected<sasl_response, error>;
    [[nodiscard]] auto finish(const sasl_outcome&)
        -> std::expected<void, error>;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

[[nodiscard]] auto encode_sasl_performative(const sasl_performative&)
    -> binary;
[[nodiscard]] auto decode_sasl_performative(std::span<const std::byte>)
    -> std::expected<sasl_performative, std::error_code>;
} // namespace cnetmod::amqp10
