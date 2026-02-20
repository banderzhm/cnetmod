/// cnetmod example — MySQL ORM (持久化框架演示)
/// 演示 orm::db_session 的 create_table / insert / find / update / remove
/// 需要本地 MySQL 运行，数据库: mall

#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mysql;

#include <cnetmod/orm.hpp>

namespace cn = cnetmod;
namespace mysql = cn::mysql;
namespace orm = mysql::orm;

// =============================================================================
// 模型定义
// =============================================================================

struct Article {
    std::int64_t                id          = 0;
    std::string                 title;
    std::string                 content;
    int                         status      = 0;
    int                         view_count  = 0;
    std::optional<std::string>  author;
};

CNETMOD_MODEL(Article, "orm_articles",
    CNETMOD_FIELD(id,         "id",         bigint,   PK | AUTO_INC),
    CNETMOD_FIELD(title,      "title",      varchar),
    CNETMOD_FIELD(content,    "content",    text),
    CNETMOD_FIELD(status,     "status",     int_),
    CNETMOD_FIELD(view_count, "view_count", int_),
    CNETMOD_FIELD(author,     "author",     varchar,  NULLABLE)
)

// =============================================================================
// UUID 主键模型: Tag
// =============================================================================

struct Tag {
    orm::uuid    id;      ///< UUID v4 主键
    std::string  name;
    std::string  color;
};

CNETMOD_MODEL(Tag, "orm_tags",
    CNETMOD_FIELD(id,    "id",    char_,   UUID_PK_FLAGS, UUID_PK_STRATEGY),
    CNETMOD_FIELD(name,  "name",  varchar),
    CNETMOD_FIELD(color, "color", varchar)
)

// =============================================================================
// 雪花算法主键模型: Event
// =============================================================================

struct Event {
    std::int64_t  id     = 0;   ///< 雪花 ID
    std::string   title;
    int           level  = 0;
};

CNETMOD_MODEL(Event, "orm_events",
    CNETMOD_FIELD(id,    "id",    bigint, SNOWFLAKE_PK_FLAGS, SNOWFLAKE_PK_STRATEGY),
    CNETMOD_FIELD(title, "title", varchar),
    CNETMOD_FIELD(level, "level", int_)
)

// =============================================================================
// sync_schema 演示用模型: Product (V1 → V2 动态迁移)
// =============================================================================

struct Product {
    std::int64_t                id    = 0;
    std::string                 name;
    double                      price = 0.0;
    std::optional<std::string>  desc;   ///< V2 新增字段
};

CNETMOD_MODEL(Product, "orm_products",
    CNETMOD_FIELD(id,    "id",    bigint,  PK | AUTO_INC),
    CNETMOD_FIELD(name,  "name",  varchar),
    CNETMOD_FIELD(price, "price", double_),
    CNETMOD_FIELD(desc,  "description", text, NULLABLE)
)

// =============================================================================
// 辅助：打印文章列表
// =============================================================================

void print_articles(std::span<const Article> articles, std::string_view label = {}) {
    if (!label.empty())
        std::println("\n── {} ──", label);

    if (articles.empty()) {
        std::println("  (empty)");
        return;
    }

    for (auto& a : articles) {
        std::println("  id={} title={:<20s} status={} views={} author={}",
                     a.id, a.title, a.status, a.view_count,
                     a.author.value_or("(null)"));
    }
    std::println("  ({} rows)", articles.size());
}

// =============================================================================
// Demo 1: DDL — 建表 / 删表
// =============================================================================

auto demo_ddl(orm::db_session& db) -> cn::task<void> {
    // 先删除旧表（如果存在）
    auto drop = co_await db.drop_table<Article>();
    if (drop.is_err()) {
        std::println("  DROP TABLE error: {}", drop.error_msg);
        co_return;
    }
    std::println("\n── DROP TABLE (cleanup) ── OK");

    // 创建表
    auto create = co_await db.create_table<Article>();
    if (create.is_err()) {
        std::println("  CREATE TABLE error: {}", create.error_msg);
        co_return;
    }
    std::println("── CREATE TABLE ── OK");
}

