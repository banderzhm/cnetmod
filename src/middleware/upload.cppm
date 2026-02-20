/**
 * @file upload.cppm
 * @brief 文件上传中间件 — 解析并校验 multipart/form-data，支持业务自定义处理
 *
 * 与 save_upload handler 的区别：
 *   save_upload  — 终端 handler，固定逻辑：解析 → 保存磁盘 → 返回 JSON
 *   upload()     — 中间件，负责解析与校验；业务 handler 自行决定如何处置文件
 *
 * 使用示例:
 *   import cnetmod.middleware.upload;
 *
 *   // 1. 挂载中间件（全局或路由级别）
 *   svr.use(cnetmod::upload({
 *       .max_file_size  = 5 * 1024 * 1024,   // 5MB / 文件
 *       .max_files      = 3,                   // 最多 3 个文件字段
 *       .allowed_types  = {"image/"},          // MIME 前缀白名单
 *       .allowed_exts   = {".jpg", ".png", ".webp"},
 *   }));
 *
 *   // 2. 业务 handler：ctx.parse_form() 已由中间件解析完毕，直接使用
 *   router.post("/upload/avatar",
 *       [](http::request_context& ctx) -> task<void>
 *   {
 *       auto form = *ctx.parse_form();    //中间件已解析并缓存
 *       auto* img = form->file("avatar");
 *       if (!img) {
 *           ctx.json(http::status::bad_request, R"({"error":"avatar required"})");
 *           co_return;
 *       }
 *       // 自定义业务：写入指定目录、上传 OSS、入库、生成缩略图...
 *       auto save_path = uploads_dir / img->filename;
 *       co_await save_bytes(save_path, img->data);
 *
 *       ctx.json(http::status::created, std::format(
 *           R"({{"url":"/static/{}","size":{}}})", img->filename, img->size()));
 *   });
 */
export module cnetmod.middleware.upload;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// upload_config — 文件上传中间件配置
// =============================================================================

export struct upload_config {
    /// 单个文件最大字节数 (默认 10MB；设 0 = 不限)
    std::size_t max_file_size = 10 * 1024 * 1024;

    /// 请求总 body 大小上限 (0 = 不限)
    std::size_t max_total_size = 0;

    /// 最大文件字段数 (0 = 不限)
    std::size_t max_files = 0;

    /// 最大普通表单字段数 (0 = 不限)
    std::size_t max_fields = 0;

    /// MIME 类型前缀白名单 (空 = 全部允许)
    /// 前缀匹配，e.g. {"image/"} 允许所有图片；{"image/png"} 仅 PNG
    std::vector<std::string> allowed_types;

    /// 文件扩展名白名单，含前导点 (空 = 全部允许)
    /// 大小写不敏感，e.g. {".jpg", ".png", ".pdf"}
    std::vector<std::string> allowed_exts;
};

// =============================================================================
// 内部实现
// =============================================================================

