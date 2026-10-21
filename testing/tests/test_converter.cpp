#include "test_framework.hpp"

import std;
import cnetmod.core.net_init;
import cnetmod.utils.converter;

TEST(converter_module_exposes_standard_types_and_binary_views)
{
    const std::string input{"a\0b", 3};
    const auto bytes = utils::conv::to_bytes(input);
    ASSERT_EQ(bytes.size(), input.size());
    ASSERT_TRUE(bytes.data() == reinterpret_cast<const std::byte*>(input.data()));
    ASSERT_EQ(utils::conv::to_string_view(bytes), std::string_view(input));

    std::array<std::uint8_t, 12> packet{};
    utils::conv::write_be16(packet, 0x1234, 1);
    utils::conv::write_be32(packet, 0x56789abc, 3);
    ASSERT_EQ(utils::conv::read_be16(packet, 1), 0x1234);
    ASSERT_EQ(utils::conv::read_be32(packet, 3), 0x56789abcU);
    ASSERT_EQ(utils::conv::ntoh(utils::conv::hton(std::uint64_t{0x123456789abcdef0})),
        std::uint64_t{0x123456789abcdef0});
}

TEST(converter_module_preserves_register_and_crc_interoperability)
{
    const auto registers = utils::conv::RegisterConverter::from_float_hilo(1.5F);
    ASSERT_EQ(registers[0], 0x3fc0);
    ASSERT_EQ(registers[1], 0);
    ASSERT_EQ(utils::conv::RegisterConverter::to_float_hilo(registers[0], registers[1]),
        1.5F);
    const std::array<std::uint8_t, 9> input{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    ASSERT_EQ(utils::conv::CRC16::calculate(input), 0x4b37);
    std::vector<std::uint8_t> frame(input.begin(), input.end());
    utils::conv::CRC16::append(frame);
    ASSERT_EQ(frame[9], 0x37);
    ASSERT_EQ(frame[10], 0x4b);
    ASSERT_TRUE(utils::conv::CRC16::verify_and_remove(frame));
    ASSERT_EQ(frame.size(), input.size());
    ASSERT_EQ(utils::conv::Hex::encode(input), std::string{"313233343536373839"});
}

auto main() -> int
{
    cnetmod::net_init net;
    return cnetmod::test::run_all();
}
