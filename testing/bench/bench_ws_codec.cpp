import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.protocol.websocket;

namespace ws = cnetmod::ws;

namespace {
template <typename F>
void measure(std::string_view name, std::size_t size, std::size_t iterations, F operation)
{
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t checksum = 0;
    for (std::size_t i = 0; i < iterations; ++i)
        checksum += operation(i);
    const auto ns = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - start)
                        .count();
    logger::info{"{} bytes={} iterations={} ns/op={:.3f} checksum={}",
        name, size, iterations, ns / iterations, checksum};
}
} // namespace

auto main(int argc, char** argv) -> int
{
    cnetmod::net_init net;
    std::size_t iterations = 200'000;
    if (argc == 2)
    {
        const auto text = std::string_view{argv[1]};
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), iterations);
        if (ec != std::errc{} || end != text.data() + text.size() || iterations == 0)
            return 2;
    }
    for (const std::size_t size : {16, 125, 126, 4096, 65535, 65536})
    {
        std::vector<std::byte> payload(size, std::byte{0x61});
        auto wire = ws::build_frame(ws::opcode::binary, payload, false);
        measure("parse", size, iterations, [&](std::size_t)
            {
                auto header = ws::parse_frame_header(wire);
                return header ? header->first.payload_length : 0;
            });
        measure("mask", size, iterations, [&](std::size_t i)
            {
                ws::apply_mask(payload, static_cast<std::uint32_t>(0x12345678 + i));
                return std::to_integer<unsigned>(payload[i % size]);
            });
        measure("encode", size, iterations, [&](std::size_t i)
            {
                ws::build_frame_into(wire, ws::opcode::binary, payload, false);
                return std::to_integer<unsigned>(wire[wire.size() - 1 - i % size]);
            });
    }
    logger::shutdown();
}
