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
    service_unknown = 3,
};

export class registry {
public:
    void set(std::string service, serving_status status) {
        std::scoped_lock lock(mutex_);
        statuses_[std::move(service)] = status;
    }

    [[nodiscard]] auto get(std::string_view service) const -> serving_status {
        std::scoped_lock lock(mutex_);
        auto it = statuses_.find(std::string(service));
        if (it != statuses_.end()) return it->second;
        if (service.empty()) {
            return default_status_;
        }
        return serving_status::service_unknown;
    }

    void set_default(serving_status status) {
        std::scoped_lock lock(mutex_);
        default_status_ = status;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, serving_status> statuses_;
    serving_status default_status_ = serving_status::serving;
};

export auto encode_request(std::string_view service) -> byte_buffer {
    byte_buffer out;
    proto::append_string(out, 1, service);
    return out;
}

export auto decode_request(std::span<const std::byte> payload)
    -> std::expected<std::string, status>
{
    auto fields = proto::decode_message(payload);
    if (!fields) {
        return std::unexpected(status{
            .code = status_code::invalid_argument,
            .message = fields.error().message(),
        });
    }
    auto* service = proto::find_first(*fields, 1);
    if (!service) return std::string{};
    auto text = proto::field_string(*service);
    if (!text) {
        return std::unexpected(status{
            .code = status_code::invalid_argument,
            .message = "invalid health check service field",
        });
    }
    return *text;
}

export auto encode_response(serving_status status) -> byte_buffer {
    byte_buffer out;
    proto::append_uint64(out, 1, static_cast<std::uint64_t>(status));
    return out;
}

export auto decode_response(std::span<const std::byte> payload)
    -> std::expected<serving_status, status>
{
    auto fields = proto::decode_message(payload);
    if (!fields) {
        return std::unexpected(status{
            .code = status_code::invalid_argument,
            .message = fields.error().message(),
        });
    }
    auto* st = proto::find_first(*fields, 1);
    if (!st) return serving_status::unknown;
    auto value = proto::field_uint64(*st);
    if (!value) {
        return std::unexpected(status{
            .code = status_code::invalid_argument,
            .message = "invalid health check status field",
        });
    }
    return static_cast<serving_status>(*value);
}

export void register_service(service_router& router, registry& reg) {
    router.add_unary("grpc.health.v1.Health", "Check",
        [&reg](std::span<const std::byte> payload, const call_context&) -> task<std::expected<byte_buffer, status>> {
            auto service = decode_request(payload);
            if (!service) co_return std::unexpected(service.error());
            auto st = reg.get(*service);
            if (st == serving_status::service_unknown) {
                co_return std::unexpected(status{
                    .code = status_code::not_found,
                    .message = "service is not registered",
                });
            }
            co_return encode_response(st);
        });

    router.add_server_streaming("grpc.health.v1.Health", "Watch",
        [&reg](std::span<const std::byte> payload, const call_context&) -> task<std::expected<std::vector<byte_buffer>, status>> {
            auto service = decode_request(payload);
            if (!service) co_return std::unexpected(service.error());
            auto st = reg.get(*service);
            if (st == serving_status::service_unknown) {
                co_return std::unexpected(status{
                    .code = status_code::not_found,
                    .message = "service is not registered",
                });
            }
            co_return std::vector<byte_buffer>{encode_response(st)};
        });
}

} // namespace cnetmod::grpc::health
