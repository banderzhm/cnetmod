module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.grpc.governance.endpoint;

import std;

namespace cnetmod::grpc::governance {

endpoint::endpoint() = default;

endpoint::endpoint(std::string endpoint_id, std::string endpoint_url,
                   std::uint32_t endpoint_weight,
                   std::uint32_t endpoint_priority,
                   endpoint_state endpoint_state)
    : id(std::move(endpoint_id)), url(std::move(endpoint_url)),
      weight(endpoint_weight), priority(endpoint_priority),
      state(endpoint_state) {}

auto endpoint::is_available() const noexcept -> bool {
  return state == endpoint_state::healthy;
}

} // namespace cnetmod::grpc::governance
