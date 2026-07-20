module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.discovery;

import std;
import cnetmod.protocol.grpc.governance.endpoint;

namespace cnetmod::grpc::governance {

export class static_discovery {
public:
  static_discovery();
  explicit static_discovery(std::vector<endpoint> endpoints);

  void replace_snapshot(std::vector<endpoint> endpoints);
  [[nodiscard]] auto snapshot() const -> std::vector<endpoint>;

private:
  mutable std::shared_mutex mutex_;
  std::vector<endpoint> endpoints_;
};

} // namespace cnetmod::grpc::governance
