module cnetmod.protocol.http;

import std;
import :multipart_builder;

namespace cnetmod::http {
multipart_builder::multipart_builder() : boundary_(generate_boundary()) {}

multipart_builder::multipart_builder(std::string boundary)
    : boundary_(std::move(boundary)) {}

auto multipart_builder::add_field(std::string_view name, std::string_view value)
    -> multipart_builder & {
  part item;
  item.disposition = std::format("form-data; name=\"{}\"", name);
  item.body.assign(
      reinterpret_cast<const std::byte *>(value.data()),
      reinterpret_cast<const std::byte *>(value.data() + value.size()));
  parts_.push_back(std::move(item));
  return *this;
}

auto multipart_builder::add_file(std::string_view field,
                                 std::string_view filename,
                                 std::string_view type,
                                 std::span<const std::byte> data)
    -> multipart_builder & {
  part item;
  item.disposition =
      std::format("form-data; name=\"{}\"; filename=\"{}\"", field, filename);
  item.content_type = std::string(type);
  item.body.assign(data.begin(), data.end());
  parts_.push_back(std::move(item));
  return *this;
}

auto multipart_builder::add_file(std::string_view field,
                                 std::string_view filename,
                                 std::string_view type, std::string_view data)
    -> multipart_builder & {
  return add_file(
      field, filename, type,
      {reinterpret_cast<const std::byte *>(data.data()), data.size()});
}

auto multipart_builder::content_type() const -> std::string {
  return std::format("multipart/form-data; boundary={}", boundary_);
}

auto multipart_builder::boundary() const noexcept -> std::string_view {
  return boundary_;
}

auto multipart_builder::build() const -> std::string {
  std::string out;
  for (const auto &item : parts_) {
    out += "--";
    out += boundary_;
    out += "\r\nContent-Disposition: ";
    out += item.disposition;
    out += "\r\n";
    if (!item.content_type.empty()) {
      out += "Content-Type: ";
      out += item.content_type;
      out += "\r\n";
    }
    out += "\r\n";
    out.append(reinterpret_cast<const char *>(item.body.data()),
               item.body.size());
    out += "\r\n";
  }
  out += "--";
  out += boundary_;
  out += "--\r\n";
  return out;
}

auto multipart_builder::generate_boundary() -> std::string {
  static constexpr char chars[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  auto seed = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  auto next = [&seed] {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
  };
  std::string boundary = "----cnetmod";
  for (int i{}; i < 16; ++i)
    boundary += chars[next() % (sizeof(chars) - 1)];
  return boundary;
}
} // namespace cnetmod::http