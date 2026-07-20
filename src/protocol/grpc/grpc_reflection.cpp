module cnetmod.protocol.grpc.reflection;
import std;
import cnetmod.coro.task;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.proto;
import cnetmod.protocol.grpc.server;
namespace cnetmod::grpc::reflection {
auto encode_list_services_request(std::string_view host) -> byte_buffer {
  byte_buffer out;
  if (!host.empty())
    proto::append_string(out, 1, host);
  proto::append_string(out, 7, "");
  return out;
}
auto decode_request(std::span<const std::byte> payload)
    -> std::expected<reflection_request, status> {
  auto fields = proto::decode_message(payload);
  if (!fields)
    return std::unexpected(make_status(status_code::invalid_argument,
                                       "invalid reflection request"));
  for (const auto [number, kind, error] :
       {std::tuple{3U, request_kind::file_by_filename,
                   "invalid file_by_filename request"},
        std::tuple{4U, request_kind::file_containing_symbol,
                   "invalid file_containing_symbol request"},
        std::tuple{7U, request_kind::list_services,
                   "invalid list_services request"}})
    if (auto *field = proto::find_first(*fields, number)) {
      auto value = proto::field_string(*field);
      return value
                 ? std::expected<reflection_request, status>{reflection_request{
                       .kind = kind, .value = std::move(*value)}}
                 : std::unexpected(
                       make_status(status_code::invalid_argument, error));
    }
  return reflection_request{};
}
auto encode_error_response(std::span<const std::byte> request, status_code code,
                           std::string_view message) -> byte_buffer {
  byte_buffer error;
  proto::append_uint64(error, 1, static_cast<std::uint64_t>(code));
  proto::append_string(error, 2, message);
  byte_buffer out;
  proto::append_bytes(out, 2, request);
  proto::append_bytes(out, 7, error);
  return out;
}
auto encode_list_services_response(std::span<const std::byte> request,
                                   std::span<const std::string> services)
    -> byte_buffer {
  byte_buffer list;
  for (const auto &service : services) {
    byte_buffer entry;
    proto::append_string(entry, 1, service);
    proto::append_bytes(list, 1, entry);
  }
  byte_buffer out;
  proto::append_bytes(out, 2, request);
  proto::append_bytes(out, 6, list);
  return out;
}
auto decode_list_services_response(std::span<const std::byte> payload)
    -> std::expected<std::vector<std::string>, status> {
  auto fields = proto::decode_message(payload);
  if (!fields)
    return std::unexpected(make_status(status_code::invalid_argument,
                                       "invalid reflection response"));
  auto *list = proto::find_first(*fields, 6);
  if (!list)
    return std::unexpected(make_status(status_code::not_found,
                                       "reflection list response missing"));
  auto list_fields = proto::decode_message(list->bytes);
  if (!list_fields)
    return std::unexpected(make_status(status_code::invalid_argument,
                                       "invalid reflection list response"));
  std::vector<std::string> services;
  for (const auto &entry : *list_fields) {
    if (entry.number != 1)
      continue;
    auto message = proto::decode_message(entry.bytes);
    if (!message)
      continue;
    if (auto *name = proto::find_first(*message, 1))
      if (auto text = proto::field_string(*name))
        services.push_back(std::move(*text));
  }
  return services;
}
void install_service(service_router &router,
                     std::vector<std::string> services) {
  services.emplace_back(v1alpha_service);
  services.emplace_back(v1_service);
  std::ranges::sort(services);
  services.erase(std::ranges::unique(services).begin(), services.end());
  auto handler =
      [services = std::move(services)](std::span<const byte_buffer> requests,
                                       const call_context &)
      -> task<std::expected<std::vector<byte_buffer>, status>> {
    std::vector<byte_buffer> responses;
    responses.reserve(requests.size());
    for (const auto &request : requests) {
      auto parsed = decode_request(request);
      if (!parsed)
        co_return std::unexpected(parsed.error());
      if (parsed->kind == request_kind::list_services)
        responses.push_back(encode_list_services_response(
            request, {services.data(), services.size()}));
      else
        responses.push_back(
            encode_error_response(request, status_code::unimplemented,
                                  "reflection request is not implemented"));
    }
    co_return responses;
  };
  router.add_bidi_streaming(std::string(v1alpha_service),
                            std::string(reflection_method), handler);
  router.add_bidi_streaming(std::string(v1_service),
                            std::string(reflection_method), std::move(handler));
}
} // namespace cnetmod::grpc::reflection
