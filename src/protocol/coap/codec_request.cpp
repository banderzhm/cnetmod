module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :codec_request;

import std;
import :types;
import :codec;

namespace cnetmod::coap {

auto make_request(request_options opts) -> message {
    message msg;
    msg.type = opts.type;
    msg.set_method(opts.method_code);

    if (!opts.path.empty()) {
        std::size_t start = 0;
        while (start < opts.path.size()) {
            while (start < opts.path.size() && opts.path[start] == '/') {
                ++start;
            }
            const auto end = opts.path.find('/', start);
            const auto count = end == std::string::npos ? opts.path.size() - start : end - start;
            if (count > 0) {
                msg.add_string_option(option_number::uri_path, std::string_view{opts.path}.substr(start, count));
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    if (!opts.query.empty()) {
        std::size_t start = 0;
        while (start <= opts.query.size()) {
            const auto end = opts.query.find('&', start);
            const auto count = end == std::string::npos ? opts.query.size() - start : end - start;
            if (count > 0) {
                msg.add_string_option(option_number::uri_query, std::string_view{opts.query}.substr(start, count));
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    if (opts.content_type) {
        msg.add_uint_option(option_number::content_format, static_cast<std::uint16_t>(*opts.content_type));
    }
    if (opts.accept) {
        msg.add_uint_option(option_number::accept, static_cast<std::uint16_t>(*opts.accept));
    }
    if (opts.observe) {
        msg.add_uint_option(option_number::observe, *opts.observe);
    }
    msg.payload = std::move(opts.payload);
    return msg;
}

auto extract_path(const message& msg) -> std::string {
    std::string path;
    for (const auto& opt : msg.options) {
        if (opt.number == static_cast<std::uint16_t>(option_number::uri_path)) {
            path.push_back('/');
            path += opt.as_string();
        }
    }
    return path.empty() ? "/" : path;
}

auto extract_query(const message& msg) -> std::string {
    std::string query;
    for (const auto& opt : msg.options) {
        if (opt.number == static_cast<std::uint16_t>(option_number::uri_query)) {
            if (!query.empty()) {
                query.push_back('&');
            }
            query += opt.as_string();
        }
    }
    return query;
}

} // namespace cnetmod::coap
