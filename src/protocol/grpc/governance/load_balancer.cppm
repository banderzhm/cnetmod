module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.load_balancer;

import std;
import cnetmod.protocol.grpc.governance.discovery;
import cnetmod.protocol.grpc.governance.endpoint;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::grpc::governance {

export class load_balancer
{
public:
    [[nodiscard]] auto pick(std::span<const endpoint> endpoints)
        -> std::optional<endpoint>;
    [[nodiscard]] auto pick(const static_discovery& discovery)
        -> std::optional<endpoint>;

private:
    std::unordered_map<std::string, std::int64_t> current_weights_;
    concurrent_containers::atomic_rw_latch latch_;
};

} // namespace cnetmod::grpc::governance
