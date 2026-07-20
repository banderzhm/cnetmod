module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.grpc.governance.client;

import std;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.protocol.grpc.client;
import cnetmod.protocol.grpc.governance.circuit_breaker;
import cnetmod.protocol.grpc.governance.discovery;
import cnetmod.protocol.grpc.governance.load_balancer;
import cnetmod.protocol.grpc.governance.retry;
import cnetmod.protocol.grpc.types;

namespace cnetmod::grpc::governance {

namespace {

auto unavailable(std::string message) -> status {
  return make_status(status_code::unavailable, std::move(message));
}

} // namespace

governed_client::governed_client(io_context &ctx, static_discovery &discovery,
                                 load_balancer &balancer, retry_policy retry,
                                 retry_budget *budget, circuit_breaker *breaker,
                                 client_options options)
    : ctx_(&ctx), discovery_(&discovery), balancer_(&balancer),
      retry_(std::move(retry)), budget_(budget), breaker_(breaker),
      options_(std::move(options)) {}

auto governed_client::unary(unary_request req, bool idempotent)
    -> task<std::expected<unary_response, status>> {
  std::uint32_t completed_attempts = 0;

  for (;;) {
    auto endpoint = balancer_->pick(*discovery_);
    if (!endpoint)
      co_return std::unexpected(unavailable("no available grpc endpoints"));

    if (breaker_ && !breaker_->try_acquire()) {
      co_return std::unexpected(unavailable("grpc circuit breaker is open"));
    }

    client temporary{*ctx_, endpoint->url, options_};
    auto response = co_await temporary.unary(req);
    temporary.close();
    ++completed_attempts;

    if (response) {
      if (breaker_)
        breaker_->record_success();
      if (budget_)
        budget_->record_success();
      co_return std::move(response);
    }

    if (breaker_)
      breaker_->record_failure();

    if (!idempotent ||
        !retry_.allows_retry(response.error().code, completed_attempts)) {
      co_return std::unexpected(response.error());
    }

    if (budget_ && !budget_->try_acquire())
      co_return std::unexpected(response.error());

    co_await async_sleep(*ctx_, retry_.backoff_for(completed_attempts - 1));
  }
}

} // namespace cnetmod::grpc::governance
