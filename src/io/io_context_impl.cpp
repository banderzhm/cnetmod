module;

#include <cnetmod/config.hpp>

module cnetmod.io.io_context;

import std;

namespace cnetmod {

void post_node::dispatch() noexcept {
  if (callback)
    callback(callback_arg);
  else if (coroutine)
    coroutine.resume();
}

io_context::~io_context() { discard_post_queue(); }

void io_context::post(std::coroutine_handle<> coroutine) {
  auto *node = new post_node{};
  node->coroutine = coroutine;
  node->heap_owned = true;
  push_node(node);
  wake();
}

void io_context::post(void (*callback)(void *), void *argument,
                      void (*cleanup)(void *)) {
  auto *node = new post_node{};
  node->callback = callback;
  node->callback_arg = argument;
  node->callback_cleanup = cleanup;
  node->heap_owned = true;
  push_node(node);
  wake();
}

void io_context::post_node_raw(post_node *node) {
  node->next.store(nullptr, std::memory_order_relaxed);
  push_node_no_delete(node);
  wake();
}

auto io_context::drain_post_queue() -> std::size_t {
  auto *chain = post_head_.exchange(nullptr, std::memory_order_acquire);
  if (!chain)
    return 0;

  post_node *reversed = nullptr;
  while (chain) {
    auto *next = chain->next.load(std::memory_order_relaxed);
    chain->next.store(reversed, std::memory_order_relaxed);
    reversed = chain;
    chain = next;
  }

  std::size_t count = 0;
  while (reversed) {
    auto *node = reversed;
    reversed = node->next.load(std::memory_order_relaxed);
    node->dispatch();
    if (node->heap_owned)
      delete node;
    ++count;
  }
  return count;
}

void io_context::discard_post_queue() noexcept {
  auto *node = post_head_.exchange(nullptr, std::memory_order_acq_rel);
  while (node) {
    auto *next = node->next.load(std::memory_order_relaxed);
    if (node->heap_owned) {
      if (node->callback_cleanup)
        node->callback_cleanup(node->callback_arg);
      delete node;
    }
    node = next;
  }
}

void io_context::push_node(post_node *node) {
  node->next.store(nullptr, std::memory_order_relaxed);
  auto *previous = post_head_.exchange(node, std::memory_order_acq_rel);
  if (previous)
    node->next.store(previous, std::memory_order_relaxed);
}

void io_context::push_node_no_delete(post_node *node) {
  auto *previous = post_head_.exchange(node, std::memory_order_acq_rel);
  if (previous)
    node->next.store(previous, std::memory_order_relaxed);
}

auto post_awaitable::await_ready() const noexcept -> bool { return false; }

void post_awaitable::await_suspend(std::coroutine_handle<> coroutine) noexcept {
  ctx.post(coroutine);
}

void post_awaitable::await_resume() noexcept {}

} // namespace cnetmod
