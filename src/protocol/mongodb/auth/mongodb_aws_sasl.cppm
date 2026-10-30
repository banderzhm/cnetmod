export module cnetmod.protocol.mongodb:aws_sasl;
import std;
import :error;
import :bson_document;
import :aws_credentials;

export namespace cnetmod::mongodb {
struct aws_signed_request
{
    std::string authorization;
    std::string amz_date;
};

auto make_aws_client_nonce() -> result<std::array<std::byte, 32>>;
auto aws_first_message(std::span<const std::byte, 32> nonce) -> result<std::vector<std::byte>>;
auto aws_sts_region(std::string_view host) -> result<std::string>;
/// Pure SigV4 operation. Does not contact STS or log its input/output.
auto sign_aws_sts_request(std::string_view host, std::span<const std::byte, 64> nonce,
    const aws_credentials& credentials, std::string_view utc_timestamp) -> result<aws_signed_request>;
auto aws_challenge_response(std::span<const std::byte> challenge,
    std::span<const std::byte, 32> client_nonce, const aws_credentials& credentials,
    std::string_view utc_timestamp) -> result<std::vector<std::byte>>;
} // namespace cnetmod::mongodb
