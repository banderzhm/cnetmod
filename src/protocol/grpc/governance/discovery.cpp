module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.grpc.governance.discovery;

import std;
import cnetmod.protocol.grpc.governance.endpoint;

namespace cnetmod::grpc::governance {

static_discovery::static_discovery() = default;

static_discovery::static_discovery(std::vector<endpoint> endpoints)
    : endpoints_(std::move(endpoints)) {}

void static_discovery::replace_snapshot(std::vector<endpoint> endpoints)
{
    endpoints_.store(std::move(endpoints));
}

auto static_discovery::snapshot() const -> std::vector<endpoint>
{
    return endpoints_.snapshot();
}

} // namespace cnetmod::grpc::governance