// =============================================================================
// Demo 2: INSERT — 单条 + 批量
// =============================================================================

auto demo_insert(orm::db_session& db) -> cn::task<void> {
    // 单条插入
    Article a1;
    a1.title      = "C++23 Modules 入门";
    a1.content    = "本文介绍 C++23 模块系统的基本用法。";
    a1.status     = 1;
    a1.view_count = 0;
    a1.author     = "张三";

    auto r1 = co_await db.insert(a1);
    if (r1.is_err()) { std::println("  INSERT error: {}", r1.error_msg); co_return; }
    std::println("\n── INSERT 1 ── id={}, affected={}", a1.id, r1.affected_rows);

    // 批量插入
    std::array<Article, 3> batch = {{
        {0, "协程异步 I/O 实战",   "深入讲解 C++20 协程 + IOCP/epoll 异步编程。",     1, 10, "李四"},
        {0, "MySQL 连接池设计",    "如何设计高性能的 MySQL 连接池。",                  1, 5,  "王五"},
        {0, "草稿：WebSocket 协议", "WebSocket 帧格式解析（草稿状态）",                0, 0,  std::nullopt},
    }};
    std::span<Article> batch_span(batch);
    auto r2 = co_await db.insert_many(batch_span);
    if (r2.is_err()) { std::println("  INSERT MANY error: {}", r2.error_msg); co_return; }
    std::println("── INSERT 3 ── affected={}, first_id={}", r2.affected_rows, r2.last_insert_id);
}

// =============================================================================
// Demo 3: SELECT — 全量 / 按 ID / 自定义条件
// =============================================================================

auto demo_select(orm::db_session& db) -> cn::task<void> {
    // 全量
    auto all = co_await db.find_all<Article>();
    if (all.is_err()) { std::println("  find_all error: {}", all.error_msg); co_return; }
    print_articles(all.data, "SELECT ALL");

    // 按 ID 查询（取第一条的 id）
    if (!all.data.empty()) {
        auto first_id = all.data.front().id;
        auto by_id = co_await db.find_by_id<Article>(mysql::param_value::from_int(first_id));
        if (by_id.ok())
            print_articles(by_id.data, std::format("SELECT BY ID ({})", first_id));
    }

    // 自定义条件查询: status=1, 按 view_count 降序, 取前 2 条
    auto qb = orm::select<Article>()
        .where("`status` = {}", {mysql::param_value::from_int(1)})
        .order_by("`view_count` DESC")
        .limit(2);

    auto top2 = co_await db.find(qb);
    if (top2.ok())
        print_articles(top2.data, "SELECT TOP 2 (status=1, order by views DESC)");
}

// =============================================================================
// Demo 4: UPDATE — 按 PK 更新
// =============================================================================

auto demo_update(orm::db_session& db) -> cn::task<void> {
    // 先查全部
    auto all = co_await db.find_all<Article>();
    if (all.is_err() || all.empty()) co_return;

    // 修改第一条
    auto& a = all.data.front();
    a.title      = a.title + " (updated)";
    a.view_count = a.view_count + 100;

    auto r = co_await db.update(a);
    if (r.is_err()) { std::println("  UPDATE error: {}", r.error_msg); co_return; }
    std::println("\n── UPDATE ── affected={}", r.affected_rows);

    // 验证
    auto updated = co_await db.find_by_id<Article>(mysql::param_value::from_int(a.id));
    if (updated.ok())
        print_articles(updated.data, "VERIFY UPDATE");
}

// =============================================================================
// Demo 5: DELETE — 按模型 / 按 ID
// =============================================================================

