module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:request_handler;
import std;
import :types;
import :data_store;

export namespace cnetmod::modbus {

class request_handler {
public:
  explicit request_handler(data_store &store);
  ~request_handler();
  request_handler(request_handler &&) noexcept;
  auto operator=(request_handler &&) noexcept -> request_handler &;
  request_handler(const request_handler &) = delete;
  auto operator=(const request_handler &) -> request_handler & = delete;
  auto handle(const modbus_request &request) -> modbus_response;

private:
  class impl;
  std::unique_ptr<impl> impl_;
};

} // namespace cnetmod::modbus
