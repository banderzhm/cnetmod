module;

#include <cnetmod/config.hpp>
#include <cstring>

module cnetmod.protocol.mysql;

import std;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :connection_client;

namespace cnetmod::mysql {

client::client(io_context &ctx) noexcept : ctx_(ctx) {}

auto client::connect(connect_options opts) -> task<result_set> {
  result_set err_rs;
  last_io_ec_.clear();
  last_opts_ = opts;

  // TCP connection
  auto connect_r =
      co_await async_connect_happy_eyeballs(ctx_, opts.host, opts.port);
  if (!connect_r) {
    err_rs.error_msg = "connect: " + connect_r.error().message();
    err_rs.diag.assign_client(err_rs.error_msg);
    co_return err_rs;
  }
  sock_ = std::move(connect_r->sock);

  // Server Greeting
  auto greeting_pkt = co_await read_packet();
  if (greeting_pkt.empty()) {
    sock_.close();
    err_rs.error_msg = "no server greeting";
    co_return err_rs;
  }

  auto greeting_r =
      detail::parse_server_greeting(greeting_pkt.data(), greeting_pkt.size());
  if (!greeting_r) {
    sock_.close();
    err_rs.error_msg = "greeting: " + greeting_r.error();
    co_return err_rs;
  }
  auto &greeting = *greeting_r;
  server_caps_ = greeting.capabilities;
  auth_plugin_ = greeting.auth_plugin_name;
  auth_scramble_.assign(greeting.auth_data.begin(), greeting.auth_data.end());

  // Capability negotiation
  std::uint32_t client_caps =
      CLIENT_LONG_PASSWORD | CLIENT_FOUND_ROWS | CLIENT_LONG_FLAG |
      CLIENT_PROTOCOL_41 | CLIENT_TRANSACTIONS | CLIENT_SECURE_CONNECTION |
      CLIENT_PLUGIN_AUTH | CLIENT_PLUGIN_AUTH_LENENC | CLIENT_DEPRECATE_EOF |
      CLIENT_MULTI_RESULTS | CLIENT_PS_MULTI_RESULTS;

  if (!opts.database.empty())
    client_caps |= CLIENT_CONNECT_WITH_DB;
  if (opts.multi_statements)
    client_caps |= CLIENT_MULTI_STATEMENTS;

  client_caps &= server_caps_;
  if (!(client_caps & CLIENT_PROTOCOL_41)) {
    sock_.close();
    err_rs.error_msg = "server does not support protocol 4.1";
    co_return err_rs;
  }

  // TLS — ssl_mode: disable / enable / require
  bool want_tls = (opts.ssl != ssl_mode::disable);
  bool server_has_ssl = (server_caps_ & CLIENT_SSL) != 0;

  if (opts.ssl == ssl_mode::require && !server_has_ssl) {
    sock_.close();
    err_rs.error_msg = "ssl_mode::require but server does not support SSL";
    err_rs.diag.assign_client(err_rs.error_msg);
    co_return err_rs;
  }

#ifdef CNETMOD_HAS_SSL
  if (want_tls && server_has_ssl) {
    client_caps |= CLIENT_SSL;

    auto ssl_req = detail::build_ssl_request(client_caps);
    seq_ = 1;
    auto wr = co_await write_packet(ssl_req, seq_++);
    if (!wr) {
      sock_.close();
      err_rs.error_msg = "failed to send SSL request";
      co_return err_rs;
    }

    auto ssl_ctx_r = ssl_context::client();
    if (!ssl_ctx_r) {
      sock_.close();
      err_rs.error_msg = "ssl context: " + ssl_ctx_r.error().message();
      co_return err_rs;
    }
    ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));
    ssl_ctx_->set_verify_peer(opts.tls_verify);

    if (!opts.tls_ca_file.empty())
      (void)ssl_ctx_->load_ca_file(opts.tls_ca_file);
    else if (opts.tls_verify)
      (void)ssl_ctx_->set_default_ca();
    if (!opts.tls_cert_file.empty())
      (void)ssl_ctx_->load_cert_file(opts.tls_cert_file);
    if (!opts.tls_key_file.empty())
      (void)ssl_ctx_->load_key_file(opts.tls_key_file);

    ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
    ssl_->set_connect_state();
    ssl_->set_hostname(opts.host);

