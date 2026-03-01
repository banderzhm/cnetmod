export module cnetmod.protocol.mysql:orm_crud;

import std;
import :types;
import :format_sql;
import :client;
import :orm_id_gen;
import :orm_meta;
import :orm_mapper;
import :orm_query;
import cnetmod.coro.task;

namespace cnetmod::mysql::orm {

// =============================================================================
// orm_result<T> — ORM operation result
// =============================================================================

export template <class T>
struct orm_result {
    std::vector<T> data;
    std::uint64_t  affected_rows  = 0;
    std::uint64_t  last_insert_id = 0;
    std::string    error_msg;

    auto ok()     const noexcept -> bool { return error_msg.empty(); }
    auto is_err() const noexcept -> bool { return !ok(); }
    auto empty()  const noexcept -> bool { return data.empty(); }

    auto first() const -> std::optional<T> {
        if (data.empty()) return std::nullopt;
        return data.front();
    }
};

// =============================================================================
// db_session — Async ORM session, wraps mysql::client
// =============================================================================

export class db_session {
public:
    explicit db_session(client& cli) noexcept : cli_(cli) {}

    /// Constructor with snowflake generator
    db_session(client& cli, snowflake_generator& sf) noexcept
        : cli_(cli), snowflake_(&sf) {}

    // ── Query ─────────────────────────────────────────────────

    /// SELECT * FROM table
    template <Model T>
    auto find_all() -> task<orm_result<T>> {
        auto sql = select<T>().build_sql(cli_.current_format_opts());
        co_return co_await exec_select<T>(sql);
    }

    /// SELECT * FROM table WHERE pk = ?
    template <Model T>
    auto find_by_id(param_value id) -> task<orm_result<T>> {
        auto& meta = model_traits<T>::meta();
        auto* pk = meta.pk();
        if (!pk) co_return make_err<T>("model has no primary key");

        std::string where_fmt = "`";
        where_fmt.append(pk->col.column_name);
        where_fmt.append("` = {}");

        auto sql = select<T>()
            .where(where_fmt, {std::move(id)})
            .limit(1)
            .build_sql(cli_.current_format_opts());
        co_return co_await exec_select<T>(sql);
    }

    /// Custom select_builder query
    template <Model T>
    auto find(const select_builder<T>& qb) -> task<orm_result<T>> {
        auto sql = qb.build_sql(cli_.current_format_opts());
        co_return co_await exec_select<T>(sql);
    }

    // ── Insert ─────────────────────────────────────────────────

    /// INSERT single record (auto-generate uuid/snowflake ID, fill back auto_increment ID)
    template <Model T>
    auto insert(T& model) -> task<orm_result<T>> {
        generate_id_if_needed(model);

        auto [sql, params] = insert_of<T>().values(model).build(cli_.current_format_opts());
        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        fill_insert_id<T>(model, rs.last_insert_id);

        orm_result<T> r;
        r.affected_rows  = rs.affected_rows;
        r.last_insert_id = rs.last_insert_id;
        r.data.push_back(model);
        co_return r;
    }

    /// INSERT batch
    template <Model T>
    auto insert_many(std::span<T> models) -> task<orm_result<T>> {
        if (models.empty()) co_return orm_result<T>{};

        // Generate ID for each record
        for (auto& m : models)
            generate_id_if_needed(m);

        // Build as const span
        std::vector<T> copy(models.begin(), models.end());
        auto [sql, params] = insert_of<T>()
            .values(std::span<const T>(copy))
            .build(cli_.current_format_opts());

        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        // Fill back first auto_increment id
        if (rs.last_insert_id > 0 && !models.empty())
            fill_insert_id<T>(models[0], rs.last_insert_id);

        orm_result<T> r;
        r.affected_rows  = rs.affected_rows;
        r.last_insert_id = rs.last_insert_id;
        co_return r;
    }

    // ── Update ─────────────────────────────────────────────────

    /// UPDATE by PK (model itself carries PK value)
    template <Model T>
    auto update(const T& model) -> task<orm_result<T>> {
        auto [sql, params] = update_of<T>().set(model).build(cli_.current_format_opts());
        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        orm_result<T> r;
        r.affected_rows = rs.affected_rows;
        co_return r;
    }

    /// UPDATE custom builder
    template <Model T>
    auto update(const update_builder<T>& ub) -> task<orm_result<T>> {
        auto [sql, params] = ub.build(cli_.current_format_opts());
        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        orm_result<T> r;
        r.affected_rows = rs.affected_rows;
        co_return r;
    }

