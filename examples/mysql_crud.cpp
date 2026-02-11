/// cnetmod example — MySQL CRUD (cms_help 表增删改查)
/// 演示 mysql::client 的连接 / query / with_params / prepared statement / pipeline
/// 需要本地 MySQL 运行，数据库: mall

#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.protocol.mysql;

namespace cn = cnetmod;
namespace mysql = cn::mysql;

// ─────────────────────────────────────────────────────────────────────────────
// 辅助：打印结果集
// ─────────────────────────────────────────────────────────────────────────────

void print_result(const mysql::result_set& rs, std::string_view label = {}) {
    if (!label.empty())
        std::println("\n── {} ──", label);

    if (rs.is_err()) {
        std::println("  ERROR [{}]: {}", rs.error_code, rs.error_msg);
        return;
    }

    if (!rs.has_rows()) {
        std::println("  OK: affected_rows={}, last_insert_id={}, warnings={}",
                     rs.affected_rows, rs.last_insert_id, rs.warning_count);
        return;
    }

    // 打印列头
    std::print("  ");
    for (std::size_t i = 0; i < rs.columns.size(); ++i) {
        if (i > 0) std::print(" | ");
        std::print("{:<16s}", rs.columns[i].name);
    }
    std::println("");
    std::print("  ");
    for (std::size_t i = 0; i < rs.columns.size(); ++i) {
        if (i > 0) std::print("-+-");
        std::print("{:-<16s}", "");
    }
    std::println("");

    // 打印行
    for (auto& row : rs.rows) {
        std::print("  ");
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) std::print(" | ");
            std::print("{:<16s}", row[i].to_string());
        }
        std::println("");
    }
    std::println("  ({} rows)", rs.rows.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 1: 建表 (CREATE TABLE IF NOT EXISTS)
// ─────────────────────────────────────────────────────────────────────────────

auto demo_create_table(mysql::client& db) -> cn::task<void> {
    auto rs = co_await db.query(
        "CREATE TABLE IF NOT EXISTS `cms_help` ("
        "  `id` bigint(20) NOT NULL AUTO_INCREMENT,"
        "  `category_id` bigint(20) DEFAULT NULL,"
        "  `icon` varchar(500) DEFAULT NULL,"
        "  `title` varchar(100) DEFAULT NULL,"
        "  `show_status` int(1) DEFAULT NULL,"
        "  `create_time` datetime DEFAULT NULL,"
        "  `read_count` int(1) DEFAULT NULL,"
        "  `content` text,"
        "  PRIMARY KEY (`id`) USING BTREE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='帮助表'"
    );
    print_result(rs, "CREATE TABLE");
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 2: 插入 — with_params（客户端格式化 SQL，自动转义）
// ─────────────────────────────────────────────────────────────────────────────

auto demo_insert(mysql::client& db) -> cn::task<void> {
    using P = mysql::param_value;

    // 插入单条
    auto rs1 = co_await db.execute(mysql::with_params(
        "INSERT INTO cms_help (category_id, icon, title, show_status, create_time, read_count, content) "
        "VALUES ({}, {}, {}, {}, NOW(), {}, {})",
        {
            P::from_int(1),
            P::from_string("/icons/faq.png"),
            P::from_string("如何注册账号"),
            P::from_int(1),
            P::from_int(0),
            P::from_string("打开网站首页，点击右上角「注册」按钮，填写手机号和密码即可完成注册。"),
        }
    ));
    print_result(rs1, "INSERT 1");

    // 插入多条
    auto rs2 = co_await db.execute(mysql::with_params(
        "INSERT INTO cms_help (category_id, icon, title, show_status, create_time, read_count, content) VALUES "
        "({}, {}, {}, {}, NOW(), {}, {}), "
        "({}, {}, {}, {}, NOW(), {}, {}), "
        "({}, {}, {}, {}, NOW(), {}, {})",
        {
            P::from_int(1), P::from_string("/icons/login.png"),
            P::from_string("忘记密码怎么办"), P::from_int(1), P::from_int(0),
            P::from_string("点击登录页「忘记密码」，通过手机验证码重置密码。"),

            P::from_int(2), P::from_string("/icons/order.png"),
            P::from_string("如何查看订单"), P::from_int(1), P::from_int(0),
            P::from_string("登录后进入「我的订单」页面，可查看所有历史订单。"),

            P::from_int(2), P::from_string("/icons/refund.png"),
            P::from_string("退款流程说明"), P::from_int(0), P::from_int(0),
            P::from_string("在订单详情页点击「申请退款」，填写退款原因后提交审核。"),
        }
    ));
    print_result(rs2, "INSERT 3 rows");
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 3: 查询 — SELECT 全量 / 条件查询
// ─────────────────────────────────────────────────────────────────────────────

auto demo_select(mysql::client& db) -> cn::task<void> {
    // 全量查询
    auto rs1 = co_await db.query(
        "SELECT id, category_id, title, show_status, create_time, read_count FROM cms_help ORDER BY id"
    );
    print_result(rs1, "SELECT ALL");

    // 条件查询：按 category_id
    using P = mysql::param_value;
    auto rs2 = co_await db.execute(mysql::with_params(
        "SELECT id, title, content FROM cms_help WHERE category_id = {} AND show_status = {}",
        {P::from_int(1), P::from_int(1)}
    ));
    print_result(rs2, "SELECT WHERE category_id=1 AND show_status=1");

    // 模糊查询：LIKE
    auto rs3 = co_await db.execute(mysql::with_params(
        "SELECT id, title FROM cms_help WHERE title LIKE {}",
        {P::from_string("%订单%")}
    ));
    print_result(rs3, "SELECT LIKE '%订单%'");
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 4: 更新 — UPDATE
// ─────────────────────────────────────────────────────────────────────────────

auto demo_update(mysql::client& db) -> cn::task<void> {
    using P = mysql::param_value;

    // 更新 read_count
    auto rs1 = co_await db.execute(mysql::with_params(
        "UPDATE cms_help SET read_count = read_count + {}, show_status = {} WHERE title = {}",
        {P::from_int(1), P::from_int(1), P::from_string("退款流程说明")}
    ));
    print_result(rs1, "UPDATE read_count+1, show_status=1");

    // 验证更新
    auto rs2 = co_await db.execute(mysql::with_params(
        "SELECT id, title, show_status, read_count FROM cms_help WHERE title = {}",
        {P::from_string("退款流程说明")}
    ));
    print_result(rs2, "VERIFY UPDATE");
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 5: Prepared Statement — 参数化查询 + 执行
// ─────────────────────────────────────────────────────────────────────────────

auto demo_prepared_stmt(mysql::client& db) -> cn::task<void> {
    // prepare
    auto stmt_r = co_await db.prepare(
        "SELECT id, title, content FROM cms_help WHERE category_id = ? AND show_status = ?"
    );
    if (!stmt_r) {
        std::println("  PREPARE failed: {}", stmt_r.error());
        co_return;
    }
    auto stmt = *stmt_r;
    std::println("\n── PREPARED STATEMENT ──");
    std::println("  stmt_id={}, params={}, columns={}", stmt.id, stmt.num_params, stmt.num_columns);

    // execute with params (binary protocol)
    using P = mysql::param_value;
    std::array<P, 2> params = {P::from_int(2), P::from_int(1)};
    auto rs = co_await db.execute_stmt(stmt, params);
    print_result(rs, "EXECUTE STMT (category_id=2, show_status=1)");

    // close statement
    co_await db.close_stmt(stmt);
    std::println("  Statement closed.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 6: Pipeline — 批量操作单次往返
// ─────────────────────────────────────────────────────────────────────────────

auto demo_pipeline(mysql::client& db) -> cn::task<void> {
    std::println("\n── PIPELINE ──");

    mysql::pipeline_request req;
    req.add_execute("SELECT COUNT(*) AS total FROM cms_help")
       .add_execute("SELECT id, title FROM cms_help WHERE show_status = 1 ORDER BY id LIMIT 2")
       .add_execute("UPDATE cms_help SET read_count = read_count + 10 WHERE category_id = 1");

    std::vector<mysql::stage_response> responses;
    co_await db.run_pipeline(req, responses);

    for (std::size_t i = 0; i < responses.size(); ++i) {
        auto& resp = responses[i];
        std::println("  [stage {}]", i);
        if (resp.has_error()) {
            std::println("    ERROR: {}", resp.error_msg());
        } else if (resp.has_results()) {
            auto& rs = resp.get_results();
            if (rs.has_rows()) {
                for (auto& row : rs.rows) {
                    std::print("    ");
                    for (std::size_t j = 0; j < row.size(); ++j) {
                        if (j > 0) std::print(" | ");
                        std::print("{}", row[j].to_string());
                    }
                    std::println("");
                }
            } else {
                std::println("    OK: affected_rows={}", rs.affected_rows);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 7: 删除 — DELETE
// ─────────────────────────────────────────────────────────────────────────────

auto demo_delete(mysql::client& db) -> cn::task<void> {
    using P = mysql::param_value;

    // 按条件删除
    auto rs1 = co_await db.execute(mysql::with_params(
        "DELETE FROM cms_help WHERE category_id = {} AND show_status = {}",
        {P::from_int(2), P::from_int(0)}
    ));
    print_result(rs1, "DELETE WHERE category_id=2 AND show_status=0");

    // 全部删除（清理测试数据）
    auto rs2 = co_await db.query("DELETE FROM cms_help");
    print_result(rs2, "DELETE ALL (cleanup)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 8: 元数据检查 — column metadata
// ─────────────────────────────────────────────────────────────────────────────

auto demo_metadata(mysql::client& db) -> cn::task<void> {
    // 先插入一条确保有数据
    co_await db.query(
        "INSERT INTO cms_help (category_id, title, show_status, create_time, read_count, content) "
        "VALUES (1, 'metadata test', 1, NOW(), 42, 'hello')"
    );

    auto rs = co_await db.query("SELECT * FROM cms_help LIMIT 1");
    if (rs.is_err()) { print_result(rs, "METADATA"); co_return; }

    std::println("\n── COLUMN METADATA ──");
    for (std::size_t i = 0; i < rs.columns.size(); ++i) {
        auto& col = rs.columns[i];
        std::println("  [{}] name={:<16s} type={:<20s} nullable={} auto_inc={} pk={}",
                     i, col.name, col.type_str(),
                     !col.is_not_null(), col.is_auto_increment(), col.is_primary_key());
    }

    // 演示 field_value 类型安全访问
    if (!rs.rows.empty()) {
        auto& row = rs.rows[0];
        std::println("\n  field_kind of each column:");
        for (std::size_t i = 0; i < row.size(); ++i) {
            std::println("    {}: kind={}, value={}",
                         rs.columns[i].name,
                         mysql::field_kind_to_str(row[i].kind()),
                         row[i].to_string());
        }
    }

    // 清理
    co_await db.query("DELETE FROM cms_help");
}

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

auto run(cn::io_context& ctx) -> cn::task<void> {
    mysql::client db(ctx);

    mysql::connect_options opts;
    opts.host     = "127.0.0.1";
    opts.port     = 3306;
    opts.username = "root";
    opts.password = "123456";      // ← 按实际修改
    opts.database = "mall";
    opts.ssl      = mysql::ssl_mode::disable;
    auto rs = co_await db.connect(std::move(opts));

    if (rs.is_err()) {
        std::println("MySQL 连接失败: {}", rs.error_msg);
        ctx.stop();
        co_return;
    }
    std::println("已连接 MySQL (mall)");

    co_await demo_create_table(db);
    co_await demo_insert(db);
    co_await demo_select(db);
    co_await demo_update(db);
    co_await demo_prepared_stmt(db);
    co_await demo_pipeline(db);
    co_await demo_metadata(db);
    co_await demo_delete(db);

    co_await db.quit();
    std::println("\nDone.");
    ctx.stop();
}

auto main() -> int {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    try {
        std::fprintf(stderr, "[mysql_crud] starting...\n");
        std::println("=== cnetmod: MySQL CRUD (cms_help) ===");

        cn::net_init net;
        auto ctx = cn::make_io_context();
        std::fprintf(stderr, "[mysql_crud] io_context created, spawning...\n");
        cn::spawn(*ctx, run(*ctx));
        ctx->run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[mysql_crud] EXCEPTION: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "[mysql_crud] UNKNOWN EXCEPTION\n");
        return 1;
    }

    return 0;
}
