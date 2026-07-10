/// cnetmod example — High-level HTTP Server (Plus)
/// Demonstratesfeatures
/// 1. (kick)- /admin/kick/:token session
/// 2. requeststatistics(stats)- /admin/stats returnrequeststatistics JSON
/// 3. rate limiting(rate limit)- Token Bucket , O(1) , , Retry-After
/// 4. session - CSPRNG , TTL expired, IP session, background GC cleanup
/// 5. (async_mutex)- protectsharedconcurrentsecurity
/// 6. channel asynctask - task, background worker , state
/// 7. IP parse - X-Forwarded-For / X-Real-IP
///
/// Server registerroute, Client requestverifyfeatures

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.access_log;
import cnetmod.protocol.http.middleware.recover;
import cnetmod.protocol.http.middleware.cors;
import cnetmod.protocol.http.middleware.request_id;
import cnetmod.protocol.http.middleware.body_limit;
import cnetmod.protocol.http.middleware.metrics;
import cnetmod.protocol.http.middleware.rate_limiter;
import cnetmod.protocol.http.middleware.jwt_auth;

namespace cn = cnetmod;
namespace http = cnetmod::http;

constexpr std::uint16_t PORT = 19090;

// =============================================================================
// Enum ↔ string (traits )
// =============================================================================

/// Enum static constexpr map
template <typename E>
struct enum_traits;

/// To_string / from_string - enum_traits
template <typename E>
constexpr auto to_string(E val, std::string_view fallback = "unknown") -> std::string_view {
    for (auto& [k, v] : enum_traits<E>::map)
        if (k == val) return v;
    return fallback;
}

template <typename E>
constexpr auto from_string(std::string_view s) -> std::optional<E> {
    for (auto& [k, v] : enum_traits<E>::map)
        if (v == s) return k;
    return std::nullopt;
}

// =============================================================================
// Asynctask - channel background worker
// =============================================================================

enum class task_type {
    generate_report,
    export_data,
    send_email,
};

template <> struct enum_traits<task_type> {
    static constexpr std::pair<task_type, std::string_view> map[] = {
        {task_type::generate_report, "generate_report"},
        {task_type::export_data,     "export_data"},
        {task_type::send_email,      "send_email"},
    };
};

/// Channel task
struct task_job {
    std::string task_id;
    task_type   type;
    std::string params;   // Task( "user_id=42&month=2026-01")
};

/// Taskstate( shared_state , worker update, handler query)
enum class task_status { pending, processing, done, failed };

template <> struct enum_traits<task_status> {
    static constexpr std::pair<task_status, std::string_view> map[] = {
        {task_status::pending,    "pending"},
        {task_status::processing, "processing"},
        {task_status::done,       "done"},
        {task_status::failed,     "failed"},
    };
};

enum class session_status { active, kicked, expired };

template <> struct enum_traits<session_status> {
    static constexpr std::pair<session_status, std::string_view> map[] = {
        {session_status::active,  "active"},
        {session_status::kicked,  "kicked"},
        {session_status::expired, "expired"},
    };
};

struct task_record {
    std::string task_id;
    task_type   type;
    std::string params;
    task_status status = task_status::pending;
    std::string result;   // Complete
    std::string submitted_by;  // Implementation note: token.
};

/// Task channel(producer: HTTP handler, consumer: background worker)
static cn::channel<task_job>* g_task_ch = nullptr;

// =============================================================================
// Configure
// =============================================================================

struct server_config {
    // Session handling.
    std::chrono::seconds session_ttl{1800};          // Implementation note: 30 .
    std::chrono::seconds session_gc_interval{60};    // Implementation note: GC.
    std::size_t max_sessions_per_ip = 10;            // IP concurrentsession
};

// =============================================================================
// Session handling.
// =============================================================================

struct session_info {
    std::string token;
    std::string client_ip;
    std::string user_agent;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_activity;
    session_status status = session_status::active;
};

// =============================================================================
// Sharedstate - routeshared, async_mutex protect
// =============================================================================

struct shared_state {
    server_config config;

    // HTTP metrics( server , metrics_middleware update)
    cn::metrics_collector metrics;

