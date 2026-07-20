module cnetmod.protocol.redis;

import :request;
import std;

namespace cnetmod::redis::detail {

void add_separator(std::string &payload) { payload.append("\r\n"); }

void add_header(std::string &payload, resp3_type type, std::size_t size) {
  payload += to_code(type);
  std::format_to(std::back_inserter(payload), "{}", size);
  add_separator(payload);
}

void add_bulk(std::string &payload, std::string_view data) {
  payload += '$';
  std::format_to(std::back_inserter(payload), "{}", data.size());
  add_separator(payload);
  payload.append(data);
  add_separator(payload);
}

void serialize_one(std::string &payload, std::string_view value) {
  add_bulk(payload, value);
}

void serialize_one(std::string &payload, const std::string &value) {
  add_bulk(payload, value);
}

void serialize_one(std::string &payload, const char *value) {
  add_bulk(payload, std::string_view(value));
}

} // namespace cnetmod::redis::detail

namespace cnetmod::redis {

auto request::payload() const noexcept -> std::string_view { return payload_; }

auto request::size() const noexcept -> std::size_t { return commands_; }

auto request::empty() const noexcept -> bool { return commands_ == 0; }

void request::clear() {
  payload_.clear();
  commands_ = 0;
}

void request::reserve(std::size_t size) { payload_.reserve(size); }

} // namespace cnetmod::redis
