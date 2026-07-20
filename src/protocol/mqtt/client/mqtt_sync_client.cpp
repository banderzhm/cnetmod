module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import :client;
import :sync_client;

namespace cnetmod::mqtt {

sync_client::sync_client() : ctx_(make_io_context()), client_(*ctx_) {}

auto sync_client::connect_sync(connect_options options)
    -> std::expected<void, std::string> {
  std::expected<void, std::string> result;
  bool done = false;
  spawn(*ctx_, [&]() -> task<void> {
    result = co_await client_.connect(std::move(options));
    done = true;
  }());
  run_until(done);
  return result;
}

auto sync_client::publish_sync(std::string_view topic, std::string_view payload,
                               qos delivery_qos, bool retain,
                               const properties &properties)
    -> std::expected<void, std::string> {
  std::expected<void, std::string> result;
  bool done = false;
  spawn(*ctx_, [&]() -> task<void> {
    result = co_await client_.publish(topic, payload, delivery_qos, retain,
                                      properties);
    done = true;
  }());
  run_until(done);
  return result;
}

auto sync_client::subscribe_sync(std::string filter, qos maximum_qos,
                                 const properties &properties)
    -> std::expected<std::vector<std::uint8_t>, std::string> {
  std::expected<std::vector<std::uint8_t>, std::string> result;
  bool done = false;
  spawn(*ctx_, [&]() -> task<void> {
    result =
        co_await client_.subscribe(std::move(filter), maximum_qos, properties);
    done = true;
  }());
  run_until(done);
  return result;
}

auto sync_client::unsubscribe_sync(std::vector<std::string> filters,
                                   const properties &properties)
    -> std::expected<void, std::string> {
  std::expected<void, std::string> result;
  bool done = false;
  spawn(*ctx_, [&]() -> task<void> {
    result = co_await client_.unsubscribe(std::move(filters), properties);
    done = true;
  }());
  run_until(done);
  return result;
}

auto sync_client::disconnect_sync(std::uint8_t reason_code,
                                  const properties &properties)
    -> std::expected<void, std::string> {
  std::expected<void, std::string> result;
  bool done = false;
  spawn(*ctx_, [&]() -> task<void> {
    result = co_await client_.disconnect(reason_code, properties);
    done = true;
  }());
  run_until(done);
  return result;
}

void sync_client::on_message(message_callback callback) {
  client_.on_message(std::move(callback));
}

void sync_client::on_disconnect(disconnect_callback callback) {
  client_.on_disconnect(std::move(callback));
}

void sync_client::poll() { (void)ctx_->poll(); }

void sync_client::poll_all() { (void)ctx_->poll(); }

auto sync_client::async_client() noexcept -> client & { return client_; }

auto sync_client::async_client() const noexcept -> const client & {
  return client_;
}

auto sync_client::context() noexcept -> io_context & { return *ctx_; }

auto sync_client::is_connected() const noexcept -> bool {
  return client_.is_connected();
}

void sync_client::run_until(bool &done) {
  while (!done)
    (void)ctx_->poll();
}

} // namespace cnetmod::mqtt