    auto hs = co_await ssl_->async_handshake();
    if (!hs) {
      sock_.close();
      err_rs.error_msg = "ssl handshake: " + hs.error().message();
      co_return err_rs;
    }
    secure_channel_ = true;
  }
#else
  if (opts.ssl == ssl_mode::require) {
    sock_.close();
    err_rs.error_msg = "SSL not available (compiled without OpenSSL)";
    err_rs.diag.assign_client(err_rs.error_msg);
    co_return err_rs;
  }
#endif

  client_caps_ = client_caps;
  password_ = opts.password;

  // Password hash
  auto auth_response = detail::hash_password_for_plugin(
      greeting.auth_plugin_name, opts.password,
      std::span<const std::uint8_t>(greeting.auth_data.data(),
                                    greeting.auth_data.size()));

  // Login packet
  auto login_pkt =
      detail::build_login_packet(opts, greeting, client_caps, auth_response);
  auto lw = co_await write_packet(login_pkt, seq_++);
  if (!lw) {
    sock_.close();
    err_rs.error_msg = "failed to send login";
    co_return err_rs;
  }

  // Authentication response loop
  auto auth_rs = co_await handle_auth_response(opts.password);
  if (auth_rs.is_err()) {
    sock_.close();
    co_return auth_rs;
  }

  connected_ = true;
  co_return auth_rs;
}

auto client::query(std::string_view sql) -> task<result_set> {
  result_set err_rs;
  if (!connected_) {
    err_rs.error_msg = "not connected";
    co_return err_rs;
  }

  seq_ = 0;
  auto pkt = detail::build_query_command(sql);
  auto wr = co_await write_packet(pkt, seq_++);
  if (!wr) {
    err_rs.error_msg = last_io_ec_ ? std::format("failed to send query: {}",
                                                 last_io_ec_.message())
                                   : "failed to send query";
    co_return err_rs;
  }

  co_return co_await read_result_set(false);
}

auto client::execute(std::string_view sql) -> task<result_set> {
  return query(sql);
}

auto client::execute(with_params_t wp) -> task<result_set> {
  result_set err_rs;
  if (!connected_) {
    err_rs.error_msg = "not connected";
    co_return err_rs;
  }

  auto sql_r = format_sql(format_opts_, wp.query, wp.args);
  if (!sql_r) {
    err_rs.error_msg = "format_sql error";
    err_rs.diag.assign_client(err_rs.error_msg);
    co_return err_rs;
  }

  co_return co_await query(*sql_r);
}

auto client::current_format_opts() const noexcept -> const format_options & {
  return format_opts_;
}

auto client::start_execution(std::string_view sql, execution_state &st)
    -> task<void> {
  if (!connected_) {
    st.set_error(0, "not connected");
    co_return;
  }

  seq_ = 0;
  auto pkt = detail::build_query_command(sql);
  auto wr = co_await write_packet(pkt, seq_++);
  if (!wr) {
    st.set_error(0, last_io_ec_ ? std::format("failed to send query: {}",
                                              last_io_ec_.message())
                                : "failed to send query");
    co_return;
  }

  co_await read_resultset_head_impl(st, false);
}

auto client::start_execution(with_params_t wp, execution_state &st)
    -> task<void> {
  if (!connected_) {
    st.set_error(0, "not connected");
    co_return;
  }

  auto sql_r = format_sql(format_opts_, wp.query, wp.args);
  if (!sql_r) {
    st.set_error(0, "format_sql error");
    co_return;
  }

  co_return co_await start_execution(*sql_r, st);
}

