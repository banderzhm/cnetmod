module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.sasl_authenticator;
import std;
import cnetmod.protocol.kafka.protocol_constants;

export namespace cnetmod::kafka {
class sasl_authenticator
{
public:
    virtual ~sasl_authenticator() = default;
    [[nodiscard]] virtual auto mechanism_name() const noexcept
        -> std::string_view = 0;
    virtual auto initial_response() -> result<bytes> = 0;
    virtual auto challenge(std::span<const std::byte>) -> result<bytes> = 0;
    virtual auto complete() const noexcept -> bool = 0;
};

[[nodiscard]] auto make_plain_authenticator(std::string, std::string)
    -> std::unique_ptr<sasl_authenticator>;
[[nodiscard]] auto
    make_scram_authenticator(sasl_mechanism, std::string, std::string,
        std::shared_ptr<scram_crypto_provider>)
        -> result<std::unique_ptr<sasl_authenticator>>;
} // namespace cnetmod::kafka
