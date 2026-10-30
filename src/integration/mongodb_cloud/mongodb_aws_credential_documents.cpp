module;
#include <pugixml.hpp>
module cnetmod.integration.mongodb_cloud;
import std;
import cnetmod.json;
import cnetmod.protocol.mongodb;

namespace cnetmod::mongodb::cloud::detail {
auto parse_aws_json_credentials(std::string_view value) -> result<aws_credentials>
{
    auto document = cnetmod::json::parse_document(value);
    if (!document)
        return std::unexpected(make_error(error_code::authentication_failed, "invalid AWS credential JSON"));
    try
    {
        aws_credentials credentials{document->at("AccessKeyId").get<std::string>(), document->at("SecretAccessKey").get<std::string>(),
            document->at("Token").get<std::string>(), {}};
        auto expiry = parse_utc_expiry(document->at("Expiration").get<std::string>());
        if (!expiry)
            return std::unexpected(expiry.error());
        credentials.expires_at = *expiry;
        auto valid = validate_aws_credentials(credentials);
        if (!valid)
            return std::unexpected(valid.error());
        return credentials;
    }
    catch (...)
    {
        return std::unexpected(make_error(error_code::authentication_failed, "incomplete AWS credential JSON"));
    }
}

auto parse_aws_sts_credentials(std::string_view value) -> result<aws_credentials>
{
    pugi::xml_document document;
    if (!document.load_buffer(value.data(), value.size()))
        return std::unexpected(make_error(error_code::authentication_failed, "invalid STS XML response"));
    const auto child = [](pugi::xml_node parent, std::string_view local_name) -> pugi::xml_node
    {
        pugi::xml_node found;
        for (const auto node : parent.children())
        {
            const std::string_view name{node.name()};
            if (name.substr(name.rfind(':') == std::string_view::npos ? 0 : name.rfind(':') + 1) != local_name)
                continue;
            if (found)
                return {}; // Duplicate credential fields are ambiguous.
            found = node;
        }
        return found;
    };
    const auto response = child(document, "AssumeRoleWithWebIdentityResponse");
    const auto result = child(response, "AssumeRoleWithWebIdentityResult");
    const auto node = child(result, "Credentials");
    if (!node)
        return std::unexpected(make_error(error_code::authentication_failed, "STS response has no credentials"));
    aws_credentials credentials{child(node, "AccessKeyId").child_value(), child(node, "SecretAccessKey").child_value(),
        child(node, "SessionToken").child_value(), {}};
    if (credentials.session_token.empty())
        return std::unexpected(make_error(error_code::authentication_failed, "STS credentials have no session token"));
    auto expiry = parse_utc_expiry(child(node, "Expiration").child_value());
    if (!expiry)
        return std::unexpected(expiry.error());
    credentials.expires_at = *expiry;
    auto valid = validate_aws_credentials(credentials);
    if (!valid)
        return std::unexpected(valid.error());
    return credentials;
}
} // namespace cnetmod::mongodb::cloud::detail