auto client::read_some_rows(execution_state &st) -> task<std::vector<row>> {
  std::vector<row> batch;
  if (!st.should_read_rows())
    co_return batch;

  // Read a batch of rows (max 100 rows or until EOF)
  for (int i = 0; i < 100; ++i) {
    auto row_pkt = co_await read_packet();
    if (row_pkt.empty()) {
      st.set_error(0, "connection lost");
      break;
    }

    // EOF/OK = result set end
    if (row_pkt[0] == EOF_HEADER && row_pkt.size() < 9) {
      auto ok = detail::parse_ok_packet(row_pkt.data(), row_pkt.size());
      st.set_ok_data(ok.affected_rows, ok.last_insert_id, ok.warnings,
                     ok.status_flags, ok.info);
      if (st.has_more_results()) {
        st.set_state(execution_state::state_t::reading_head);
      } else {
        st.set_state(execution_state::state_t::complete);
      }
      break;
    }
    if (row_pkt[0] == ERR_HEADER) {
      auto ep = detail::parse_err_packet(row_pkt.data(), row_pkt.size());
      st.set_error(ep.error_code, ep.message);
      break;
    }

    // Parse row data
    batch.push_back(
        detail::parse_text_row(row_pkt.data(), row_pkt.size(), st.columns()));
  }

  co_return batch;
}

auto client::read_resultset_head(execution_state &st) -> task<void> {
  if (!st.should_read_head())
    co_return;
  co_await read_resultset_head_impl(st, false);
}

auto client::run_pipeline(const pipeline_request &req,
                          std::vector<stage_response> &responses)
    -> task<void> {
  responses.clear();
  responses.resize(req.size());

  if (!connected_) {
    for (auto &r : responses)
      r.set_error(0, "not connected");
    co_return;
  }

  // Send and read responses one by one
  for (std::size_t i = 0; i < req.size(); ++i) {
    auto &stage = req.stages()[i];
    auto &resp = responses[i];

    switch (stage.kind) {
    case stage_kind::execute:
    case stage_kind::set_character_set: {
      auto rs = co_await query(stage.sql);
      if (rs.is_err())
        resp.set_error(rs.error_code, rs.error_msg);
      else
        resp.set_results(std::move(rs));
      break;
    }
    case stage_kind::prepare: {
      auto stmt_r = co_await prepare(stage.sql);
      if (stmt_r)
        resp.set_statement(*stmt_r);
      else
        resp.set_error(0, stmt_r.error());
      break;
    }
    case stage_kind::close_statement: {
      statement s;
      s.id = stage.stmt_id;
      co_await close_stmt(s);
      resp.set_ok();
      break;
    }
    case stage_kind::reset_connection: {
      auto rs = co_await reset_connection();
      if (rs.is_err())
        resp.set_error(rs.error_code, rs.error_msg);
      else
        resp.set_ok();
      break;
    }
    }
  }
}

auto client::prepare(std::string_view sql)
    -> task<std::expected<statement, std::string>> {
  if (!connected_)
    co_return std::unexpected(std::string("not connected"));

  seq_ = 0;
  auto pkt = detail::build_prepare_stmt_command(sql);
  auto wr = co_await write_packet(pkt, seq_++);
  if (!wr)
    co_return std::unexpected(std::string("failed to send prepare"));

  auto resp = co_await read_packet();
  if (resp.empty())
    co_return std::unexpected(std::string("no prepare response"));

  if (resp[0] == ERR_HEADER) {
    auto ep = detail::parse_err_packet(resp.data(), resp.size());
    co_return std::unexpected(ep.message);
  }

  auto pr = detail::parse_prepare_stmt_response(resp.data(), resp.size());
  if (!pr)
    co_return std::unexpected(pr.error());

  statement stmt;
  stmt.id = pr->stmt_id;
  stmt.num_params = pr->num_params;
  stmt.num_columns = pr->num_columns;

  // Skip param column definition packets
  bool has_deprecate_eof = (client_caps_ & CLIENT_DEPRECATE_EOF) != 0;
  if (stmt.num_params > 0) {
    for (std::uint16_t i = 0; i < stmt.num_params; ++i)
      (void)co_await read_packet();
    if (!has_deprecate_eof)
      (void)co_await read_packet(); // EOF
  }

  // Skip column definition packets
  if (stmt.num_columns > 0) {
    for (std::uint16_t i = 0; i < stmt.num_columns; ++i)
      (void)co_await read_packet();
    if (!has_deprecate_eof)
      (void)co_await read_packet(); // EOF
  }

  co_return stmt;
}

