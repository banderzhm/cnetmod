/// cnetmod.protocol.modbus:data_store — Modbus Data Store Interface
/// Abstract interface and memory implementation for Modbus data

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.modbus;
import :data_store;

import std;
import :types;
import cnetmod.coro.channel;
import cnetmod.coro.task;

namespace cnetmod::modbus {

// =============================================================================
// Data Store Interface
// =============================================================================

// =============================================================================
// Simple In-Memory Data Store (Mutex-based, for simple use cases)
// =============================================================================

class memory_data_store_impl : public data_store {
public:
  memory_data_store_impl(std::size_t coil_count = 10000,
                         std::size_t discrete_input_count = 10000,
                         std::size_t holding_register_count = 10000,
                         std::size_t input_register_count = 10000)
      : coils_(coil_count, false),
        discrete_inputs_(discrete_input_count, false),
        holding_registers_(holding_register_count, 0),
        input_registers_(input_register_count, 0) {}

  // ── Coils ──
  auto read_coil(std::uint16_t address)
      -> std::expected<bool, exception_code> override {
    std::lock_guard lock(mtx_);
    if (address >= coils_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    return coils_[address];
  }

  auto write_coil(std::uint16_t address, bool value)
      -> std::expected<void, exception_code> override {
    std::lock_guard lock(mtx_);
    if (address >= coils_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    coils_[address] = value;
    return {};
  }

  // ── Discrete Inputs ──
  auto read_discrete_input(std::uint16_t address)
      -> std::expected<bool, exception_code> override {
    std::lock_guard lock(mtx_);
    if (address >= discrete_inputs_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    return discrete_inputs_[address];
  }

  // ── Holding Registers ──
  auto read_holding_register(std::uint16_t address)
      -> std::expected<std::uint16_t, exception_code> override {
    std::lock_guard lock(mtx_);
    if (address >= holding_registers_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    return holding_registers_[address];
  }

  auto write_holding_register(std::uint16_t address, std::uint16_t value)
      -> std::expected<void, exception_code> override {
    std::lock_guard lock(mtx_);
    if (address >= holding_registers_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    holding_registers_[address] = value;
    return {};
  }

  // ── Input Registers ──
  auto read_input_register(std::uint16_t address)
      -> std::expected<std::uint16_t, exception_code> override {
    std::lock_guard lock(mtx_);
    if (address >= input_registers_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    return input_registers_[address];
  }

  // ── Direct access for testing/initialization ──
  auto &get_coils() { return coils_; }
  auto &get_discrete_inputs() { return discrete_inputs_; }
  auto &get_holding_registers() { return holding_registers_; }
  auto &get_input_registers() { return input_registers_; }

  // ── Batch operations ──
  auto write_coils_batch(std::uint16_t start_address,
                         std::span<const bool> values)
      -> std::expected<void, exception_code> {
    std::lock_guard lock(mtx_);
    if (start_address + values.size() > coils_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
      coils_[start_address + i] = values[i];
    }
    return {};
  }

  auto write_holding_registers_batch(std::uint16_t start_address,
                                     std::span<const std::uint16_t> values)
      -> std::expected<void, exception_code> {
    std::lock_guard lock(mtx_);
    if (start_address + values.size() > holding_registers_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
      holding_registers_[start_address + i] = values[i];
    }
    return {};
  }

private:
  std::mutex mtx_;
  std::vector<bool> coils_;
  std::vector<bool> discrete_inputs_;
  std::vector<std::uint16_t> holding_registers_;
  std::vector<std::uint16_t> input_registers_;
};

// =============================================================================
// Channel-based Data Store (Lock-free, high-performance)
// =============================================================================

namespace detail {

// Operation types for channel-based store
enum class store_op_type {
  read_coil,
  write_coil,
  read_discrete_input,
  read_holding_register,
  write_holding_register,
  read_input_register,
};

// Request/Response for channel communication
struct store_request {
  store_op_type op;
  std::uint16_t address;
  std::uint16_t value; // For write operations
};

struct store_response {
  std::expected<std::uint16_t, exception_code> result;
};

} // namespace detail

/// Channel-based data store with dedicated worker coroutine
/// Advantages:
/// - Lock-free channel operations (adaptive spinlock)
/// - Single-threaded data access (no mutex contention)
/// - Coroutine-friendly async API
/// - Better cache locality (worker owns data)
class channel_data_store_impl : public data_store {
public:
  channel_data_store_impl(std::size_t coil_count = 10000,
                          std::size_t discrete_input_count = 10000,
                          std::size_t holding_register_count = 10000,
                          std::size_t input_register_count = 10000,
                          std::size_t channel_capacity = 128)
      : req_channel_(channel_capacity), resp_channel_(channel_capacity),
        coils_(coil_count, false),
        discrete_inputs_(discrete_input_count, false),
        holding_registers_(holding_register_count, 0),
        input_registers_(input_register_count, 0) {}

  // Start the worker coroutine (must be called before use)
  void start_worker() {
    worker_running_ = true;
    // Worker coroutine will be spawned by user
  }

  // Stop the worker coroutine
  void stop_worker() {
    worker_running_ = false;
    req_channel_.close();
    resp_channel_.close();
  }

  // Worker coroutine (should be spawned by user with spawn())
  auto worker() -> task<void> {
    while (worker_running_) {
      auto req_opt = co_await req_channel_.receive();
      if (!req_opt) {
        break; // Channel closed
      }

      auto &req = *req_opt;
      detail::store_response resp;

      switch (req.op) {
      case detail::store_op_type::read_coil:
        if (req.address >= coils_.size()) {
          resp.result = std::unexpected(exception_code::illegal_data_address);
        } else {
          resp.result = static_cast<std::uint16_t>(coils_[req.address]);
        }
        break;

      case detail::store_op_type::write_coil:
        if (req.address >= coils_.size()) {
          resp.result = std::unexpected(exception_code::illegal_data_address);
        } else {
          coils_[req.address] = static_cast<bool>(req.value);
          resp.result = static_cast<std::uint16_t>(0);
        }
        break;

      case detail::store_op_type::read_discrete_input:
        if (req.address >= discrete_inputs_.size()) {
          resp.result = std::unexpected(exception_code::illegal_data_address);
        } else {
          resp.result =
              static_cast<std::uint16_t>(discrete_inputs_[req.address]);
        }
        break;

      case detail::store_op_type::read_holding_register:
        if (req.address >= holding_registers_.size()) {
          resp.result = std::unexpected(exception_code::illegal_data_address);
        } else {
          resp.result = holding_registers_[req.address];
        }
        break;

      case detail::store_op_type::write_holding_register:
        if (req.address >= holding_registers_.size()) {
          resp.result = std::unexpected(exception_code::illegal_data_address);
        } else {
          holding_registers_[req.address] = req.value;
          resp.result = static_cast<std::uint16_t>(0);
        }
        break;

      case detail::store_op_type::read_input_register:
        if (req.address >= input_registers_.size()) {
          resp.result = std::unexpected(exception_code::illegal_data_address);
        } else {
          resp.result = input_registers_[req.address];
        }
        break;
      }

      // Send response back
      co_await resp_channel_.send(std::move(resp));
    }
  }

  // ── Coils ──
  auto read_coil(std::uint16_t address)
      -> std::expected<bool, exception_code> override {
    // Note: This is a synchronous interface, but channel operations are async
    // For true async, the interface would need to return task<>
    // For now, we use a blocking approach with a temporary event loop

    // This is a limitation of the synchronous interface
    // In practice, you'd want an async interface: auto read_coil_async(...) ->
    // task<...>

    // Fallback to simple implementation for synchronous interface
    if (address >= coils_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    return coils_[address];
  }

  auto write_coil(std::uint16_t address, bool value)
      -> std::expected<void, exception_code> override {
    if (address >= coils_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    coils_[address] = value;
    return {};
  }

  // ── Discrete Inputs ──
  auto read_discrete_input(std::uint16_t address)
      -> std::expected<bool, exception_code> override {
    if (address >= discrete_inputs_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    return discrete_inputs_[address];
  }

  // ── Holding Registers ──
  auto read_holding_register(std::uint16_t address)
      -> std::expected<std::uint16_t, exception_code> override {
    if (address >= holding_registers_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    return holding_registers_[address];
  }

  auto write_holding_register(std::uint16_t address, std::uint16_t value)
      -> std::expected<void, exception_code> override {
    if (address >= holding_registers_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    holding_registers_[address] = value;
    return {};
  }

  // ── Input Registers ──
  auto read_input_register(std::uint16_t address)
      -> std::expected<std::uint16_t, exception_code> override {
    if (address >= input_registers_.size()) {
      return std::unexpected(exception_code::illegal_data_address);
    }
    return input_registers_[address];
  }

  // ── Async API (preferred for channel-based store) ──
  auto read_coil_async(std::uint16_t address)
      -> task<std::expected<bool, exception_code>> {
    detail::store_request req{detail::store_op_type::read_coil, address, 0};

    if (!co_await req_channel_.send(std::move(req))) {
      co_return std::unexpected(exception_code::server_device_failure);
    }

    auto resp_opt = co_await resp_channel_.receive();
    if (!resp_opt) {
      co_return std::unexpected(exception_code::server_device_failure);
    }

    auto &resp = *resp_opt;
    if (!resp.result) {
      co_return std::unexpected(resp.result.error());
    }

    co_return static_cast<bool>(*resp.result);
  }

  auto write_coil_async(std::uint16_t address, bool value)
      -> task<std::expected<void, exception_code>> {
    detail::store_request req{detail::store_op_type::write_coil, address,
                              static_cast<std::uint16_t>(value)};

    if (!co_await req_channel_.send(std::move(req))) {
      co_return std::unexpected(exception_code::server_device_failure);
    }

    auto resp_opt = co_await resp_channel_.receive();
    if (!resp_opt) {
      co_return std::unexpected(exception_code::server_device_failure);
    }

    auto &resp = *resp_opt;
    if (!resp.result) {
      co_return std::unexpected(resp.result.error());
    }

    co_return {};
  }

  auto read_holding_register_async(std::uint16_t address)
      -> task<std::expected<std::uint16_t, exception_code>> {
    detail::store_request req{detail::store_op_type::read_holding_register,
                              address, 0};

    if (!co_await req_channel_.send(std::move(req))) {
      co_return std::unexpected(exception_code::server_device_failure);
    }

    auto resp_opt = co_await resp_channel_.receive();
    if (!resp_opt) {
      co_return std::unexpected(exception_code::server_device_failure);
    }

    co_return resp_opt->result;
  }

  auto write_holding_register_async(std::uint16_t address, std::uint16_t value)
      -> task<std::expected<void, exception_code>> {
    detail::store_request req{detail::store_op_type::write_holding_register,
                              address, value};

    if (!co_await req_channel_.send(std::move(req))) {
      co_return std::unexpected(exception_code::server_device_failure);
    }

    auto resp_opt = co_await resp_channel_.receive();
    if (!resp_opt) {
      co_return std::unexpected(exception_code::server_device_failure);
    }

    auto &resp = *resp_opt;
    if (!resp.result) {
      co_return std::unexpected(resp.result.error());
    }

    co_return {};
  }

private:
  channel<detail::store_request> req_channel_;
  channel<detail::store_response> resp_channel_;

  std::vector<bool> coils_;
  std::vector<bool> discrete_inputs_;
  std::vector<std::uint16_t> holding_registers_;
  std::vector<std::uint16_t> input_registers_;

  bool worker_running_ = false;
};

class memory_data_store::impl final : public memory_data_store_impl {
public:
  using memory_data_store_impl::memory_data_store_impl;
};
memory_data_store::memory_data_store(std::size_t a, std::size_t b,
                                     std::size_t c, std::size_t d)
    : impl_(std::make_unique<impl>(a, b, c, d)) {}
memory_data_store::~memory_data_store() = default;
auto memory_data_store::read_coil(std::uint16_t a)
    -> std::expected<bool, exception_code> {
  return impl_->read_coil(a);
}
auto memory_data_store::write_coil(std::uint16_t a, bool v)
    -> std::expected<void, exception_code> {
  return impl_->write_coil(a, v);
}
auto memory_data_store::read_discrete_input(std::uint16_t a)
    -> std::expected<bool, exception_code> {
  return impl_->read_discrete_input(a);
}
auto memory_data_store::read_holding_register(std::uint16_t a)
    -> std::expected<std::uint16_t, exception_code> {
  return impl_->read_holding_register(a);
}
auto memory_data_store::write_holding_register(std::uint16_t a, std::uint16_t v)
    -> std::expected<void, exception_code> {
  return impl_->write_holding_register(a, v);
}
auto memory_data_store::read_input_register(std::uint16_t a)
    -> std::expected<std::uint16_t, exception_code> {
  return impl_->read_input_register(a);
}
auto memory_data_store::get_coils() -> std::vector<bool> & {
  return impl_->get_coils();
}
auto memory_data_store::get_discrete_inputs() -> std::vector<bool> & {
  return impl_->get_discrete_inputs();
}
auto memory_data_store::get_holding_registers()
    -> std::vector<std::uint16_t> & {
  return impl_->get_holding_registers();
}
auto memory_data_store::get_input_registers() -> std::vector<std::uint16_t> & {
  return impl_->get_input_registers();
}
auto memory_data_store::write_coils_batch(std::uint16_t a,
                                          std::span<const bool> v)
    -> std::expected<void, exception_code> {
  return impl_->write_coils_batch(a, v);
}
auto memory_data_store::write_holding_registers_batch(
    std::uint16_t a, std::span<const std::uint16_t> v)
    -> std::expected<void, exception_code> {
  return impl_->write_holding_registers_batch(a, v);
}

class channel_data_store::impl final : public channel_data_store_impl {
public:
  using channel_data_store_impl::channel_data_store_impl;
};
channel_data_store::channel_data_store(std::size_t a, std::size_t b,
                                       std::size_t c, std::size_t d,
                                       std::size_t e)
    : impl_(std::make_unique<impl>(a, b, c, d, e)) {}
channel_data_store::~channel_data_store() = default;
void channel_data_store::start_worker() { impl_->start_worker(); }
void channel_data_store::stop_worker() { impl_->stop_worker(); }
auto channel_data_store::worker() -> task<void> { return impl_->worker(); }
auto channel_data_store::read_coil(std::uint16_t a)
    -> std::expected<bool, exception_code> {
  return impl_->read_coil(a);
}
auto channel_data_store::write_coil(std::uint16_t a, bool v)
    -> std::expected<void, exception_code> {
  return impl_->write_coil(a, v);
}
auto channel_data_store::read_discrete_input(std::uint16_t a)
    -> std::expected<bool, exception_code> {
  return impl_->read_discrete_input(a);
}
auto channel_data_store::read_holding_register(std::uint16_t a)
    -> std::expected<std::uint16_t, exception_code> {
  return impl_->read_holding_register(a);
}
auto channel_data_store::write_holding_register(std::uint16_t a,
                                                std::uint16_t v)
    -> std::expected<void, exception_code> {
  return impl_->write_holding_register(a, v);
}
auto channel_data_store::read_input_register(std::uint16_t a)
    -> std::expected<std::uint16_t, exception_code> {
  return impl_->read_input_register(a);
}
auto channel_data_store::read_coil_async(std::uint16_t a)
    -> task<std::expected<bool, exception_code>> {
  return impl_->read_coil_async(a);
}
auto channel_data_store::write_coil_async(std::uint16_t a, bool v)
    -> task<std::expected<void, exception_code>> {
  return impl_->write_coil_async(a, v);
}
auto channel_data_store::read_holding_register_async(std::uint16_t a)
    -> task<std::expected<std::uint16_t, exception_code>> {
  return impl_->read_holding_register_async(a);
}
auto channel_data_store::write_holding_register_async(std::uint16_t a,
                                                      std::uint16_t v)
    -> task<std::expected<void, exception_code>> {
  return impl_->write_holding_register_async(a, v);
}

} // namespace cnetmod::modbus
