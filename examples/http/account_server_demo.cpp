/// cnetmod example — Freelance Accounting REST Server
/// 结构化并发实现的自由职业记账系统（从 Python Flask 版移植）
///
/// 亮点:
///   1. when_all 并行查询 — 同时查 projects + files 两条 SQL，替代 Python ThreadPoolExecutor
///   2. sharded_connection_pool — 分片异步 MySQL 连接池，降低锁竞争
///   3. spawn 每连接 — http::server 自动为每个 TCP 连接 spawn 独立协程
///   4. 内置中间件 — cors() + access_log() + jwt_auth() 洋葱模型
///   5. cancel_token + with_timeout — I/O 超时保护

#include <cnetmod/config.hpp>
#include <cstdio>
#include <jwt-cpp/jwt.h>
#include <cnetmod/orm.hpp>
#include "embedded_mappers.hpp"

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.core.net_init;
import cnetmod.core.file;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;
import cnetmod.protocol.mysql;
import cnetmod.protocol.openai;
import cnetmod.protocol.http.middleware.recover;
import cnetmod.protocol.http.middleware.cors;
import cnetmod.protocol.http.middleware.access_log;
import cnetmod.protocol.http.middleware.jwt_auth;
import cnetmod.core.log;
import nlohmann.json;

namespace cn   = cnetmod;
namespace http = cnetmod::http;
namespace mysql = cnetmod::mysql;
namespace openai = cnetmod::openai;
namespace orm    = mysql::orm;

// =============================================================================
// 配置（对应 Python 版 DB_CONFIG / AUTH_CONFIG / LLM_CONFIG）
// =============================================================================

constexpr std::uint16_t SERVER_PORT = 8086;

struct app_config {
    // 数据库
    std::string db_host     = "114.66.62.23";
    std::uint16_t db_port   = 3306;
    std::string db_user     = "root";
    std::string db_password = "ydc061588";
    std::string db_name     = "freelance_accounting";

    // JWT
    std::string jwt_secret  = "cnetmod_demo_secret_key_2025";
    int jwt_expiry_seconds  = 24 * 3600;

    // 管理员（密码: admin123 的 SHA256）
    std::string admin_user      = "admin";
    std::string admin_pass_hash = "240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9";

    // 文件上传目录
    std::string upload_dir  = "project_uploads";

    // 大模型配置 (OpenAI协议兼容)
    std::string llm_api_base    = "https://dashscope.aliyuncs.com/compatible-mode/v1";
    std::string llm_api_key     = "YOUR_API_KEY_HERE";  // 请设置环境变量或通过 API 配置
    std::string llm_model       = "qwen3-max";
    int         llm_max_tokens  = 9999;
    double      llm_temperature = 0.7;
};

static app_config g_cfg;
static orm::mapper_registry g_mapper_registry;  // XML Mapper 注册表

// =============================================================================
// ORM 模型定义
// =============================================================================

struct Client {
    std::int64_t id = 0;
    std::string  wechat_id;
    std::string  name;
};

CNETMOD_MODEL(Client, "clients",
    CNETMOD_FIELD(id,        "id",        bigint,  PK | AUTO_INC),
    CNETMOD_FIELD(wechat_id, "wechat_id", varchar),
    CNETMOD_FIELD(name,      "name",      varchar)
)

struct Expense {
    std::int64_t id = 0;
    std::int64_t project_id = 0;
    std::string  item_name;
    double       amount = 0.0;
};

CNETMOD_MODEL(Expense, "expenses",
    CNETMOD_FIELD(id,         "id",         bigint,  PK | AUTO_INC),
    CNETMOD_FIELD(project_id, "project_id", bigint),
    CNETMOD_FIELD(item_name,  "item_name",  varchar),
    CNETMOD_FIELD(amount,     "amount",     double_)
)

struct ProjectFile {
    std::int64_t id = 0;
    std::int64_t project_id = 0;
    std::string  original_filename;
    std::string  stored_filename;
    std::string  filepath;
};

CNETMOD_MODEL(ProjectFile, "project_files",
    CNETMOD_FIELD(id,                "id",                bigint,  PK | AUTO_INC),
    CNETMOD_FIELD(project_id,        "project_id",        bigint),
    CNETMOD_FIELD(original_filename, "original_filename", varchar),
    CNETMOD_FIELD(stored_filename,   "stored_filename",   varchar),
    CNETMOD_FIELD(filepath,          "filepath",          varchar)
)

struct AiPrompt {
    std::int64_t                id = 0;
    std::string                 name;
    std::string                 prompt_template;
    bool                        is_default = false;
    std::optional<std::string>  created_at;
};

CNETMOD_MODEL(AiPrompt, "ai_prompts",
    CNETMOD_FIELD(id,              "id",              bigint,   PK | AUTO_INC),
    CNETMOD_FIELD(name,            "name",            varchar),
    CNETMOD_FIELD(prompt_template, "prompt_template", text),
    CNETMOD_FIELD(is_default,      "is_default",      tinyint),
    CNETMOD_FIELD(created_at,      "created_at",      datetime, NULLABLE)
)

struct AiAnalysisHistory {
    std::int64_t                id = 0;
    std::string                 start_date;
    std::string                 end_date;
    std::int64_t                prompt_id = 0;
    std::optional<std::string>  analysis_result;
    std::optional<std::string>  project_summary;
    std::optional<std::string>  projects_json;
    std::optional<std::string>  created_at;
};

CNETMOD_MODEL(AiAnalysisHistory, "ai_analysis_history",
    CNETMOD_FIELD(id,              "id",              bigint,   PK | AUTO_INC),
    CNETMOD_FIELD(start_date,      "start_date",      date),
    CNETMOD_FIELD(end_date,        "end_date",        date),
    CNETMOD_FIELD(prompt_id,       "prompt_id",       bigint),
    CNETMOD_FIELD(analysis_result, "analysis_result", text,     NULLABLE),
    CNETMOD_FIELD(project_summary, "project_summary", text,     NULLABLE),
    CNETMOD_FIELD(projects_json,   "projects_json",   text,     NULLABLE),
    CNETMOD_FIELD(created_at,      "created_at",      datetime, NULLABLE)
)

/// 辅助: 生成当前时间 MySQL datetime 字符串
auto now_datetime() -> std::string {
    auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// =============================================================================
// 辅助: SHA256（密码哈希 — 使用 OpenSSL EVP）
// =============================================================================

auto sha256_hex(std::string_view input) -> std::string {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(input.data(), input.size(), hash, &len, EVP_sha256(), nullptr);
    std::string hex;
    hex.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i)
        hex += std::format("{:02x}", hash[i]);
    return hex;
}

// =============================================================================
// 辅助: JWT 生成与验证（jwt-cpp + HS256）
// =============================================================================

auto generate_jwt(std::string_view username) -> std::string {
    auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_payload_claim("username", jwt::claim(std::string(username)))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(g_cfg.jwt_expiry_seconds))
        .sign(jwt::algorithm::hs256{g_cfg.jwt_secret});
}

auto verify_jwt(std::string_view token) -> std::optional<std::string> {
    try {
        auto decoded = jwt::decode(std::string(token));
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{g_cfg.jwt_secret});
        verifier.verify(decoded);
        return decoded.get_payload_claim("username").as_string();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

auto get_bearer_token(const http::request_context& ctx) -> std::string_view {
    auto auth = ctx.get_header("Authorization");
    if (auth.starts_with("Bearer ")) return auth.substr(7);
    return {};
}

// =============================================================================
// 辅助: JSON 响应构建
// =============================================================================

/// 构建错误响应 JSON 字符串
inline auto json_error(std::string_view message) -> std::string {
    nlohmann::json j{{"error", message}};
    return j.dump();
}

/// 构建成功响应 JSON 字符串
inline auto json_success(std::string_view message) -> std::string {
    nlohmann::json j{{"message", message}};
    return j.dump();
}

/// 构建成功响应 JSON 字符串 (带额外字段)
template<typename... Args>
inline auto json_success_with(std::string_view message, Args&&... args) -> std::string {
    nlohmann::json j{{"message", message}};
    (j.update(std::forward<Args>(args)), ...);
    return j.dump();
}

// =============================================================================
// 辅助: JSON 字符串转义 (保留用于特殊场景)
// =============================================================================

auto json_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

/// field_value → nlohmann::json 值
inline auto field_to_json(const mysql::field_value& f) -> nlohmann::json {
    if (f.is_null()) return nullptr;
    if (f.is_int64()) return f.get_int64();
    if (f.is_uint64()) return f.get_uint64();
    if (f.is_double()) return f.get_double();
    if (f.is_float()) return f.get_float();
    if (f.is_date()) return f.get_date().to_string();
    if (f.is_datetime()) return f.get_datetime().to_string();
    return f.to_string();
}

/// 安全地将 field_value 转换为 double（处理 DECIMAL/字符串类型）
inline auto to_double(const mysql::field_value& f) -> double {
    if (f.is_double()) return f.get_double();
    if (f.is_float())  return static_cast<double>(f.get_float());
    if (f.is_int64())  return static_cast<double>(f.get_int64());
    if (f.is_uint64()) return static_cast<double>(f.get_uint64());
    // 字符串/十进制回退解析
    auto sv = f.get_string();
    double v = 0;
    auto r = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (r.ec == std::errc{}) return v;
    try { return std::stod(std::string(sv)); } catch (...) { return 0.0; }
}

/// 清理字符串为安全的目录名（对齐 Python sanitize_directory_name）
auto sanitize_directory_name(std::string_view name) -> std::string {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '\\' || c == '/' || c == '*' || c == '?' ||
            c == ':' || c == '"' || c == '<' || c == '>' || c == '|')
            continue;
        out += c;
    }
    // 连续空格替换为下划线
    std::string result;
    bool prev_space = false;
    for (char c : out) {
        if (c == ' ') {
            if (!prev_space) result += '_';
            prev_space = true;
        } else {
            result += c;
            prev_space = false;
        }
    }
    if (result.size() > 100) result.resize(100);
    return result;
}

