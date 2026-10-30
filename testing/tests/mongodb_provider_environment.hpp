#pragma once

namespace mongodb_test {
struct provider_environment
{
    std::map<std::string, std::optional<std::string>> saved;

    provider_environment()
    {
        for (const auto key : {"AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY", "AWS_SESSION_TOKEN",
                 "AWS_WEB_IDENTITY_TOKEN_FILE", "AWS_ROLE_ARN", "AWS_ROLE_SESSION_NAME", "AWS_REGION", "AWS_DEFAULT_REGION",
                 "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "AWS_CONTAINER_CREDENTIALS_FULL_URI",
                 "AWS_CONTAINER_AUTHORIZATION_TOKEN", "AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE", "AWS_EC2_METADATA_DISABLED",
                 "AZURE_FEDERATED_TOKEN_FILE", "OIDC_TOKEN_FILE"})
        {
            const auto previous = std::getenv(key);
            saved.emplace(key, previous ? std::optional<std::string>{previous} : std::nullopt);
            set(key, {});
        }
    }

    static void set(const std::string& key, std::optional<std::string> value)
    {
#ifdef _WIN32
        _putenv_s(key.c_str(), value ? value->c_str() : "");
#else
        if (value)
            setenv(key.c_str(), value->c_str(), 1);
        else
            unsetenv(key.c_str());
#endif
    }

    ~provider_environment()
    {
        for (const auto& [key, value] : saved)
            set(key, value);
    }
};

struct token_file
{
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("cnetmod-token-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / "token";

    explicit token_file(std::string_view token)
    {
        std::filesystem::create_directory(directory);
        std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
        std::ofstream output(path, std::ios::binary);
        output << token;
    }

    ~token_file()
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
} // namespace mongodb_test
