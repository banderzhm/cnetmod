module cnetmod.protocol.websocket;
import std;
import :server;

namespace cnetmod::ws {
namespace detail {
auto parse_ws_pattern(std::string_view pattern) -> std::vector<seg> {
  std::vector<seg> result;
  if (pattern.starts_with('/'))
    pattern.remove_prefix(1);
  while (!pattern.empty()) {
    const auto slash = pattern.find('/');
    const auto part = slash == std::string_view::npos
                          ? pattern
                          : pattern.substr(0, slash);
    if (part.starts_with(':'))
      result.push_back({seg_kind::param, std::string(part.substr(1))});
    else if (part.starts_with('*')) {
      result.push_back({seg_kind::wildcard,
                        std::string(part.size() > 1 ? part.substr(1) : "path")});
      break;
    } else
      result.push_back({seg_kind::exact, std::string(part)});
    if (slash == std::string_view::npos)
      break;
    pattern.remove_prefix(slash + 1);
  }
  return result;
}

auto split_ws_path(std::string_view path) -> std::vector<std::string_view> {
  std::vector<std::string_view> result;
  if (path.starts_with('/'))
    path.remove_prefix(1);
  while (!path.empty()) {
    const auto slash = path.find('/');
    if (slash == std::string_view::npos) {
      result.push_back(path);
      break;
    }
    if (slash != 0)
      result.push_back(path.substr(0, slash));
    path.remove_prefix(slash + 1);
  }
  return result;
}

auto ws_route_params::get(std::string_view key) const noexcept
    -> std::string_view {
  const auto it = named.find(std::string(key));
  return it == named.end() ? std::string_view{} : std::string_view{it->second};
}

auto try_ws_match(const std::vector<seg> &segments,
                  const std::vector<std::string_view> &parts,
                  ws_route_params &out) -> bool {
  std::size_t part_index = 0;
  for (const auto &segment : segments) {
    if (segment.kind == seg_kind::wildcard) {
      std::string rest;
      for (std::size_t i = part_index; i < parts.size(); ++i) {
        if (!rest.empty())
          rest.push_back('/');
        rest.append(parts[i]);
      }
      out.wildcard = std::move(rest);
      out.named[segment.value] = out.wildcard;
      return true;
    }
    if (part_index >= parts.size())
      return false;
    if (segment.kind == seg_kind::exact) {
      if (parts[part_index] != segment.value)
        return false;
    } else
      out.named[segment.value] = parts[part_index];
    ++part_index;
  }
  return part_index == parts.size();
}
} // namespace detail

ws_context::ws_context(connection &conn, std::string path,
                       http::header_map headers, std::string query,
                       detail::ws_route_params params)
    : conn_(conn), path_(std::move(path)), headers_(std::move(headers)),
      query_(std::move(query)), params_(std::move(params)) {}

auto ws_context::path() const noexcept -> std::string_view { return path_; }

auto ws_context::query_string() const noexcept -> std::string_view {
  return query_;
}

auto ws_context::headers() const noexcept -> const http::header_map & {
  return headers_;
}

auto ws_context::get_header(std::string_view key) const -> std::string_view {
  const auto it = headers_.find(std::string(key));
  return it == headers_.end() ? std::string_view{} : std::string_view{it->second};
}

auto ws_context::param(std::string_view name) const noexcept
    -> std::string_view {
  return params_.get(name);
}

auto ws_context::send_text(std::string_view text)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await conn_.async_send_text(text);
}

auto ws_context::send_binary(std::span<const std::byte> data)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await conn_.async_send_binary(data);
}

auto ws_context::recv() -> task<std::expected<ws_message, std::error_code>> {
  co_return co_await conn_.async_recv();
}

auto ws_context::close(std::uint16_t code, std::string_view reason)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await conn_.async_close(code, reason);
}

auto ws_context::is_open() const noexcept -> bool { return conn_.is_open(); }

auto ws_context::raw_connection() noexcept -> connection & { return conn_; }

server::server(io_context &ctx) : ctx_(ctx) {}

server::server(server_context &sctx) : ctx_(sctx.accept_io()), sctx_(&sctx) {}

#ifdef CNETMOD_HAS_SSL
void server::set_ssl_context(ssl_context &ssl_ctx) { ssl_ctx_ = &ssl_ctx; }
#endif