auto client::execute_stmt(const statement &stmt,
                          std::span<const param_value> params)
    -> task<result_set> {
  result_set err_rs;
  if (!connected_) {
    err_rs.error_msg = "not connected";
    co_return err_rs;
  }
  if (!stmt.valid()) {
    err_rs.error_msg = "invalid statement";
    co_return err_rs;
  }

  seq_ = 0;
  auto pkt = detail::build_execute_stmt_command(stmt.id, params);
  auto wr = co_await write_packet(pkt, seq_++);
  if (!wr) {
    err_rs.error_msg = "failed to send execute";
    co_return err_rs;
  }

  co_return co_await read_result_set(true);
}

auto client::close_stmt(const statement &stmt) -> task<void> {
  if (!connected_ || !stmt.valid())
    co_return;

  seq_ = 0;
  auto pkt = detail::build_close_stmt_command(stmt.id);
  (void)co_await write_packet(pkt, seq_++);
  // COM_STMT_CLOSE has no response
}

auto client::ping() -> task<result_set> {
  result_set err_rs;
  if (!connected_) {
    err_rs.error_msg = "not connected";
    co_return err_rs;
  }

  seq_ = 0;
  std::vector<std::uint8_t> pkt{COM_PING};
  auto wr = co_await write_packet(pkt, seq_++);
  if (!wr) {
    err_rs.error_msg = last_io_ec_ ? std::format("failed to send ping: {}",
                                                 last_io_ec_.message())
                                   : "failed to send ping";
    co_return err_rs;
  }

  auto resp = co_await read_packet();
  if (resp.empty()) {
    err_rs.error_msg = "no ping response";
    co_return err_rs;
  }

  if (resp[0] == ERR_HEADER) {
    auto ep = detail::parse_err_packet(resp.data(), resp.size());
    err_rs.error_code = ep.error_code;
    err_rs.error_msg = ep.message;
    co_return err_rs;
  }

  co_return result_set{};
}

auto client::reset_connection() -> task<result_set> {
  result_set err_rs;
  if (!connected_) {
    err_rs.error_msg = "not connected";
    co_return err_rs;
  }

  seq_ = 0;
  std::vector<std::uint8_t> pkt{COM_RESET_CONNECTION};
  auto wr = co_await write_packet(pkt, seq_++);
  if (!wr) {
    err_rs.error_msg = last_io_ec_ ? std::format("failed to send reset: {}",
                                                 last_io_ec_.message())
                                   : "failed to send reset";
    co_return err_rs;
  }

  auto resp = co_await read_packet();
  if (resp.empty()) {
    err_rs.error_msg = "no reset response";
    co_return err_rs;
  }

  if (resp[0] == ERR_HEADER) {
    auto ep = detail::parse_err_packet(resp.data(), resp.size());
    err_rs.error_code = ep.error_code;
    err_rs.error_msg = ep.message;
    co_return err_rs;
  }

  co_return result_set{};
}

auto client::quit() -> task<void> {
  if (connected_) {
    seq_ = 0;
    std::vector<std::uint8_t> pkt{COM_QUIT};
    (void)co_await write_packet(pkt, seq_++);
    connected_ = false;
  }
#ifdef CNETMOD_HAS_SSL
  ssl_.reset();
  ssl_ctx_.reset();
#endif
  sock_.close();
}

auto client::is_open() const noexcept -> bool {
  return connected_ && sock_.is_open();
}

auto client::reconnect() -> task<result_set> {
  auto opts = last_opts_;
  co_await quit();
  co_return co_await connect(std::move(opts));
}

[[nodiscard]] auto client::last_error() const noexcept -> std::error_code {
  return last_io_ec_;
}

[[nodiscard]] auto client::secure_channel() const noexcept -> bool {
  return secure_channel_;
}

