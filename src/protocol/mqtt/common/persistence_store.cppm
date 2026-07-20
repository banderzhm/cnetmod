/// MQTT filesystem persistence public API.
module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:persistence;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import :types;
import :session;
import :retained;

export namespace cnetmod::mqtt {
struct persistence_options {
  std::string data_dir = "./mqtt_data";
  std::chrono::seconds flush_interval{30};
};

class persistence {
public:
  explicit persistence(persistence_options opts = {});
  auto save_sessions(const session_store &store)
      -> std::expected<void, std::string>;
  auto load_sessions() -> std::expected<session_store, std::string>;
  auto save_retained(const retained_store &store)
      -> std::expected<void, std::string>;
  auto load_retained() -> std::expected<retained_store, std::string>;
  auto start_auto_flush(io_context &ctx, session_store &sessions,
                        retained_store &retained) -> task<void>;
  [[nodiscard]] auto options() const noexcept -> const persistence_options &;

private:
  void ensure_dir();
  static auto write_file(const std::string &path, const std::string &content)
      -> std::expected<void, std::string>;
  static auto read_file(const std::string &path)
      -> std::expected<std::string, std::string>;
  persistence_options opts_;
};
} // namespace cnetmod::mqtt
