module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.proto;

import std;
import cnetmod.core.error;
import cnetmod.protocol.grpc.types;

namespace cnetmod::grpc::proto {

export enum class wire_type : std::uint8_t {
  varint = 0,
  fixed64 = 1,
  length_delimited = 2,
  fixed32 = 5
};
export struct field {
  std::uint32_t number = 0;
  wire_type type = wire_type::varint;
  std::uint64_t varint_value = 0;
  std::uint64_t fixed64_value = 0;
  byte_buffer bytes;
  std::uint32_t fixed32_value = 0;
};
export struct field_def {
  std::string label;
  std::string type;
  std::string name;
  std::uint32_t number = 0;
};
export struct message_def {
  std::string name;
  std::vector<field_def> fields;
};
export struct rpc_def {
  std::string name;
  std::string request_type;
  std::string response_type;
  bool client_streaming = false;
  bool server_streaming = false;
};
export struct service_def {
  std::string name;
  std::vector<rpc_def> rpcs;
};
export struct file_def {
  std::string syntax = "proto3";
  std::string package;
  std::vector<message_def> messages;
  std::vector<service_def> services;
};

export auto parse_schema(std::string_view proto_text)
    -> std::expected<file_def, std::error_code>;
export auto encode_varint(std::uint64_t value) -> byte_buffer;
export auto decode_varint(std::span<const std::byte> data, std::size_t &pos)
    -> std::optional<std::uint64_t>;
export auto zigzag_encode(std::int64_t value) noexcept -> std::uint64_t;
export auto zigzag_decode(std::uint64_t value) noexcept -> std::int64_t;
export void append_key(byte_buffer &out, std::uint32_t number, wire_type type);
export void append_uint64(byte_buffer &out, std::uint32_t number,
                          std::uint64_t value);
export void append_int64(byte_buffer &out, std::uint32_t number,
                         std::int64_t value);
export void append_sint64(byte_buffer &out, std::uint32_t number,
                          std::int64_t value);
export void append_bool(byte_buffer &out, std::uint32_t number, bool value);
export void append_bytes(byte_buffer &out, std::uint32_t number,
                         std::span<const std::byte> value);
export void append_string(byte_buffer &out, std::uint32_t number,
                          std::string_view value);
export auto decode_message(std::span<const std::byte> data)
    -> std::expected<std::vector<field>, std::error_code>;
export auto find_first(std::span<const field> fields, std::uint32_t number)
    -> const field *;
export auto field_string(const field &f) -> std::optional<std::string>;
export auto field_bytes(const field &f) -> std::optional<byte_buffer>;
export auto field_uint64(const field &f) -> std::optional<std::uint64_t>;
export auto field_bool(const field &f) -> std::optional<bool>;
export auto find_message(const file_def &file, std::string_view name)
    -> const message_def *;
export auto find_service(const file_def &file, std::string_view name)
    -> const service_def *;
export auto find_rpc(const service_def &service, std::string_view name)
    -> const rpc_def *;
export auto rpc_path(const file_def &file, const service_def &service,
                     const rpc_def &rpc) -> std::string;
export auto rpc_kind(const rpc_def &rpc) noexcept -> cnetmod::grpc::call_kind;

} // namespace cnetmod::grpc::proto