auto server::listen(std::string_view host, std::uint16_t port,
                    socket_options opts)
    -> std::expected<void, std::error_code> {
  auto address = ip_address::from_string(host);
  if (!address)
    return std::unexpected(address.error());
  acc_ = std::make_unique<tcp::acceptor>(ctx_);
  opts.reuse_address = true;
  if (auto opened = acc_->open(endpoint{*address, port}, opts); !opened)
    return std::unexpected(opened.error());
  return {};
}

void server::on(std::string_view pattern, ws_handler_fn handler) {
  routes_.push_back(
      {detail::parse_ws_pattern(pattern), std::move(handler)});
}

auto server::run() -> task<void> {
  if (!acc_)
    co_return;
  running_ = true;
  while (running_) {
    auto accepted = co_await async_accept(ctx_, acc_->native_socket());
    if (!accepted) {
      if (!running_)
        break;
      continue;
    }
    if (sctx_) {
      auto &worker = sctx_->next_worker_io();
      spawn_on(worker, handle_connection(std::move(*accepted), worker));
    } else
      spawn(ctx_, handle_connection(std::move(*accepted), ctx_));
  }
}

void server::stop() {
  running_ = false;
  if (acc_)
    acc_->close();
}

auto server::handle_connection(socket client, io_context &io) -> task<void> {
#ifdef CNETMOD_HAS_SSL
  if (ssl_ctx_) {
    connection conn(io);
    auto ar = co_await conn.async_accept_tls(std::move(client), *ssl_ctx_);
    if (!ar)
      co_return;

    std::string path(conn.handshake_path());
    std::string query(conn.handshake_query());
    detail::ws_route_params rp;
    const route_entry *matched = nullptr;
    auto parts = detail::split_ws_path(path);
    for (auto &entry : routes_) {
      detail::ws_route_params tmp;
      if (detail::try_ws_match(entry.segments, parts, tmp)) {
        matched = &entry;
        rp = std::move(tmp);
        break;
      }
    }
    if (!matched) {
      (void)co_await conn.async_close(close_code::policy_violation,
                                      "no websocket route");
      co_return;
    }

    ws_context wctx(conn, std::move(path), conn.handshake_headers(),
                    std::move(query), std::move(rp));
    co_await matched->handler(wctx);
    if (conn.is_open())
      (void)co_await conn.async_close();
    co_return;
  }
#endif

  http::request_parser req_parser;
  std::array<std::byte, 4096> buf{};
  while (!req_parser.ready()) {
    auto rd =
        co_await async_read(io, client, mutable_buffer{buf.data(), buf.size()});
    if (!rd || *rd == 0) {
      client.close();
      co_return;
    }
    auto consumed =
        req_parser.consume(reinterpret_cast<const char *>(buf.data()), *rd);
    if (!consumed) {
      client.close();
      co_return;
    }
  }

  auto uri = req_parser.uri();
  std::string path, query;
  const auto qpos = uri.find('?');
  if (qpos != std::string_view::npos) {
    path = std::string(uri.substr(0, qpos));
    query = std::string(uri.substr(qpos + 1));
  } else
    path = std::string(uri);

  detail::ws_route_params rp;
  const route_entry *matched = nullptr;
  auto parts = detail::split_ws_path(path);
  for (auto &entry : routes_) {
    detail::ws_route_params tmp;
    if (detail::try_ws_match(entry.segments, parts, tmp)) {
      matched = &entry;
      rp = std::move(tmp);
      break;
    }
  }
  if (!matched) {
    http::response resp(http::status::not_found);
    resp.set_body(std::string_view{"404 Not Found"});
    auto data = resp.serialize();
    (void)co_await async_write_all(io, client,
                                   const_buffer{data.data(), data.size()});
    client.close();
    co_return;
  }

  auto accept_key = validate_upgrade_request(req_parser);
  if (!accept_key) {
    http::response resp(http::status::bad_request);
    resp.set_body(std::string_view{"Bad WebSocket handshake"});
    auto data = resp.serialize();
    (void)co_await async_write_all(io, client,
                                   const_buffer{data.data(), data.size()});
    client.close();
    co_return;
  }

  auto upgrade_resp = build_upgrade_response(*accept_key);
  auto resp_data = upgrade_resp.serialize();
  auto wr = co_await async_write_all(
      io, client, const_buffer{resp_data.data(), resp_data.size()});
  if (!wr) {
    client.close();
    co_return;
  }

  connection conn(io);
  conn.attach(std::move(client), true);
  ws_context wctx(conn, std::move(path), req_parser.headers(), std::move(query),
                  std::move(rp));
  co_await matched->handler(wctx);
  if (conn.is_open())
    (void)co_await conn.async_close();
}
} // namespace cnetmod::ws