    // Sessionstatistics(atomic , )
    std::atomic<std::uint64_t> total_kicked{0};
    std::atomic<std::uint64_t> sessions_created{0};
    std::atomic<std::uint64_t> sessions_expired{0};
    std::atomic<std::uint64_t> active_sessions{0};  // Session, login++ kick/expire

    // Session(session_mtx protect)
    cn::async_mutex session_mtx;
    std::unordered_map<std::string, session_info> sessions;
    std::unordered_map<std::string, std::size_t>  sessions_per_ip;  // O(1) IP session

    // Task(task_mtx protect)
    cn::async_mutex task_mtx;
    std::unordered_map<std::string, task_record> tasks;
    std::uint64_t next_task_id = 1;
};

/// Sharedstate(server handler )
static shared_state* g_state = nullptr;


// =============================================================================
// Session GC - cleanupexpiredsession + rate limiting
// =============================================================================

/// Background GC : cleanupexpired / session, rate limiting
auto session_gc(cn::io_context& io) -> cn::task<void> {
    constexpr std::size_t batch_size = 128;

    while (true) {
        co_await cn::async_sleep(io, g_state->config.session_gc_interval);

        auto now = std::chrono::steady_clock::now();
        auto ttl = g_state->config.session_ttl;

        // Cleanupsession(session_mtx)
        std::size_t swept = 0;
        {
            co_await g_state->session_mtx.lock();
            cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

            std::size_t count = 0;
            for (auto it = g_state->sessions.begin(); it != g_state->sessions.end(); ) {
                auto& s = it->second;
                bool remove = false;

                if (s.status == session_status::kicked) {
                    remove = true;
                } else if (s.status == session_status::active &&
                           (now - s.last_activity) > ttl) {
                    s.status = session_status::expired;
                    remove = true;
                    g_state->sessions_expired.fetch_add(1, std::memory_order_relaxed);
                    g_state->active_sessions.fetch_sub(1, std::memory_order_relaxed);
                }

                if (remove) {
                    // Sessions_per_ip
                    auto& cnt = g_state->sessions_per_ip[s.client_ip];
                    if (cnt > 0) --cnt;
                    if (cnt == 0) g_state->sessions_per_ip.erase(s.client_ip);

                    it = g_state->sessions.erase(it);
                    ++swept;
                } else {
                    ++it;
                }

                // Blocking
                if (++count % batch_size == 0) {
                    guard.~async_lock_guard();
                    co_await cn::async_sleep(io, std::chrono::milliseconds{0});
                    co_await g_state->session_mtx.lock();
                    new (&guard) cn::async_lock_guard(g_state->session_mtx, std::adopt_lock);
                    now = std::chrono::steady_clock::now();  // Implementation note.
                }
            }
        }

        if (swept > 0)
            logger::info("[GC] swept {} expired/kicked sessions", swept);
    }
}

// =============================================================================
// Task worker - backgroundconsumer(channel )
// =============================================================================

/// Channel task, simulate, update shared_state taskstate
/// Producer-consumer: HTTP handler task(), worker async()
auto task_worker(cn::io_context& io, cn::channel<task_job>& ch) -> cn::task<void> {
    logger::info("[task-worker] started, waiting for jobs...");
    int count = 0;

    while (true) {
        // 1. blockingwait for channel task
        auto job = co_await ch.receive();
        if (!job) break;  // Channel closed -> graceful

        ++count;
        logger::info("[task-worker] #{} picked up task={} type={} params={}",
            count, job->task_id, to_string(job->type), job->params);

        // 2. updatestate processing(task_mtx)
        {
            co_await g_state->task_mtx.lock();
            cn::async_lock_guard guard(g_state->task_mtx, std::adopt_lock);
            auto it = g_state->tasks.find(job->task_id);
            if (it != g_state->tasks.end())
                it->second.status = task_status::processing;
        }

        // 3. simulate(task)
        auto process_time = std::chrono::milliseconds{200};
        switch (job->type) {
            case task_type::generate_report: process_time = std::chrono::milliseconds{500}; break;
            case task_type::export_data:     process_time = std::chrono::milliseconds{300}; break;
            case task_type::send_email:      process_time = std::chrono::milliseconds{100}; break;
        }
        co_await cn::async_sleep(io, process_time);

        // 4. generate, updatestate done
        auto result = std::format("completed in {}ms, output=/{}_result.csv",
            process_time.count(), job->task_id);

        {
            co_await g_state->task_mtx.lock();
            cn::async_lock_guard guard(g_state->task_mtx, std::adopt_lock);
            auto it = g_state->tasks.find(job->task_id);
            if (it != g_state->tasks.end()) {
                it->second.status = task_status::done;
                it->second.result = std::move(result);
            }
        }

        logger::info("[task-worker] #{} task={} done", count, job->task_id);
    }

    logger::info("[task-worker] channel closed, total {} jobs processed", count);
}


