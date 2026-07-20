module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.grpc.reflection;
import std;
import cnetmod.coro.task;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.proto;
import cnetmod.protocol.grpc.server;
namespace cnetmod::grpc::reflection {
export inline constexpr std::string_view v1alpha_service =
    "grpc.reflection.v1alpha.ServerReflection";
export inline constexpr std::string_view v1_service =
    "grpc.reflection.v1.ServerReflection";
export inline constexpr std::string_view reflection_method =
    "ServerReflectionInfo";
export enum class request_kind {
  unsupported,
  file_by_filename,
  file_containing_symbol,
  list_services
};
export struct reflection_request {
  request_kind kind = request_kind::unsupported;
  std::string value;
};
export auto encode_list_services_request(std::string_view host = {})
    -> byte_buffer;
export auto decode_request(std::span<const std::byte> payload)
    -> std::expected<reflection_request, status>;
export auto encode_error_response(std::span<const std::byte> original_request,
                                  status_code code, std::string_view message)
    -> byte_buffer;
export auto
encode_list_services_response(std::span<const std::byte> original_request,
                              std::span<const std::string> services)
    -> byte_buffer;
export auto decode_list_services_response(std::span<const std::byte> payload)
    -> std::expected<std::vector<std::string>, status>;
export void install_service(service_router &router,
                            std::vector<std::string> services);
} // namespace cnetmod::grpc::reflection
