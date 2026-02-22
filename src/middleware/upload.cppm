/**
 * @file upload.cppm
 * @brief File Upload Middleware — Parse and validate multipart/form-data, supports custom business handling
 *
 * Difference from save_upload handler:
 *   save_upload  — Terminal handler, fixed logic: parse → save to disk → return JSON
 *   upload()     — Middleware, handles parsing and validation; business handler decides how to handle files
 *
 * Usage Example:
 *   import cnetmod.middleware.upload;
 *
 *   // 1. Mount middleware (global or route level)
 *   svr.use(cnetmod::upload({
 *       .max_file_size  = 5 * 1024 * 1024,   // 5MB per file
 *       .max_files      = 3,                   // Max 3 file fields
 *       .allowed_types  = {"image/"},          // MIME prefix whitelist
 *       .allowed_exts   = {".jpg", ".png", ".webp"},
 *   }));
 *
 *   // 2. Business handler: ctx.parse_form() already parsed by middleware, use directly
 *   router.post("/upload/avatar",
 *       [](http::request_context& ctx) -> task<void>
 *   {
 *       auto form = *ctx.parse_form();    // Already parsed and cached by middleware
 *       auto* img = form->file("avatar");
 *       if (!img) {
 *           ctx.json(http::status::bad_request, R"({"error":"avatar required"})");
 *           co_return;
 *       }
 *       // Custom business logic: write to directory, upload to OSS, save to DB, generate thumbnails...
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
// upload_config — File Upload Middleware Configuration
// =============================================================================

export struct upload_config {
    /// Max bytes per file (default 10MB; 0 = unlimited)
    std::size_t max_file_size = 10 * 1024 * 1024;

    /// Total body size limit (0 = unlimited)
    std::size_t max_total_size = 0;

    /// Max file fields (0 = unlimited)
    std::size_t max_files = 0;

    /// Max regular form fields (0 = unlimited)
    std::size_t max_fields = 0;

    /// MIME type prefix whitelist (empty = allow all)
    /// Prefix match, e.g. {"image/"} allows all images; {"image/png"} only PNG
    std::vector<std::string> allowed_types;

    /// File extension whitelist, with leading dot (empty = allow all)
    /// Case insensitive, e.g. {".jpg", ".png", ".pdf"}
    std::vector<std::string> allowed_exts;
};

// =============================================================================
// Internal Implementation
// =============================================================================

namespace detail {

/// Convert to lowercase (in-place)
inline void to_lower_inplace(std::string& s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c += 32;
}

/// upload middleware coroutine body (not exported, avoids MSVC "exported coroutine lambda" ICE)
auto upload_mw_body(const upload_config& cfg,
                    http::request_context& ctx,
                    http::next_fn next) -> task<void>
{
    // -------------------------------------------------------------------------
    // 1. Content-Type check
    // -------------------------------------------------------------------------
    auto ct_hdr = ctx.get_header("Content-Type");
    auto ct = http::parse_content_type(ct_hdr);
    if (ct.mime != "multipart/form-data") {
        ctx.json(http::status::unsupported_media_type,
                 R"({"error":"Content-Type must be multipart/form-data"})");
        co_return;
    }

    // -------------------------------------------------------------------------
    // 2. Total body size check
    // -------------------------------------------------------------------------
    if (cfg.max_total_size > 0 && ctx.body().size() > cfg.max_total_size) {
        ctx.json(http::status::payload_too_large, std::format(
            R"({{"error":"request body too large","limit":{},"size":{}}})",
            cfg.max_total_size, ctx.body().size()));
        co_return;
    }

    // -------------------------------------------------------------------------
    // 3. Parse multipart (result cached in ctx, downstream uses ctx.parse_form() directly)
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
    // 4. File count check
    // -------------------------------------------------------------------------
    if (cfg.max_files > 0 && form.file_count() > cfg.max_files) {
        ctx.json(http::status::bad_request, std::format(
            R"({{"error":"too many files","limit":{},"count":{}}})",
            cfg.max_files, form.file_count()));
        co_return;
    }

    // -------------------------------------------------------------------------
    // 5. Form field count check
    // -------------------------------------------------------------------------
    if (cfg.max_fields > 0 && form.field_count() > cfg.max_fields) {
        ctx.json(http::status::bad_request, std::format(
            R"({{"error":"too many form fields","limit":{},"count":{}}})",
            cfg.max_fields, form.field_count()));
        co_return;
    }

    // -------------------------------------------------------------------------
    // 6. Per-file validation: size / MIME / extension
    // -------------------------------------------------------------------------
    for (const auto& ff : form.all_files()) {

        // Single file size
        if (cfg.max_file_size > 0 && ff.size() > cfg.max_file_size) {
            ctx.json(http::status::payload_too_large, std::format(
                R"({{"error":"file too large","file":"{}","limit":{},"size":{}}})",
                ff.filename, cfg.max_file_size, ff.size()));
            co_return;
        }

        // MIME type prefix whitelist
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

        // Extension whitelist (case insensitive)
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
    // 7. All validation passed → pass to downstream handler for business logic
    // -------------------------------------------------------------------------
    co_await next();
}

} // namespace detail

// =============================================================================
// upload — File Upload Middleware Factory
// =============================================================================

/// Create file upload middleware.
///
/// Execution order:
///   Parse multipart/form-data → Validate per upload_config → co_await next()
///
/// Downstream handlers get parsed form_data via ctx.parse_form(),
/// and decide file save paths, OSS uploads, database writes, image processing, etc.
export inline auto upload(upload_config cfg = {}) -> http::middleware_fn {
    auto cfg_ptr = std::make_shared<upload_config>(std::move(cfg));
    return [cfg_ptr](http::request_context& ctx, http::next_fn next) -> task<void> {
        return detail::upload_mw_body(*cfg_ptr, ctx, std::move(next));
    };
}

} // namespace cnetmod