auto demo_delete(orm::db_session& db) -> cn::task<void> {
    auto all = co_await db.find_all<Article>();
    if (all.is_err() || all.data.size() < 2) co_return;

    // 按模型删除最后一条
    auto& last = all.data.back();
    auto r1 = co_await db.remove(last);
    if (r1.is_err()) { std::println("  REMOVE error: {}", r1.error_msg); co_return; }
    std::println("\n── DELETE (by model, id={}) ── affected={}", last.id, r1.affected_rows);

    // 按 ID 删除第一条
    auto first_id = all.data.front().id;
    auto r2 = co_await db.remove_by_id<Article>(mysql::param_value::from_int(first_id));
    if (r2.is_err()) { std::println("  REMOVE_BY_ID error: {}", r2.error_msg); co_return; }
    std::println("── DELETE (by id={}) ── affected={}", first_id, r2.affected_rows);

    // 查看剩余
    auto remaining = co_await db.find_all<Article>();
    if (remaining.ok())
        print_articles(remaining.data, "REMAINING AFTER DELETE");
}

// =============================================================================
// Demo 6: 自定义 delete builder — 条件删除
// =============================================================================

auto demo_delete_builder(orm::db_session& db) -> cn::task<void> {
    auto db_result = orm::delete_of<Article>()
        .where("`status` = {}", {mysql::param_value::from_int(0)});

    auto r = co_await db.remove(db_result);
    if (r.is_err()) { std::println("  DELETE builder error: {}", r.error_msg); co_return; }
    std::println("\n── DELETE WHERE status=0 ── affected={}", r.affected_rows);

    auto remaining = co_await db.find_all<Article>();
    if (remaining.ok())
        print_articles(remaining.data, "REMAINING AFTER CONDITIONAL DELETE");
}

// =============================================================================
// Demo 7: UUID 主键 CRUD
// =============================================================================

auto demo_uuid(orm::db_session& db) -> cn::task<void> {
    co_await db.drop_table<Tag>();
    co_await db.create_table<Tag>();
    std::println("\n── UUID PK: Tag ──");

    Tag t1; t1.name = "C++";    t1.color = "blue";
    Tag t2; t2.name = "Rust";   t2.color = "orange";
    Tag t3; t3.name = "Python"; t3.color = "green";

    co_await db.insert(t1);
    co_await db.insert(t2);
    co_await db.insert(t3);

    std::println("  inserted t1.id = {}", t1.id.to_string());
    std::println("  inserted t2.id = {}", t2.id.to_string());
    std::println("  inserted t3.id = {}", t3.id.to_string());

    // 按 UUID 查询
    auto r = co_await db.find_by_id<Tag>(mysql::param_value::from_string(t1.id.to_string()));
    if (r.ok() && !r.data.empty())
        std::println("  find_by_id: name={}, color={}", r.data[0].name, r.data[0].color);

    // 保留表供 Navicat 查看
    // co_await db.drop_table<Tag>();
    std::println("  Tag table kept.");
}

// =============================================================================
// Demo 8: 雪花算法主键 CRUD
// =============================================================================

auto demo_snowflake(orm::db_session& db) -> cn::task<void> {
    co_await db.drop_table<Event>();
    co_await db.create_table<Event>();
    std::println("\n── SNOWFLAKE PK: Event ──");

    Event e1; e1.title = "服务启动";     e1.level = 1;
    Event e2; e2.title = "数据库连接异常"; e2.level = 3;
    Event e3; e3.title = "定时任务执行";   e3.level = 2;

    co_await db.insert(e1);
    co_await db.insert(e2);
    co_await db.insert(e3);

    std::println("  inserted e1.id = {}", e1.id);
    std::println("  inserted e2.id = {}", e2.id);
    std::println("  inserted e3.id = {}", e3.id);

    auto all = co_await db.find_all<Event>();
    if (all.ok()) {
        for (auto& e : all.data)
            std::println("  id={} title={} level={}", e.id, e.title, e.level);
    }

    // co_await db.drop_table<Event>();
    std::println("  Event table kept.");
}

// =============================================================================
// Demo 9: Schema 迁移 — sync_schema
// =============================================================================

