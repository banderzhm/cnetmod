module cnetmod.application.mongodb_credentials;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.protocol.mongodb;

namespace cnetmod::application::mongodb_credentials::detail {
auto read_token_file(const std::filesystem::path& path) -> result<std::string>
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
        return std::unexpected(make_error(error_code::authentication_failed, "cloud token file is not a regular readable file"));
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > 1024U * 1024U)
        return std::unexpected(make_error(error_code::authentication_failed, "cloud token file has invalid size"));
    std::ifstream input(path, std::ios::binary);
    std::string value(static_cast<std::size_t>(size), '\0');
    if (!input.read(value.data(), value.size()) || input.peek() != std::char_traits<char>::eof())
        return std::unexpected(make_error(error_code::authentication_failed, "cloud token file changed or could not be read"));
    while (value.ends_with('\n') || value.ends_with('\r'))
        value.pop_back();
    if (value.empty() || value.find('\0') != std::string::npos)
        return std::unexpected(make_error(error_code::authentication_failed, "cloud token file is empty or malformed"));
    return value;
}
} // namespace cnetmod::application::mongodb_credentials::detail

#endif
