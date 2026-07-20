module cnetmod.protocol.mysql;
import :pipeline;

namespace cnetmod::mysql {
auto stage_response::has_statement() const noexcept -> bool {
  return has_stmt_;
}
auto stage_response::has_results() const noexcept -> bool { return has_rs_; }
auto stage_response::has_error() const noexcept -> bool {
  return !error_msg_.empty() || error_code_ != 0;
}
auto stage_response::get_statement() const noexcept -> statement {
  return stmt_;
}
auto stage_response::get_results() const noexcept -> const result_set & {
  return rs_;
}
auto stage_response::get_results() noexcept -> result_set & { return rs_; }
auto stage_response::error_code() const noexcept -> std::uint16_t {
  return error_code_;
}
auto stage_response::error_msg() const noexcept -> std::string_view {
  return error_msg_;
}
auto stage_response::diag() const noexcept -> const diagnostics & {
  return diag_;
}

void stage_response::set_statement(statement s) {
  stmt_ = s;
  has_stmt_ = true;
  has_rs_ = false;
}

void stage_response::set_results(result_set r) {
  rs_ = std::move(r);
  has_rs_ = true;
  has_stmt_ = false;
}

void stage_response::set_error(std::uint16_t c, std::string m) {
  error_code_ = c;
  error_msg_ = std::move(m);
  diag_.assign_server(error_msg_);
  has_stmt_ = has_rs_ = false;
}

void stage_response::set_ok() {
  has_stmt_ = has_rs_ = false;
  error_code_ = 0;
  error_msg_.clear();
}

auto pipeline_request::add_execute(std::string sql) -> pipeline_request & {
  stages_.push_back({stage_kind::execute, std::move(sql)});
  return *this;
}

auto pipeline_request::add_prepare(std::string sql) -> pipeline_request & {
  stages_.push_back({stage_kind::prepare, std::move(sql)});
  return *this;
}

auto pipeline_request::add_close_statement(std::uint32_t id)
    -> pipeline_request & {
  pipeline_stage s;
  s.kind = stage_kind::close_statement;
  s.stmt_id = id;
  stages_.push_back(std::move(s));
  return *this;
}

auto pipeline_request::add_reset_connection() -> pipeline_request & {
  pipeline_stage s;
  s.kind = stage_kind::reset_connection;
  stages_.push_back(std::move(s));
  return *this;
}

auto pipeline_request::add_set_character_set(std::string charset)
    -> pipeline_request & {
  pipeline_stage s;
  s.kind = stage_kind::set_character_set;
  s.sql = "SET NAMES " + charset;
  stages_.push_back(std::move(s));
  return *this;
}

auto pipeline_request::stages() const noexcept
    -> const std::vector<pipeline_stage> & {
  return stages_;
}
auto pipeline_request::size() const noexcept -> std::size_t {
  return stages_.size();
}
auto pipeline_request::empty() const noexcept -> bool {
  return stages_.empty();
}
void pipeline_request::clear() { stages_.clear(); }
} // namespace cnetmod::mysql