/// 生成随机 hex 字符串（用于存储文件名，模拟 uuid4().hex）
auto generate_hex_id(std::size_t bytes = 16) -> std::string {
    static thread_local std::random_device rd;
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        auto b = static_cast<std::uint8_t>(rd() & 0xFF);
        out += hex[(b >> 4) & 0x0F];
        out += hex[b & 0x0F];
    }
    return out;
}

/// 解析 JSON body（带缓存，避免重复解析）
auto parse_json(std::string_view body) -> nlohmann::json {
    // 约定: API body 必须是 JSON object。为了避免未捕获异常导致进程退出，
    // 这里对解析失败/非 object 的情况统一降级为空对象。
    if (body.empty())
        return nlohmann::json::object();

    // parse(..., allow_exceptions=false) -> 解析失败返回 discarded，不抛异常
    auto j = nlohmann::json::parse(std::string(body), nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return nlohmann::json::object();
    return j;
}

static inline auto json_string(const nlohmann::json& j, const char* key) -> std::string {
    // nlohmann::json::value() throws if the key exists but the value is null/wrong type.
    // We treat "missing or not a string" as empty to avoid unhandled exceptions.
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return {};
    if (!it->is_string()) return {};
    return it->get<std::string>();
}

static inline auto json_bool(const nlohmann::json& j, const char* key, bool def = false) -> bool {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (!it->is_boolean()) return def;
    return it->get<bool>();
}

static inline auto json_int64(const nlohmann::json& j, const char* key, std::int64_t def = 0) -> std::int64_t {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (!it->is_number_integer()) return def;
    return it->get<std::int64_t>();
}


// =============================================================================
// 全局: 连接池指针（在 main 中初始化）
// =============================================================================

static mysql::sharded_connection_pool* g_pool = nullptr;
static openai::client* g_openai = nullptr;

// =============================================================================
// 数据库辅助: 获取连接并执行查询
// =============================================================================

// 慢查询阈值（毫秒）
constexpr int SLOW_QUERY_THRESHOLD_MS = 500;

constexpr int DB_MAX_RETRY = 2;  // MySQL gone away 时最多重试次数

static auto db_should_retry(const mysql::result_set& rs) -> bool {
    if (!rs.is_err()) return false;
    if (rs.error_code == 2006) return true;  // MySQL server has gone away
    if (rs.error_code != 0 && mysql::is_fatal_error(rs.error_code)) return true;

    // Client-side / transport errors (no server error_code).
    // Keep this conservative: only retry obvious disconnect symptoms.
    const auto& msg = rs.error_msg;
    if (msg.empty()) return false;
    if (msg == "not connected") return true;
    if (msg.find("failed to send") != std::string::npos) return true;
    if (msg.find("connection lost") != std::string::npos) return true;
    if (msg.find("no response") != std::string::npos) return true;
    return false;
}

auto db_query(std::string_view sql) -> cn::task<mysql::result_set> {
    for (int attempt = 0; attempt <= DB_MAX_RETRY; ++attempt) {
        auto t0 = std::chrono::steady_clock::now();
        auto conn_r = co_await g_pool->async_get_connection();
        if (!conn_r) {
            if (attempt < DB_MAX_RETRY) continue;
            mysql::result_set rs;
            rs.error_msg = "pool timeout";
            co_return rs;
        }
        auto conn = std::move(*conn_r);
        auto t1 = std::chrono::steady_clock::now();
        auto rs = co_await conn->execute(sql);
        auto t2 = std::chrono::steady_clock::now();

        // 连接断开 / 写失败 / 协议层断开 — 换连接重试
        if (db_should_retry(rs) && attempt < DB_MAX_RETRY) {
            logger::warn("[DB RETRY] code={} msg={} attempt {}", rs.error_code, rs.error_msg, attempt + 1);
            continue;
        }
        if (rs.is_err()) {
            logger::error("[DB ERROR] code={} msg={} SQL: {}", rs.error_code, rs.error_msg, sql);
        }
        conn.return_without_reset();

        auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        auto exec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        auto total_ms = wait_ms + exec_ms;
        if (total_ms >= SLOW_QUERY_THRESHOLD_MS) {
            logger::warn("[SLOW SQL] total={}ms (pool_wait={}ms, exec={}ms) pool_size={}/{} SQL: {}",
                total_ms, wait_ms, exec_ms,
                g_pool->size(), g_pool->idle_count(),
                sql.size() > 200 ? std::format("{}...", sql.substr(0, 200)) : std::string(sql));
        }
        co_return rs;
    }
    mysql::result_set rs;
    rs.error_msg = "max retries exceeded";
    co_return rs;
}

auto db_execute(mysql::with_params_t wp) -> cn::task<mysql::result_set> {
    auto sql_template = wp.query;  // 保存模板用于日志

    for (int attempt = 0; attempt <= DB_MAX_RETRY; ++attempt) {
        auto t0 = std::chrono::steady_clock::now();
        auto conn_r = co_await g_pool->async_get_connection();
        if (!conn_r) {
            if (attempt < DB_MAX_RETRY) continue;
            logger::error("[DB ERROR] pool timeout: {}", conn_r.error().message());
            mysql::result_set rs;
            rs.error_msg = "pool timeout";
            co_return rs;
        }
        auto conn = std::move(*conn_r);
        auto t1 = std::chrono::steady_clock::now();
        auto rs = co_await conn->execute(wp);  // keep wp reusable for retry
        auto t2 = std::chrono::steady_clock::now();

        if (db_should_retry(rs) && attempt < DB_MAX_RETRY) {
            logger::warn("[DB RETRY] code={} msg={} attempt {}", rs.error_code, rs.error_msg, attempt + 1);
            continue;
        }
        if (rs.is_err()) {
            logger::error("[DB ERROR] code={} msg={}", rs.error_code, rs.error_msg);
        }
        conn.return_without_reset();

        auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        auto exec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        auto total_ms = wait_ms + exec_ms;
        if (total_ms >= SLOW_QUERY_THRESHOLD_MS) {
            logger::warn("[SLOW SQL] total={}ms (pool_wait={}ms, exec={}ms) pool_size={}/{} SQL: {}",
                total_ms, wait_ms, exec_ms,
                g_pool->size(), g_pool->idle_count(),
                sql_template.size() > 200 ? std::format("{}...", sql_template.substr(0, 200)) : std::string(sql_template));
        }
        co_return rs;
    }

    mysql::result_set rs;
    rs.error_msg = "max retries exceeded";
    co_return rs;
}

/// ORM 辅助: 从池获取连接并创建 db_session
auto db_orm(cn::io_context* io = nullptr)
    -> cn::task<std::optional<std::pair<mysql::pooled_connection, orm::db_session>>>
{
    std::expected<mysql::pooled_connection, std::error_code> conn_r;
    if (io) {
        conn_r = co_await g_pool->async_get_connection(*io);
    } else {
        conn_r = co_await g_pool->async_get_connection();
    }
    if (!conn_r) co_return std::nullopt;
    auto conn = std::move(*conn_r);
    orm::db_session db(conn.get());
    co_return std::pair{std::move(conn), std::move(db)};
}

// =============================================================================
// GET /api/years — 获取所有存在项目记录的年份
// =============================================================================

auto handle_get_years(http::request_context& ctx) -> cn::task<void> {
    auto conn = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn) {
        ctx.json(503, json_error("Database unavailable"));
        co_return;
    }

    orm::mapper_session session(conn->get(), g_mapper_registry);

    // 使用 ProjectMapper.selectDistinctYears
    auto result = co_await session.query_tuple<std::int64_t>(
        "ProjectMapper.selectDistinctYears",
        std::flat_map<std::string, mysql::param_value>{}
    );

    if (result.is_err()) {
        ctx.json(500, json_error(result.error_msg));
        co_return;
    }

    nlohmann::json years = nlohmann::json::array();
    for (const auto& [year] : result.data) {
        years.push_back(year);
    }
    ctx.json(200, years.dump());
}

// =============================================================================
// GET /api/projects — 按日期范围查询（★ when_all 并行查询 ★）
// =============================================================================

/// 查询 projects 主数据（协程 A）- 使用 XML Mapper
auto query_projects(mysql::client& conn, std::string_view start_date, std::string_view end_date)
    -> cn::task<mysql::result_set>
{
    orm::mapper_session session(conn, g_mapper_registry);

    std::flat_map<std::string, mysql::param_value> params;
    params["start_date"] = mysql::param_value::from_string(std::string(start_date));
    params["end_date"] = mysql::param_value::from_string(std::string(end_date));

    co_return co_await session.execute_query("ProjectMapper.selectWithExpensesByDateRange", std::move(params));
}

/// 查询 project_files（协程 B）- 使用 XML Mapper
auto query_files(mysql::client& conn, std::string_view start_date, std::string_view end_date)
    -> cn::task<mysql::result_set>
{
    orm::mapper_session session(conn, g_mapper_registry);

    std::flat_map<std::string, mysql::param_value> params;
    params["start_date"] = mysql::param_value::from_string(std::string(start_date));
    params["end_date"] = mysql::param_value::from_string(std::string(end_date));

    co_return co_await session.execute_query("ProjectMapper.selectFilesByDateRange", std::move(params));
}

