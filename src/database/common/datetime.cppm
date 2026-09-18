export module cnetmod.database.datetime;

import std;
import cnetmod.database.sql_query_data;

export namespace cnetmod::database {

/**
 * Converts Unix seconds to a timezone-free database datetime interpreted as
 * UTC.
 *
 * The conversion fails when the timestamp is outside the calendar range
 * supported by SQL datetime values.
 */
[[nodiscard]] auto datetime_from_unix_seconds(std::int64_t value) noexcept
    -> std::optional<calendar_datetime>;

/**
 * Interprets a timezone-free database datetime as UTC and converts it to Unix
 * seconds.
 *
 * The conversion fails when any calendar or clock component is invalid.
 * Sub-second precision is intentionally discarded.
 */
[[nodiscard]] auto unix_seconds_from_datetime(
    const calendar_datetime& value) noexcept
    -> std::optional<std::int64_t>;

/**
 * Converts an optional database datetime while preserving SQL NULL.
 */
[[nodiscard]] auto unix_seconds_from_datetime(
    const std::optional<calendar_datetime>& value) noexcept
    -> std::optional<std::int64_t>;

} // namespace cnetmod::database