    // ── Delete ─────────────────────────────────────────────────

    /// DELETE by PK
    template <Model T>
    auto remove(const T& model) -> task<orm_result<T>> {
        auto& meta = model_traits<T>::meta();
        auto* pk = meta.pk();
        if (!pk) co_return make_err<T>("model has no primary key");

        std::string where_fmt = "`";
        where_fmt.append(pk->col.column_name);
        where_fmt.append("` = {}");

        auto [sql, params] = delete_of<T>()
            .where(where_fmt, {pk->getter(model)})
            .build(cli_.current_format_opts());

        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        orm_result<T> r;
        r.affected_rows = rs.affected_rows;
        co_return r;
    }

    /// DELETE by PK value
    template <Model T>
    auto remove_by_id(param_value id) -> task<orm_result<T>> {
        auto& meta = model_traits<T>::meta();
        auto* pk = meta.pk();
        if (!pk) co_return make_err<T>("model has no primary key");

        std::string where_fmt = "`";
        where_fmt.append(pk->col.column_name);
        where_fmt.append("` = {}");

        auto [sql, params] = delete_of<T>()
            .where(where_fmt, {std::move(id)})
            .build(cli_.current_format_opts());

        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        orm_result<T> r;
        r.affected_rows = rs.affected_rows;
        co_return r;
    }

    /// DELETE custom builder
    template <Model T>
    auto remove(const delete_builder<T>& db) -> task<orm_result<T>> {
        auto [sql, params] = db.build(cli_.current_format_opts());
        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        orm_result<T> r;
        r.affected_rows = rs.affected_rows;
        co_return r;
    }

    // ── DDL ──────────────────────────────────────────────────

    /// CREATE TABLE IF NOT EXISTS
    template <Model T>
    auto create_table() -> task<orm_result<T>> {
        auto sql = build_create_table_sql<T>();
        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);
        co_return orm_result<T>{};
    }

    /// DROP TABLE IF EXISTS
    template <Model T>
    auto drop_table() -> task<orm_result<T>> {
        auto sql = build_drop_table_sql<T>();
        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);
        co_return orm_result<T>{};
    }

    /// Raw SQL query
    auto raw_query(std::string_view sql) -> task<result_set> {
        co_return co_await cli_.query(sql);
    }

    /// Underlying client access
    auto underlying() noexcept -> client& { return cli_; }

private:
    client& cli_;
    snowflake_generator* snowflake_ = nullptr;

    /// Auto-generate ID before insert (uuid / snowflake)
    template <Model T>
    void generate_id_if_needed(T& model) {
        auto& meta = model_traits<T>::meta();
        auto* pk = meta.pk();
        if (!pk) return;

        if (pk->col.is_uuid()) {
            // Check if current PK is empty (nil uuid or empty string)
            auto cur = pk->getter(model);
            bool need_gen = (cur.kind == param_value::kind_t::null_kind)
                         || (cur.kind == param_value::kind_t::string_kind && cur.str_val.empty())
                         || (cur.kind == param_value::kind_t::string_kind
                             && cur.str_val == "00000000-0000-0000-0000-000000000000");
            if (need_gen) {
                auto id = uuid_v4();
                field_value fv = field_value::from_string(id.to_string());
                pk->setter(model, fv);
            }
        } else if (pk->col.is_snowflake()) {
            // Check if current PK is 0
            auto cur = pk->getter(model);
            bool need_gen = (cur.kind == param_value::kind_t::null_kind)
                         || (cur.kind == param_value::kind_t::int64_kind && cur.int_val == 0)
                         || (cur.kind == param_value::kind_t::uint64_kind && cur.uint_val == 0);
            if (need_gen && snowflake_) {
                auto id = snowflake_->next_id();
                field_value fv = field_value::from_int64(id);
                pk->setter(model, fv);
            }
        }
    }

    template <Model T>
    auto exec_select(const std::string& sql) -> task<orm_result<T>> {
        auto rs = co_await cli_.execute(sql);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        orm_result<T> r;
        r.data = from_result_set<T>(rs);
        r.affected_rows = rs.affected_rows;
        co_return r;
    }

    template <class T>
    static auto make_err(std::string msg) -> orm_result<T> {
        orm_result<T> r;
        r.error_msg = std::move(msg);
        return r;
    }
};

} // namespace cnetmod::mysql::orm