/// 合并 projects + files 为 JSON 字符串
auto build_projects_json(const mysql::result_set& proj_rs,
                         const mysql::result_set& file_rs) -> std::string
{
    // 构建 files_map: project_id → [{id, name}, ...]
    std::unordered_map<std::int64_t, std::vector<std::pair<std::int64_t, std::string>>> files_map;
    for (auto& row : file_rs.rows) {
        auto pid = row[1].get_int64();  // project_id
        files_map[pid].emplace_back(row[0].get_int64(), row[2].to_string());
    }

    // 构建 projects（去重 + 聚合 expenses）
    struct project_data {
        std::int64_t id;
        std::string title, status, created_date, delivery_date, remarks;
        std::string client_name, client_wechat;
        double amount, commission;
        std::vector<std::pair<std::string, double>> expenses;  // item, amount
    };
    std::vector<std::int64_t> order;
    std::unordered_map<std::int64_t, project_data> projects;

    for (auto& row : proj_rs.rows) {
        auto pid = row[0].get_int64();
        if (projects.find(pid) == projects.end()) {
            order.push_back(pid);
            auto& p = projects[pid];
            p.id = pid;
            p.title = row[1].to_string();
            p.amount = to_double(row[2]);
            p.commission = to_double(row[3]);
            p.status = row[4].to_string();
            p.created_date = row[5].to_string();
            p.delivery_date = row[6].is_null() ? "" : row[6].to_string();
            p.remarks = row[7].is_null() ? "" : row[7].to_string();
            p.client_name = row[8].to_string();
            p.client_wechat = row[9].to_string();
        }
        // expense (可能为 NULL — LEFT JOIN)
        if (!row[10].is_null()) {
            projects[pid].expenses.emplace_back(
                row[11].to_string(), to_double(row[12]));
        }
    }

    // 序列化为 JSON
    nlohmann::json result = nlohmann::json::array();
    for (auto pid : order) {
        auto& p = projects[pid];
        nlohmann::json proj;
        proj["id"] = p.id;
        proj["projectTitle"] = p.title;
        proj["client"] = {
            {"name", p.client_name},
            {"wechatId", p.client_wechat}
        };
        proj["financials"] = {
            {"totalAmount", p.amount},
            {"commission", p.commission}
        };
        proj["status"] = p.status;
        proj["createdDate"] = p.created_date;
        proj["deliveryDate"] = p.delivery_date.empty() ? nullptr : nlohmann::json(p.delivery_date);
        proj["remarks"] = p.remarks;

        // expenses
        nlohmann::json expenses = nlohmann::json::array();
        for (auto& [item, amt] : p.expenses) {
            expenses.push_back({{"item", item}, {"amount", amt}});
        }
        proj["expenses"] = expenses;

        // files
        nlohmann::json files = nlohmann::json::array();
        auto fit = files_map.find(pid);
        if (fit != files_map.end()) {
            for (auto& [fid, fname] : fit->second) {
                files.push_back({{"id", fid}, {"name", fname}});
            }
        }
        proj["files"] = files;

        result.push_back(proj);
    }

    return result.dump();
}

auto handle_get_projects(http::request_context& ctx) -> cn::task<void> {
    auto start_date = http::parse_query_param(ctx.query_string(), "startDate");
    auto end_date   = http::parse_query_param(ctx.query_string(), "endDate");
    if (start_date.empty() || end_date.empty()) {
        ctx.json(400, json_error("必须提供startDate和endDate参数"));
        co_return;
    }

    auto conn_r = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn_r) {
        ctx.json(500, json_error("数据库连接失败"));
        co_return;
    }
    auto conn = std::move(*conn_r);

    // 顺序查询 — 每个请求只占1个连接，高并发下吞吐翻倍
    auto proj_rs = co_await query_projects(conn.get(), start_date, end_date);
    auto file_rs = co_await query_files(conn.get(), start_date, end_date);

    if (proj_rs.is_err()) {
        ctx.json(500, json_error(proj_rs.error_msg));
        co_return;
    }

    auto json = build_projects_json(proj_rs, file_rs);
    ctx.json(200, json);
}

// =============================================================================
// POST /api/projects — 创建项目（事务）
// =============================================================================

auto handle_create_project(http::request_context& ctx) -> cn::task<void> {
    auto j = parse_json(ctx.body());

    auto title      = j.value("projectTitle", std::string{});
    auto status      = j.value("status", std::string{});
    auto created     = j.value("createdDate", std::string{});
    auto delivery    = j.value("deliveryDate", std::string{});
    auto remarks     = j.value("remarks", std::string{});

    // 嵌套 client 对象: data.client.{name, wechatId}
    auto client_obj    = j.value("client", nlohmann::json::object());
    auto client_name   = client_obj.value("name", std::string{});
    auto client_wechat = client_obj.value("wechatId", std::string{});

    // 嵌套 financials 对象: data.financials.{totalAmount, commission}
    auto fin_obj    = j.value("financials", nlohmann::json::object());
    auto amount     = fin_obj.value("totalAmount", 0.0);
    auto commission = fin_obj.value("commission", 0.0);

    auto conn_r = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto conn = std::move(*conn_r);
    orm::db_session db(conn.get());

    std::uint64_t project_id = 0;

    // 使用事务
    auto tx_result = co_await db.transaction([&]() -> cn::task<void> {
        // 查找或创建 client（ORM）
        auto client_qb = orm::select<Client>()
            .where("`wechat_id` = {}", {mysql::param_value::from_string(client_wechat)})
            .limit(1);
        auto client_rs = co_await db.find(client_qb);

        std::int64_t client_id = 0;
        if (!client_rs.empty()) {
            client_id = client_rs.data[0].id;
        } else {
            Client client;
            client.wechat_id = client_wechat;
            client.name      = client_name;
            auto ins = co_await db.insert(client);
            if (ins.is_err()) {
                throw std::runtime_error(ins.error_msg);
            }
            client_id = client.id;
        }

        // 插入 project（使用 XML Mapper）
        orm::mapper_session session(conn.get(), g_mapper_registry);

        std::flat_map<std::string, mysql::param_value> params;
        params["client_id"] = mysql::param_value::from_int(client_id);
        params["title"] = mysql::param_value::from_string(title);
        params["total_amount"] = mysql::param_value::from_double(amount);
        params["commission"] = mysql::param_value::from_double(commission);
        params["status"] = mysql::param_value::from_string(status);
        params["created_date"] = mysql::param_value::from_string(created);

        // 可选字段
        if (!delivery.empty()) {
            params["delivery_date"] = mysql::param_value::from_string(delivery);
        }
        if (!remarks.empty()) {
            params["remarks"] = mysql::param_value::from_string(remarks);
        }

        auto proj_result = co_await session.execute("ProjectMapper.insertFull", params);
        if (proj_result.is_err()) {
            throw std::runtime_error(proj_result.error_msg);
        }
        project_id = proj_result.last_insert_id;

        // 插入 expenses 数组（ORM）
        if (j.contains("expenses") && j["expenses"].is_array()) {
            for (auto& exp : j["expenses"]) {
                auto item       = exp.value("item", std::string{});
                auto exp_amount = exp.value("amount", 0.0);
                if (!item.empty()) {
                    Expense expense;
                    expense.project_id = static_cast<std::int64_t>(project_id);
                    expense.item_name  = item;
                    expense.amount     = exp_amount;
                    auto ins_exp = co_await db.insert(expense);
                    if (ins_exp.is_err()) {
                        throw std::runtime_error(ins_exp.error_msg);
                    }
                }
            }
        }

        co_return;
    });

    if (tx_result.is_err()) {
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(tx_result.error_msg)));
        co_return;
    }

    conn.return_without_reset();
    ctx.json(201, std::format(R"({{"message":"项目创建成功","id":{}}})", project_id));
}

// =============================================================================
// PUT /api/projects/:id — 更新项目
// =============================================================================

auto handle_update_project(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");
    std::int64_t project_id = 0;
    std::from_chars(id_str.data(), id_str.data() + id_str.size(), project_id);

    auto j = parse_json(ctx.body());
    auto title      = j.value("projectTitle", std::string{});
    auto status      = j.value("status", std::string{});
    auto created     = j.value("createdDate", std::string{});
    auto delivery    = j.value("deliveryDate", std::string{});
    auto remarks     = j.value("remarks", std::string{});

    // 嵌套 client 对象: data.client.{name, wechatId}
    auto client_obj    = j.value("client", nlohmann::json::object());
    auto client_name   = client_obj.value("name", std::string{});
    auto client_wechat = client_obj.value("wechatId", std::string{});

    // 嵌套 financials 对象: data.financials.{totalAmount, commission}
    auto fin_obj    = j.value("financials", nlohmann::json::object());
    auto amount     = fin_obj.value("totalAmount", 0.0);
    auto commission = fin_obj.value("commission", 0.0);

    auto conn_r = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto conn = std::move(*conn_r);
    orm::db_session db(conn.get());

    auto tx_result = co_await db.transaction([&]() -> cn::task<void> {
        // 查找或创建 client（ORM）
        auto client_qb = orm::select<Client>()
            .where("`wechat_id` = {}", {mysql::param_value::from_string(client_wechat)})
            .limit(1);
        auto client_rs = co_await db.find(client_qb);

        std::int64_t client_id = 0;
        if (!client_rs.empty()) {
            client_id = client_rs.data[0].id;
        } else {
            Client client;
            client.wechat_id = client_wechat;
            client.name      = client_name;
            auto ins = co_await db.insert(client);
            if (ins.is_err()) {
                throw std::runtime_error(ins.error_msg);
            }
            client_id = client.id;
        }

        // 更新 project（使用 XML Mapper）
        orm::mapper_session session(conn.get(), g_mapper_registry);

        std::flat_map<std::string, mysql::param_value> params;
        params["id"] = mysql::param_value::from_int(project_id);
        params["client_id"] = mysql::param_value::from_int(client_id);
        params["title"] = mysql::param_value::from_string(title);
        params["total_amount"] = mysql::param_value::from_double(amount);
        params["commission"] = mysql::param_value::from_double(commission);
        params["status"] = mysql::param_value::from_string(status);
        params["created_date"] = mysql::param_value::from_string(created);

        // 可选字段
        if (!delivery.empty()) {
            params["delivery_date"] = mysql::param_value::from_string(delivery);
        } else {
            params["delivery_date"] = mysql::param_value::from_string("");  // 触发 NULL
        }
        if (!remarks.empty()) {
            params["remarks"] = mysql::param_value::from_string(remarks);
        }

        auto upd_result = co_await session.execute("ProjectMapper.updateFull", params);
        if (upd_result.is_err()) {
            throw std::runtime_error(upd_result.error_msg);
        }

        // 重建 expenses（ORM 先删后插）
        auto del_exp = co_await db.remove(orm::delete_of<Expense>()
            .where("`project_id` = {}", {mysql::param_value::from_int(project_id)}));
        if (del_exp.is_err()) {
            throw std::runtime_error(del_exp.error_msg);
        }

        if (j.contains("expenses") && j["expenses"].is_array()) {
            for (auto& exp : j["expenses"]) {
                auto item       = exp.value("item", std::string{});
                auto exp_amount = exp.value("amount", 0.0);
                if (!item.empty()) {
                    Expense expense;
                    expense.project_id = project_id;
                    expense.item_name  = item;
                    expense.amount     = exp_amount;
                    auto ins_exp = co_await db.insert(expense);
                    if (ins_exp.is_err()) {
                        throw std::runtime_error(ins_exp.error_msg);
                    }
                }
            }
        }

        co_return;
    });

    if (tx_result.is_err()) {
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(tx_result.error_msg)));
        co_return;
    }

    conn.return_without_reset();

    ctx.json(200, R"({"message":"项目更新成功"})");
}

