// libFuzzer entry point for unauthenticated QUIC packet parsing.
// Build with -DCNETMOD_BUILD_FUZZERS=ON and a Clang libFuzzer toolchain.

import cnetmod.protocol.quic;

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int
{
    const auto bytes = std::span{reinterpret_cast<const std::byte*>(data), size};
    (void)cnetmod::quic::decode_packet_type(bytes);
    (void)cnetmod::quic::decode_long_header(bytes);
    (void)cnetmod::quic::decode_short_header(bytes, 8);
    (void)cnetmod::quic::split_coalesced_packets(bytes);
    return 0;
}