void client::mark_disconnected(std::error_code ec) noexcept {
  last_io_ec_ = ec;
  connected_ = false;
  secure_channel_ = false;
  rbuf_pos_ = 0;
  rbuf_len_ = 0;
#ifdef CNETMOD_HAS_SSL
  ssl_.reset();
  ssl_ctx_.reset();
#endif
  sock_.close();
}

auto client::do_write(const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
  if (ssl_) {
    auto r = co_await ssl_->async_write_all(buf);
    if (!r) {
      mark_disconnected(r.error());
      co_return std::unexpected(r.error());
    }
    co_return buf.size;
  }
#endif
  auto r = co_await async_write_all(ctx_, sock_, buf);
  if (!r) {
    mark_disconnected(r.error());
    co_return std::unexpected(r.error());
  }
  co_return buf.size;
}

auto client::do_read(mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
  if (ssl_) {
    auto r = co_await ssl_->async_read(buf);
    if (!r)
      mark_disconnected(r.error());
    co_return r;
  }
#endif
  auto r = co_await async_read(ctx_, sock_, buf);
  if (!r)
    mark_disconnected(r.error());
  co_return r;
}

auto client::read_exact(std::uint8_t *dst, std::size_t n) -> task<bool> {
  std::size_t got = 0;
  while (got < n) {
    std::size_t from_buf = std::min(n - got, rbuf_len_ - rbuf_pos_);
    if (from_buf > 0) {
      std::memcpy(dst + got, rbuf_.data() + rbuf_pos_, from_buf);
      rbuf_pos_ += from_buf;
      got += from_buf;
      if (rbuf_pos_ == rbuf_len_) {
        rbuf_pos_ = 0;
        rbuf_len_ = 0;
      }
      continue;
    }
    auto r = co_await do_read(mutable_buffer{rbuf_.data(), rbuf_.size()});
    if (!r)
      co_return false; // do_read() already marked disconnected
    if (*r == 0) {
      // Peer closed connection gracefully. Treat as connection lost.
      mark_disconnected(make_error_code(std::errc::connection_reset));
      co_return false;
    }
    rbuf_pos_ = 0;
    rbuf_len_ = *r;
  }
  co_return true;
}

auto client::read_packet() -> task<std::vector<std::uint8_t>> {
  std::vector<std::uint8_t> payload;

  for (;;) {
    std::uint8_t hdr[4];
    if (!co_await read_exact(hdr, 4))
      co_return std::vector<std::uint8_t>{};

    std::uint32_t pkt_len = detail::read_u24_le(hdr);
    seq_ = hdr[3] + 1;

    if (pkt_len == 0)
      break;

    std::size_t old_size = payload.size();
    payload.resize(old_size + pkt_len);
    if (!co_await read_exact(payload.data() + old_size, pkt_len))
      co_return std::vector<std::uint8_t>{};

    if (pkt_len < max_packet_payload)
      break;
  }

  co_return payload;
}

auto client::write_packet(std::span<const std::uint8_t> data,
                          std::uint8_t seqnum) -> task<bool> {
  std::size_t offset = 0;
  while (offset < data.size() || offset == 0) {
    std::size_t chunk = std::min(data.size() - offset, max_packet_payload);

    std::uint8_t hdr[4];
    detail::write_u24_le(hdr, static_cast<std::uint32_t>(chunk));
    hdr[3] = seqnum++;

    auto w1 = co_await do_write(const_buffer{hdr, 4});
    if (!w1)
      co_return false;

    if (chunk > 0) {
      auto w2 = co_await do_write(const_buffer{data.data() + offset, chunk});
      if (!w2)
        co_return false;
    }

    offset += chunk;

    if (chunk == max_packet_payload && offset == data.size()) {
      std::uint8_t empty_hdr[4] = {0, 0, 0, seqnum++};
      auto w3 = co_await do_write(const_buffer{empty_hdr, 4});
      if (!w3)
        co_return false;
      break;
    }
  }

  seq_ = seqnum;
  co_return true;
}