// =============================================================================
// DELETE /api/projects/:id — 删除项目及文件
// =============================================================================

auto handle_delete_project(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");
    std::int64_t project_id = 0;
    std::from_chars(id_str.data(), id_str.data() + id_str.size(), project_id);

    auto conn_r = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto conn = std::move(*conn_r);
    orm::db_session db(conn.get());

    std::string project_dir;
    auto tx_result = co_await db.transaction([&]() -> cn::task<void> {
        // 查询文件路径，用于清理物理目录（ORM）
        auto file_qb = orm::select<ProjectFile>()
            .where("`project_id` = {}", {mysql::param_value::from_int(project_id)})
            .limit(1);
        auto file_rs = co_await db.find(file_qb);
        if (!file_rs.empty()) {
            auto& filepath = file_rs.data[0].filepath;
            auto sep = filepath.find('/');
            if (sep == std::string::npos) sep = filepath.find('\\');
            if (sep != std::string::npos) {
                project_dir = (std::filesystem::path(g_cfg.upload_dir) /
                               filepath.substr(0, sep)).string();
            }
        }

        // 删除关联数据（ORM）
        auto del_exp = co_await db.remove(orm::delete_of<Expense>()
            .where("`project_id` = {}", {mysql::param_value::from_int(project_id)}));
        if (del_exp.is_err()) {
            throw std::runtime_error(del_exp.error_msg);
        }

        auto del_file = co_await db.remove(orm::delete_of<ProjectFile>()
            .where("`project_id` = {}", {mysql::param_value::from_int(project_id)}));
        if (del_file.is_err()) {
            throw std::runtime_error(del_file.error_msg);
        }

        // 删除 project（使用 XML Mapper）
        orm::mapper_session session(conn.get(), g_mapper_registry);
        std::flat_map<std::string, mysql::param_value> params;
        params["id"] = mysql::param_value::from_int(project_id);

        auto del = co_await session.execute("ProjectMapper.delete", params);
        if (del.is_err()) {
            throw std::runtime_error(del.error_msg);
        }

        co_return;
    });

    conn.return_without_reset();

    // 清理物理文件目录
    if (!project_dir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(project_dir, ec);
    }

    if (tx_result.is_err())
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(tx_result.error_msg)));
    else
        ctx.json(200, R"({"message":"项目及其所有文件已删除"})");
}

// =============================================================================
// POST /api/projects/:id/files — 上传文件（multipart/form-data）
// =============================================================================

auto handle_upload_files(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");
    std::int64_t project_id = 0;
    std::from_chars(id_str.data(), id_str.data() + id_str.size(), project_id);

    // 解析 multipart body
    auto form_r = ctx.parse_form();
    if (!form_r) {
        ctx.json(400, R"({"error":"没有文件部分"})");
        co_return;
    }
    auto& form = **form_r;
    auto uploaded_files = form.files("files");
    if (uploaded_files.empty()) {
        ctx.json(400, R"({"error":"没有选择文件"})");
        co_return;
    }

    // 查询项目标题
    auto conn_r = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }

    orm::mapper_session session(conn_r->get(), g_mapper_registry);
    std::flat_map<std::string, mysql::param_value> params;
    params["id"] = mysql::param_value::from_int(project_id);

    auto title_result = co_await session.query_tuple<std::string>("ProjectMapper.selectTitleById", params);
    if (title_result.is_err() || title_result.data.empty()) {
        ctx.json(404, R"({"error":"项目未找到"})");
        co_return;
    }

    auto [title] = title_result.data[0];
    auto dir_name = sanitize_directory_name(title) + "_" + std::to_string(project_id);
    auto project_dir = std::filesystem::path(g_cfg.upload_dir) / dir_name;
    std::error_code ec;
    std::filesystem::create_directories(project_dir, ec);

    orm::db_session db(conn_r->get());

    int saved_count = 0;
    for (auto* ff : uploaded_files) {
        auto original_name = ff->filename;
        if (original_name.empty()) continue;

        // 生成存储文件名: uuid_hex + 扩展名
        auto ext_pos = original_name.rfind('.');
        auto ext = (ext_pos != std::string::npos) ? original_name.substr(ext_pos) : "";
        auto stored_name = generate_hex_id() + ext;
        auto rel_path = dir_name + "/" + stored_name;
        auto full_path = project_dir / stored_name;

        // 写入文件
        auto f = co_await cn::async_file_open(ctx.io_ctx(), full_path,
            cn::open_mode::write | cn::open_mode::create | cn::open_mode::truncate);
        if (!f) continue;
        auto wr = co_await cn::async_file_write(ctx.io_ctx(), *f,
            cn::const_buffer{ff->data.data(), ff->data.size()});
        if (!wr) continue;

        // 异步刷新和关闭文件
        co_await cn::async_file_flush(ctx.io_ctx(), *f);
        co_await cn::async_file_close(ctx.io_ctx(), *f);

        // 插入 DB 记录（ORM）
        ProjectFile file_model;
        file_model.project_id        = project_id;
        file_model.original_filename = original_name;
        file_model.stored_filename   = stored_name;
        file_model.filepath          = rel_path;
        co_await db.insert(file_model);
        ++saved_count;
    }

    ctx.json(201, std::format(
        R"({{"message":"{}\u4e2a\u6587\u4ef6\u4e0a\u4f20\u6210\u529f"}})", saved_count));
}

// =============================================================================
// DELETE /api/files/:id — 删除单个文件
// =============================================================================

auto handle_delete_file(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");

    auto orm_r = co_await db_orm(&ctx.io_ctx());
    if (!orm_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto& [conn, db] = *orm_r;

    auto rs = co_await db.find_by_id<ProjectFile>(
        mysql::param_value::from_string(std::string(id_str)));
    if (rs.empty()) {
        ctx.json(404, R"({"error":"文件未找到"})");
        co_return;
    }

    auto& pf = rs.data[0];
    auto full_path = std::filesystem::path(g_cfg.upload_dir) / pf.filepath;
    std::error_code ec;
    std::filesystem::remove(full_path, ec);

    co_await db.remove(pf);
    conn.return_without_reset();

    ctx.json(200, R"({"message":"文件删除成功"})");
}

// =============================================================================
// POST /api/files/batch-delete — 批量删除文件
// =============================================================================

auto handle_batch_delete_files(http::request_context& ctx) -> cn::task<void> {
    auto j = parse_json(ctx.body());
    if (!j.contains("file_ids") || !j["file_ids"].is_array() || j["file_ids"].empty()) {
        ctx.json(400, R"({"error":"需要提供文件ID列表"})");
        co_return;
    }

    auto conn_r = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto conn = std::move(*conn_r);
    orm::db_session db(conn.get());

    std::int64_t affected = 0;
    auto tx_result = co_await db.transaction([&]() -> cn::task<void> {
        // 构建 IN 列表并查询文件路径（ORM）
        std::string id_list;
        for (auto& fid : j["file_ids"]) {
            if (!id_list.empty()) id_list += ",";
            id_list += std::to_string(fid.get<std::int64_t>());
        }

        auto file_rs = co_await db.find(
            orm::select<ProjectFile>().where(std::format("`id` IN ({})", id_list)));

        // 删除物理文件
        for (auto& pf : file_rs.data) {
            auto full_path = std::filesystem::path(g_cfg.upload_dir) / pf.filepath;
            std::error_code ec;
            std::filesystem::remove(full_path, ec);
        }

        // 删除 DB 记录（ORM）
        auto del = co_await db.remove(
            orm::delete_of<ProjectFile>().where(std::format("`id` IN ({})", id_list)));
        if (del.is_err()) {
            throw std::runtime_error(del.error_msg);
        }
        affected = del.affected_rows;

        co_return;
    });

    conn.return_without_reset();

    if (tx_result.is_err()) {
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(tx_result.error_msg)));
    } else {
        ctx.json(200, std::format(
            R"({{"message":"{}\u4e2a\u6587\u4ef6\u6279\u91cf\u5220\u9664\u6210\u529f"}})", affected));
    }
}

// =============================================================================
// GET /api/files/download/:id — 下载文件
// =============================================================================

auto handle_download_file(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");

    // ORM 查询文件记录
    auto orm_r = co_await db_orm(&ctx.io_ctx());
    if (!orm_r) {
        ctx.text(500, "数据库连接失败");
        co_return;
    }
    auto& [conn, db] = *orm_r;
    auto rs = co_await db.find_by_id<ProjectFile>(
        mysql::param_value::from_string(std::string(id_str)));
    conn.return_without_reset();  // 早期释放连接，后续是流式发送
    if (rs.empty()) {
        ctx.text(404, "文件未找到");
        co_return;
    }

    auto& pf = rs.data[0];
    auto full_path = std::filesystem::path(g_cfg.upload_dir) / pf.filepath;

    std::error_code ec;
    if (!std::filesystem::exists(full_path, ec)) {
        ctx.text(404, "文件未找到");
        co_return;
    }

    auto f = co_await cn::async_file_open(ctx.io_ctx(), full_path, cn::open_mode::read);
    if (!f) {
        ctx.text(500, "无法打开文件");
        co_return;
    }
    auto file_size_r = f->size();
    if (!file_size_r) {
        ctx.text(500, "无法读取文件大小");
        co_return;
    }
    auto file_size = *file_size_r;

    // URL 编码文件名（RFC 5987）
    auto encoded_name = http::url_encode(pf.original_filename);

    auto& resp = ctx.resp();
    resp.set_status(200);
    resp.set_header("Content-Type", "application/octet-stream");
    resp.set_header("Content-Length", std::to_string(file_size));
    resp.set_header("Content-Disposition",
        std::format("attachment; filename*=UTF-8''{}", encoded_name));

    // 流式发送: 先发响应头，再分块发送文件内容
    auto header_data = resp.serialize();
    auto wr = co_await cn::async_write(ctx.io_ctx(), ctx.raw_socket(),
        cn::const_buffer{header_data.data(), header_data.size()});
    if (!wr) co_return;

    constexpr std::size_t CHUNK_SIZE = 65536;
    std::vector<std::byte> buf(CHUNK_SIZE);
    std::uint64_t offset = 0;
    std::uint64_t remaining = file_size;

    while (remaining > 0) {
        auto to_read = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, CHUNK_SIZE));
        auto rd = co_await cn::async_file_read(ctx.io_ctx(), *f,
            cn::mutable_buffer{buf.data(), to_read}, offset);
        if (!rd || *rd == 0) break;

        auto wf = co_await cn::async_write(ctx.io_ctx(), ctx.raw_socket(),
            cn::const_buffer{buf.data(), *rd});
        if (!wf) break;

        offset += *rd;
        remaining -= *rd;
    }

    // 异步关闭文件
    co_await cn::async_file_close(ctx.io_ctx(), *f);

    resp.set_header("X-Streamed", "1");
}

