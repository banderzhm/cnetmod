#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
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

TEST(quic_stream_initial_peer_send_credit_can_be_zero)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    stream.set_initial_send_limit(0);
    stream.init();
    const std::array data{std::byte{'x'}};
    const auto rejected = cnetmod::sync_wait(stream.send(data));
    ASSERT_FALSE(rejected.has_value());
    ASSERT_EQ(stream.bytes_sent(), 0U);
    stream.update_send_limit(1);
    const auto sent = cnetmod::sync_wait(stream.send(data));
    ASSERT_TRUE(sent.has_value());
    ASSERT_EQ(stream.bytes_sent(), 1U);
}

TEST(quic_stream_initial_peer_send_credit_can_exceed_fallback)
{
    cnetmod::quic::quic_stream stream{0, cnetmod::quic::quic_role::server, true};
    constexpr std::size_t size = 300U * 1024U;
    stream.set_initial_send_limit(size);
    stream.init();
    const std::vector<std::byte> data(size, std::byte{'x'});
    const auto sent = cnetmod::sync_wait(stream.send(data));
    ASSERT_TRUE(sent.has_value());
    ASSERT_EQ(stream.bytes_sent(), size);
    const std::array extra{std::byte{'y'}};
    const auto rejected = cnetmod::sync_wait(stream.send(extra));
    ASSERT_FALSE(rejected.has_value());
    ASSERT_EQ(stream.bytes_sent(), size);
}

TEST(quic_retired_stream_history_coalesces_without_reopening_old_ids)
{
    cnetmod::quic::retired_stream_history history;
    for (std::uint64_t index{}; index < 100000U; ++index)
        for (std::uint64_t type{}; type < 4U; ++type)
            history.insert(index * 4U + type, {index, index * 2U});
    ASSERT_EQ(history.range_count(), 4U);
    ASSERT_EQ(history.final_size_count(), history.final_size_capacity);
    ASSERT_TRUE(history.contains(0U));
    ASSERT_TRUE(history.contains(399999U));
    ASSERT_FALSE(history.contains(400000U));
    ASSERT_FALSE(history.find_final_sizes(0U).has_value());
    const auto latest = history.find_final_sizes(399999U);
    ASSERT_TRUE(latest.has_value());
    ASSERT_EQ(latest->received_final_size, 99999U);
    ASSERT_EQ(latest->sent_final_size, 199998U);
}

TEST(quic_retired_stream_history_preserves_gaps_and_merges_out_of_order)
{
    cnetmod::quic::retired_stream_history history;
    history.insert(12U, {3U, 30U});
    history.insert(0U, {0U, 0U});
    history.insert(8U, {2U, 20U});
    ASSERT_EQ(history.range_count(), 2U);
    ASSERT_FALSE(history.contains(4U));
    ASSERT_FALSE(history.contains(1U));
    history.insert(4U, {1U, 10U});
    ASSERT_EQ(history.range_count(), 1U);
    ASSERT_TRUE(history.contains(4U));
    history.insert(4U, {99U, 99U});
    ASSERT_EQ(history.find_final_sizes(4U)->received_final_size, 1U);
    ASSERT_EQ(history.final_size_count(), 4U);
}

RUN_TESTS();
