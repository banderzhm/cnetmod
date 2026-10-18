"""Create a test-only connection implementation copy. Never edit the input.

Use the same fixture and transform for old/new implementations. Link ONLY the
dedicated quic_packet_failure_server against the resulting diagnostic object.
No output may be inside the repository. This script does not compile or run.
"""
import argparse
from pathlib import Path


def replace_once(source, old, new):
    assert source.count(old) == 1, (source.count(old), old[:100])
    return source.replace(old, new, 1)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
assert args.source.resolve() != args.out.resolve()
source = args.source.read_text()
source = replace_once(source, "namespace cnetmod::quic {", '''namespace cnetmod::quic {
extern "C" int cnetmod_test_packet_fault() noexcept;
extern "C" void cnetmod_test_packet_committed() noexcept;
extern "C" void cnetmod_test_packet_fault_observed(bool, const char*, int) noexcept;
''')
start = source.index("auto quic_connection::pack_one_rtt_packet(")
end = source.index("auto quic_connection::pack_path_validation_packet(", start)
packet = source[start:end]
anchor = "    const auto pn_offset = packet.size() - pn_length;"
packet = replace_once(packet, anchor, anchor + '''
    const bool test_response = first_scheduled_frame_metadata &&
        first_scheduled_frame_metadata->second.stream == 0U;
    const auto test_fault = test_response ? cnetmod_test_packet_fault() : 0;
    auto test_seal = [&]() -> std::expected<void, std::error_code>
    {
        if (test_fault != 1 && test_fault != 3)
            return append_sealed_payload(*keys, payload, packet, packet_number, path.id);
        auto fault_keys = *keys;
        // Actual AEAD remains configured with a 16-byte tag. Passing zero
        // reserves no tag output, provoking real CIPHER_R_BUFFER_TOO_SMALL.
        fault_keys.tag_len = 0;
        auto result = append_sealed_payload(fault_keys, payload, packet, packet_number, path.id);
        const auto observed = result ? std::error_code{} : result.error();
        cnetmod_test_packet_fault_observed(!result, observed.category().name(), observed.value());
        // This case keeps the real AEAD failure but simulates an empty backend
        // error queue to verify the local nonzero fallback independently.
        if (test_fault == 3 && !result)
            return std::unexpected(std::error_code{});
        return result;
    };
    auto test_protect = [&]() -> std::expected<void, std::error_code>
    {
        if (test_fault != 2)
            return protect_header(*keys, packet, pn_offset, false);
        auto fault_keys = *keys;
        fault_keys.hp_key.clear();
        auto result = protect_header(fault_keys, packet, pn_offset, false);
        const auto observed = result ? std::error_code{} : result.error();
        cnetmod_test_packet_fault_observed(!result, observed.category().name(), observed.value());
        return result;
    };
''')
# Replace only call sites after the injected lambda definitions.
boundary = packet.index("    };\n", packet.index("    auto test_protect")) + len("    };\n")
tail = packet[boundary:]
tail = replace_once(tail, "append_sealed_payload(*keys, payload, packet, packet_number, path.id)", "test_seal()")
tail = replace_once(tail, "protect_header(*keys, packet, pn_offset, false)", "test_protect()")
tail = replace_once(tail, "    return packet;", "    if (test_response)\n        cnetmod_test_packet_committed();\n    return packet;")
packet = packet[:boundary] + tail
source = source[:start] + packet + source[end:]
args.out.parent.mkdir(parents=True, exist_ok=True)
assert not args.out.exists(), "never overwrite an existing diagnostic source"
args.out.write_text(source)