// =============================================================================
// POST /api/login — 登录
// =============================================================================

auto handle_login(http::request_context& ctx) -> cn::task<void> {
    auto j = parse_json(ctx.body());
    auto username = j.value("username", std::string{});
    auto password = j.value("password", std::string{});

    if (username.empty() || password.empty()) {
        ctx.json(400, R"({"error":"缺少用户名或密码"})");
        co_return;
    }

    auto pass_hash = sha256_hex(password);

    if (username == g_cfg.admin_user && pass_hash == g_cfg.admin_pass_hash) {
        auto token = generate_jwt(username);
        ctx.json(200, std::format(
            R"({{"status":"success","access_token":"{}","token_type":"Bearer",)"
            R"("expires_in":{},"user":{{"username":"{}","role":"admin"}}}})",
            token, g_cfg.jwt_expiry_seconds, username));
    } else {
        ctx.json(401, R"({"error":"用户名或密码错误"})");
    }
}

// =============================================================================
// GET /api/check-auth — 验证认证状态
// =============================================================================

auto handle_check_auth(http::request_context& ctx) -> cn::task<void> {
    auto token = get_bearer_token(ctx);
    if (token.empty()) {
        ctx.json(401, R"({"authenticated":false,"error":"未找到认证令牌"})");
        co_return;
    }
    auto user = verify_jwt(token);
    if (!user) {
        ctx.json(401, R"({"authenticated":false,"error":"无效或已过期的令牌"})");
        co_return;
    }
    ctx.json(200, std::format(
        R"({{"authenticated":true,"user":{{"username":"{}","role":"admin"}}}})",
        json_escape(*user)));
}

// =============================================================================
// POST /api/statistics — 图表统计数据
// =============================================================================

auto handle_statistics(http::request_context& ctx) -> cn::task<void> {
    auto j = parse_json(ctx.body());
    auto start_date = json_string(j, "startDate");
    auto end_date   = json_string(j, "endDate");

    if (start_date.empty() || end_date.empty()) {
        ctx.json(400, json_error("必须提供startDate和endDate参数"));
        co_return;
    }

    // 注意: 同一个 MySQL 连接不能并发执行两条 query（会导致 packets out of order）。
    // 这里为了保留并行性能，借两条连接分别执行。
    auto [conn_a_r, conn_b_r] = co_await cn::when_all(
        g_pool->async_get_connection(ctx.io_ctx()),
        g_pool->async_get_connection(ctx.io_ctx()));

    if (!conn_a_r || !conn_b_r) {
        ctx.json(500, json_error("数据库连接失败"));
        co_return;
    }

    auto conn_a = std::move(*conn_a_r);
    auto conn_b = std::move(*conn_b_r);

    // ★ 并行查询（不同连接）★
    auto [proj_rs, file_rs] = co_await cn::when_all(
        query_projects(conn_a.get(), start_date, end_date),
        query_files(conn_b.get(), start_date, end_date));

    if (proj_rs.is_err()) {
        ctx.json(500, json_error(proj_rs.error_msg));
        co_return;
    }

    // 聚合统计（对齐 Python 逻辑：基于去重后的项目求和）
    struct monthly_info { double total = 0; double comm = 0; };
    std::map<std::string, monthly_info> monthly;       // key: YYYY-MM
    std::map<std::string, double> client_contrib;      // 客户 -> 金额
    std::map<std::string, int> status_dist;            // 状态 -> 数量

    auto to_month = [](const mysql::field_value& f) -> std::string {
        if (f.is_date())     { auto s = f.get_date().to_string();     return s.substr(0, 7); }
        if (f.is_datetime()) { auto s = f.get_datetime().to_string(); return s.substr(0, 7); }
        auto s = std::string(f.get_string());
        if (s.size() >= 7) return s.substr(0, 7);
        return s;
    };

    std::unordered_set<std::int64_t> seen;
    for (auto& row : proj_rs.rows) {
        auto pid = row[0].get_int64();
        if (seen.count(pid)) continue; // 每个项目只计一次
        seen.insert(pid);

        auto month = to_month(row[5]);
        double amt  = to_double(row[2]);
        double comm = to_double(row[3]);

        monthly[month].total += amt;
        monthly[month].comm  += comm;
        auto client_name = row[8].to_string();
        if (!client_name.empty()) client_contrib[client_name] += amt;
        status_dist[row[4].to_string()]++;
    }

    // 构建 JSON 响应
    nlohmann::json result;

    // monthlyIncome
    nlohmann::json months = nlohmann::json::array();
    nlohmann::json totalAmounts = nlohmann::json::array();
    nlohmann::json commissions = nlohmann::json::array();
    for (auto& [month, info] : monthly) {
        months.push_back(month);
        totalAmounts.push_back(std::round(info.total * 100) / 100);  // 保留2位小数
        commissions.push_back(std::round(info.comm * 100) / 100);
    }
    result["monthlyIncome"] = {
        {"months", months},
        {"totalAmounts", totalAmounts},
        {"commissions", commissions}
    };

    // clientContribution
    nlohmann::json clientContrib = nlohmann::json::array();
    for (auto& [name, val] : client_contrib) {
        clientContrib.push_back({
            {"name", name},
            {"value", std::round(val * 100) / 100}
        });
    }
    result["clientContribution"] = clientContrib;

    // projectStatus
    nlohmann::json projectStatus = nlohmann::json::array();
    for (auto& [name, val] : status_dist) {
        projectStatus.push_back({
            {"name", name},
            {"value", val}
        });
    }
    result["projectStatus"] = projectStatus;

    ctx.json(200, result.dump());
}

// =============================================================================
// POST /api/overall-stats — 总计数据
// =============================================================================

auto handle_overall_stats(http::request_context& ctx) -> cn::task<void> {
    auto j = parse_json(ctx.body());
    auto start_date = json_string(j, "startDate");
    auto end_date   = json_string(j, "endDate");

    if (start_date.empty() || end_date.empty()) {
        ctx.json(400, R"({"error":"必须提供startDate和endDate参数"})");
        co_return;
    }

    // 同一连接并发查询会导致 packets out of order，这里借两条连接并行跑两条统计 SQL。
    auto [conn_a_r, conn_b_r] = co_await cn::when_all(
        g_pool->async_get_connection(ctx.io_ctx()),
        g_pool->async_get_connection(ctx.io_ctx()));
    if (!conn_a_r || !conn_b_r) {
        ctx.json(503, json_error("数据库连接失败"));
        co_return;
    }

    auto conn_a = std::move(*conn_a_r);
    auto conn_b = std::move(*conn_b_r);

    orm::mapper_session session_a(conn_a.get(), g_mapper_registry);
    orm::mapper_session session_b(conn_b.get(), g_mapper_registry);
    std::flat_map<std::string, mysql::param_value> params;
    params["start_date"] = mysql::param_value::from_string(start_date);
    params["end_date"] = mysql::param_value::from_string(end_date);

    auto [financial_result, expense_result] = co_await cn::when_all(
        session_a.query_tuple<double, double>("ProjectMapper.selectFinancialSummary", params),
        session_b.query_tuple<double>("ProjectMapper.selectTotalExpenses", params)
    );

    if (financial_result.is_err()) {
        ctx.json(500, json_error(financial_result.error_msg));
        co_return;
    }

    double total_income = 0, total_commission = 0, total_expenses = 0;
    if (!financial_result.data.empty()) {
        const auto& [income, commission] = financial_result.data[0];
        total_income = income;
        total_commission = commission;
    }
    if (!expense_result.is_err() && !expense_result.data.empty()) {
        const auto& [expenses] = expense_result.data[0];
        total_expenses = expenses;
    }

    double expenses_combined = total_commission + total_expenses;
    double balance = total_income - expenses_combined;

    nlohmann::json result = {
        {"totalIncome", std::round(total_income * 100) / 100},
        {"totalExpenses", std::round(expenses_combined * 100) / 100},
        {"balance", std::round(balance * 100) / 100}
    };
    ctx.json(200, result.dump());
}

// =============================================================================
// GET /api/clients/search — 精确搜索
// =============================================================================

auto handle_client_search(http::request_context& ctx) -> cn::task<void> {
    auto wechat_id = http::parse_query_param(ctx.query_string(), "wechat_id");
    if (wechat_id.empty()) {
        ctx.json(400, R"({"error":"缺少微信ID参数"})");
        co_return;
    }

    auto conn = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn) {
        ctx.json(503, json_error("数据库连接失败"));
        co_return;
    }

    orm::mapper_session session(conn->get(), g_mapper_registry);
    std::flat_map<std::string, mysql::param_value> params;
    params["wechat_id"] = mysql::param_value::from_string(std::string(wechat_id));

    auto result_obj = co_await session.query_tuple<std::string, std::string>(
        "ClientMapper.selectByWechatId", params);

    if (result_obj.is_err() || result_obj.data.empty()) {
        ctx.json(404, R"({"name":null,"message":"未找到该微信ID的发单人"})");
    } else {
        const auto& [name, wechat] = result_obj.data[0];
        ctx.json(200, std::format(R"({{"name":"{}","wechat_id":"{}"}})",
            json_escape(name),
            json_escape(wechat)));
    }
}

