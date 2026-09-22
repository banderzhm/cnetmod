/**
 * @brief Converts YAML configuration documents to the Application JSON model.
 *
 * This adapter owns the YAML dependency boundary.  Configuration validation,
 * secret expansion, redaction, and reload semantics remain centralized in
 * cnetmod.application.configuration after conversion.
 */
export module cnetmod.application.yaml_configuration;

import std;
import cnetmod.json;

namespace cnetmod::application {

/**
 * @brief Loads one YAML mapping as a JSON-compatible configuration document.
 * @param path YAML file path.
 * @return A JSON object, or an error for unreadable, malformed, cyclic,
 * duplicate-key, or non-object documents.
 *
 * YAML anchors are accepted only when their expansion stays within the bounded
 * document depth.  The conversion never performs environment expansion;
 * load_configuration() performs that consistently for JSON and YAML inputs.
 */
export [[nodiscard]] auto load_yaml_configuration_document(
    const std::filesystem::path& path)
    -> std::expected<cnetmod::json::document, std::error_code>;

} // namespace cnetmod::application
