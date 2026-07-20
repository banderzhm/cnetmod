module cnetmod.protocol.grpc.health;
import std;
import cnetmod.coro.task;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.server;
import cnetmod.protocol.grpc.proto;
namespace cnetmod::grpc::health {
void registry::set(std::string service, serving_status status) {
  std::scoped_lock lock(mutex_);
  statuses_[std::move(service)] = status;
}
auto registry::get(std::string_view service) const -> serving_status {
  std::scoped_lock lock(mutex_);
  if (auto it = statuses_.find(std::string(service)); it != statuses_.end())
    return it->second;
  return service.empty() ? default_status_ : serving_status::service_unknown;
}
void registry::set_default(serving_status status) {
  std::scoped_lock lock(mutex_);
  default_status_ = status;
}
auto encode_request(std::string_view service) -> byte_buffer {
  byte_buffer out;
  proto::append_string(out, 1, service);
  return out;
}
auto decode_request(std::span<const std::byte> payload)
    -> std::expected<std::string, status> {
  auto fields = proto::decode_message(payload);
  if (!fields)
    return std::unexpected(status{.code = status_code::invalid_argument,
                                  .message = fields.error().message()});
  auto *service = proto::find_first(*fields, 1);
  if (!service)
    return std::string{};
  auto text = proto::field_string(*service);
  return text ? std::expected<std::string, status>{std::move(*text)}
              : std::unexpected(
                    status{.code = status_code::invalid_argument,
                           .message = "invalid health check service field"});
}
auto encode_response(serving_status state) -> byte_buffer {
  byte_buffer out;
  proto::append_uint64(out, 1, static_cast<std::uint64_t>(state));
  return out;
}
auto decode_response(std::span<const std::byte> payload)
    -> std::expected<serving_status, status> {
  auto fields = proto::decode_message(payload);
  if (!fields)
    return std::unexpected(status{.code = status_code::invalid_argument,
                                  .message = fields.error().message()});
  auto *field = proto::find_first(*fields, 1);
  if (!field)
    return serving_status::unknown;
  auto value = proto::field_uint64(*field);
  return value ? std::expected<serving_status,
                               status>{static_cast<serving_status>(*value)}
               : std::unexpected(
                     status{.code = status_code::invalid_argument,
                            .message = "invalid health check status field"});
}
void register_service(service_router &router, registry &registry) {
  router.add_unary(
      "grpc.health.v1.Health", "Check",
      [&registry](std::span<const std::byte> payload, const call_context &)
          -> task<std::expected<byte_buffer, status>> {
        auto service = decode_request(payload);
        if (!service)
          co_return std::unexpected(service.error());
        auto state = registry.get(*service);
        if (state == serving_status::service_unknown)
          co_return std::unexpected(
              status{.code = status_code::not_found,
                     .message = "service is not registered"});
        co_return encode_response(state);
      });
  router.add_server_streaming(
      "grpc.health.v1.Health", "Watch",
      [&registry](std::span<const std::byte> payload, const call_context &)
          -> task<std::expected<std::vector<byte_buffer>, status>> {
        auto service = decode_request(payload);
        if (!service)
          co_return std::unexpected(service.error());
        auto state = registry.get(*service);
        if (state == serving_status::service_unknown)
          co_return std::unexpected(
              status{.code = status_code::not_found,
                     .message = "service is not registered"});
        co_return std::vector<byte_buffer>{encode_response(state)};
      });
}
} // namespace cnetmod::grpc::health