// =============================================================================
// GET /api/clients/search-fuzzy — 模糊搜索
// =============================================================================

auto handle_client_fuzzy(http::request_context& ctx) -> cn::task<void> {
    auto q = http::parse_query_param(ctx.query_string(), "q");
    if (q.empty()) {
        ctx.json(200, "[]");
        co_return;
    }

    auto conn = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn) {
        ctx.json(503, json_error("数据库连接失败"));
        co_return;
    }

    orm::mapper_session session(conn->get(), g_mapper_registry);
    std::flat_map<std::string, mysql::param_value> params;
    params["keyword"] = mysql::param_value::from_string(std::string(q));

    auto result_obj = co_await session.query_tuple<std::string, std::string>(
        "ClientMapper.searchFuzzy", params);

    if (result_obj.is_err()) {
        ctx.json(500, json_error(result_obj.error_msg));
        co_return;
    }

    nlohmann::json result = nlohmann::json::array();
    for (const auto& [wechat_id, name] : result_obj.data) {
        result.push_back({
            {"wechatId", wechat_id},
            {"name", name}
        });
    }
    ctx.json(200, result.dump());
}

// =============================================================================
// POST /api/logout
// =============================================================================

auto handle_logout(http::request_context& ctx) -> cn::task<void> {
    ctx.json(200, R"({"status":"success"})");
    co_return;
}

// =============================================================================
// GET /api/ai/prompts — 获取所有提示词
// =============================================================================

auto handle_get_prompts(http::request_context& ctx) -> cn::task<void> {
    auto orm_r = co_await db_orm(&ctx.io_ctx());
    if (!orm_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto& [conn, db] = *orm_r;
    auto rs = co_await db.find(
        orm::select<AiPrompt>().order_by("`is_default` DESC, `created_at` DESC"));
    conn.return_without_reset();
    if (rs.is_err()) {
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(rs.error_msg)));
        co_return;
    }
    std::string json = "[";
    for (std::size_t i = 0; i < rs.data.size(); ++i) {
        if (i > 0) json += ",";
        auto& p = rs.data[i];
        json += std::format(
            R"({{"id":{},"name":"{}","prompt_template":"{}","is_default":{},"created_at":"{}"}})",
            p.id,
            json_escape(p.name),
            json_escape(p.prompt_template),
            p.is_default ? "true" : "false",
            p.created_at.value_or(""));
    }
    json += "]";
    ctx.json(200, json);
}

// =============================================================================
// POST /api/ai/prompts — 创建提示词
// =============================================================================

auto handle_create_prompt(http::request_context& ctx) -> cn::task<void> {
    auto j = parse_json(ctx.body());
    auto name = j.value("name", std::string{});
    auto tpl  = j.value("prompt_template", std::string{});
    if (name.empty() || tpl.empty()) {
        ctx.json(400, R"({"error":"名称和模板不能为空"})");
        co_return;
    }
    auto orm_r = co_await db_orm(&ctx.io_ctx());
    if (!orm_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto& [conn, db] = *orm_r;
    AiPrompt prompt;
    prompt.name            = name;
    prompt.prompt_template = tpl;
    prompt.is_default      = false;
    prompt.created_at      = now_datetime();
    auto rs = co_await db.insert(prompt);
    conn.return_without_reset();
    if (rs.is_err()) {
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(rs.error_msg)));
        co_return;
    }
    ctx.json(201, std::format(R"({{"message":"提示词创建成功","id":{}}})", prompt.id));
}

// =============================================================================
// PUT /api/ai/prompts/:id — 更新提示词
// =============================================================================

auto handle_update_prompt(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");
    auto j = parse_json(ctx.body());
    auto name = j.value("name", std::string{});
    auto tpl  = j.value("prompt_template", std::string{});
    if (name.empty() || tpl.empty()) {
        ctx.json(400, R"({"error":"名称和模板不能为空"})");
        co_return;
    }
    auto orm_r = co_await db_orm(&ctx.io_ctx());
    if (!orm_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto& [conn, db] = *orm_r;
    // 加载现有记录，保留 is_default / created_at
    auto existing = co_await db.find_by_id<AiPrompt>(
        mysql::param_value::from_string(std::string(id_str)));
    if (existing.empty()) {
        conn.return_without_reset();
        ctx.json(404, R"({"error":"提示词未找到"})");
        co_return;
    }
    auto prompt = existing.data[0];
    prompt.name            = name;
    prompt.prompt_template = tpl;
    auto rs = co_await db.update(prompt);
    conn.return_without_reset();
    if (rs.is_err())
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(rs.error_msg)));
    else
        ctx.json(200, R"({"message":"提示词更新成功"})");
}

// =============================================================================
// DELETE /api/ai/prompts/:id — 删除提示词（不可删默认）
// =============================================================================

auto handle_delete_prompt(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");
    auto orm_r = co_await db_orm(&ctx.io_ctx());
    if (!orm_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto& [conn, db] = *orm_r;
    auto rs = co_await db.remove(
        orm::delete_of<AiPrompt>().where(
            "`id` = {} AND `is_default` = FALSE",
            {mysql::param_value::from_string(std::string(id_str))}));
    conn.return_without_reset();
    if (rs.is_err()) {
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(rs.error_msg)));
        co_return;
    }
    if (rs.affected_rows == 0)
        ctx.json(400, R"({"error":"无法删除默认提示词"})");
    else
        ctx.json(200, R"({"message":"提示词删除成功"})");
}

// =============================================================================
// PUT /api/ai/prompts/:id/default — 设置默认提示词
// =============================================================================

auto handle_set_default_prompt(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");
    auto conn_r = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn_r) { ctx.json(500, R"({"error":"数据库连接失败"})"); co_return; }
    auto conn = std::move(*conn_r);
    orm::db_session db(conn.get());

    auto tx_result = co_await db.transaction([&]() -> cn::task<void> {
        orm::mapper_session session(conn.get(), g_mapper_registry);

        // 批量重置（使用 XML Mapper）
        auto reset_rs = co_await session.execute("AiMapper.clearDefaultPrompts", std::flat_map<std::string, mysql::param_value>{});
        if (reset_rs.is_err()) {
            throw std::runtime_error(reset_rs.error_msg);
        }

        // ORM 加载 + 更新
        auto existing = co_await db.find_by_id<AiPrompt>(
            mysql::param_value::from_string(std::string(id_str)));
        if (!existing.empty()) {
            auto prompt = existing.data[0];
            prompt.is_default = true;
            auto upd = co_await db.update(prompt);
            if (upd.is_err()) {
                throw std::runtime_error(upd.error_msg);
            }
        }

        co_return;
    });

    conn.return_without_reset();

    if (tx_result.is_err()) {
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(tx_result.error_msg)));
    } else {
        ctx.json(200, R"({"message":"默认提示词设置成功"})");
    }
}

// =============================================================================
// GET /api/ai/history — 分析历史列表
// =============================================================================

auto handle_ai_history(http::request_context& ctx) -> cn::task<void> {
    auto conn = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn) {
        ctx.json(503, json_error("数据库连接失败"));
        co_return;
    }

    orm::mapper_session session(conn->get(), g_mapper_registry);

    auto result_obj = co_await session.query_tuple<
        std::int64_t, std::string, std::string, std::optional<std::string>,
        std::string, std::optional<std::string>
    >("AiMapper.selectAnalysisHistoryWithPrompt", std::flat_map<std::string, mysql::param_value>{});

    if (result_obj.is_err()) {
        ctx.json(500, std::format(R"({{"error":"{}"}})", json_escape(result_obj.error_msg)));
        co_return;
    }

    std::string json = "[";
    for (std::size_t i = 0; i < result_obj.data.size(); ++i) {
        if (i > 0) json += ",";
        const auto& [id, start_date, end_date, project_summary, created_at, prompt_name] = result_obj.data[i];
        json += std::format(
            R"({{"id":{},"start_date":"{}","end_date":"{}",)"
            R"("project_summary":"{}","created_at":"{}","prompt_name":"{}"}})",
            id,
            start_date,
            end_date,
            json_escape(project_summary.value_or("")),
            created_at,
            json_escape(prompt_name.value_or("")));
    }
    json += "]";
    ctx.json(200, json);
}

// =============================================================================
// GET /api/ai/history/:id — 分析详情
// =============================================================================

auto handle_ai_history_detail(http::request_context& ctx) -> cn::task<void> {
    auto id_str = ctx.param("id");

    auto conn = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn) {
        ctx.json(503, json_error("数据库连接失败"));
        co_return;
    }

    orm::mapper_session session(conn->get(), g_mapper_registry);
    std::flat_map<std::string, mysql::param_value> params;
    params["id"] = mysql::param_value::from_int(std::stoll(std::string(id_str)));

    auto result_obj = co_await session.query_tuple<
        std::int64_t, std::string, std::string, std::optional<std::int64_t>,
        std::optional<std::string>, std::optional<std::string>, std::optional<std::string>,
        std::string, std::optional<std::string>, std::optional<std::string>
    >("AiMapper.selectAnalysisDetailById", params);

    if (result_obj.is_err() || result_obj.data.empty()) {
        ctx.json(404, json_error("未找到分析记录"));
        co_return;
    }

    const auto& [id, start_date, end_date, prompt_id, analysis_result, project_summary,
                 projects_json, created_at, prompt_name, prompt_template] = result_obj.data[0];

    nlohmann::json result = {
        {"id", id},
        {"start_date", start_date},
        {"end_date", end_date},
        {"prompt_id", prompt_id.has_value() ? nlohmann::json(prompt_id.value()) : nullptr},
        {"analysis_result", analysis_result.value_or("")},
        {"project_summary", project_summary.value_or("")},
        {"created_at", created_at},
        {"prompt_name", prompt_name.value_or("")},
        {"prompt_template", prompt_template.value_or("")}
    };
    ctx.json(200, result.dump());
}

// =============================================================================
// GET /api/ai/cache — 检查缓存分析结果
// =============================================================================

