module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.grpc.health;
import std;
import cnetmod.coro.task;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.server;
import cnetmod.protocol.grpc.proto;
namespace cnetmod::grpc::health {
export enum class serving_status : std::uint64_t {
  unknown = 0,
  serving = 1,
  not_serving = 2,
  service_unknown = 3
};
export class registry {
public:
  void set(std::string service, serving_status status);
  [[nodiscard]] auto get(std::string_view service) const -> serving_status;
  void set_default(serving_status status);

private:
  mutable std::mutex mutex_;
  std::map<std::string, serving_status> statuses_;
  serving_status default_status_ = serving_status::serving;
};
export auto encode_request(std::string_view service) -> byte_buffer;
export auto decode_request(std::span<const std::byte> payload)
    -> std::expected<std::string, status>;
export auto encode_response(serving_status status) -> byte_buffer;
export auto decode_response(std::span<const std::byte> payload)
    -> std::expected<serving_status, status>;
export void register_service(service_router &router, registry &registry);
} // namespace cnetmod::grpc::health
