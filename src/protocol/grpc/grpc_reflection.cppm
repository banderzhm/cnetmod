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
    list_services,
};

export struct reflection_request {
    request_kind kind = request_kind::unsupported;
    std::string value;
};

export auto encode_list_services_request(std::string_view host = {}) -> byte_buffer {
    byte_buffer out;
    if (!host.empty()) proto::append_string(out, 1, host);
    proto::append_string(out, 7, "");
    return out;
}

export auto decode_request(std::span<const std::byte> payload)
    -> std::expected<reflection_request, status>
{
    auto fields = proto::decode_message(payload);
    if (!fields) {
        return std::unexpected(make_status(status_code::invalid_argument, "invalid reflection request"));
    }

    if (auto* f = proto::find_first(*fields, 3)) {
        auto value = proto::field_string(*f);
        if (!value) return std::unexpected(make_status(status_code::invalid_argument, "invalid file_by_filename request"));
        return reflection_request{.kind = request_kind::file_by_filename, .value = std::move(*value)};
    }
    if (auto* f = proto::find_first(*fields, 4)) {
        auto value = proto::field_string(*f);
        if (!value) return std::unexpected(make_status(status_code::invalid_argument, "invalid file_containing_symbol request"));
        return reflection_request{.kind = request_kind::file_containing_symbol, .value = std::move(*value)};
    }
    if (auto* f = proto::find_first(*fields, 7)) {
        auto value = proto::field_string(*f);
        if (!value) return std::unexpected(make_status(status_code::invalid_argument, "invalid list_services request"));
        return reflection_request{.kind = request_kind::list_services, .value = std::move(*value)};
    }

    return reflection_request{};
}

export auto encode_error_response(std::span<const std::byte> original_request,
                                  status_code code,
                                  std::string_view message) -> byte_buffer
{
    byte_buffer error;
    proto::append_uint64(error, 1, static_cast<std::uint64_t>(code));
    proto::append_string(error, 2, message);

    byte_buffer out;
    proto::append_bytes(out, 2, original_request);
    proto::append_bytes(out, 7, error);
    return out;
}

export auto encode_list_services_response(std::span<const std::byte> original_request,
                                          std::span<const std::string> services) -> byte_buffer
{
    byte_buffer list_response;
    for (const auto& service : services) {
        byte_buffer service_response;
        proto::append_string(service_response, 1, service);
        proto::append_bytes(list_response, 1, service_response);
    }

    byte_buffer out;
    proto::append_bytes(out, 2, original_request);
    proto::append_bytes(out, 6, list_response);
    return out;
}

export auto decode_list_services_response(std::span<const std::byte> payload)
    -> std::expected<std::vector<std::string>, status>
{
    auto fields = proto::decode_message(payload);
    if (!fields) {
        return std::unexpected(make_status(status_code::invalid_argument, "invalid reflection response"));
    }
    auto* list = proto::find_first(*fields, 6);
    if (!list) return std::unexpected(make_status(status_code::not_found, "reflection list response missing"));

    auto list_fields = proto::decode_message(list->bytes);
    if (!list_fields) {
        return std::unexpected(make_status(status_code::invalid_argument, "invalid reflection list response"));
    }

    std::vector<std::string> services;
    for (const auto& service_field : *list_fields) {
        if (service_field.number != 1) continue;
        auto service_msg = proto::decode_message(service_field.bytes);
        if (!service_msg) continue;
        auto* name = proto::find_first(*service_msg, 1);
        if (!name) continue;
        if (auto text = proto::field_string(*name)) services.push_back(std::move(*text));
    }
    return services;
}

export void install_service(service_router& router, std::vector<std::string> services) {
    services.emplace_back(std::string(v1alpha_service));
    services.emplace_back(std::string(v1_service));
    std::ranges::sort(services);
    services.erase(std::ranges::unique(services).begin(), services.end());

    auto handler = [services = std::move(services)](
        std::span<const byte_buffer> requests,
        const call_context&) -> cnetmod::task<std::expected<std::vector<byte_buffer>, status>> {
        std::vector<byte_buffer> responses;
        responses.reserve(requests.size());
        for (const auto& request : requests) {
            auto decoded = decode_request(request);
            if (!decoded) co_return std::unexpected(decoded.error());
            if (decoded->kind == request_kind::list_services) {
                responses.push_back(encode_list_services_response(
                    request, std::span<const std::string>{services.data(), services.size()}));
            } else {
                responses.push_back(encode_error_response(
                    request, status_code::unimplemented,
                    "reflection request is not implemented"));
            }
        }
        co_return responses;
    };

    router.add_bidi_streaming(std::string(v1alpha_service), std::string(reflection_method), handler);
    router.add_bidi_streaming(std::string(v1_service), std::string(reflection_method), std::move(handler));
}

} // namespace cnetmod::grpc::reflection
