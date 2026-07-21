module cnetmod.protocol.websocket;

import :connection;
import cnetmod.io.io_context;

namespace cnetmod::ws {

connection::connection(io_context &ctx) noexcept : ctx_(ctx) {}

connection::~connection() { close_socket(); }

connection::connection(connection &&o) noexcept
    : ctx_(o.ctx_), sock_(std::move(o.sock_)), is_server_(o.is_server_),
      connected_(std::exchange(o.connected_, false)),
      close_sent_(std::exchange(o.close_sent_, false)),
      close_received_(std::exchange(o.close_received_, false)),
      recv_buf_(std::move(o.recv_buf_))
#ifdef CNETMOD_HAS_SSL
      ,
      ssl_ctx_(std::move(o.ssl_ctx_)), ssl_(std::move(o.ssl_)),
      secure_(std::exchange(o.secure_, false))
#endif
{
}

auto connection::async_connect(std::string_view url_str,
                               const connect_options &opts)
    -> task<std::expected<void, std::error_code>> {
  auto url_r = http::url::parse(url_str);
  if (!url_r)
    co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
  auto &u = *url_r;

  bool use_ssl = (u.scheme == "wss" || u.scheme == "https");

#ifndef CNETMOD_HAS_SSL
  if (use_ssl) {
    co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
  }
#endif

  auto connect_r = co_await async_connect_happy_eyeballs(ctx_, u.host, u.port);
  if (!connect_r) {
    co_return std::unexpected(connect_r.error());
  }
  sock_ = std::move(connect_r->sock);

  // SSL/TLS
#ifdef CNETMOD_HAS_SSL
  if (use_ssl) {
    auto ssl_ctx_r = ssl_context::client();
    if (!ssl_ctx_r) {
      close_socket();
      co_return std::unexpected(ssl_ctx_r.error());
    }
    ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));
    ssl_ctx_->set_verify_peer(opts.tls_verify);

    if (!opts.tls_ca_file.empty())
      (void)ssl_ctx_->load_ca_file(opts.tls_ca_file);
    else if (opts.tls_verify)
      (void)ssl_ctx_->set_default_ca();

    ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
    ssl_->set_connect_state();
    ssl_->set_hostname(u.host);

    auto hs = co_await ssl_->async_handshake();
    if (!hs) {
      close_socket();
      co_return std::unexpected(hs.error());
    }
    secure_ = true;
  }
#endif

  // WebSocket handshake
  auto sec_key = generate_sec_key();
  auto expected_accept = compute_accept_key(sec_key);

  // Host header with port (for non-default ports)
  auto host_header = http::format_authority(
      u.host, u.port, u.scheme == "wss" || u.scheme == "https");

  auto req = build_upgrade_request(host_header, u.path, sec_key,
                                   opts.subprotocol, opts.origin);
  auto req_data = req.serialize();

  auto wr = co_await async_write_all(req_data.data(), req_data.size());
  if (!wr) {
    close_socket();
    co_return std::unexpected(wr.error());
  }

  // Read response
  http::response_parser resp_parser;
  while (!resp_parser.ready()) {
    auto buf = recv_buf_.prepare(4096);
    auto rd = co_await async_read_some(static_cast<char *>(buf.data), buf.size);
    if (!rd || *rd == 0) {
      close_socket();
      co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
    }
    recv_buf_.commit(*rd);

    auto readable = recv_buf_.data();
    auto consumed = resp_parser.consume(
        static_cast<const char *>(readable.data), readable.size);
    if (!consumed) {
      close_socket();
      co_return std::unexpected(consumed.error());
    }
    recv_buf_.consume(*consumed);
  }

  auto vr = validate_upgrade_response(resp_parser, expected_accept);
  if (!vr) {
    close_socket();
    co_return std::unexpected(vr.error());
  }

  is_server_ = false;
  connected_ = true;
  co_return {};
}

auto connection::async_accept(socket client_sock)
    -> task<std::expected<void, std::error_code>> {
  sock_ = std::move(client_sock);
  reset_server_request_metadata();

  // Read client HTTP upgrade request
  http::request_parser req_parser;
  while (!req_parser.ready()) {
    auto buf = recv_buf_.prepare(4096);
    auto rd = co_await async_read_some(static_cast<char *>(buf.data), buf.size);
    if (!rd || *rd == 0) {
      close_socket();
      co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
    }
    recv_buf_.commit(*rd);

    auto readable = recv_buf_.data();
    auto consumed = req_parser.consume(static_cast<const char *>(readable.data),
                                       readable.size);
    if (!consumed) {
      close_socket();
      co_return std::unexpected(consumed.error());
    }
    recv_buf_.consume(*consumed);
  }

  store_server_request_metadata(req_parser);

  auto accept_key = validate_upgrade_request(req_parser);
  if (!accept_key) {
    close_socket();
    co_return std::unexpected(accept_key.error());
  }

  auto resp = build_upgrade_response(*accept_key);
  auto resp_data = resp.serialize();

  auto wr = co_await async_write_all(resp_data.data(), resp_data.size());
  if (!wr) {
    close_socket();
    co_return std::unexpected(wr.error());
  }

  is_server_ = true;
  connected_ = true;
  co_return {};
}

