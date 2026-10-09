/**
 * @brief Backward-compatible observability umbrella.
 *
 * Application internals import the narrow telemetry or messaging modules.
 * Consumers may keep importing cnetmod.observability to receive both APIs.
 */
export module cnetmod.observability;

export import cnetmod.observability.telemetry;
export import cnetmod.observability.messaging;