auto handle_ai_cache(http::request_context& ctx) -> cn::task<void> {
    auto start_date = http::parse_query_param(ctx.query_string(), "startDate");
    auto end_date   = http::parse_query_param(ctx.query_string(), "endDate");
    if (start_date.empty() || end_date.empty()) {
        ctx.json(400, R"({"error":"必须提供日期范围"})");
        co_return;
    }
    auto orm_r = co_await db_orm(&ctx.io_ctx());
    if (!orm_r) {
        ctx.json(500, R"({"error":"数据库连接失败"})");
        co_return;
    }
    auto& [conn, db] = *orm_r;
    auto rs = co_await db.find(
        orm::select<AiAnalysisHistory>()
            .where("`start_date` = {} AND `end_date` = {}",
                {mysql::param_value::from_string(std::string(start_date)),
                 mysql::param_value::from_string(std::string(end_date))}));
    conn.return_without_reset();
    if (!rs.empty()) {
        auto& h = rs.data[0];
        ctx.json(200, std::format(
            R"({{"cached":true,"data":{{"id":{},"analysis_result":"{}","created_at":"{}"}}}})",
            h.id,
            json_escape(h.analysis_result.value_or("")),
            h.created_at.value_or("")));
    } else {
        ctx.json(200, R"({"cached":false})");
    }
}

// =============================================================================
// POST /api/ai/analyze — AI分析（SSE 流式输出）
// =============================================================================

/// SSE 流式发送辅助
auto sse_send(cn::io_context& ctx, cn::socket& sock, std::string_view event_data)
    -> cn::task<bool>
{
    auto line = std::format("data: {}\n\n", event_data);
    auto wr = co_await cn::async_write(ctx, sock,
        cn::const_buffer{line.data(), line.size()});
    co_return wr.has_value();
}

auto handle_ai_analyze(http::request_context& ctx) -> cn::task<void> {
    auto j = parse_json(ctx.body());
    auto start_date = json_string(j, "startDate");
    auto end_date   = json_string(j, "endDate");
    auto no_cache   = json_bool(j, "noCache", false);
    auto prompt_id  = json_int64(j, "promptId", 0);

    if (start_date.empty() || end_date.empty()) {
        ctx.json(400, R"({"error":"必须提供日期范围"})");
        co_return;
    }

    // 检查缓存（ORM）
    if (!no_cache) {
        auto cache_orm = co_await db_orm(&ctx.io_ctx());
        if (cache_orm) {
            auto& [cache_conn, cache_db] = *cache_orm;
            auto cache_rs = co_await cache_db.find(
                orm::select<AiAnalysisHistory>()
                    .where("`start_date` = {} AND `end_date` = {}",
                        {mysql::param_value::from_string(start_date),
                         mysql::param_value::from_string(end_date)}));
            cache_conn.return_without_reset();
            if (!cache_rs.empty() && cache_rs.data[0].analysis_result.has_value()) {
                auto& result = *cache_rs.data[0].analysis_result;
                if (!result.empty()) {
                    ctx.json(200, std::format(
                        R"({{"cached":true,"result":"{}"}})",
                        json_escape(result)));
                    co_return;
                }
            }
        }
    }

    // 检查 OpenAI 配置 (返回 JSON 响应)
    if (!g_openai || g_cfg.llm_api_key.empty()) {
        ctx.json(200, R"({"cached":false,"error":"未配置LLM API密钥，请通过 PUT /api/ai/config 设置 api_key"})");
        co_return;
    }

    auto conn = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn) {
        ctx.json(503, json_error("数据库连接失败"));
        co_return;
    }

    // 同一连接并发 query 会导致 packets out of order。
    // 这里额外借一条连接来并行获取 files，主连接继续用于后续 ORM 查询。
    auto conn_files = co_await g_pool->async_get_connection(ctx.io_ctx());
    if (!conn_files) {
        ctx.json(503, json_error("数据库连接失败"));
        co_return;
    }

    // ★ 并行获取项目数据（不同连接）★
    auto [proj_rs, file_rs] = co_await cn::when_all(
        query_projects(conn->get(), start_date, end_date),
        query_files(conn_files->get(), start_date, end_date));

    if (proj_rs.is_err() || !proj_rs.has_rows()) {
        ctx.json(400, R"({"error":"该时间段内没有项目数据"})");
        co_return;
    }

    auto projects_json = build_projects_json(proj_rs, file_rs);

    // 获取提示词
    std::string prompt_template;
    std::int64_t final_prompt_id = prompt_id;

    orm::mapper_session session(conn->get(), g_mapper_registry);

    if (prompt_id > 0) {
        std::flat_map<std::string, mysql::param_value> params;
        params["id"] = mysql::param_value::from_int(prompt_id);

        auto result_obj = co_await session.query_tuple<std::string>(
            "AiMapper.selectPromptTemplateById", params);

        if (!result_obj.is_err() && !result_obj.data.empty()) {
            auto [tmpl] = result_obj.data[0];
            prompt_template = tmpl;
        }
    }

    if (prompt_template.empty()) {
        auto result_obj = co_await session.query_tuple<std::int64_t, std::string>(
            "AiMapper.selectDefaultPromptTemplate", std::flat_map<std::string, mysql::param_value>{});

        if (!result_obj.is_err() && !result_obj.data.empty()) {
            final_prompt_id = std::get<0>(result_obj.data[0]);
            prompt_template = std::get<1>(result_obj.data[0]);
        }
    }

    if (prompt_template.empty()) {
        ctx.json(400, R"({"error":"未找到提示词"})");
        co_return;
    }

    // 构建数据摘要
    std::flat_map<std::string, mysql::param_value> params;
    params["start_date"] = mysql::param_value::from_string(std::string(start_date));
    params["end_date"] = mysql::param_value::from_string(std::string(end_date));

    auto summary_result = co_await session.query_tuple<std::int64_t, double, double>(
        "ProjectMapper.selectProjectStats", params);

    if (summary_result.is_err()) {
        ctx.json(500, json_error(summary_result.error_msg));
        co_return;
    }

    std::string data_summary;
    if (!summary_result.data.empty()) {
        const auto& [count, total_income, total_commission] = summary_result.data[0];

        data_summary = std::format(
            "- 时间范围：{} 至 {}\n- 项目总数：{}\n"
            "- 总收入：¥{:.2f}\n- 总抽成：¥{:.2f}\n- 净收入：¥{:.2f}",
            start_date, end_date, count,
            total_income, total_commission, total_income - total_commission);
    }

    // 填充提示词模板
    std::string final_prompt = prompt_template;
    if (auto pos = final_prompt.find("{data_summary}"); pos != std::string::npos)
        final_prompt.replace(pos, 14, data_summary);
    if (auto pos = final_prompt.find("{projects_json}"); pos != std::string::npos)
        final_prompt.replace(pos, 15, projects_json);

    // ★ 发送 SSE 响应头 ★
    auto& resp = ctx.resp();
    resp.set_status(200);
    resp.set_header("Content-Type", "text/event-stream");
    resp.set_header("Cache-Control", "no-cache");
    resp.set_header("Connection", "keep-alive");
    resp.set_header("X-Accel-Buffering", "no");  // Nginx 禁用缓冲

    auto header_data = resp.serialize();
    auto wr = co_await cn::async_write(ctx.io_ctx(), ctx.raw_socket(),
        cn::const_buffer{header_data.data(), header_data.size()});
    if (!wr) co_return;

    // 流式调用 OpenAI API
    openai::chat_request ai_req;
    ai_req.model       = g_cfg.llm_model;
    ai_req.max_tokens  = g_cfg.llm_max_tokens;
    ai_req.temperature = g_cfg.llm_temperature;
    ai_req.stream      = true;  // 启用流式
    ai_req.messages.push_back(openai::message{
        .role = "user", .content = final_prompt, .content_parts = {}, .name = {}, .tool_calls = {}, .tool_call_id = {}});

    logger::info("[AI] Calling {} (model={}, stream=true)...", g_cfg.llm_api_base, g_cfg.llm_model);

    std::string full_content;
    auto stream_result = co_await g_openai->chat_stream_async(
        std::move(ai_req),
        [&](const openai::chat_chunk& chunk) -> cn::task<bool> {
            // 每收到一个 chunk 就发送 SSE 事件
            if (!chunk.delta_content.empty()) {
                full_content += chunk.delta_content;
                // 发送给前端: {"content": "..."}
                auto event_json = std::format(
                    R"({{"content":"{}"}})",
                    json_escape(chunk.delta_content));
                auto line = std::format("data: {}\n\n", event_json);
                // 异步发送 SSE 事件
                auto wr = co_await cn::async_write(ctx.io_ctx(), ctx.raw_socket(),
                    cn::const_buffer{line.data(), line.size()});
                if (!wr) co_return false;  // 写入失败，中止流
            }
            co_return true;  // 继续接收
        });

    if (!stream_result) {
        auto err_json = std::format(R"({{"error":"{}"}})", json_escape(stream_result.error()));
        co_await sse_send(ctx.io_ctx(), ctx.raw_socket(), err_json);
        co_return;
    }

    logger::info("[AI] Stream completed ({} chars)", full_content.size());

    // 发送完成事件
    co_await sse_send(ctx.io_ctx(), ctx.raw_socket(), R"({"done":true})");

    // 保存到数据库
    std::flat_map<std::string, mysql::param_value> save_params;
    save_params["start_date"] = mysql::param_value::from_string(std::string(start_date));
    save_params["end_date"] = mysql::param_value::from_string(std::string(end_date));
    save_params["prompt_id"] = mysql::param_value::from_int(final_prompt_id);
    save_params["analysis_result"] = mysql::param_value::from_string(full_content);
    save_params["project_summary"] = mysql::param_value::from_string(data_summary);
    save_params["projects_json"] = mysql::param_value::from_string(projects_json);

    co_await session.execute("AiMapper.replaceAnalysisHistory", save_params);

    // 标记响应已流式发送，跳过框架的正常响应发送
    resp.set_header("X-Streamed", "1");
}

// =============================================================================
// GET /api/ai/config — 获取大模型配置
// =============================================================================

