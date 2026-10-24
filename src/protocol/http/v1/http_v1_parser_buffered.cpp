module cnetmod.protocol.http;

import std;
import :parser;

namespace cnetmod::http {

auto request_parser::take_buffered_data() -> std::string
{
    if (!ready_)
        return {};
    preserve_headers();
    auto result = std::exchange(buf_, {});
    result.erase(0, std::exchange(read_pos_, 0));
    return result;
}

auto response_parser::take_buffered_data() -> std::string
{
    if (!ready_)
        return {};
    auto result = std::exchange(buf_, {});
    result.erase(0, std::exchange(read_pos_, 0));
    return result;
}

} // namespace cnetmod::http
