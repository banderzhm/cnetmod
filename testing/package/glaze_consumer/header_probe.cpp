#include <compare>
#include <glaze/version.hpp>

auto glaze_header_is_available() -> bool
{
    return glz::version.major >= 8;
}
