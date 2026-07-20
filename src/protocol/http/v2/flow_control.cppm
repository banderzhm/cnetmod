export module cnetmod.protocol.http.v2.flow_control;

import std;

export namespace cnetmod::http::v2 {
class flow_window {
public:
  explicit flow_window(std::int32_t initial = 65'535) noexcept;
  [[nodiscard]] auto available() const noexcept -> std::int32_t;
  [[nodiscard]] auto consume(std::uint32_t bytes) noexcept -> bool;
  [[nodiscard]] auto increase(std::uint32_t bytes) noexcept -> bool;

private:
  std::int32_t value_;
};
} // namespace cnetmod::http::v2