auto connection::async_accept_tls(socket client_sock, ssl_context &ssl_ctx)
    -> task<std::expected<void, std::error_code>> {
  sock_ = std::move(client_sock);
  reset_server_request_metadata();

  ssl_ = std::make_unique<ssl_stream>(ssl_ctx, ctx_, sock_);
  ssl_->set_accept_state();

  auto hs = co_await ssl_->async_handshake();
  if (!hs) {
    close_socket();
    co_return std::unexpected(hs.error());
  }
  secure_ = true;

  // Then perform WebSocket handshake (reuse async_accept logic)
  http::request_parser req_parser;
  while (!req_parser.ready()) {
    auto buf = recv_buf_.prepare(4096);
    auto rd = co_await async_read_some(static_cast<char *>(buf.data), buf.size);
    if (!rd || *rd == 0) {
      close_socket();
      co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
    }
    recv_buf_.commit(*rd);

    auto readable = recv_buf_.data();
    auto consumed = req_parser.consume(static_cast<const char *>(readable.data),
                                       readable.size);
    if (!consumed) {
      close_socket();
      co_return std::unexpected(consumed.error());
    }
    recv_buf_.consume(*consumed);
  }

  store_server_request_metadata(req_parser);

  auto accept_key = validate_upgrade_request(req_parser);
  if (!accept_key) {
    close_socket();
    co_return std::unexpected(accept_key.error());
  }

  auto resp = build_upgrade_response(*accept_key);
  auto resp_data = resp.serialize();

  auto wr = co_await async_write_all(resp_data.data(), resp_data.size());
  if (!wr) {
    close_socket();
    co_return std::unexpected(wr.error());
  }

  is_server_ = true;
  connected_ = true;
  co_return {};
}

auto connection::async_send_text(std::string_view text)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await async_send(
      opcode::text,
      std::span{reinterpret_cast<const std::byte *>(text.data()), text.size()});
}

auto connection::async_send_binary(std::span<const std::byte> data)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await async_send(opcode::binary, data);
}

auto connection::async_ping(std::span<const std::byte> payload)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await async_send(opcode::ping, payload);
}

auto connection::async_send(opcode op, std::span<const std::byte> payload)
    -> task<std::expected<void, std::error_code>> {
  if (!connected_)
    co_return std::unexpected(make_error_code(ws_errc::not_connected));
  if (close_sent_)
    co_return std::unexpected(make_error_code(ws_errc::already_closed));

  bool do_mask = !is_server_; // Client must mask
  build_frame_into(send_buf_, op, payload, do_mask);
  co_return co_await async_write_all(
      reinterpret_cast<const char *>(send_buf_.data()), send_buf_.size());
}

auto connection::async_recv()
    -> task<std::expected<ws_message, std::error_code>> {
  if (!connected_)
    co_return std::unexpected(make_error_code(ws_errc::not_connected));

  ws_message msg;
  bool first_frame = true;

  for (;;) {
    // Ensure recv_buf_ has data
    auto readable = recv_buf_.data();
    auto hdr_r = parse_frame_header(std::span{
        static_cast<const std::byte *>(readable.data), readable.size});

    if (!hdr_r && hdr_r.error() == make_error_code(ws_errc::need_more_data)) {
      // Read more data
      auto buf = recv_buf_.prepare(4096);
      auto rd =
          co_await async_read_some(static_cast<char *>(buf.data), buf.size);
      if (!rd || *rd == 0) {
        connected_ = false;
        co_return std::unexpected(make_error_code(errc::connection_reset));
      }
      recv_buf_.commit(*rd);
      continue;
    }

    if (!hdr_r)
      co_return std::unexpected(hdr_r.error());

    auto &[hdr, hdr_size] = *hdr_r;
    auto total_frame = hdr_size + hdr.payload_length;

    // Ensure complete frame is available
    while (recv_buf_.readable_bytes() < total_frame) {
      auto buf = recv_buf_.prepare(4096);
      auto rd =
          co_await async_read_some(static_cast<char *>(buf.data), buf.size);
      if (!rd || *rd == 0) {
        connected_ = false;
        co_return std::unexpected(make_error_code(errc::connection_reset));
      }
      recv_buf_.commit(*rd);
    }

    // Extract payload
    readable = recv_buf_.data();
    auto payload_ptr = static_cast<const std::byte *>(readable.data) + hdr_size;
    std::vector<std::byte> payload_data(payload_ptr,
                                        payload_ptr + hdr.payload_length);

    // Unmask
    if (hdr.masked)
      apply_mask(payload_data, hdr.masking_key);

    recv_buf_.consume(total_frame);

    // Handle control frames
    if (is_control(hdr.op)) {
      co_await handle_control_frame(hdr, payload_data);
      if (hdr.op == opcode::close) {
        close_received_ = true;
        // Build close message to return
        ws_message close_msg;
        close_msg.op = opcode::close;
        close_msg.payload = std::move(payload_data);
        co_return close_msg;
      }
      continue; // ping/pong already handled, continue reading
    }

    // Data frame
    if (first_frame) {
      msg.op = hdr.op;
      first_frame = false;
      if (hdr.fin) {
        msg.payload = std::move(payload_data);
        co_return msg;
      }
    }

    msg.payload.insert(msg.payload.end(), payload_data.begin(),
                       payload_data.end());

    if (hdr.fin) {
      co_return msg;
    }
    // Otherwise continue reading continuation frames
  }
}

