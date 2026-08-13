module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.discovery;

import std;
import cnetmod.protocol.grpc.governance.endpoint;
import cnetmod.utils.concurrent_containers.copy_on_write_value;

namespace cnetmod::grpc::governance {

export class static_discovery
{
public:
    static_discovery();
    explicit static_discovery(std::vector<endpoint> endpoints);

    void replace_snapshot(std::vector<endpoint> endpoints);
    [[nodiscard]] auto snapshot() const -> std::vector<endpoint>;

private:
    concurrent_containers::copy_on_write_value<std::vector<endpoint>> endpoints_;
};

} // namespace cnetmod::grpc::governance
