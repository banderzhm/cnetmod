module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.modbus:data_store;
import std;
import :types;
import cnetmod.coro.task;

export namespace cnetmod::modbus {
class data_store {
public:
  virtual ~data_store() = default;
  virtual auto read_coil(std::uint16_t)
      -> std::expected<bool, exception_code> = 0;
  virtual auto write_coil(std::uint16_t, bool)
      -> std::expected<void, exception_code> = 0;
  virtual auto read_discrete_input(std::uint16_t)
      -> std::expected<bool, exception_code> = 0;
  virtual auto read_holding_register(std::uint16_t)
      -> std::expected<std::uint16_t, exception_code> = 0;
  virtual auto write_holding_register(std::uint16_t, std::uint16_t)
      -> std::expected<void, exception_code> = 0;
  virtual auto read_input_register(std::uint16_t)
      -> std::expected<std::uint16_t, exception_code> = 0;
};
class memory_data_store final : public data_store {
public:
  memory_data_store(std::size_t = 10000, std::size_t = 10000,
                    std::size_t = 10000, std::size_t = 10000);
  ~memory_data_store();
  auto read_coil(std::uint16_t) -> std::expected<bool, exception_code> override;
  auto write_coil(std::uint16_t, bool)
      -> std::expected<void, exception_code> override;
  auto read_discrete_input(std::uint16_t)
      -> std::expected<bool, exception_code> override;
  auto read_holding_register(std::uint16_t)
      -> std::expected<std::uint16_t, exception_code> override;
  auto write_holding_register(std::uint16_t, std::uint16_t)
      -> std::expected<void, exception_code> override;
  auto read_input_register(std::uint16_t)
      -> std::expected<std::uint16_t, exception_code> override;
  auto get_coils() -> std::vector<bool> &;
  auto get_discrete_inputs() -> std::vector<bool> &;
  auto get_holding_registers() -> std::vector<std::uint16_t> &;
  auto get_input_registers() -> std::vector<std::uint16_t> &;
  auto write_coils_batch(std::uint16_t, std::span<const bool>)
      -> std::expected<void, exception_code>;
  auto write_holding_registers_batch(std::uint16_t,
                                     std::span<const std::uint16_t>)
      -> std::expected<void, exception_code>;

private:
  class impl;
  std::unique_ptr<impl> impl_;
};
class channel_data_store final : public data_store {
public:
  channel_data_store(std::size_t = 10000, std::size_t = 10000,
                     std::size_t = 10000, std::size_t = 10000,
                     std::size_t = 128);
  ~channel_data_store();
  void start_worker();
  void stop_worker();
  auto worker() -> task<void>;
  auto read_coil(std::uint16_t) -> std::expected<bool, exception_code> override;
  auto write_coil(std::uint16_t, bool)
      -> std::expected<void, exception_code> override;
  auto read_discrete_input(std::uint16_t)
      -> std::expected<bool, exception_code> override;
  auto read_holding_register(std::uint16_t)
      -> std::expected<std::uint16_t, exception_code> override;
  auto write_holding_register(std::uint16_t, std::uint16_t)
      -> std::expected<void, exception_code> override;
  auto read_input_register(std::uint16_t)
      -> std::expected<std::uint16_t, exception_code> override;
  auto read_coil_async(std::uint16_t)
      -> task<std::expected<bool, exception_code>>;
  auto write_coil_async(std::uint16_t, bool)
      -> task<std::expected<void, exception_code>>;
  auto read_holding_register_async(std::uint16_t)
      -> task<std::expected<std::uint16_t, exception_code>>;
  auto write_holding_register_async(std::uint16_t, std::uint16_t)
      -> task<std::expected<void, exception_code>>;

private:
  class impl;
  std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::modbus
