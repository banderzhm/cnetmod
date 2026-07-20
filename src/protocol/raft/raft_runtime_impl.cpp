module cnetmod.protocol.raft;

import std;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import :types;
import :node;
import :tcp_transport;
import :runtime;

namespace cnetmod::raft {
raft_node_runtime::raft_node_runtime(io_context &ctx, raft_node &node,
                                     raft_tcp_transport &transport,
                                     endpoint listen_endpoint,
                                     raft_options options,
                                     raft_runtime_options runtime_options)
    : ctx_(ctx), node_(node), transport_(transport),
      listen_endpoint_(std::move(listen_endpoint)), options_(options),
      runtime_options_(runtime_options) {
  transport_.set_node(node_);
}

void raft_node_runtime::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true))
    return;
  if (runtime_options_.start_tcp_server)
    spawn_loop(serve_loop());
  if (runtime_options_.auto_election)
    spawn_loop(election_loop());
  if (runtime_options_.auto_heartbeat)
    spawn_loop(heartbeat_loop());
}

auto raft_node_runtime::async_stop() -> task<void> {
  stop();
  co_await transport_.async_stop();
  while (active_loops_.load(std::memory_order_acquire) != 0)
    (void)co_await async_timer_wait(ctx_, std::chrono::milliseconds{1});
}

void raft_node_runtime::spawn_loop(task<void> loop) {
  active_loops_.fetch_add(1, std::memory_order_acq_rel);
  spawn(ctx_, run_loop(std::move(loop)));
}

auto raft_node_runtime::run_loop(task<void> loop) -> task<void> {
  struct completion_guard {
    std::atomic<std::size_t> &count;
    ~completion_guard() { count.fetch_sub(1, std::memory_order_acq_rel); }
  } guard{active_loops_};
  co_await std::move(loop);
}

void raft_node_runtime::stop() noexcept {
  if (!running_.exchange(false))
    return;
  transport_.stop();
  accept_token_.cancel();
  election_token_.cancel();
  heartbeat_token_.cancel();
}

auto raft_node_runtime::running() const noexcept -> bool {
  return running_.load(std::memory_order_acquire);
}

void raft_node_runtime::tick_now() {
  if (node_.role() == node_role::leader)
    transport_.replicate_to_all(node_);
  else if (node_.election_timeout_elapsed(options_.election_timeout)) {
    if (options_.pre_vote)
      transport_.broadcast_pre_vote(node_);
    else
      transport_.broadcast_request_vote(node_);
  }
}

auto raft_node_runtime::transfer_leader(const node_id &target)
    -> std::expected<void, raft_error> {
  return transport_.transfer_leader(node_, target);
}

auto raft_node_runtime::maybe_snapshot_now()
    -> std::expected<std::optional<snapshot_metadata>, raft_error> {
  return node_.maybe_create_snapshot(runtime_options_.snapshot_policy);
}

auto raft_node_runtime::async_read_index(read_index_request request)
    -> task<std::expected<read_index_response, raft_error>> {
  auto submitted = node_.read_index(std::move(request));
  if (!submitted)
    co_return std::unexpected(submitted.error());
  if (submitted->ready) {
    auto consumed = node_.consume_read_index(submitted->id);
    co_return consumed ? *consumed : *submitted;
  }
  transport_.replicate_to_all(node_);
  const auto deadline =
      std::chrono::steady_clock::now() + options_.read_index_timeout;
  while (running() || !runtime_options_.auto_heartbeat) {
    if (auto ready = node_.consume_read_index(submitted->id))
      co_return *ready;
    if (std::chrono::steady_clock::now() >= deadline) {
      (void)node_.expire_read_indexes(std::chrono::milliseconds{0});
      co_return std::unexpected(
          raft_error{.code = raft_errc::stopped,
                     .message = "read-index request timed out"});
    }
    const auto delay =
        std::min(options_.heartbeat_interval,
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     deadline - std::chrono::steady_clock::now()));
    (void)co_await async_timer_wait(ctx_, delay);
  }
  co_return std::unexpected(raft_error{.code = raft_errc::stopped,
                                       .message = "raft runtime stopped"});
}

auto raft_node_runtime::serve_loop() -> task<void> {
  auto result = co_await transport_.serve(listen_endpoint_, accept_token_);
  if (!result)
    stop();
}

auto raft_node_runtime::election_loop() -> task<void> {
  while (running()) {
    election_token_.reset();
    const auto delay = randomized_election_timeout();
    auto result = co_await async_timer_wait(ctx_, delay, election_token_);
    if (!result || !running() || !node_.election_timeout_elapsed(delay))
      continue;
    if (options_.pre_vote)
      transport_.broadcast_pre_vote(node_);
    else
      transport_.broadcast_request_vote(node_);
  }
}

auto raft_node_runtime::heartbeat_loop() -> task<void> {
  while (running()) {
    heartbeat_token_.reset();
    auto result = co_await async_timer_wait(ctx_, options_.heartbeat_interval,
                                            heartbeat_token_);
    if (!result || !running() || node_.role() != node_role::leader)
      continue;
    (void)node_.expire_read_indexes(options_.read_index_timeout);
    if (options_.check_quorum &&
        !node_.check_leader_quorum(options_.leader_lease_timeout))
      continue;
    if (runtime_options_.auto_snapshot) {
      auto snapshotted =
          node_.maybe_create_snapshot(runtime_options_.snapshot_policy);
      if (!snapshotted) {
        node_.stop(snapshotted.error());
        continue;
      }
    }
    transport_.cleanup_snapshot_sessions();
    if (auto cleaned = co_await transport_.cleanup_snapshot_files(); !cleaned) {
      node_.stop(raft_error{.code = raft_errc::storage_error,
                            .message = cleaned.error().message()});
      continue;
    }
    transport_.replicate_to_all(node_);
  }
}

auto raft_node_runtime::randomized_election_timeout()
    -> std::chrono::milliseconds {
  const auto base =
      std::max<std::int64_t>(1, options_.election_timeout.count());
  std::uniform_int_distribution<std::int64_t> distribution{0, base};
  return std::chrono::milliseconds{base + distribution(rng_)};
}
} // namespace cnetmod::raft
