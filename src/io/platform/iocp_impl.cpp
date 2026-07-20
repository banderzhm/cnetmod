module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IOCP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <Windows.h>
#include <MSWSock.h>

module cnetmod.io.platform.iocp;

import std;

namespace cnetmod {

iocp_overlapped::iocp_overlapped() noexcept { reset(); }

void iocp_overlapped::reset() noexcept {
  Internal = 0;
  InternalHigh = 0;
  Offset = 0;
  OffsetHigh = 0;
  hEvent = nullptr;
  coroutine = {};
  error = {};
  bytes_transferred = 0;
}

void iocp_overlapped::set_offset(std::uint64_t offset) noexcept {
  Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
  OffsetHigh = static_cast<DWORD>(offset >> 32);
}

iocp_context::iocp_context() {
  iocp_handle_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
  if (!iocp_handle_)
    throw std::system_error(static_cast<int>(::GetLastError()),
                            std::system_category(),
                            "CreateIoCompletionPort failed");
}

iocp_context::~iocp_context() {
  if (iocp_handle_ && iocp_handle_ != INVALID_HANDLE_VALUE)
    ::CloseHandle(iocp_handle_);
}

void iocp_context::run() {
  while (!stopped_.load(std::memory_order_relaxed))
    run_batch_impl(INFINITE);
}

auto iocp_context::run_one() -> std::size_t { return run_batch_impl(INFINITE); }
auto iocp_context::poll() -> std::size_t { return run_batch_impl(0); }

void iocp_context::stop() {
  stopped_.store(true, std::memory_order_relaxed);
  ::PostQueuedCompletionStatus(iocp_handle_, 0, 0, nullptr);
}

auto iocp_context::stopped() const noexcept -> bool {
  return stopped_.load(std::memory_order_relaxed);
}
void iocp_context::restart() {
  stopped_.store(false, std::memory_order_relaxed);
}

auto iocp_context::associate(HANDLE handle)
    -> std::expected<void, std::error_code> {
  if (!::CreateIoCompletionPort(handle, iocp_handle_, 0, 0))
    return std::unexpected(std::error_code(static_cast<int>(::GetLastError()),
                                           std::system_category()));
  return {};
}

void iocp_context::post_completion(iocp_overlapped *ov) {
  ::PostQueuedCompletionStatus(iocp_handle_, 0, 0,
                               reinterpret_cast<LPOVERLAPPED>(ov));
}

auto iocp_context::native_handle() const noexcept -> HANDLE {
  return iocp_handle_;
}

void iocp_context::wake() {
  ::PostQueuedCompletionStatus(iocp_handle_, 0, 1, nullptr);
}

auto iocp_context::run_batch_impl(DWORD timeout_ms) -> std::size_t {
  OVERLAPPED_ENTRY entries[max_batch_];
  ULONG removed = 0;
  if (!::GetQueuedCompletionStatusEx(iocp_handle_, entries, max_batch_,
                                     &removed, timeout_ms, FALSE) ||
      removed == 0)
    return 0;
  std::size_t handled = 0;
  for (ULONG i = 0; i < removed; ++i) {
    auto &entry = entries[i];
    if (!entry.lpOverlapped) {
      if (entry.lpCompletionKey == 1)
        handled += drain_post_queue();
      continue;
    }
    auto *iov = static_cast<iocp_overlapped *>(entry.lpOverlapped);
    if (entry.lpOverlapped->Internal != 0) {
      iov->error =
          std::error_code(static_cast<int>(ntstatus_to_win32(
                              static_cast<long>(entry.lpOverlapped->Internal))),
                          std::system_category());
    }
    iov->bytes_transferred = entry.dwNumberOfBytesTransferred;
    if (iov->coroutine)
      iov->coroutine.resume();
    ++handled;
  }
  return handled;
}

auto iocp_context::ntstatus_to_win32(long ntstatus) noexcept -> unsigned long {
  using fn_t = unsigned long(__stdcall *)(long);
  static fn_t fn = []() -> fn_t {
    auto *mod = ::GetModuleHandleW(L"ntdll.dll");
    return mod ? reinterpret_cast<fn_t>(
                     ::GetProcAddress(mod, "RtlNtStatusToDosError"))
               : nullptr;
  }();
  return fn ? fn(ntstatus) : static_cast<unsigned long>(ntstatus);
}

} // namespace cnetmod

#endif // CNETMOD_HAS_IOCP
