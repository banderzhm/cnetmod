module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.client;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.grpc.client;
import cnetmod.protocol.grpc.governance.circuit_breaker;
import cnetmod.protocol.grpc.governance.discovery;
import cnetmod.protocol.grpc.governance.load_balancer;
import cnetmod.protocol.grpc.governance.retry;
import cnetmod.protocol.grpc.types;

namespace cnetmod::grpc::governance {

export class governed_client {
public:
  governed_client(io_context &ctx, static_discovery &discovery,
                  load_balancer &balancer, retry_policy retry = {},
                  retry_budget *budget = nullptr,
                  circuit_breaker *breaker = nullptr,
                  client_options options = {});

  [[nodiscard]] auto unary(unary_request req, bool idempotent = false)
      -> task<std::expected<unary_response, status>>;

private:
  io_context *ctx_;
  static_discovery *discovery_;
  load_balancer *balancer_;
  retry_policy retry_;
  retry_budget *budget_;
  circuit_breaker *breaker_;
  client_options options_;
};

} // namespace cnetmod::grpc::governance
