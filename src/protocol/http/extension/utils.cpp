module cnetmod.protocol.http;
import std;
import :utils;
import :router;
namespace cnetmod::http {
auto resolve_client_ip(const request_context& ctx,std::string_view fallback)->std::string{if(auto x=ctx.get_header("X-Forwarded-For");!x.empty()){auto p=x.substr(0,x.find(','));while(!p.empty()&&p.front()==' ')p.remove_prefix(1);while(!p.empty()&&p.back()==' ')p.remove_suffix(1);if(!p.empty())return std::string(p);}if(auto x=ctx.get_header("X-Real-IP");!x.empty())return std::string(x);return std::string(fallback);}
auto parse_query_param(std::string_view q,std::string_view key)->std::string{for(std::size_t p=0;p<q.size();){auto a=q.find('&',p);auto s=q.substr(p,a==std::string_view::npos?q.size()-p:a-p);auto e=s.find('=');if(e!=std::string_view::npos&&s.substr(0,e)==key)return std::string(s.substr(e+1));if(a==std::string_view::npos)break;p=a+1;}return {};}
auto parse_query_params(std::string_view q)->std::unordered_map<std::string,std::string>{std::unordered_map<std::string,std::string> r;for(std::size_t p=0;p<q.size();){auto a=q.find('&',p);auto s=q.substr(p,a==std::string_view::npos?q.size()-p:a-p);auto e=s.find('=');if(e!=std::string_view::npos)r.insert_or_assign(std::string(s.substr(0,e)),std::string(s.substr(e+1)));if(a==std::string_view::npos)break;p=a+1;}return r;}
}
