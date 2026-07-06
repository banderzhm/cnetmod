#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: grpc_codegen_test.py <codegen.py> <echo.proto>", file=sys.stderr)
        return 2

    codegen = pathlib.Path(sys.argv[1])
    proto = pathlib.Path(sys.argv[2])
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "echo.grpc.cppm"
        subprocess.run(
            [
                sys.executable,
                str(codegen),
                str(proto),
                "--module",
                "cnetmod.testing.echo.grpc",
                "-o",
                str(out),
            ],
            check=True,
        )
        text = out.read_text(encoding="utf-8")

    required = [
        "export module cnetmod.testing.echo.grpc;",
        "import cnetmod.coro.task;",
        "struct EchoRequest",
        "std::string name;",
        "std::uint64_t sequence{};",
        "bool urgent{};",
        "cnetmod::grpc::proto::append_string(out, 1, name);",
        "static auto decode(std::span<const std::byte> payload)",
        "struct EchoReply",
        "class EchoService_client",
        "static constexpr std::string_view service_name = \"cnetmod.testing.grpc.EchoService\";",
        "auto Say(EchoRequest request, cnetmod::grpc::call_options opts = {})",
        "-> cnetmod::task<std::expected<EchoReply, cnetmod::grpc::status>>",
        "auto Say(cnetmod::grpc::byte_buffer payload, cnetmod::grpc::call_options opts = {})",
        "return client_.unary(std::move(req));",
        "auto Chat(cnetmod::grpc::streaming_request req)",
        "return client_.bidi_streaming(std::move(req));",
        "struct EchoService_handlers",
        "cnetmod::grpc::unary_handler Say;",
        "cnetmod::grpc::streaming_handler Chat;",
        "inline void register_EchoService(cnetmod::grpc::service_router& router, EchoService_handlers handlers)",
        "router.add_unary(\"cnetmod.testing.grpc.EchoService\", \"Say\", std::move(handlers.Say));",
        "router.add_bidi_streaming(\"cnetmod.testing.grpc.EchoService\", \"Chat\", std::move(handlers.Chat));",
    ]
    missing = [token for token in required if token not in text]
    if missing:
        raise AssertionError(f"generated stub missing expected tokens: {missing}")

    print("[  PYTHON  ] grpc cppm codegen contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