// =============================================================================
// Route handler: POST /login - simulate, createsession
// =============================================================================

auto handle_login(http::request_context& ctx) -> cn::task<void> {
    auto client_ip = http::resolve_client_ip(ctx);
    auto user_agent = std::string(ctx.get_header("User-Agent"));

    std::string token;
    {
        co_await g_state->session_mtx.lock();
        cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

        // O(1) IP session
        auto ip_it = g_state->sessions_per_ip.find(client_ip);
        std::size_t ip_sessions = (ip_it != g_state->sessions_per_ip.end()) ? ip_it->second : 0;

        if (ip_sessions >= g_state->config.max_sessions_per_ip) {
            ctx.json(http::status::too_many_requests, std::format(
                R"({{"error":"too many sessions from this IP","limit":{}}})",
                g_state->config.max_sessions_per_ip));
            co_return;
        }

        auto now = std::chrono::steady_clock::now();
        token = cn::generate_secure_token();
        g_state->sessions[token] = session_info{
            .token         = token,
            .client_ip     = client_ip,
            .user_agent    = std::move(user_agent),
            .created_at    = now,
            .last_activity = now,
            .status        = session_status::active,
        };
        g_state->sessions_per_ip[client_ip]++;
        g_state->sessions_created.fetch_add(1, std::memory_order_relaxed);
        g_state->active_sessions.fetch_add(1, std::memory_order_relaxed);
    }

    // Token 16 (security)
    auto short_tok = token.substr(0, 16) + "...";
    logger::info("[handler] /login → token={} ip={}", short_tok, client_ip);
    ctx.json(http::status::ok,
        std::format(R"({{"token":"{}","message":"login success"}})", token));
    co_return;
}

// =============================================================================
// Route handler: GET /api/data - token
// =============================================================================

auto handle_data(http::request_context& ctx) -> cn::task<void> {
    auto token = std::string(ctx.get_header("Authorization"));
    if (token.empty()) {
        ctx.json(http::status::unauthorized,
            R"({"error":"missing Authorization header"})");
        co_return;
    }

    {
        co_await g_state->session_mtx.lock();
        cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

        auto it = g_state->sessions.find(token);
        if (it == g_state->sessions.end()) {
            ctx.json(http::status::unauthorized,
                R"({"error":"invalid or expired token"})");
            co_return;
        }

        auto& sess = it->second;
        auto now = std::chrono::steady_clock::now();

        // Sessionstate
        if (sess.status == session_status::kicked) {
            ctx.json(http::status::forbidden,
                R"({"error":"session terminated by administrator"})");
            co_return;
        }
        if (sess.status == session_status::expired ||
            (now - sess.last_activity) > g_state->config.session_ttl) {
            sess.status = session_status::expired;
            g_state->active_sessions.fetch_sub(1, std::memory_order_relaxed);
            ctx.json(http::status::unauthorized,
                R"({"error":"session expired, please login again"})");
            co_return;
        }

        // Implementation note.
        sess.last_activity = now;
    }

    ctx.json(http::status::ok,
        std::format(R"({{"data":"secret payload","token":"{}"}})", token));
    co_return;
}

// =============================================================================
// Route handler: POST /admin/kick/:token
// =============================================================================

auto handle_kick(http::request_context& ctx) -> cn::task<void> {
    auto target_token = std::string(ctx.param("token"));

    bool found = false;
    std::string kicked_ip;
    {
        co_await g_state->session_mtx.lock();
        cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

        auto it = g_state->sessions.find(target_token);
        if (it != g_state->sessions.end() &&
            it->second.status == session_status::active) {
            it->second.status = session_status::kicked;
            kicked_ip = it->second.client_ip;
            g_state->total_kicked.fetch_add(1, std::memory_order_relaxed);
            g_state->active_sessions.fetch_sub(1, std::memory_order_relaxed);
            found = true;
        }
    }

    if (found) {
        auto short_tok = target_token.substr(0, 16) + "...";
        logger::info("[handler] /admin/kick → kicked token={} ip={}",
                     short_tok, kicked_ip);
        ctx.json(http::status::ok,
            std::format(R"({{"kicked":"{}","message":"session terminated"}})",
                        target_token));
    } else {
        ctx.json(http::status::not_found,
            std::format(R"({{"error":"active session not found","token":"{}"}})",
                        target_token));
    }
    co_return;
}

