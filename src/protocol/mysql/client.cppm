module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.mysql:client;

import std;
import :types;
import :protocol;
import :auth;
import :deserialization;
import :serialization;
import :format_sql;
import :diagnostics;
import :pipeline;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::mysql {

// =============================================================================
// mysql::client — Async MySQL client
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx) noexcept : ctx_(ctx) {}

    // ── Connection ────────────────────────────────────────────────

    auto connect(connect_options opts = {}) -> task<result_set> {
        result_set err_rs;

        // TCP connection
        auto addr_r = ip_address::from_string(opts.host);
        if (!addr_r) { err_rs.error_msg = "invalid host"; co_return err_rs; }

        auto family = addr_r->is_v4() ? address_family::ipv4 : address_family::ipv6;
        auto sock_r = socket::create(family, socket_type::stream);
        if (!sock_r) { err_rs.error_msg = "socket create failed"; co_return err_rs; }
        sock_ = std::move(*sock_r);

        auto cr = co_await async_connect(ctx_, sock_, endpoint{*addr_r, opts.port});
        if (!cr) {
            sock_.close();
            err_rs.error_msg = "connect: " + cr.error().message();
            co_return err_rs;
        }

        // Server Greeting
        auto greeting_pkt = co_await read_packet();
        if (greeting_pkt.empty()) {
            sock_.close();
            err_rs.error_msg = "no server greeting";
            co_return err_rs;
        }

        auto greeting_r = detail::parse_server_greeting(greeting_pkt.data(), greeting_pkt.size());
        if (!greeting_r) {
            sock_.close();
            err_rs.error_msg = "greeting: " + greeting_r.error();
            co_return err_rs;
        }
        auto& greeting = *greeting_r;
        server_caps_ = greeting.capabilities;
        auth_plugin_ = greeting.auth_plugin_name;
        auth_scramble_.assign(greeting.auth_data.begin(), greeting.auth_data.end());

        // Capability negotiation
        std::uint32_t client_caps =
            CLIENT_LONG_PASSWORD | CLIENT_FOUND_ROWS | CLIENT_LONG_FLAG |
            CLIENT_PROTOCOL_41 | CLIENT_TRANSACTIONS |
            CLIENT_SECURE_CONNECTION | CLIENT_PLUGIN_AUTH |
            CLIENT_PLUGIN_AUTH_LENENC | CLIENT_DEPRECATE_EOF |
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
            if (!wr) { sock_.close(); err_rs.error_msg = "failed to send SSL request"; co_return err_rs; }

            auto ssl_ctx_r = ssl_context::client();
            if (!ssl_ctx_r) { sock_.close(); err_rs.error_msg = "ssl context: " + ssl_ctx_r.error().message(); co_return err_rs; }
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
            if (!hs) { sock_.close(); err_rs.error_msg = "ssl handshake: " + hs.error().message(); co_return err_rs; }
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
            std::span<const std::uint8_t>(greeting.auth_data.data(), greeting.auth_data.size()));

        // Login packet
        auto login_pkt = detail::build_login_packet(opts, greeting, client_caps, auth_response);
        auto lw = co_await write_packet(login_pkt, seq_++);
        if (!lw) { sock_.close(); err_rs.error_msg = "failed to send login"; co_return err_rs; }

        // Authentication response loop
        auto auth_rs = co_await handle_auth_response(opts.password);
        if (auth_rs.is_err()) { sock_.close(); co_return auth_rs; }

        connected_ = true;
        co_return auth_rs;
    }

    // ── COM_QUERY ────────────────────────────────────────────

    auto query(std::string_view sql) -> task<result_set> {
        result_set err_rs;
        if (!connected_) { err_rs.error_msg = "not connected"; co_return err_rs; }

        seq_ = 0;
        auto pkt = detail::build_query_command(sql);
        auto wr = co_await write_packet(pkt, seq_++);
        if (!wr) { err_rs.error_msg = "failed to send query"; co_return err_rs; }

        co_return co_await read_result_set(false);
    }

    /// execute is an alias for query
    auto execute(std::string_view sql) -> task<result_set> {
        return query(sql);
    }

    /// execute with_params — Client-side SQL formatting then execute (COM_QUERY)
    auto execute(with_params_t wp) -> task<result_set> {
        result_set err_rs;
        if (!connected_) { err_rs.error_msg = "not connected"; co_return err_rs; }

        auto sql_r = format_sql(format_opts_, wp.query, wp.args);
        if (!sql_r) {
            err_rs.error_msg = "format_sql error";
            err_rs.diag.assign_client(err_rs.error_msg);
            co_return err_rs;
        }

        co_return co_await query(*sql_r);
    }

    /// Get current connection's format options
    auto current_format_opts() const noexcept -> const format_options& {
        return format_opts_;
    }

    // ── Multi-function: start_execution ─────────────────

    auto start_execution(std::string_view sql, execution_state& st) -> task<void> {
        if (!connected_) { st.set_error(0, "not connected"); co_return; }

        seq_ = 0;
        auto pkt = detail::build_query_command(sql);
        auto wr = co_await write_packet(pkt, seq_++);
        if (!wr) { st.set_error(0, "failed to send query"); co_return; }

        co_await read_resultset_head_impl(st, false);
    }

    auto start_execution(with_params_t wp, execution_state& st) -> task<void> {
        if (!connected_) { st.set_error(0, "not connected"); co_return; }

        auto sql_r = format_sql(format_opts_, wp.query, wp.args);
        if (!sql_r) { st.set_error(0, "format_sql error"); co_return; }

        co_return co_await start_execution(*sql_r, st);
    }

    // ── Multi-function: read_some_rows ──────────────────

    auto read_some_rows(execution_state& st) -> task<std::vector<row>> {
        std::vector<row> batch;
        if (!st.should_read_rows()) co_return batch;

        // Read a batch of rows (max 100 rows or until EOF)
        for (int i = 0; i < 100; ++i) {
            auto row_pkt = co_await read_packet();
            if (row_pkt.empty()) { st.set_error(0, "connection lost"); break; }

            // EOF/OK = result set end
            if (row_pkt[0] == EOF_HEADER && row_pkt.size() < 9) {
                auto ok = detail::parse_ok_packet(row_pkt.data(), row_pkt.size());
                st.set_ok_data(ok.affected_rows, ok.last_insert_id,
                               ok.warnings, ok.status_flags, ok.info);
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

    // ── Multi-function: read_resultset_head ─────────────

    auto read_resultset_head(execution_state& st) -> task<void> {
        if (!st.should_read_head()) co_return;
        co_await read_resultset_head_impl(st, false);
    }

    // ── run_pipeline — Batch execute multiple commands (experimental)────

    auto run_pipeline(const pipeline_request& req,
                      std::vector<stage_response>& responses) -> task<void>
    {
        responses.clear();
        responses.resize(req.size());

        if (!connected_) {
            for (auto& r : responses)
                r.set_error(0, "not connected");
            co_return;
        }

        // Send and read responses one by one
        for (std::size_t i = 0; i < req.size(); ++i) {
            auto& stage = req.stages()[i];
            auto& resp  = responses[i];

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

    // ── COM_STMT_PREPARE ─────────────────────────────────

    auto prepare(std::string_view sql) -> task<std::expected<statement, std::string>> {
        if (!connected_)
            co_return std::unexpected(std::string("not connected"));

        seq_ = 0;
        auto pkt = detail::build_prepare_stmt_command(sql);
        auto wr = co_await write_packet(pkt, seq_++);
        if (!wr) co_return std::unexpected(std::string("failed to send prepare"));

        auto resp = co_await read_packet();
        if (resp.empty()) co_return std::unexpected(std::string("no prepare response"));

        if (resp[0] == ERR_HEADER) {
            auto ep = detail::parse_err_packet(resp.data(), resp.size());
            co_return std::unexpected(ep.message);
        }

        auto pr = detail::parse_prepare_stmt_response(resp.data(), resp.size());
        if (!pr) co_return std::unexpected(pr.error());

        statement stmt;
        stmt.id          = pr->stmt_id;
        stmt.num_params  = pr->num_params;
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

    // ── COM_STMT_EXECUTE ─────────────────────────────────────

    auto execute_stmt(const statement& stmt, std::span<const param_value> params = {})
        -> task<result_set>
    {
        result_set err_rs;
        if (!connected_) { err_rs.error_msg = "not connected"; co_return err_rs; }
        if (!stmt.valid()) { err_rs.error_msg = "invalid statement"; co_return err_rs; }

        seq_ = 0;
        auto pkt = detail::build_execute_stmt_command(stmt.id, params);
        auto wr = co_await write_packet(pkt, seq_++);
        if (!wr) { err_rs.error_msg = "failed to send execute"; co_return err_rs; }

        co_return co_await read_result_set(true);
    }

    // ── COM_STMT_CLOSE ───────────────────────────────────────

    auto close_stmt(const statement& stmt) -> task<void> {
        if (!connected_ || !stmt.valid()) co_return;

        seq_ = 0;
        auto pkt = detail::build_close_stmt_command(stmt.id);
        (void)co_await write_packet(pkt, seq_++);
        // COM_STMT_CLOSE has no response
    }

    // ── COM_PING ─────────────────────────────────────────────

    auto ping() -> task<result_set> {
        result_set err_rs;
        if (!connected_) { err_rs.error_msg = "not connected"; co_return err_rs; }

        seq_ = 0;
        std::vector<std::uint8_t> pkt{COM_PING};
        auto wr = co_await write_packet(pkt, seq_++);
        if (!wr) { err_rs.error_msg = "failed to send ping"; co_return err_rs; }

        auto resp = co_await read_packet();
        if (resp.empty()) { err_rs.error_msg = "no ping response"; co_return err_rs; }

        if (resp[0] == ERR_HEADER) {
            auto ep = detail::parse_err_packet(resp.data(), resp.size());
            err_rs.error_code = ep.error_code;
            err_rs.error_msg = ep.message;
            co_return err_rs;
        }

        co_return result_set{};
    }

    // ── COM_RESET_CONNECTION ─────────────────────────────────

    auto reset_connection() -> task<result_set> {
        result_set err_rs;
        if (!connected_) { err_rs.error_msg = "not connected"; co_return err_rs; }

        seq_ = 0;
        std::vector<std::uint8_t> pkt{COM_RESET_CONNECTION};
        auto wr = co_await write_packet(pkt, seq_++);
        if (!wr) { err_rs.error_msg = "failed to send reset"; co_return err_rs; }

        auto resp = co_await read_packet();
        if (resp.empty()) { err_rs.error_msg = "no reset response"; co_return err_rs; }

        if (resp[0] == ERR_HEADER) {
            auto ep = detail::parse_err_packet(resp.data(), resp.size());
            err_rs.error_code = ep.error_code;
            err_rs.error_msg = ep.message;
            co_return err_rs;
        }

        co_return result_set{};
    }

    // ── COM_QUIT ─────────────────────────────────────────────

    auto quit() -> task<void> {
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

    auto is_open() const noexcept -> bool { return connected_ && sock_.is_open(); }

    // ── Transaction ──────────────────────────────────────────────

    /// Execute a function within a transaction (auto commit/rollback)
    template <typename Func>
        requires std::invocable<Func> && 
                 requires(Func f) { { f() } -> std::same_as<task<void>>; }
    auto transaction(Func&& func) -> task<result_set> {
        result_set err_rs;
        if (!connected_) { err_rs.error_msg = "not connected"; co_return err_rs; }

        // Start transaction
        auto start_rs = co_await execute("START TRANSACTION");
        if (start_rs.is_err()) co_return start_rs;

        // Execute function and handle result
        bool success = false;
        std::string error_msg;
        
        try {
            co_await func();
            success = true;
        } catch (const std::exception& e) {
            error_msg = std::string("transaction failed: ") + e.what();
        } catch (...) {
            error_msg = "transaction failed: unknown exception";
        }

        // Commit or rollback based on success
        if (success) {
            auto commit_rs = co_await execute("COMMIT");
            co_return commit_rs;
        } else {
            auto rollback_rs = co_await execute("ROLLBACK");
            (void)rollback_rs;
            err_rs.error_msg = std::move(error_msg);
            co_return err_rs;
        }
    }

    /// Execute a function within a transaction with specific isolation level
    template <typename Func>
        requires std::invocable<Func> && 
                 requires(Func f) { { f() } -> std::same_as<task<void>>; }
    auto transaction(Func&& func, isolation_level level) -> task<result_set> {
        result_set err_rs;
        if (!connected_) { err_rs.error_msg = "not connected"; co_return err_rs; }

        // Set isolation level
        std::string sql = "SET TRANSACTION ISOLATION LEVEL ";
        sql.append(isolation_level_to_str(level));
        auto set_rs = co_await execute(sql);
        if (set_rs.is_err()) co_return set_rs;

        // Start transaction
        auto start_rs = co_await execute("START TRANSACTION");
        if (start_rs.is_err()) co_return start_rs;

        // Execute function and handle result
        bool success = false;
        std::string error_msg;
        
        try {
            co_await func();
            success = true;
        } catch (const std::exception& e) {
            error_msg = std::string("transaction failed: ") + e.what();
        } catch (...) {
            error_msg = "transaction failed: unknown exception";
        }

        // Commit or rollback based on success
        if (success) {
            auto commit_rs = co_await execute("COMMIT");
            co_return commit_rs;
        } else {
            auto rollback_rs = co_await execute("ROLLBACK");
            (void)rollback_rs;
            err_rs.error_msg = std::move(error_msg);
            co_return err_rs;
        }
    }

private:
    // ── Transport layer ──────────────────────────────────────────────

    auto do_write(const_buffer buf) -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) co_return co_await ssl_->async_write(buf);
#endif
        co_return co_await async_write(ctx_, sock_, buf);
    }

    auto do_read(mutable_buffer buf) -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) co_return co_await ssl_->async_read(buf);
#endif
        co_return co_await async_read(ctx_, sock_, buf);
    }

    // ── Read exactly N bytes ─────────────────────────────────────

    auto read_exact(std::uint8_t* dst, std::size_t n) -> task<bool> {
        std::size_t got = 0;
        while (got < n) {
            std::size_t from_buf = std::min(n - got, rbuf_len_ - rbuf_pos_);
            if (from_buf > 0) {
                std::memcpy(dst + got, rbuf_.data() + rbuf_pos_, from_buf);
                rbuf_pos_ += from_buf;
                got += from_buf;
                if (rbuf_pos_ == rbuf_len_) { rbuf_pos_ = 0; rbuf_len_ = 0; }
                continue;
            }
            auto r = co_await do_read(mutable_buffer{rbuf_.data(), rbuf_.size()});
            if (!r || *r == 0) co_return false;
            rbuf_pos_ = 0;
            rbuf_len_ = *r;
        }
        co_return true;
    }

    // ── MySQL packet read/write ────────────────────────────────────────

    auto read_packet() -> task<std::vector<std::uint8_t>> {
        std::vector<std::uint8_t> payload;

        for (;;) {
            std::uint8_t hdr[4];
            if (!co_await read_exact(hdr, 4))
                co_return std::vector<std::uint8_t>{};

            std::uint32_t pkt_len = detail::read_u24_le(hdr);
            seq_ = hdr[3] + 1;

            if (pkt_len == 0) break;

            std::size_t old_size = payload.size();
            payload.resize(old_size + pkt_len);
            if (!co_await read_exact(payload.data() + old_size, pkt_len))
                co_return std::vector<std::uint8_t>{};

            if (pkt_len < max_packet_payload) break;
        }

        co_return payload;
    }

    auto write_packet(std::span<const std::uint8_t> data, std::uint8_t seqnum)
        -> task<bool>
    {
        std::size_t offset = 0;
        while (offset < data.size() || offset == 0) {
            std::size_t chunk = std::min(data.size() - offset, max_packet_payload);

            std::uint8_t hdr[4];
            detail::write_u24_le(hdr, static_cast<std::uint32_t>(chunk));
            hdr[3] = seqnum++;

            auto w1 = co_await do_write(const_buffer{hdr, 4});
            if (!w1) co_return false;

            if (chunk > 0) {
                auto w2 = co_await do_write(const_buffer{data.data() + offset, chunk});
                if (!w2) co_return false;
            }

            offset += chunk;

            if (chunk == max_packet_payload && offset == data.size()) {
                std::uint8_t empty_hdr[4] = {0, 0, 0, seqnum++};
                auto w3 = co_await do_write(const_buffer{empty_hdr, 4});
                if (!w3) co_return false;
                break;
            }
        }

        seq_ = seqnum;
        co_return true;
    }

    // ── Authentication response handling ────────────────────────────────────────

    auto handle_auth_response(std::string_view password) -> task<result_set> {
        result_set err_rs;

        for (int attempt = 0; attempt < 5; ++attempt) {
            auto resp = co_await read_packet();
            if (resp.empty()) { err_rs.error_msg = "no auth response"; co_return err_rs; }

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
                    std::span<const std::uint8_t>(sw.auth_data.data(), sw.auth_data.size()));

                auto w = co_await write_packet(new_auth, seq_++);
                if (!w) { err_rs.error_msg = "failed to send auth switch response"; co_return err_rs; }
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
                        if (!w) { err_rs.error_msg = "failed to send full auth"; co_return err_rs; }
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

    // ── read_resultset_head_impl (internal, for streaming)───────

    auto read_resultset_head_impl(execution_state& st, [[maybe_unused]] bool binary) -> task<void> {
        auto resp = co_await read_packet();
        if (resp.empty()) { st.set_error(0, "no response"); co_return; }

        if (resp[0] == ERR_HEADER) {
            auto ep = detail::parse_err_packet(resp.data(), resp.size());
            st.set_error(ep.error_code, ep.message);
            co_return;
        }

        // OK (no result set — INSERT/UPDATE/DELETE etc.)
        // 0x00 is always OK packet (column count > 0, cannot be lenenc 0)
        if (resp[0] == OK_HEADER) {
            auto ok = detail::parse_ok_packet(resp.data(), resp.size());
            st.set_ok_data(ok.affected_rows, ok.last_insert_id,
                           ok.warnings, ok.status_flags, ok.info);
            if (st.has_more_results()) {
                st.set_state(execution_state::state_t::reading_head);
            } else {
                st.set_state(execution_state::state_t::complete);
            }
            co_return;
        }

        // Result set: column count
        auto col_count_r = detail::read_lenenc(resp.data(), resp.size());
        if (col_count_r.bytes_consumed == 0) { st.set_error(0, "bad column count"); co_return; }
        auto num_cols = static_cast<std::size_t>(col_count_r.value);

        std::vector<column_meta> cols;
        cols.reserve(num_cols);
        for (std::size_t i = 0; i < num_cols; ++i) {
            auto col_pkt = co_await read_packet();
            if (col_pkt.empty()) { st.set_error(0, "truncated column def"); co_return; }
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

    // ── Result set reading (text / binary)─────────────────────

    auto read_result_set(bool binary) -> task<result_set> {
        result_set err_rs;

        auto resp = co_await read_packet();
        if (resp.empty()) { err_rs.error_msg = "no response"; err_rs.diag.assign_client(err_rs.error_msg); co_return err_rs; }

        // Error?
        if (resp[0] == ERR_HEADER) {
            auto ep = detail::parse_err_packet(resp.data(), resp.size());
            err_rs.error_code = ep.error_code;
            err_rs.error_msg  = ep.message;
            err_rs.sql_state  = ep.sql_state;
            err_rs.diag.assign_server(ep.message);
            co_return err_rs;
        }

        // OK (no result set — INSERT/UPDATE/DELETE)
        // 0x00 is always OK packet (result set column count > 0, won't conflict with lenenc 0)
        if (resp[0] == OK_HEADER) {
            auto ok = detail::parse_ok_packet(resp.data(), resp.size());
            result_set rs;
            rs.affected_rows  = ok.affected_rows;
            rs.last_insert_id = ok.last_insert_id;
            rs.warning_count  = ok.warnings;
            rs.status_flags   = ok.status_flags;
            rs.info           = ok.info;
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
            if (col_pkt.empty()) { err_rs.error_msg = "truncated column def"; co_return err_rs; }
            rs.columns.push_back(detail::parse_column_def(col_pkt.data(), col_pkt.size()));
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
            if (row_pkt.empty()) break;

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

    // ── Members ────────────────────────────────────────────────

    io_context& ctx_;
    socket      sock_;
    bool        connected_      = false;
    bool        secure_channel_ = false;
    std::uint8_t  seq_          = 0;
    std::uint32_t server_caps_  = 0;
    std::uint32_t client_caps_  = 0;
    std::string   password_;
    std::string   auth_plugin_;
    std::vector<std::uint8_t> auth_scramble_;
    format_options format_opts_;

    // Read buffer
    std::array<std::uint8_t, 8192> rbuf_{};
    std::size_t rbuf_pos_ = 0;
    std::size_t rbuf_len_ = 0;

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream>  ssl_;
#endif
};

} // namespace cnetmod::mysql
