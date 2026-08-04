#include "test_framework.hpp"

#include <array>

import cnetmod.protocol.quic;

TEST(quic_stream_out_of_order_gap_is_not_readable)
{
    cnetmod::quic::quic_stream stream{
        0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(1024);
    stream.init();

    const std::array later{
        std::byte{'w'}, std::byte{'o'}, std::byte{'r'}, std::byte{'l'},
        std::byte{'d'}};
    const auto buffered = stream.push_received(5, later, true);
    ASSERT_TRUE(buffered.has_value());
    ASSERT_FALSE(stream.is_readable());

    const std::array first{
        std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'},
        std::byte{'o'}};
    const auto completed_gap = stream.push_received(0, first, false);
    ASSERT_TRUE(completed_gap.has_value());
    ASSERT_TRUE(stream.is_readable());
}

TEST(quic_stream_duplicate_fin_is_idempotent)
{
    cnetmod::quic::quic_stream stream{
        0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_receive_limit(1024);
    stream.init();

    const auto first_fin = stream.push_received(0, {}, true);
    ASSERT_TRUE(first_fin.has_value());
    ASSERT_TRUE(stream.is_readable());

    const auto duplicate_fin = stream.push_received(0, {}, true);
    ASSERT_TRUE(duplicate_fin.has_value());

    const std::array invalid{std::byte{'x'}};
    const auto beyond_final_size = stream.push_received(0, invalid, true);
    ASSERT_FALSE(beyond_final_size.has_value());
    ASSERT_EQ(beyond_final_size.error(),
        cnetmod::quic::make_error_code(
            cnetmod::quic::quic_errc::final_size_error));
}

RUN_TESTS();