// =============================================================================
// Route handler: GET /admin/stats - statistics
// =============================================================================

auto handle_stats(http::request_context& ctx) -> cn::task<void> {
    // HTTP statistics shared_state metrics_collector( atomic, )
    auto& mc     = g_state->metrics;
    auto uptime  = mc.uptime_seconds();
    auto req     = mc.requests_total.load(std::memory_order_relaxed);
    auto r2xx    = mc.responses_2xx.load(std::memory_order_relaxed);
    auto r3xx    = mc.responses_3xx.load(std::memory_order_relaxed);
    auto r4xx    = mc.responses_4xx.load(std::memory_order_relaxed);
    auto r5xx    = mc.responses_5xx.load(std::memory_order_relaxed);

    // Sessionstatistics shared_state
    auto kicked  = g_state->total_kicked.load(std::memory_order_relaxed);
    auto created = g_state->sessions_created.load(std::memory_order_relaxed);
    auto expired = g_state->sessions_expired.load(std::memory_order_relaxed);
    auto active  = g_state->active_sessions.load(std::memory_order_relaxed);

    auto json = std::format(
        R"({{"uptime_seconds":{:.1f},"total_requests":{},"responses_2xx":{},"responses_3xx":{},"responses_4xx":{},"responses_5xx":{},"total_kicked":{},"sessions_created":{},"sessions_expired":{},"active_sessions":{}}})",
        uptime, req, r2xx, r3xx, r4xx, r5xx,
        kicked, created, expired, active);

    ctx.json(http::status::ok, json);
    co_return;
}

// =============================================================================
// Route handler: POST /api/task - asynctask(producer -> channel)
// =============================================================================

auto handle_submit_task(http::request_context& ctx) -> cn::task<void> {
    auto token = std::string(ctx.get_header("Authorization"));
    if (token.empty()) {
        ctx.json(http::status::unauthorized,
            R"({"error":"missing Authorization header"})");
        co_return;
    }

    // Query string parsetask: /api/task?type=generate_report&params=month=2026-01
    auto qs = ctx.query_string();
    auto type_str   = http::parse_query_param(qs, "type");
    auto params_str = http::parse_query_param(qs, "params");

    auto tt = from_string<task_type>(type_str);
    if (!tt) {
        ctx.json(http::status::bad_request,
            R"({"error":"invalid task type, use: generate_report, export_data, send_email"})");
        co_return;
    }

    // Createtaskreturn task_id(blocking)
    std::string task_id;
    {
        co_await g_state->task_mtx.lock();
        cn::async_lock_guard guard(g_state->task_mtx, std::adopt_lock);

        task_id = std::format("task_{}", g_state->next_task_id++);
        g_state->tasks[task_id] = task_record{
            .task_id      = task_id,
            .type         = *tt,
            .params       = params_str,
            .status       = task_status::pending,
            .result       = {},
            .submitted_by = token,
        };
    }

    // Channel -> background worker async
    co_await g_task_ch->send(task_job{
        .task_id = task_id,
        .type    = *tt,
        .params  = params_str,
    });

    logger::info("[handler] /api/task → submitted {} type={}", task_id, type_str);
    ctx.json(http::status::accepted, std::format(
        R"({{"task_id":"{}","type":"{}","status":"pending","message":"task submitted, poll GET /api/task/{}"}})",
        task_id, type_str, task_id));
    co_return;
}

// =============================================================================
// Route handler: GET /api/task/:id - querytaskstate()
// =============================================================================

