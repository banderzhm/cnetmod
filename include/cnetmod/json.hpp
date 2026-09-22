#pragma once

/**
 * @brief Marks an aggregate DTO as JSON serializable.
 *
 * Glaze reflects public aggregate fields directly. This declaration marker
 * intentionally emits no metadata or intermediate mapping code.
 */
#define CNETMOD_JSON(TYPE, ...)

/**
 * @brief Names a field in a CNETMOD_JSON declaration marker.
 */
#define CNETMOD_JSON_FIELD(MEMBER)
