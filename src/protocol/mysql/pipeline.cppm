export module cnetmod.protocol.mysql:pipeline;
import std;
import :types;
import :diagnostics;
export namespace cnetmod::mysql {
enum class stage_kind : std::uint8_t {
  execute,
  prepare,
  close_statement,
  reset_connection,
  set_character_set
};

struct pipeline_stage {
  stage_kind kind = stage_kind::execute;
  std::string sql;
  std::uint32_t stmt_id{};
  std::vector<param_value> params;
};

class stage_response {
public:
  stage_response() noexcept = default;
  [[nodiscard]] auto has_statement() const noexcept -> bool;
  [[nodiscard]] auto has_results() const noexcept -> bool;
  [[nodiscard]] auto has_error() const noexcept -> bool;
  [[nodiscard]] auto get_statement() const noexcept -> statement;
  [[nodiscard]] auto get_results() const noexcept -> const result_set &;
  auto get_results() noexcept -> result_set &;
  [[nodiscard]] auto error_code() const noexcept -> std::uint16_t;
  [[nodiscard]] auto error_msg() const noexcept -> std::string_view;
  [[nodiscard]] auto diag() const noexcept -> const diagnostics &;
  void set_statement(statement);
  void set_results(result_set);
  void set_error(std::uint16_t, std::string);
  void set_ok();

private:
  bool has_stmt_{};
  bool has_rs_{};
  statement stmt_;
  result_set rs_;
  std::uint16_t error_code_{};
  std::string error_msg_;
  diagnostics diag_;
};

class pipeline_request {
public:
  pipeline_request() = default;
  auto add_execute(std::string) -> pipeline_request &;
  auto add_prepare(std::string) -> pipeline_request &;
  auto add_close_statement(std::uint32_t) -> pipeline_request &;
  auto add_reset_connection() -> pipeline_request &;
  auto add_set_character_set(std::string) -> pipeline_request &;
  [[nodiscard]] auto stages() const noexcept
      -> const std::vector<pipeline_stage> &;
  [[nodiscard]] auto size() const noexcept -> std::size_t;
  [[nodiscard]] auto empty() const noexcept -> bool;
  void clear();

private:
  std::vector<pipeline_stage> stages_;
};
} // namespace cnetmod::mysql