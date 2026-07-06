module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:sse;

import std;
import :types;
import :response;

namespace cnetmod::http::sse {

export struct event {
    std::string event;
    std::string data;
    std::string id;
    std::optional<std::chrono::milliseconds> retry;
    std::string comment;
};

export struct response_options {
    int status_code = status::ok;
    bool no_cache = true;
    bool keep_alive = true;
    bool disable_proxy_buffering = true;
};

namespace detail {

inline void append_field_lines(std::string& out,
                               std::string_view field,
                               std::string_view value) {
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto nl = value.find('\n', begin);
        const auto end = (nl == std::string_view::npos) ? value.size() : nl;
        out += field;
        out += ": ";
        out.append(value.substr(begin, end - begin));
        out += '\n';
        if (nl == std::string_view::npos) break;
        begin = nl + 1;
    }
}

} // namespace detail

export auto encode(event ev) -> std::string {
    std::string out;
    out.reserve(ev.data.size() + ev.event.size() + ev.id.size() + 32);

    bool has_control_field = false;
    if (!ev.comment.empty()) {
        has_control_field = true;
        std::size_t begin = 0;
        while (begin <= ev.comment.size()) {
            const auto nl = ev.comment.find('\n', begin);
            const auto end = (nl == std::string::npos) ? ev.comment.size() : nl;
            out += ": ";
            out.append(std::string_view(ev.comment).substr(begin, end - begin));
            out += '\n';
            if (nl == std::string::npos) break;
            begin = nl + 1;
        }
    }
    if (!ev.id.empty()) {
        has_control_field = true;
        detail::append_field_lines(out, "id", ev.id);
    }
    if (!ev.event.empty()) {
        has_control_field = true;
        detail::append_field_lines(out, "event", ev.event);
    }
    if (ev.retry) {
        has_control_field = true;
        out += "retry: ";
        out += std::to_string(ev.retry->count());
        out += '\n';
    }
    if (!ev.data.empty() || !has_control_field) {
        detail::append_field_lines(out, "data", ev.data);
    }
    out += '\n';
    return out;
}

export auto data(std::string_view payload,
                 std::string_view event_name = {},
                 std::string_view id = {}) -> std::string {
    return encode(event{
        .event = std::string(event_name),
        .data = std::string(payload),
        .id = std::string(id),
    });
}

export auto comment(std::string_view text) -> std::string {
    return encode(event{.comment = std::string(text)});
}

export auto heartbeat() -> std::string {
    return comment("keepalive");
}

export auto done() -> std::string {
    return data(R"({"done":true})");
}

export void prepare(response& resp, response_options opts = {}) {
    resp.set_status(opts.status_code);
    resp.set_header("Content-Type", "text/event-stream; charset=utf-8");
    if (opts.no_cache) {
        resp.set_header("Cache-Control", "no-cache, no-transform");
    }
    if (opts.keep_alive) {
        resp.set_header("Connection", "keep-alive");
    }
    if (opts.disable_proxy_buffering) {
        resp.set_header("X-Accel-Buffering", "no");
    }
    resp.set_header("X-Streamed", "1");
    resp.remove_header("Content-Length");
}

export auto make_response(std::span<const event> events,
                          response_options opts = {}) -> response {
    response resp(opts.status_code);
    prepare(resp, opts);
    std::string body;
    for (auto ev : events) {
        body += encode(std::move(ev));
    }
    resp.set_body(std::move(body));
    resp.remove_header("Content-Length");
    return resp;
}

} // namespace cnetmod::http::sse
