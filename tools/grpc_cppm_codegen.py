#!/usr/bin/env python3
"""Generate cnetmod gRPC C++ module messages, clients, and server binders.

The script also works as a protoc plugin when invoked by protoc with no
positional arguments. The generated message runtime is intentionally scoped to
cnetmod's lightweight protobuf wire support, not the full Google protobuf C++
runtime.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class Field:
    label: str
    type: str
    name: str
    number: int


@dataclass(frozen=True)
class Message:
    name: str
    fields: list[Field]


@dataclass(frozen=True)
class Rpc:
    name: str
    request: str
    response: str
    client_streaming: bool
    server_streaming: bool


@dataclass(frozen=True)
class Service:
    name: str
    rpcs: list[Rpc]


@dataclass(frozen=True)
class ProtoFile:
    package: str
    messages: list[Message]
    services: list[Service]


def strip_comments(text: str) -> str:
    text = re.sub(r"//.*?$", "", text, flags=re.MULTILINE)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def parse_proto(text: str) -> ProtoFile:
    clean = strip_comments(text)
    package_match = re.search(r"\bpackage\s+([A-Za-z_][A-Za-z0-9_.]*)\s*;", clean)
    package = package_match.group(1) if package_match else ""
    messages: list[Message] = []
    services: list[Service] = []

    message_re = re.compile(r"\bmessage\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{(.*?)\}", re.DOTALL)
    field_re = re.compile(
        r"\b(?:(repeated|optional|required)\s+)?"
        r"([A-Za-z_][A-Za-z0-9_.]*)\s+"
        r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([0-9]+)"
        r"(?:\s*\[[^\]]*\])?\s*;",
        re.DOTALL,
    )
    for message_match in message_re.finditer(clean):
        fields = [
            Field(
                label=field_match.group(1) or "",
                type=field_match.group(2),
                name=field_match.group(3),
                number=int(field_match.group(4)),
            )
            for field_match in field_re.finditer(message_match.group(2))
        ]
        messages.append(Message(name=message_match.group(1), fields=fields))

    service_re = re.compile(r"\bservice\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{(.*?)\}", re.DOTALL)
    rpc_re = re.compile(
        r"\brpc\s+([A-Za-z_][A-Za-z0-9_]*)\s*"
        r"\(\s*(stream\s+)?([A-Za-z_][A-Za-z0-9_.]*)\s*\)\s*"
        r"returns\s*\(\s*(stream\s+)?([A-Za-z_][A-Za-z0-9_.]*)\s*\)",
        re.DOTALL,
    )
    for service_match in service_re.finditer(clean):
        rpcs: list[Rpc] = []
        for rpc_match in rpc_re.finditer(service_match.group(2)):
            rpcs.append(
                Rpc(
                    name=rpc_match.group(1),
                    request=rpc_match.group(3),
                    response=rpc_match.group(5),
                    client_streaming=rpc_match.group(2) is not None,
                    server_streaming=rpc_match.group(4) is not None,
                )
            )
        services.append(Service(name=service_match.group(1), rpcs=rpcs))
    return ProtoFile(package=package, messages=messages, services=services)


def ns_from_package(package: str) -> str:
    return "::".join(part for part in package.split(".") if part)


def full_service_name(proto: ProtoFile, service: Service) -> str:
    return f"{proto.package}.{service.name}" if proto.package else service.name


def call_method(rpc: Rpc) -> str:
    if rpc.client_streaming and rpc.server_streaming:
        return "bidi_streaming"
    if rpc.client_streaming:
        return "client_streaming"
    if rpc.server_streaming:
        return "server_streaming"
    return "unary"


def handler_type(rpc: Rpc) -> str:
    if rpc.client_streaming or rpc.server_streaming:
        if rpc.client_streaming and not rpc.server_streaming:
            return "cnetmod::grpc::streaming_handler"
        if rpc.server_streaming and not rpc.client_streaming:
            return "cnetmod::grpc::server_streaming_handler"
        return "cnetmod::grpc::streaming_handler"
    return "cnetmod::grpc::unary_handler"


def cpp_field_type(field: Field) -> str:
    base = {
        "string": "std::string",
        "bytes": "cnetmod::grpc::byte_buffer",
        "bool": "bool",
        "uint32": "std::uint32_t",
        "uint64": "std::uint64_t",
        "int32": "std::int32_t",
        "int64": "std::int64_t",
        "sint32": "std::int32_t",
        "sint64": "std::int64_t",
    }.get(field.type, field.type.split(".")[-1])
    if field.label == "repeated":
        return f"std::vector<{base}>"
    return base


def field_default(field: Field) -> str:
    if field.label == "repeated":
        return ""
    if field.type in {"string", "bytes"} or field.type not in {
        "bool", "uint32", "uint64", "int32", "int64", "sint32", "sint64"
    }:
        return ""
    return "{}"


def append_field_line(field: Field, accessor: str) -> str | None:
    fn = {
        "string": "append_string",
        "bytes": "append_bytes",
        "bool": "append_bool",
        "uint32": "append_uint64",
        "uint64": "append_uint64",
        "int32": "append_int64",
        "int64": "append_int64",
        "sint32": "append_sint64",
        "sint64": "append_sint64",
    }.get(field.type)
    if fn:
        return f"cnetmod::grpc::proto::{fn}(out, {field.number}, {accessor});"
    return None


def decode_assign_lines(field: Field, target: str) -> list[str]:
    getter = {
        "string": "field_string",
        "bytes": "field_bytes",
        "bool": "field_bool",
        "uint32": "field_uint64",
        "uint64": "field_uint64",
        "int32": "field_uint64",
        "int64": "field_uint64",
        "sint32": "field_uint64",
        "sint64": "field_uint64",
    }.get(field.type)
    if not getter:
        return [f"            // Field {field.name}: custom message decoding is not generated yet."]
    value_expr = f"cnetmod::grpc::proto::{getter}(f)"
    if field.type in {"sint32", "sint64"}:
        cast_expr = "cnetmod::grpc::proto::zigzag_decode(*value)"
    elif field.type in {"int32", "int64"}:
        cast_expr = f"static_cast<{cpp_field_type(Field('', field.type, field.name, field.number))}>(*value)"
    elif field.type in {"uint32"}:
        cast_expr = "static_cast<std::uint32_t>(*value)"
    else:
        cast_expr = "*value"
    if field.label == "repeated":
        return [
            f"            if (auto value = {value_expr}) {{",
            f"                out.{field.name}.push_back({cast_expr});",
            "            }",
        ]
    return [
        f"            if (auto value = {value_expr}) {{",
        f"                out.{field.name} = {cast_expr};",
        "            }",
    ]


def generate_message(message: Message) -> list[str]:
    lines = [f"struct {message.name} {{", ""]
    for field in message.fields:
        default = field_default(field)
        suffix = default if default else ""
        lines.append(f"    {cpp_field_type(field)} {field.name}{suffix};")
    lines.extend(["", "    [[nodiscard]] auto encode() const -> cnetmod::grpc::byte_buffer {", "        cnetmod::grpc::byte_buffer out;"])
    for field in message.fields:
        if field.label == "repeated":
            lines.append(f"        for (const auto& value : {field.name}) {{")
            append = append_field_line(field, "value")
            if append:
                lines.append(f"            {append}")
            else:
                lines.append(f"            // Field {field.name}: custom message encoding is not generated yet.")
            lines.append("        }")
        else:
            append = append_field_line(field, field.name)
            if append:
                if field.type in {"string", "bytes"}:
                    lines.append(f"        if (!{field.name}.empty()) {append}")
                elif field.type == "bool":
                    lines.append(f"        if ({field.name}) {append}")
                else:
                    lines.append(f"        if ({field.name} != 0) {append}")
            else:
                lines.append(f"        // Field {field.name}: custom message encoding is not generated yet.")
    lines.extend(["        return out;", "    }", ""])
    lines.extend([
        f"    static auto decode(std::span<const std::byte> payload)",
        f"        -> std::expected<{message.name}, cnetmod::grpc::status>",
        "    {",
        "        auto fields = cnetmod::grpc::proto::decode_message(payload);",
        "        if (!fields) {",
        "            return std::unexpected(cnetmod::grpc::make_status(",
        "                cnetmod::grpc::status_code::invalid_argument, \"invalid protobuf message\"));",
        "        }",
        f"        {message.name} out;",
        "        for (const auto& f : *fields) {",
        "            switch (f.number) {",
    ])
    for field in message.fields:
        lines.append(f"        case {field.number}: {{")
        lines.extend(decode_assign_lines(field, "out"))
        lines.extend(["            break;", "        }"])
    lines.extend([
        "        default:",
        "            break;",
        "            }",
        "        }",
        "        return out;",
        "    }",
        "};",
        "",
    ])
    return lines


def generate(proto: ProtoFile, module_name: str) -> str:
    lines: list[str] = [
        "module;",
        "",
        "#include <cnetmod/config.hpp>",
        "",
        f"export module {module_name};",
        "",
        "import std;",
        "import cnetmod.coro.task;",
        "import cnetmod.protocol.grpc;",
        "",
    ]
    namespace = ns_from_package(proto.package)
    if namespace:
        lines.append(f"export namespace {namespace} {{")
    else:
        lines.append("export namespace grpc_generated {")
    lines.append("")

    for message in proto.messages:
        lines.extend(generate_message(message))

    for service in proto.services:
        full_name = full_service_name(proto, service)
        lines.extend(
            [
                f"class {service.name}_client {{",
                "public:",
                f"    static constexpr std::string_view service_name = \"{full_name}\";",
                f"    explicit {service.name}_client(cnetmod::grpc::client& client) noexcept : client_(client) {{}}",
                "",
            ]
        )
        for rpc in service.rpcs:
            if rpc.client_streaming:
                req_type = "cnetmod::grpc::streaming_request"
                resp_type = "cnetmod::grpc::streaming_response" if rpc.server_streaming else "cnetmod::grpc::unary_response"
                lines.extend(
                    [
                        f"    [[nodiscard]] auto {rpc.name}({req_type} req)",
                        f"        -> cnetmod::task<std::expected<{resp_type}, cnetmod::grpc::status>>",
                        "    {",
                        "        req.service = std::string(service_name);",
                        f"        req.method = \"{rpc.name}\";",
                        f"        return client_.{call_method(rpc)}(std::move(req));",
                        "    }",
                        "",
                    ]
                )
            else:
                resp_type = "cnetmod::grpc::streaming_response" if rpc.server_streaming else "cnetmod::grpc::unary_response"
                if not rpc.server_streaming:
                    lines.extend(
                        [
                            f"    [[nodiscard]] auto {rpc.name}({rpc.request} request, cnetmod::grpc::call_options opts = {{}})",
                            f"        -> cnetmod::task<std::expected<{rpc.response}, cnetmod::grpc::status>>",
                            "    {",
                            "        cnetmod::grpc::unary_request raw_req;",
                            "        raw_req.service = std::string(service_name);",
                            f"        raw_req.method = \"{rpc.name}\";",
                            "        raw_req.headers = std::move(opts.headers);",
                            "        raw_req.timeout = opts.timeout;",
                            "        raw_req.compression = opts.compression;",
                            "        raw_req.payload = request.encode();",
                            "        auto raw = co_await client_.unary(std::move(raw_req));",
                            "        if (!raw) co_return std::unexpected(raw.error());",
                            f"        auto decoded = {rpc.response}::decode(raw->payload);",
                            "        if (!decoded) co_return std::unexpected(decoded.error());",
                            "        co_return *decoded;",
                            "    }",
                            "",
                        ]
                    )
                lines.extend(
                    [
                        f"    [[nodiscard]] auto {rpc.name}(cnetmod::grpc::byte_buffer payload, cnetmod::grpc::call_options opts = {{}})",
                        f"        -> cnetmod::task<std::expected<{resp_type}, cnetmod::grpc::status>>",
                        "    {",
                        "        cnetmod::grpc::unary_request req;",
                        "        req.service = std::string(service_name);",
                        f"        req.method = \"{rpc.name}\";",
                        "        req.headers = std::move(opts.headers);",
                        "        req.timeout = opts.timeout;",
                        "        req.compression = opts.compression;",
                        "        req.payload = std::move(payload);",
                        f"        return client_.{call_method(rpc)}(std::move(req));",
                        "    }",
                        "",
                    ]
                )
        lines.extend(["private:", "    cnetmod::grpc::client& client_;", "};", ""])

        lines.extend([f"struct {service.name}_handlers {{"])
        for rpc in service.rpcs:
            lines.append(f"    {handler_type(rpc)} {rpc.name};")
        lines.extend(["};", ""])
        lines.extend(
            [
                f"inline void register_{service.name}(cnetmod::grpc::service_router& router, {service.name}_handlers handlers) {{",
            ]
        )
        for rpc in service.rpcs:
            add_method = {
                "unary": "add_unary",
                "client_streaming": "add_client_streaming",
                "server_streaming": "add_server_streaming",
                "bidi_streaming": "add_bidi_streaming",
            }[call_method(rpc)]
            lines.append(
                f"    router.{add_method}(\"{full_name}\", \"{rpc.name}\", std::move(handlers.{rpc.name}));"
            )
        lines.extend(["}", ""])

    lines.append("} // namespace")
    lines.append("")
    return "\n".join(lines)


def module_for_proto(proto_name: str, default_prefix: str) -> str:
    stem = pathlib.Path(proto_name).with_suffix("").as_posix().replace("/", ".").replace("-", "_")
    return f"{default_prefix}.{stem}"


def protoc_plugin_main() -> int:
    try:
        from google.protobuf.compiler import plugin_pb2
    except Exception as exc:
        print(f"grpc_cppm_codegen.py protoc plugin requires protobuf Python package: {exc}", file=sys.stderr)
        return 1

    request = plugin_pb2.CodeGeneratorRequest()
    request.ParseFromString(sys.stdin.buffer.read())

    params = {}
    for part in request.parameter.split(",") if request.parameter else []:
        if "=" in part:
            k, v = part.split("=", 1)
            params[k.strip()] = v.strip()
    module_prefix = params.get("module_prefix", "cnetmod.generated.grpc")

    files = {f.name: f for f in request.proto_file}
    response = plugin_pb2.CodeGeneratorResponse()
    for name in request.file_to_generate:
        f = files[name]
        source = pathlib.Path(f.name).name
        proto_text = ""
        # protoc plugins receive descriptors, not source text. Build a small
        # equivalent ProtoFile from the descriptor for generated-code purposes.
        messages = [
            Message(
                name=m.name,
                fields=[
                    Field(
                        label="repeated" if field.label == field.LABEL_REPEATED else "",
                        type=field.type_name.lstrip(".").split(".")[-1] if field.type_name else {
                            field.TYPE_STRING: "string",
                            field.TYPE_BYTES: "bytes",
                            field.TYPE_BOOL: "bool",
                            field.TYPE_UINT32: "uint32",
                            field.TYPE_UINT64: "uint64",
                            field.TYPE_INT32: "int32",
                            field.TYPE_INT64: "int64",
                            field.TYPE_SINT32: "sint32",
                            field.TYPE_SINT64: "sint64",
                        }.get(field.type, "unsupported"),
                        name=field.name,
                        number=field.number,
                    )
                    for field in m.field
                ],
            )
            for m in f.message_type
        ]
        services = [
            Service(
                name=s.name,
                rpcs=[
                    Rpc(
                        name=method.name,
                        request=method.input_type.lstrip(".").split(".")[-1],
                        response=method.output_type.lstrip(".").split(".")[-1],
                        client_streaming=method.client_streaming,
                        server_streaming=method.server_streaming,
                    )
                    for method in s.method
                ],
            )
            for s in f.service
        ]
        proto = ProtoFile(package=f.package, messages=messages, services=services)
        out = response.file.add()
        out.name = f"{pathlib.Path(source).stem}.grpc.cppm"
        out.content = generate(proto, module_for_proto(f.name, module_prefix))

    sys.stdout.buffer.write(response.SerializeToString())
    return 0


def main(argv: list[str]) -> int:
    if not argv:
        return protoc_plugin_main()

    parser = argparse.ArgumentParser()
    parser.add_argument("proto", type=pathlib.Path)
    parser.add_argument("--module", required=True)
    parser.add_argument("-o", "--output", type=pathlib.Path)
    args = parser.parse_args(argv)

    proto = parse_proto(args.proto.read_text(encoding="utf-8"))
    if not proto.services:
        raise SystemExit("no services found in proto")
    generated = generate(proto, args.module)
    if args.output:
        args.output.write_text(generated, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(generated)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
