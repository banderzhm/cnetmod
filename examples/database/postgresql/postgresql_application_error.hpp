#pragma once

namespace postgresql_example {

enum class application_error_code
{
    invalid_request,
    resource_not_found,
    service_unavailable,
    operation_forbidden
};

struct empty_response final
{
};

template <class T>
using R = cnetmod::utils::R<T, application_error_code>;

using application_error = cnetmod::utils::application_error<application_error_code>;

} // namespace postgresql_example