auto handle_task_status(http::request_context& ctx) -> cn::task<void> {
    auto task_id = std::string(ctx.param("id"));

    {
        co_await g_state->task_mtx.lock();
        cn::async_lock_guard guard(g_state->task_mtx, std::adopt_lock);

        auto it = g_state->tasks.find(task_id);
        if (it == g_state->tasks.end()) {
            ctx.json(http::status::not_found, std::format(
                R"({{"error":"task not found","task_id":"{}"}})", task_id));
            co_return;
        }

        auto& t = it->second;
        auto json = std::format(
            R"({{"task_id":"{}","type":"{}","status":"{}","params":"{}","result":"{}","submitted_by":"{}"}})",
            t.task_id, to_string(t.type), to_string(t.status),
            t.params, t.result, t.submitted_by);
        ctx.json(http::status::ok, json);
    }
    co_return;
}

// =============================================================================
// Route handler: GET /admin/sessions - session
// =============================================================================

auto handle_sessions(http::request_context& ctx) -> cn::task<void> {
    std::string json;
    {
        co_await g_state->session_mtx.lock();
        cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

        auto now = std::chrono::steady_clock::now();
        json = "[";
        bool first = true;
        for (auto& [tok, sess] : g_state->sessions) {
            if (!first) json += ",";

            auto idle_secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - sess.last_activity).count();
            auto age_secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - sess.created_at).count();

            // Token 16 (security)
            auto short_tok = tok.substr(0, 16) + "...";
            json += std::format(
                R"({{"token":"{}","ip":"{}","user_agent":"{}","status":"{}","idle_seconds":{},"age_seconds":{}}})",
                short_tok, sess.client_ip, sess.user_agent,
                to_string(sess.status), idle_secs, age_secs);
            first = false;
        }
        json += "]";
    }

    ctx.json(http::status::ok, json);
    co_return;
}

// =============================================================================
// Client: HTTP requestresponse
// =============================================================================

auto send_request(cn::io_context& ctx, http::http_method method,
                  std::string_view path,
                  std::vector<std::pair<std::string, std::string>> extra_headers = {},
                  std::string_view body = {})
    -> cn::task<std::string>  // Returnresponse body
{
    auto sock_r = cn::socket::create(cn::address_family::ipv4,
                                     cn::socket_type::stream);
    if (!sock_r) co_return "";
    auto sock = std::move(*sock_r);

    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    auto cr = co_await cn::async_connect(ctx, sock, ep);
    if (!cr) { logger::error("[client] connect failed"); co_return ""; }

    http::request req(method, path);
    req.set_header("Host", std::format("127.0.0.1:{}", PORT));
    req.set_header("Connection", "close");
    for (auto& [k, v] : extra_headers) {
        req.set_header(k, v);
    }
    if (!body.empty()) {
        req.set_body(std::string(body));
    }

    auto req_data = req.serialize();
    auto wr = co_await cn::async_write(ctx, sock,
        cn::const_buffer{req_data.data(), req_data.size()});
    if (!wr) { sock.close(); co_return ""; }

    // Response
    http::response_parser rp;
    std::array<std::byte, 8192> buf{};
    while (!rp.ready()) {
        auto rd = co_await cn::async_read(ctx, sock, cn::buffer(buf));
        if (!rd || *rd == 0) break;
        auto c = rp.consume(reinterpret_cast<const char*>(buf.data()), *rd);
        if (!c) break;
    }

    std::string resp_body;
    if (rp.ready()) {
        logger::info("[client] {} {} → {} {}",
                     http::method_to_string(method), path,
                     rp.status_code(), rp.status_message());
        resp_body = rp.body();
        if (!resp_body.empty())
            logger::debug("[client] Body: {}", resp_body);
    }
    sock.close();
    co_return resp_body;
}

// =============================================================================
// JSON token (simple)
// =============================================================================