auto client::handle_auth_response(std::string_view password)
    -> task<result_set> {
  result_set err_rs;

  for (int attempt = 0; attempt < 5; ++attempt) {
    auto resp = co_await read_packet();
    if (resp.empty()) {
      err_rs.error_msg = "no auth response";
      co_return err_rs;
    }

    auto rtype = detail::classify_handshake_response(resp.data(), resp.size());

    switch (rtype) {
    case detail::handshake_response_type::ok: {
      auto ok = detail::parse_ok_packet(resp.data(), resp.size());
      result_set rs;
      rs.affected_rows = ok.affected_rows;
      rs.warning_count = ok.warnings;
      rs.info = ok.info;
      co_return rs;
    }
    case detail::handshake_response_type::error: {
      auto ep = detail::parse_err_packet(resp.data(), resp.size());
      err_rs.error_code = ep.error_code;
      err_rs.error_msg = ep.message;
      co_return err_rs;
    }
    case detail::handshake_response_type::auth_switch: {
      auto sw = detail::parse_auth_switch(resp.data(), resp.size());
      auth_plugin_ = sw.plugin_name;
      auth_scramble_ = sw.auth_data;

      auto new_auth = detail::hash_password_for_plugin(
          sw.plugin_name, password,
          std::span<const std::uint8_t>(sw.auth_data.data(),
                                        sw.auth_data.size()));

      auto w = co_await write_packet(new_auth, seq_++);
      if (!w) {
        err_rs.error_msg = "failed to send auth switch response";
        co_return err_rs;
      }
      // continue loop to read next response
      break;
    }
    case detail::handshake_response_type::auth_more_data: {
      // caching_sha2_password flow:
      // data[1]==3 => fast auth success, read next packet for OK
      // data[1]==4 => full auth required
      if (resp.size() >= 2 && resp[1] == 3) {
        // fast auth OK — continue reading next packet (should be OK)
        break;
      }
      if (resp.size() >= 2 && resp[1] == 4) {
        // full auth: if on secure channel (TLS), send plaintext password+\0
        if (secure_channel_) {
          std::vector<std::uint8_t> pwd_pkt(password.begin(), password.end());
          pwd_pkt.push_back(0);
          auto w = co_await write_packet(pwd_pkt, seq_++);
          if (!w) {
            err_rs.error_msg = "failed to send full auth";
            co_return err_rs;
          }
        } else {
          // Non-secure channel — requires RSA encryption, not yet supported
          err_rs.error_msg = "caching_sha2_password full auth requires TLS";
          co_return err_rs;
        }
        break;
      }
      // Unknown more_data, skip and continue
      break;
    }
    default:
      err_rs.error_msg = "unexpected auth response";
      co_return err_rs;
    }
  }

  err_rs.error_msg = "auth exchange exceeded max attempts";
  co_return err_rs;
}

auto client::read_resultset_head_impl(execution_state &st,
                                      [[maybe_unused]] bool binary)
    -> task<void> {
  auto resp = co_await read_packet();
  if (resp.empty()) {
    st.set_error(0, "no response");
    co_return;
  }

  if (resp[0] == ERR_HEADER) {
    auto ep = detail::parse_err_packet(resp.data(), resp.size());
    st.set_error(ep.error_code, ep.message);
    co_return;
  }

  // OK (no result set — INSERT/UPDATE/DELETE etc.)
  // 0x00 is always OK packet (column count > 0, cannot be lenenc 0)
  if (resp[0] == OK_HEADER) {
    auto ok = detail::parse_ok_packet(resp.data(), resp.size());
    st.set_ok_data(ok.affected_rows, ok.last_insert_id, ok.warnings,
                   ok.status_flags, ok.info);
    if (st.has_more_results()) {
      st.set_state(execution_state::state_t::reading_head);
    } else {
      st.set_state(execution_state::state_t::complete);
    }
    co_return;
  }

  // Result set: column count
  auto col_count_r = detail::read_lenenc(resp.data(), resp.size());
  if (col_count_r.bytes_consumed == 0) {
    st.set_error(0, "bad column count");
    co_return;
  }
  auto num_cols = static_cast<std::size_t>(col_count_r.value);

  std::vector<column_meta> cols;
  cols.reserve(num_cols);
  for (std::size_t i = 0; i < num_cols; ++i) {
    auto col_pkt = co_await read_packet();
    if (col_pkt.empty()) {
      st.set_error(0, "truncated column def");
      co_return;
    }
    cols.push_back(detail::parse_column_def(col_pkt.data(), col_pkt.size()));
  }

  // EOF (if no DEPRECATE_EOF)
  bool has_deprecate_eof = (client_caps_ & CLIENT_DEPRECATE_EOF) != 0;
  if (!has_deprecate_eof) {
    auto eof_pkt = co_await read_packet();
    (void)eof_pkt;
  }

  st.set_columns(std::move(cols));
  st.set_state(execution_state::state_t::reading_rows);
}