auto handle_ai_config_get(http::request_context& ctx) -> cn::task<void> {
    ctx.json(200, std::format(
        R"({{"api_base":"{}","model":"{}","max_tokens":{},"temperature":{:.1f},"configured":{}}})",
        json_escape(g_cfg.llm_api_base),
        json_escape(g_cfg.llm_model),
        g_cfg.llm_max_tokens,
        g_cfg.llm_temperature,
        g_cfg.llm_api_key.empty() ? "false" : "true"));
    co_return;
}

// =============================================================================
// PUT /api/ai/config — 更新大模型配置
// =============================================================================

auto handle_ai_config_update(http::request_context& ctx) -> cn::task<void> {
    auto j = parse_json(ctx.body());
    auto api_base = j.value("api_base", std::string{});
    auto api_key  = j.value("api_key", std::string{});
    auto model    = j.value("model", std::string{});

    if (!api_base.empty()) g_cfg.llm_api_base = api_base;
    if (!api_key.empty())  g_cfg.llm_api_key = api_key;
    if (!model.empty())    g_cfg.llm_model = model;

    auto mt = j.value("max_tokens", 0.0);
    if (mt > 0) g_cfg.llm_max_tokens = static_cast<int>(mt);
    auto temp = j.value("temperature", 0.0);
    if (temp > 0) g_cfg.llm_temperature = temp;

    // 用新配置重新连接 OpenAI
    if (g_openai && !g_cfg.llm_api_key.empty()) {
        openai::connect_options opts;
        opts.api_base = g_cfg.llm_api_base;
        opts.api_key = g_cfg.llm_api_key;
        auto cr = co_await g_openai->connect(std::move(opts));
        if (!cr)
            logger::warn("[AI] Reconnect warning: {}", cr.error());
    }

    ctx.json(200, R"({"message":"配置更新成功"})");
}

// =============================================================================
// main — 启动服务器 + 连接池
// =============================================================================

auto run_server(cn::server_context& sctx, mysql::sharded_connection_pool& pool,
                openai::client& ai_client) -> cn::task<void> {
    auto& ctx = sctx.accept_io();
    logger::info("[INIT] run_server coroutine started");

    // 后台启动连接池（spawn: 建立初始连接 + 健康检查循环）
    cn::spawn(ctx, pool.async_run());

    // 等待连接池初始化
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{500});
    logger::info("[DB] Connection pool ready (size={})", pool.size());

    // 连接 OpenAI
    if (!g_cfg.llm_api_key.empty()) {
        openai::connect_options ai_opts;
        ai_opts.api_base = g_cfg.llm_api_base;
        ai_opts.api_key = g_cfg.llm_api_key;
        auto cr = co_await ai_client.connect(std::move(ai_opts));
        if (cr) logger::info("[AI] Connected to {}", g_cfg.llm_api_base);
        else    logger::warn("[AI] Connect deferred: {}", cr.error());
    }

    // 构建路由
    http::router router;

    // --- 查询接口（GET）---
    router.get("/api/years", handle_get_years);
    router.get("/api/projects", handle_get_projects);
    router.get("/api/check-auth", handle_check_auth);
    router.get("/api/clients/search", handle_client_search);
    router.get("/api/clients/search-fuzzy", handle_client_fuzzy);

    // --- 写操作接口（需认证）---
    router.post("/api/projects", handle_create_project);
    router.put("/api/projects/:id", handle_update_project);
    router.del("/api/projects/:id", handle_delete_project);

    // --- 文件管理 ---
    router.post("/api/projects/:id/files", handle_upload_files);
    router.del("/api/files/:id", handle_delete_file);
    router.post("/api/files/batch-delete", handle_batch_delete_files);
    router.get("/api/files/download/:id", handle_download_file);

    // --- 认证 ---
    router.post("/api/login", handle_login);
    router.post("/api/logout", handle_logout);

    // --- 统计 ---
    router.post("/api/statistics", handle_statistics);
    router.post("/api/overall-stats", handle_overall_stats);

    // --- AI 提示词 ---
    router.get("/api/ai/prompts", handle_get_prompts);
    router.post("/api/ai/prompts", handle_create_prompt);
    router.put("/api/ai/prompts/:id", handle_update_prompt);
    router.del("/api/ai/prompts/:id", handle_delete_prompt);
    router.put("/api/ai/prompts/:id/default", handle_set_default_prompt);

    // --- AI 分析 ---
    router.get("/api/ai/history", handle_ai_history);
    router.get("/api/ai/history/:id", handle_ai_history_detail);
    router.get("/api/ai/cache", handle_ai_cache);
    router.post("/api/ai/analyze", handle_ai_analyze);

    // --- AI 配置 ---
    router.get("/api/ai/config", handle_ai_config_get);
    router.put("/api/ai/config", handle_ai_config_update);

    // --- 静态站点（前端）---
    // 将未命中 /api/* 的 GET 请求交给静态文件服务，根目录取自 Python 版路径
    {
        std::filesystem::path static_root = "H:/study/python/account"; // 与 Flask 版保持一致
        router.get("/*filepath", http::serve_dir({.root = static_root, .index_file = "index.html"}));
    }

    // 构建 HTTP 服务器
    http::server srv(sctx);
    auto listen_r = srv.listen("0.0.0.0", SERVER_PORT);
    if (!listen_r) {
        logger::error("[ERROR] Listen failed: {}", listen_r.error().message());
        sctx.stop();
        co_return;
    }

    // 注册中间件（洋葱模型: recover → cors → jwt_auth → handler）
    // recover() 必须放最外层，避免 handler 抛异常导致进程直接终止。
    srv.use(cn::recover());
    srv.use(cn::cors());
    // access_log 支持完整 HTTP 打印模式（请求/响应头 + body 预览），调试时可打开：
    constexpr bool ACCESS_LOG_HTTP_DUMP = true;
    if (ACCESS_LOG_HTTP_DUMP) {
        srv.use(cn::access_log(cn::access_log_options{
            .lv = logger::level::info,
            .format = cn::access_log_format::http,
            .dump = cn::access_log_dump::always,
            .max_body_bytes = 4096,
            .redact_sensitive_headers = true,
        }));
    } else {
        srv.use(cn::access_log());
    }
    /*if (const char* v = std::getenv("ACCESS_LOG"); v && std::string_view(v) == "1") {
        srv.use(cn::access_log());
    }*/
    srv.use(cn::jwt_auth({
        .verify = [](std::string_view token) { return verify_jwt(token).has_value(); },
        .skip_paths = {
            "/api/login", "/api/logout", "/api/check-auth",
            "/api/years", "/api/projects", "/api/clients", "/api/files/download",
            "/api/statistics", "/api/overall-stats",
            "/api/ai/analyze", "/api/ai/history", "/api/ai/cache",
            "/", "/favicon.ico", "/index.html",
        },
    }));
    srv.set_router(std::move(router));

    logger::info("=== Freelance Accounting Server ===");
    logger::info("  Listening on http://0.0.0.0:{}", SERVER_PORT);
    logger::info("  MySQL: {}:{}/{}", g_cfg.db_host, g_cfg.db_port, g_cfg.db_name);
    logger::info("  Pool: size={}", pool.size());
    logger::info("  JWT: jwt-cpp HS256");
    logger::info("  LLM: {} (model={}){}",
        g_cfg.llm_api_base, g_cfg.llm_model,
        g_cfg.llm_api_key.empty() ? " [未配置API密钥]" : "");
    logger::info("  Concurrency: when_all parallel queries enabled");

    co_await srv.run();
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // 初始化日志系统
    logger::init("account_server", logger::level::info);

    try {
        std::fprintf(stderr, "[account_server] starting...\n");
        logger::info("=== Freelance Accounting Server (cnetmod) ===");
        logger::info("  Build: {} {}", __DATE__, __TIME__);

        // 加载嵌入的 XML Mapper 文件
        for (const auto& [name, content] : embedded_mappers::all_mappers) {
            auto result = g_mapper_registry.load_xml(content);
            if (!result) {
                std::fprintf(stderr, "[ERROR] Failed to load embedded mapper %s: %s\n",
                    name.c_str(), result.error().c_str());
                return 1;
            }
        }
        logger::info("✓ {} XML Mappers loaded from embedded resources", embedded_mappers::all_mappers.size());

        cn::net_init net;
        unsigned workers = std::thread::hardware_concurrency();
        if (const char* w = std::getenv("WORKERS")) {
            auto v = std::atoi(w);
            if (v > 0) workers = static_cast<unsigned>(v);
        }
        if (workers == 0) workers = 4;
        std::size_t shards = workers;
        if (const char* s = std::getenv("SHARDS")) {
            auto v = std::atoi(s);
            if (v > 0) shards = static_cast<std::size_t>(v);
        }
        cn::server_context sctx(workers, workers);

        // 确保上传目录存在
        std::filesystem::create_directories(g_cfg.upload_dir);

        // 在 main 中创建连接池和 AI 客户端，确保生命周期覆盖所有协程
        mysql::pool_params db_params;
        db_params.host     = g_cfg.db_host;
        db_params.port     = g_cfg.db_port;
        db_params.username = g_cfg.db_user;
        db_params.password = g_cfg.db_password;
        db_params.database = g_cfg.db_name;
        db_params.ssl      = mysql::ssl_mode::disable;
        db_params.initial_size = 20;   // 初始连接数
        db_params.max_size     = 100;   // 最大连接数（需小于 MySQL max_connections）
        db_params.pool_timeout = std::chrono::seconds(3);  // 等连接超时，快速失败

        logger::info("[Pool Config] initial_size={}, max_size={}, workers={}, shards={}, host={}:{}",
            db_params.initial_size, db_params.max_size, workers, shards, db_params.host, db_params.port);
        auto worker_ios = sctx.worker_ios();
        mysql::sharded_connection_pool pool(std::move(worker_ios), std::move(db_params), shards);
        g_pool = &pool;

        openai::client ai_client(sctx.accept_io());
        g_openai = &ai_client;

        std::fprintf(stderr, "[account_server] server_context created, spawning...\n");
        cn::spawn(sctx.accept_io(), run_server(sctx, pool, ai_client));
        sctx.run();

        // 清理全局指针
        g_pool = nullptr;
        g_openai = nullptr;

        logger::info("Server stopped.");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[account_server] EXCEPTION: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "[account_server] UNKNOWN EXCEPTION\n");
        return 1;
    }

    return 0;
}