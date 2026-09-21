export module cnetmod.database.datetime;

import std;
import cnetmod.database.sql_query_data;

export namespace cnetmod::database {

/**
 * @brief Converts Unix seconds to a timezone-free UTC database datetime.
 *
 * @return The converted datetime, or std::nullopt when the instant cannot be
 * represented by the database calendar range 0001-01-01 through 9999-12-31.
 */
[[nodiscard]] auto datetime_from_unix_seconds(std::int64_t value) noexcept
    -> std::optional<calendar_datetime>;

/**
 * @brief Interprets a timezone-free database datetime as UTC Unix seconds.
 *
 * Fractional seconds are intentionally discarded because the result is
 * expressed in whole seconds.
 *
 * @return The converted timestamp, or std::nullopt when the calendar value is
 * invalid.
 */
[[nodiscard]] auto unix_seconds_from_datetime(
    const calendar_datetime& value) noexcept
    -> std::optional<std::int64_t>;

/**
 * @brief Converts an optional UTC database datetime without inventing a
 * sentinel value for SQL NULL.
 */
[[nodiscard]] auto unix_seconds_from_datetime(
    const std::optional<calendar_datetime>& value) noexcept
    -> std::optional<std::int64_t>;

} // namespace cnetmod::database