namespace detail {

/// 转小写（原地）
inline void to_lower_inplace(std::string& s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c += 32;
}

/// upload 中间件协程体（非导出，避免 MSVC 「导出协程 lambda」ICE）
auto upload_mw_body(const upload_config& cfg,
                    http::request_context& ctx,
                    http::next_fn next) -> task<void>
{
    // -------------------------------------------------------------------------
    // 1. Content-Type 检查
    // -------------------------------------------------------------------------
    auto ct_hdr = ctx.get_header("Content-Type");
    auto ct = http::parse_content_type(ct_hdr);
    if (ct.mime != "multipart/form-data") {
        ctx.json(http::status::unsupported_media_type,
                 R"({"error":"Content-Type must be multipart/form-data"})");
        co_return;
    }

    // -------------------------------------------------------------------------
    // 2. 总体 body 大小检查
    // -------------------------------------------------------------------------
    if (cfg.max_total_size > 0 && ctx.body().size() > cfg.max_total_size) {
        ctx.json(http::status::payload_too_large, std::format(
            R"({{"error":"request body too large","limit":{},"size":{}}})",
            cfg.max_total_size, ctx.body().size()));
        co_return;
    }

    // -------------------------------------------------------------------------
    // 3. 解析 multipart（结果缓存在 ctx 内，下游直接 ctx.parse_form() 复用）
    // -------------------------------------------------------------------------
    auto form_r = ctx.parse_form();
    if (!form_r) {
        ctx.json(http::status::bad_request, std::format(
            R"({{"error":"multipart parse failed","detail":"{}"}})",
            form_r.error().message()));
        co_return;
    }
    const auto& form = **form_r;

    // -------------------------------------------------------------------------
    // 4. 文件数量检查
    // -------------------------------------------------------------------------
    if (cfg.max_files > 0 && form.file_count() > cfg.max_files) {
        ctx.json(http::status::bad_request, std::format(
            R"({{"error":"too many files","limit":{},"count":{}}})",
            cfg.max_files, form.file_count()));
        co_return;
    }

    // -------------------------------------------------------------------------
    // 5. 表单字段数检查
    // -------------------------------------------------------------------------
    if (cfg.max_fields > 0 && form.field_count() > cfg.max_fields) {
        ctx.json(http::status::bad_request, std::format(
            R"({{"error":"too many form fields","limit":{},"count":{}}})",
            cfg.max_fields, form.field_count()));
        co_return;
    }

    // -------------------------------------------------------------------------
    // 6. 逐文件校验：大小 / MIME / 扩展名
    // -------------------------------------------------------------------------
    for (const auto& ff : form.all_files()) {

        // 单文件大小
        if (cfg.max_file_size > 0 && ff.size() > cfg.max_file_size) {
            ctx.json(http::status::payload_too_large, std::format(
                R"({{"error":"file too large","file":"{}","limit":{},"size":{}}})",
                ff.filename, cfg.max_file_size, ff.size()));
            co_return;
        }

        // MIME 类型前缀白名单
        if (!cfg.allowed_types.empty()) {
            bool ok = false;
            for (const auto& t : cfg.allowed_types) {
                if (ff.content_type.starts_with(t)) { ok = true; break; }
            }
            if (!ok) {
                ctx.json(http::status::unsupported_media_type, std::format(
                    R"({{"error":"file type not allowed","file":"{}","type":"{}"}})",
                    ff.filename, ff.content_type));
                co_return;
            }
        }

        // 扩展名白名单（大小写不敏感）
        if (!cfg.allowed_exts.empty()) {
            auto dot = ff.filename.rfind('.');
            std::string ext = (dot != std::string::npos)
                              ? ff.filename.substr(dot) : "";
            to_lower_inplace(ext);

            bool ok = false;
            for (const auto& e : cfg.allowed_exts) {
                if (ext == e) { ok = true; break; }
            }
            if (!ok) {
                ctx.json(http::status::unsupported_media_type, std::format(
                    R"({{"error":"file extension not allowed","file":"{}","ext":"{}"}})",
                    ff.filename, ext));
                co_return;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 7. 全部校验通过 → 交给下游 handler 处理业务逻辑
    // -------------------------------------------------------------------------
    co_await next();
}

} // namespace detail

// =============================================================================
// upload — 文件上传中间件工厂
// =============================================================================

/// 创建文件上传中间件。
///
/// 执行顺序：
///   解析 multipart/form-data → 按 upload_config 校验 → co_await next()
///
/// 下游 handler 通过 ctx.parse_form() 获取已解析的 form_data，
/// 自行决定文件保存路径、OSS 上传、数据库写入、图像处理等业务逻辑。
export inline auto upload(upload_config cfg = {}) -> http::middleware_fn {
    auto cfg_ptr = std::make_shared<upload_config>(std::move(cfg));
    return [cfg_ptr](http::request_context& ctx, http::next_fn next) -> task<void> {
        return detail::upload_mw_body(*cfg_ptr, ctx, std::move(next));
    };
}

} // namespace cnetmod