module cnetmod.instrumentation.error;

import std;
import cnetmod.core.error;
import cnetmod.instrumentation.operation_result;

namespace cnetmod::instrumentation {

auto classify_error(std::error_code error) noexcept -> operation_result
{
    if (!error)
        return {operation_status::success, error};
    if (error == std::errc::operation_canceled || error == make_error_code(errc::operation_aborted))
        return {operation_status::cancelled, error};
    if (error == std::errc::timed_out || error == make_error_code(errc::connection_timed_out))
        return {operation_status::timeout, error};
    return {operation_status::error, error};
}

} // namespace cnetmod::instrumentation
