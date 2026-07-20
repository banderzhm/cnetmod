module cnetmod.protocol.http;
import std;
import :sse;
import :response;

namespace cnetmod::http::sse
{
    namespace
    {
        void append(std::string& o, std::string_view f, std::string_view v)
        {
            for (std::size_t b = 0;;)
            {
                auto n = v.find('\n', b);
                auto e = n == std::string_view::npos ? v.size() : n;
                o += f;
                o += ": ";
                o.append(v.substr(b, e - b));
                o += '\n';
                if (n == std::string_view::npos)break;
                b = n + 1;
            }
        }
    }

    auto encode(event e) -> std::string
    {
        std::string o;
        bool c = false;
        if (!e.comment.empty())
        {
            c = true;
            append(o, "", e.comment);
        }
        if (!e.id.empty())
        {
            c = true;
            append(o, "id", e.id);
        }
        if (!e.event.empty())
        {
            c = true;
            append(o, "event", e.event);
        }
        if (e.retry)
        {
            c = true;
            o += "retry: " + std::to_string(e.retry->count()) + '\n';
        }
        if (!e.data.empty() || !c)append(o, "data", e.data);
        o += '\n';
        return o;
    }

    auto data(std::string_view p, std::string_view n, std::string_view i) -> std::string
    {
        return encode({.event = std::string(n), .data = std::string(p), .id = std::string(i)});
    }

    auto comment(std::string_view t) -> std::string { return encode({.comment = std::string(t)}); }

    auto heartbeat() -> std::string { return comment("keepalive"); }

    auto done() -> std::string { return data(R"({"done":true})"); }

    void prepare(response& r, response_options o)
    {
        r.set_status(o.status_code);
        r.set_header("Content-Type", "text/event-stream; charset=utf-8");
        if (o.no_cache)r.set_header("Cache-Control", "no-cache, no-transform");
        if (o.keep_alive)r.set_header("Connection", "keep-alive");
        if (o.disable_proxy_buffering)r.set_header("X-Accel-Buffering", "no");
        r.set_header("X-Streamed", "1");
        r.remove_header("Content-Length");
    }

    auto make_response(std::span<const event> e, response_options o) -> response
    {
        response r(o.status_code);
        prepare(r, o);
        std::string b;
        for (auto x : e)b += encode(x);
        r.set_body(std::move(b));
        r.remove_header("Content-Length");
        return r;
    }
}