auto extract_token(const std::string& json) -> std::string {
    auto pos = json.find("\"token\":\"");
    if (pos == std::string::npos) return "";
    pos += 9;  // skip "token":"
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

// =============================================================================
// Client coroutine: verifyfeatures
// =============================================================================

auto run_client(cn::io_context& ctx, http::server& srv) -> cn::task<void> {
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});

    logger::info("========== Client: Testing All Features ==========");

    // -------------------------------------------------------
    // 1. token
    // -------------------------------------------------------
    logger::info("[1] POST /login (user A)");
    auto resp1 = co_await send_request(ctx, http::http_method::POST, "/login",
        {{"X-Forwarded-For", "192.168.1.100"}, {"User-Agent", "TestClient/1.0"}});
    auto token_a = extract_token(resp1);
    logger::info("  → token_a = {}...", token_a.substr(0, 16));

    logger::info("[2] POST /login (user B)");
    auto resp2 = co_await send_request(ctx, http::http_method::POST, "/login",
        {{"X-Forwarded-For", "192.168.1.200"}, {"User-Agent", "TestClient/2.0"}});
    auto token_b = extract_token(resp2);
    logger::info("  → token_b = {}...", token_b.substr(0, 16));

    // -------------------------------------------------------
    // 2. token protect
    // -------------------------------------------------------
    logger::info("[3] GET /api/data (user A, valid token)");
    co_await send_request(ctx, http::http_method::GET, "/api/data",
        {{"Authorization", token_a}});

    logger::info("[4] GET /api/data (no token → 401)");
    co_await send_request(ctx, http::http_method::GET, "/api/data");

    logger::info("[5] GET /api/data (invalid token → 401)");
    co_await send_request(ctx, http::http_method::GET, "/api/data",
        {{"Authorization", "fake_token"}});

    // -------------------------------------------------------
    // 3. session
    // -------------------------------------------------------
    logger::info("[6] GET /admin/sessions");
    co_await send_request(ctx, http::http_method::GET, "/admin/sessions");

    // -------------------------------------------------------
    // Implementation note: 4.
    // -------------------------------------------------------
    logger::info("[7] POST /admin/kick/{} (kick user A)", token_a);
    co_await send_request(ctx, http::http_method::POST,
        std::format("/admin/kick/{}", token_a));

    // Protect -> 403
    logger::info("[8] GET /api/data (user A after kicked → 403)");
    co_await send_request(ctx, http::http_method::GET, "/api/data",
        {{"Authorization", token_a}});

    // Implementation note: user.
    logger::info("[9] GET /api/data (user B still valid)");
    co_await send_request(ctx, http::http_method::GET, "/api/data",
        {{"Authorization", token_b}});

    // -------------------------------------------------------
    // 5. rate limitingTest: IP 6 request
    // -------------------------------------------------------
    logger::info("[10] Rate limit test: 6 rapid requests from same IP");
    for (int i = 1; i <= 6; ++i) {
        logger::info("  --- request #{} ---", i);
        co_await send_request(ctx, http::http_method::GET, "/api/data",
            {{"Authorization", token_b}, {"X-Forwarded-For", "*********"}});
    }

    // -------------------------------------------------------
    // 6. asynctask(channel -)
    // -------------------------------------------------------
    logger::info("[11] POST /api/task — submit report generation");
    auto task_resp1 = co_await send_request(ctx, http::http_method::POST,
        "/api/task?type=generate_report&params=month=2026-01",
        {{"Authorization", token_b}});

    logger::info("[12] POST /api/task — submit data export");
    auto task_resp2 = co_await send_request(ctx, http::http_method::POST,
        "/api/task?type=export_data&params=user_id=42",
        {{"Authorization", token_b}});

    logger::info("[13] POST /api/task — submit email");
    co_await send_request(ctx, http::http_method::POST,
        "/api/task?type=send_email&params=to=test@example.com",
        {{"Authorization", token_b}});

    // Query -> pending processing
    // Response task_id
    auto extract_task_id = [](const std::string& json) -> std::string {
        auto pos = json.find("\"task_id\":\"");
        if (pos == std::string::npos) return "";
        pos += 11;
        auto end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };

    auto tid1 = extract_task_id(task_resp1);
    auto tid2 = extract_task_id(task_resp2);

    logger::info("[14] GET /api/task/{} — poll immediately (should be pending/processing)", tid1);
    co_await send_request(ctx, http::http_method::GET,
        std::format("/api/task/{}", tid1));

    // Wait for worker complete
    logger::info("... waiting 800ms for worker to finish ...");
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{800});

    // Implementation note: done.
    logger::info("[15] GET /api/task/{} — poll again (should be done)", tid1);
    co_await send_request(ctx, http::http_method::GET,
        std::format("/api/task/{}", tid1));

    logger::info("[16] GET /api/task/{} — poll task 2 (should be done)", tid2);
    co_await send_request(ctx, http::http_method::GET,
        std::format("/api/task/{}", tid2));

    // -------------------------------------------------------
    // 7. statistics
    // -------------------------------------------------------
    logger::info("[17] GET /admin/stats");
    co_await send_request(ctx, http::http_method::GET, "/admin/stats");

    logger::info("========== Client: All Tests Done ==========");

    // Closetask channel, worker graceful
    g_task_ch->close();
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});

    srv.stop();
    ctx.stop();
}

