module cnetmod.protocol.http.middleware.upload;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {
namespace {
void to_lower_inplace(std::string &value) {
  for (auto &character : value)
    if (character >= 'A' && character <= 'Z')
      character = static_cast<char>(character + ('a' - 'A'));
}
auto upload_middleware(const upload_config &cfg, http::request_context &ctx,
                       http::next_fn next) -> task<void> {
  const auto content_type =
      http::parse_content_type(ctx.get_header("Content-Type"));
  if (content_type.mime != "multipart/form-data") {
    ctx.json(http::status::unsupported_media_type,
             R"({"error":"Content-Type must be multipart/form-data"})");
    co_return;
  }
  if (cfg.max_total_size > 0 && ctx.body().size() > cfg.max_total_size) {
    ctx.json(http::status::payload_too_large,
             std::format(
                 R"({{"error":"request body too large","limit":{},"size":{}}})",
                 cfg.max_total_size, ctx.body().size()));
    co_return;
  }
  const auto form_result = ctx.parse_form();
  if (!form_result) {
    ctx.json(
        http::status::bad_request,
        std::format(R"({{"error":"multipart parse failed","detail":"{}"}})",
                    form_result.error().message()));
    co_return;
  }
  const auto &form = **form_result;
  if (cfg.max_files > 0 && form.file_count() > cfg.max_files) {
    ctx.json(
        http::status::bad_request,
        std::format(R"({{"error":"too many files","limit":{},"count":{}}})",
                    cfg.max_files, form.file_count()));
    co_return;
  }
  if (cfg.max_fields > 0 && form.field_count() > cfg.max_fields) {
    ctx.json(http::status::bad_request,
             std::format(
                 R"({{"error":"too many form fields","limit":{},"count":{}}})",
                 cfg.max_fields, form.field_count()));
    co_return;
  }
  for (const auto &file : form.all_files()) {
    if (cfg.max_file_size > 0 && file.size() > cfg.max_file_size) {
      ctx.json(
          http::status::payload_too_large,
          std::format(
              R"({{"error":"file too large","file":"{}","limit":{},"size":{}}})",
              file.filename, cfg.max_file_size, file.size()));
      co_return;
    }
    if (!cfg.allowed_types.empty() &&
        std::none_of(cfg.allowed_types.begin(), cfg.allowed_types.end(),
                     [&file](const auto &allowed) {
                       return file.content_type.starts_with(allowed);
                     })) {
      ctx.json(
          http::status::unsupported_media_type,
          std::format(
              R"({{"error":"file type not allowed","file":"{}","type":"{}"}})",
              file.filename, file.content_type));
      co_return;
    }
    if (!cfg.allowed_exts.empty()) {
      const auto dot = file.filename.rfind('.');
      auto extension =
          dot == std::string::npos ? std::string{} : file.filename.substr(dot);
      to_lower_inplace(extension);
      if (std::none_of(cfg.allowed_exts.begin(), cfg.allowed_exts.end(),
                       [&extension](const auto &allowed) {
                         return extension == allowed;
                       })) {
        ctx.json(
            http::status::unsupported_media_type,
            std::format(
                R"({{"error":"file extension not allowed","file":"{}","ext":"{}"}})",
                file.filename, extension));
        co_return;
      }
    }
  }
  co_await next();
}
} // namespace
auto upload(upload_config cfg) -> http::middleware_fn {
  auto config = std::make_shared<upload_config>(std::move(cfg));
  return [config = std::move(config)](http::request_context &ctx,
                                      http::next_fn next) -> task<void> {
    return upload_middleware(*config, ctx, std::move(next));
  };
}
} // namespace cnetmod
