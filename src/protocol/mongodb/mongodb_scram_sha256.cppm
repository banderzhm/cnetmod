export module cnetmod.protocol.mongodb:scram_sha256;

import std;
import :error;

export namespace cnetmod::mongodb {

class scram_sha256_client
{
public:
    scram_sha256_client(std::string username, std::string password);
    ~scram_sha256_client();
    scram_sha256_client(scram_sha256_client&&) noexcept;
    auto operator=(scram_sha256_client&&) noexcept -> scram_sha256_client&;
    scram_sha256_client(const scram_sha256_client&) = delete;
    auto operator=(const scram_sha256_client&) -> scram_sha256_client& = delete;

    auto initial_message() -> result<std::vector<std::byte>>;
    auto respond(std::span<const std::byte> server_first)
        -> result<std::vector<std::byte>>;
    auto verify(std::span<const std::byte> server_final) -> result<void>;

private:
    struct implementation;
    std::unique_ptr<implementation> implementation_;
};

} // namespace cnetmod::mongodb