auto demo_sync_schema(mysql::client& cli) -> cn::task<void> {
    std::println("\n── SYNC SCHEMA: Product ──");

    // 1) 先清理旧表
    co_await cli.execute("DROP TABLE IF EXISTS `orm_products`");

    // 2) 手动创建 V1 表（没有 description 列，有一个多余列 old_col）
    co_await cli.execute(
        "CREATE TABLE `orm_products` ("
        "  `id` BIGINT NOT NULL AUTO_INCREMENT,"
        "  `name` VARCHAR(255) NOT NULL,"
        "  `price` DOUBLE NOT NULL,"
        "  `old_col` VARCHAR(100) DEFAULT NULL,"
        "  PRIMARY KEY (`id`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    std::println("  V1 table created (id, name, price, old_col)");

    // 3) sync_schema: 检测差异并应用
    auto result = co_await orm::sync_schema<Product>(cli);
    if (result.is_err()) {
        std::println("  sync_schema error: {}", result.error_msg);
        co_return;
    }

    if (result.created) {
        std::println("  table was missing, created fresh.");
    } else {
        std::println("  sync applied {} changes:", result.diff.changes.size());
        for (auto& chg : result.diff.changes) {
            const char* act = chg.action == orm::column_change::action_t::add ? "ADD"
                            : chg.action == orm::column_change::action_t::drop ? "DROP"
                            : "MODIFY";
            std::println("    {} `{}`: {}", act, chg.column_name, chg.ddl);
        }
    }

    // 4) 验证: 插入一条并查询
    orm::db_session db(cli);
    Product p;
    p.name  = "测试商品";
    p.price = 99.9;
    p.desc  = "这是 sync_schema 后新增的 description 字段";
    co_await db.insert(p);
    std::println("  inserted product id={}", p.id);

    auto all = co_await db.find_all<Product>();
    if (all.ok()) {
        for (auto& pp : all.data)
            std::println("  id={} name={} price={} desc={}",
                         pp.id, pp.name, pp.price, pp.desc.value_or("(null)"));
    }

    // 保留表供 Navicat 查看
    // co_await db.drop_table<Product>();
    std::println("  Product table kept.");
}

// =============================================================================
// 入口
// =============================================================================

auto run(cn::io_context& ctx) -> cn::task<void> {
    mysql::client cli(ctx);

    mysql::connect_options opts;
    opts.host     = "1.94.173.250";
    opts.port     = 3306;
    opts.username = "root";
    opts.password = "ydc061566";      // ← 按实际修改
    opts.database = "mall";
    opts.ssl      = mysql::ssl_mode::disable;

    auto rs = co_await cli.connect(std::move(opts));
    if (rs.is_err()) {
        std::println("MySQL 连接失败: {}", rs.error_msg);
        ctx.stop();
        co_return;
    }
    std::println("已连接 MySQL (mall)");

    // 雪花生成器 (machine_id=1)
    orm::snowflake_generator snowflake(1);

    // 创建 ORM 会话（带雪花生成器）
    orm::db_session db(cli, snowflake);

    // Article (auto_increment PK) CRUD
    co_await demo_ddl(db);
    co_await demo_insert(db);
    co_await demo_select(db);
    co_await demo_update(db);
    co_await demo_delete(db);
    co_await demo_delete_builder(db);
    // co_await db.drop_table<Article>();  // 保留表供 Navicat 查看

    // UUID PK CRUD
    co_await demo_uuid(db);

    // Snowflake PK CRUD
    co_await demo_snowflake(db);

    // Schema 迁移
    co_await demo_sync_schema(cli);

    co_await cli.quit();
    std::println("\nDone.");
    ctx.stop();
}

auto main() -> int {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    try {
        std::fprintf(stderr, "[mysql_orm] starting...\n");
        std::println("=== cnetmod: MySQL ORM Demo ===");

        cn::net_init net;
        auto ctx = cn::make_io_context();
        std::fprintf(stderr, "[mysql_orm] io_context created, spawning...\n");
        cn::spawn(*ctx, run(*ctx));
        ctx->run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[mysql_orm] EXCEPTION: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "[mysql_orm] UNKNOWN EXCEPTION\n");
        return 1;
    }

    return 0;
}