auto client::read_result_set(bool binary) -> task<result_set> {
  result_set err_rs;

  auto resp = co_await read_packet();
  if (resp.empty()) {
    err_rs.error_msg = "no response";
    err_rs.diag.assign_client(err_rs.error_msg);
    co_return err_rs;
  }

  // Error?
  if (resp[0] == ERR_HEADER) {
    auto ep = detail::parse_err_packet(resp.data(), resp.size());
    err_rs.error_code = ep.error_code;
    err_rs.error_msg = ep.message;
    err_rs.sql_state = ep.sql_state;
    err_rs.diag.assign_server(ep.message);
    co_return err_rs;
  }

  // OK (no result set — INSERT/UPDATE/DELETE)
  // 0x00 is always OK packet (result set column count > 0, won't conflict with
  // lenenc 0)
  if (resp[0] == OK_HEADER) {
    auto ok = detail::parse_ok_packet(resp.data(), resp.size());
    result_set rs;
    rs.affected_rows = ok.affected_rows;
    rs.last_insert_id = ok.last_insert_id;
    rs.warning_count = ok.warnings;
    rs.status_flags = ok.status_flags;
    rs.info = ok.info;
    co_return rs;
  }

  // Result set: column count
  auto col_count_r = detail::read_lenenc(resp.data(), resp.size());
  if (col_count_r.bytes_consumed == 0) {
    err_rs.error_msg = "bad column count";
    co_return err_rs;
  }
  auto num_cols = static_cast<std::size_t>(col_count_r.value);

  result_set rs;
  rs.columns.reserve(num_cols);

  // Column definitions
  for (std::size_t i = 0; i < num_cols; ++i) {
    auto col_pkt = co_await read_packet();
    if (col_pkt.empty()) {
      err_rs.error_msg = "truncated column def";
      co_return err_rs;
    }
    rs.columns.push_back(
        detail::parse_column_def(col_pkt.data(), col_pkt.size()));
  }

  // EOF (if no DEPRECATE_EOF)
  bool has_deprecate_eof = (client_caps_ & CLIENT_DEPRECATE_EOF) != 0;
  if (!has_deprecate_eof) {
    auto eof_pkt = co_await read_packet();
    (void)eof_pkt;
  }

  // Row data
  for (;;) {
    auto row_pkt = co_await read_packet();
    if (row_pkt.empty())
      break;

    // EOF / OK = result set end
    if (row_pkt[0] == EOF_HEADER && row_pkt.size() < 9) {
      auto ok = detail::parse_ok_packet(row_pkt.data(), row_pkt.size());
      rs.affected_rows = ok.affected_rows;
      rs.warning_count = ok.warnings;
      break;
    }
    if (row_pkt[0] == ERR_HEADER) {
      auto ep = detail::parse_err_packet(row_pkt.data(), row_pkt.size());
      rs.error_code = ep.error_code;
      rs.error_msg = ep.message;
      break;
    }

    if (binary) {
      rs.rows.push_back(
          detail::parse_binary_row(row_pkt.data(), row_pkt.size(), rs.columns));
    } else {
      rs.rows.push_back(
          detail::parse_text_row(row_pkt.data(), row_pkt.size(), rs.columns));
    }
  }

  co_return rs;
}

} // namespace cnetmod::mysql
