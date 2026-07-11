export module cnetmod.protocol.http:sse;
import std;
import :semantics;
import :response;
export namespace cnetmod::http::sse {
struct event { std::string event,data,id; std::optional<std::chrono::milliseconds> retry; std::string comment; };
struct response_options { int status_code=status::ok; bool no_cache=true,keep_alive=true,disable_proxy_buffering=true; };
auto encode(event)->std::string; auto data(std::string_view,std::string_view event_name={},std::string_view id={})->std::string; auto comment(std::string_view)->std::string; auto heartbeat()->std::string; auto done()->std::string; void prepare(response&,response_options opts={}); auto make_response(std::span<const event>,response_options opts={})->response;
}
