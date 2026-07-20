module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IOCP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

export module cnetmod.io.platform.iocp;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

export struct iocp_overlapped : OVERLAPPED {
  std::coroutine_handle<> coroutine{};
  std::error_code error{};
  DWORD bytes_transferred = 0;

  iocp_overlapped() noexcept;
  void reset() noexcept;
  void set_offset(std::uint64_t offset) noexcept;
};

/// Windows completion-port event loop.  OS calls and dispatch are defined in
/// iocp_impl.cpp so importers only consume the state and public API.
export class iocp_context : public io_context {
public:
  iocp_context();
  ~iocp_context() override;
  void run() override;
  [[nodiscard]] auto run_one() -> std::size_t override;
  [[nodiscard]] auto poll() -> std::size_t override;
  void stop() override;
  [[nodiscard]] auto stopped() const noexcept -> bool override;
  void restart() override;
  [[nodiscard]] auto associate(HANDLE handle)
      -> std::expected<void, std::error_code>;
  void post_completion(iocp_overlapped *ov);
  [[nodiscard]] auto native_handle() const noexcept -> HANDLE;

protected:
  void wake() override;

private:
  static constexpr ULONG max_batch_ = 64;
  auto run_batch_impl(DWORD timeout_ms) -> std::size_t;
  static auto ntstatus_to_win32(long ntstatus) noexcept -> unsigned long;

  HANDLE iocp_handle_ = INVALID_HANDLE_VALUE;
  std::atomic<bool> stopped_{false};
};

} // namespace cnetmod

#endif // CNETMOD_HAS_IOCP