auto connection::async_close(std::uint16_t code, std::string_view reason)
    -> task<std::expected<void, std::error_code>> {
  if (!connected_)
    co_return std::unexpected(make_error_code(ws_errc::not_connected));
  if (close_sent_)
    co_return std::unexpected(make_error_code(ws_errc::already_closed));

  bool do_mask = !is_server_;
  auto frame_data = build_close_frame(code, reason, do_mask);

  auto wr = co_await async_write_all(
      reinterpret_cast<const char *>(frame_data.data()), frame_data.size());
  if (!wr)
    co_return std::unexpected(wr.error());

  close_sent_ = true;

  // If peer hasn't sent close yet, wait for it
  if (!close_received_) {
    auto msg = co_await async_recv();
    // Regardless of success/failure, connection should close
  }

#ifdef CNETMOD_HAS_SSL
  if (secure_ && ssl_) {
    (void)co_await ssl_->async_shutdown();
  }
#endif
  close_socket();
  connected_ = false;
  co_return {};
}

void connection::attach(socket sock, bool as_server) noexcept {
  close_socket();
  sock_ = std::move(sock);
  is_server_ = as_server;
  connected_ = true;
  close_sent_ = false;
  close_received_ = false;
}

void connection::set_cancel_token(cancel_token *t) noexcept {
  cancel_token_ = t;
}

void connection::clear_cancel_token() noexcept { cancel_token_ = nullptr; }

[[nodiscard]] auto connection::is_open() const noexcept -> bool {
  return connected_;
}

[[nodiscard]] auto connection::is_server() const noexcept -> bool {
  return is_server_;
}

[[nodiscard]] auto connection::handshake_path() const noexcept
    -> std::string_view {
  return handshake_path_;
}

[[nodiscard]] auto connection::handshake_query() const noexcept
    -> std::string_view {
  return handshake_query_;
}

[[nodiscard]] auto connection::handshake_headers() const noexcept
    -> const http::header_map & {
  return handshake_headers_;
}

[[nodiscard]] auto connection::is_secure() const noexcept -> bool {
  return secure_;
}

auto connection::async_read_some(char *buf, std::size_t len)
    -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
  if (secure_ && ssl_) {
    co_return co_await ssl_->async_read(mutable_buffer{buf, len});
  }
#endif
  if (cancel_token_)
    co_return co_await cnetmod::async_read(
        ctx_, sock_, mutable_buffer{buf, len}, *cancel_token_);
  co_return co_await cnetmod::async_read(ctx_, sock_, mutable_buffer{buf, len});
}

auto connection::async_write_all(const char *data, std::size_t len)
    -> task<std::expected<void, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
  if (secure_ && ssl_) {
    co_return co_await ssl_->async_write_all(const_buffer{data, len});
  }
#endif
  if (cancel_token_) {
    co_return co_await cnetmod::async_write_all(
        ctx_, sock_, const_buffer{data, len}, *cancel_token_);
  }
  co_return co_await cnetmod::async_write_all(ctx_, sock_,
                                              const_buffer{data, len});
}

auto connection::handle_control_frame(const frame_header &hdr,
                                      const std::vector<std::byte> &payload)
    -> task<void> {
  if (hdr.op == opcode::ping) {
    // Auto-reply with pong
    bool do_mask = !is_server_;
    auto pong = build_frame(opcode::pong, payload, do_mask);
    (void)co_await async_write_all(reinterpret_cast<const char *>(pong.data()),
                                   pong.size());
  } else if (hdr.op == opcode::close && !close_sent_) {
    // Auto-reply with close
    bool do_mask = !is_server_;
    auto close_reply = build_frame(opcode::close, payload, do_mask);
    (void)co_await async_write_all(
        reinterpret_cast<const char *>(close_reply.data()), close_reply.size());
    close_sent_ = true;
  }
}

void connection::close_socket() noexcept {
#ifdef CNETMOD_HAS_SSL
  ssl_.reset();
  ssl_ctx_.reset();
  secure_ = false;
#endif
  sock_.close();
}

void connection::reset_server_request_metadata() {
  handshake_path_.clear();
  handshake_query_.clear();
  handshake_headers_.clear();
}

void connection::store_server_request_metadata(
    const http::request_parser &req_parser) {
  auto uri = req_parser.uri();
  auto qpos = uri.find('?');
  if (qpos != std::string_view::npos) {
    handshake_path_ = std::string(uri.substr(0, qpos));
    handshake_query_ = std::string(uri.substr(qpos + 1));
  } else {
    handshake_path_ = std::string(uri);
    handshake_query_.clear();
  }
  handshake_headers_ = req_parser.headers();
}

} // namespace cnetmod::ws
