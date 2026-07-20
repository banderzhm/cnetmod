export module cnetmod.protocol.http:multipart_builder;

import std;

export namespace cnetmod::http {
class multipart_builder {
public:
  multipart_builder();
  explicit multipart_builder(std::string boundary);
  auto add_field(std::string_view name, std::string_view value)
      -> multipart_builder &;
  auto add_file(std::string_view field_name, std::string_view filename,
                std::string_view content_type, std::span<const std::byte> data)
      -> multipart_builder &;
  auto add_file(std::string_view field_name, std::string_view filename,
                std::string_view content_type, std::string_view data)
      -> multipart_builder &;
  [[nodiscard]] auto content_type() const -> std::string;
  [[nodiscard]] auto boundary() const noexcept -> std::string_view;
  [[nodiscard]] auto build() const -> std::string;

private:
  struct part {
    std::string disposition;
    std::string content_type;
    std::vector<std::byte> body;
  };

  static auto generate_boundary() -> std::string;
  std::string boundary_;
  std::vector<part> parts_;
};
} // namespace cnetmod::http