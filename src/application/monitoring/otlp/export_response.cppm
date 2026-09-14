/**
 * @brief Validates bounded OTLP JSON acknowledgements independently of transport.
 */
export module cnetmod.observability.export_response;

import std;

namespace cnetmod::observability::detail {

/**
 * @brief Collector acknowledgement in wire-record units, never producer calls.
 */
export struct export_acknowledgement
{
    std::uint64_t rejected{};
    bool partial{};
    bool warning{};
};

/**
 * @brief Parses an acknowledgement without retaining diagnostic text or a JSON DOM.
 * @param rejected_field Signal-specific lower-camel-case rejection field.
 * @param sent Number of spans, log records, or metric data points in the request.
 * @return No value for malformed, oversized, or inconsistent acknowledgements.
 *
 * Parsing is limited to 64 KiB and 16 container levels. This limit does not
 * replace the transport's receive-size limit. Invalid responses must not retry.
 */
export [[nodiscard]] auto parse_export_response(std::string_view body,
    std::string_view rejected_field, std::uint64_t sent) noexcept
    -> std::optional<export_acknowledgement>;

} // namespace cnetmod::observability::detail