// =============================================================================
// main
// =============================================================================

int main() {
    logger::init("cnetmod-demo", logger::level::debug);
    logger::info("=== cnetmod: High-level HTTP Plus Demo ===");
    logger::info("Features: kick, stats(/admin/stats), metrics(/metrics), "
                 "rate-limit, session, async_mutex, channel task-queue");

    cn::net_init net;
    auto ctx = cn::make_io_context();

    // Sharedstate
    shared_state state;
    state.config.session_ttl          = std::chrono::seconds{60};  // Demo: 60s
    state.config.session_gc_interval  = std::chrono::seconds{5};   // Demo: 5s GC
    state.config.max_sessions_per_ip  = 5;
    g_state = &state;

    // Task channel( 16, : wait for)
    cn::channel<task_job> task_ch(16);
    g_task_ch = &task_ch;

    // Buildroute
    http::router router;

    // GET /metrics - Prometheus metrics( server )
    router.get("/metrics", cn::metrics_handler(state.metrics));

    // Implementation note: GET.
    router.get("/", [](http::request_context& ctx) -> cn::task<void> {
        ctx.html(http::status::ok,
            "<h1>cnetmod HTTP Plus Demo</h1>"
            "<p>Features: kick, stats, rate-limit, shared-state, async_mutex</p>"
            "<ul>"
            "<li>POST /login</li>"
            "<li>GET /api/data (Authorization: token)</li>"
            "<li>POST /admin/kick/:token</li>"
            "<li>GET /admin/stats</li>"
            "<li>GET /admin/sessions</li>"
            "<li>POST /api/task?type=...&params=...</li>"
            "<li>GET /api/task/:id</li>"
            "</ul>");
        co_return;
    });

    // POST /login - simulate
    router.post("/login", handle_login);

    // GET /api/data - protect
    router.get("/api/data", handle_data);

    // POST /admin/kick/:token
    router.post("/admin/kick/:token", handle_kick);

    // GET /admin/stats - statistics
    router.get("/admin/stats", handle_stats);

    // GET /admin/sessions - session
    router.get("/admin/sessions", handle_sessions);

    // POST /api/task - asynctask(-> channel -> worker)
    router.post("/api/task", handle_submit_task);

    // GET /api/task/:id - querytaskstate()
    router.get("/api/task/:id", handle_task_status);

    // Implementation note.
    http::server srv(*ctx);
    auto listen_r = srv.listen("127.0.0.1", PORT);
    if (!listen_r) {
        logger::error("Listen failed: {}", listen_r.error().message());
        return 1;
    }

    // Registermiddleware(: recover -> access_log -> cors -> request_id -> body_limit -> rate_limiter -> metrics -> handler)
    srv.use(cn::recover());
    srv.use(cn::access_log());
    srv.use(cn::cors());
    srv.use(cn::request_id());
    srv.use(cn::body_limit(2 * 1024 * 1024));  // 2MB
    srv.use(cn::rate_limiter({.rate = 2.0, .burst = 5.0}));   // 2 req/s, 5
    srv.use(cn::metrics_middleware(state.metrics));
    srv.set_router(std::move(router));

    logger::info("Server listening on *********:{}", PORT);
    logger::info("Rate limit: 2 req/s, burst=5");
    logger::info("Session TTL: {}s, GC interval: {}s, max per IP: {}",
                 state.config.session_ttl.count(),
                 state.config.session_gc_interval.count(),
                 state.config.max_sessions_per_ip);

    // Startbackground
    cn::spawn(*ctx, session_gc(*ctx));         // Session GC
    cn::spawn(*ctx, task_worker(*ctx, task_ch)); // Task worker

    // StartClient
    cn::spawn(*ctx, srv.run());
    cn::spawn(*ctx, run_client(*ctx, srv));

    ctx->run();
    logger::info("Done.");
    return 0;
}
