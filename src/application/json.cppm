/**
 * @brief Application-managed asynchronous JSON parsing and serialization.
 */
export module cnetmod.application.json;

import std;
import nlohmann.json;
import cnetmod.application.runtime;
import cnetmod.coro.task;

namespace cnetmod::application {

/**
 * @brief Parses JSON on the application CPU pool.
 * @param runtime Application-owned execution facade.
 * @param text Owned input retained until parsing completes.
 * @return Parsed JSON or a classified portable error.
 */
export auto parse_offloaded(application_runtime& runtime, std::string text)
    -> task<std::expected<nlohmann::json, std::error_code>>;

/**
 * @brief Serializes JSON on the application CPU pool.
 * @param runtime Application-owned execution facade.
 * @param value Owned document retained until serialization completes.
 * @param indentation Negative for compact output, otherwise indentation width.
 * @return Serialized JSON or a classified portable error.
 */
export auto dump_offloaded(application_runtime& runtime, nlohmann::json value,
    int indentation = -1)
    -> task<std::expected<std::string, std::error_code>>;

} // namespace cnetmod::application
