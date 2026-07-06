#include "test_framework.hpp"
#include <chrono>
#include <filesystem>

import cnetmod.io.io_context;
import cnetmod.protocol.mqtt;

TEST(mqtt_parser_drains_many_publish_frames_from_one_buffer) {
    constexpr std::size_t frame_count = 10000;
    std::string wire;
    wire.reserve(frame_count * 64);

    for (std::size_t i = 0; i < frame_count; ++i) {
        wire += cnetmod::mqtt::encode_publish(
            "bench/topic",
            "payload-" + std::to_string(i),
            cnetmod::mqtt::qos::at_most_once,
            false,
            false,
            0,
            cnetmod::mqtt::protocol_version::v3_1_1);
    }

    cnetmod::mqtt::mqtt_parser parser;
    parser.feed(wire);

    for (std::size_t i = 0; i < frame_count; ++i) {
        auto frame = parser.next();
        ASSERT_TRUE(frame.has_value());
        ASSERT_EQ(static_cast<int>(frame->type),
                  static_cast<int>(cnetmod::mqtt::control_packet_type::publish));

        auto msg = cnetmod::mqtt::decode_publish(
            frame->payload,
            frame->flags,
            cnetmod::mqtt::protocol_version::v3_1_1);
        ASSERT_TRUE(msg.has_value());
        ASSERT_EQ(msg->topic, std::string("bench/topic"));
        ASSERT_EQ(msg->payload, "payload-" + std::to_string(i));
    }

    ASSERT_TRUE(!parser.next().has_value());
    ASSERT_EQ(parser.pending(), 0u);
}

TEST(mqtt_broker_metrics_initial_snapshot) {
    auto ctx = cnetmod::make_io_context();
    cnetmod::mqtt::broker broker(*ctx);

    auto metrics = broker.metrics();
    ASSERT_EQ(metrics.accepted_connections, 0u);
    ASSERT_EQ(metrics.active_connections, 0u);
    ASSERT_EQ(metrics.routed_messages, 0u);
    ASSERT_EQ(metrics.delivered_messages, 0u);
    ASSERT_EQ(metrics.offline_enqueued_messages, 0u);
    ASSERT_EQ(metrics.write_failures, 0u);
    ASSERT_EQ(metrics.protocol_errors, 0u);
}

TEST(mqtt_persistence_roundtrip_sessions_and_retained) {
    auto dir = std::filesystem::temp_directory_path() /
        ("cnetmod-mqtt-persist-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    cnetmod::mqtt::persistence store({
        .data_dir = dir.string(),
        .flush_interval = std::chrono::seconds{1},
    });

    cnetmod::mqtt::session_store sessions;
    auto [session, _] = sessions.create_or_resume(
        "client-a", false, cnetmod::mqtt::protocol_version::v5);
    session.session_expiry_interval = 3600;
    cnetmod::mqtt::subscribe_entry entry;
    entry.topic_filter = "sensors/+/temp";
    entry.max_qos = cnetmod::mqtt::qos::exactly_once;
    entry.subscription_id = 7;
    session.add_subscription(entry);

    cnetmod::mqtt::publish_message offline;
    offline.topic = "sensors/a/temp";
    offline.payload = "21.5";
    offline.qos_value = cnetmod::mqtt::qos::at_least_once;
    session.enqueue_offline(std::move(offline));

    cnetmod::mqtt::retained_store retained;
    retained.store("status/node-a", cnetmod::mqtt::retained_message{
        .topic = "status/node-a",
        .payload = "online",
        .qos_value = cnetmod::mqtt::qos::at_least_once,
    });

    auto sr = store.save_sessions(sessions);
    ASSERT_TRUE(sr.has_value());
    auto rr = store.save_retained(retained);
    ASSERT_TRUE(rr.has_value());

    auto loaded_sessions = store.load_sessions();
    ASSERT_TRUE(loaded_sessions.has_value());
    auto* loaded = loaded_sessions->find("client-a");
    ASSERT_TRUE(loaded != nullptr);
    ASSERT_EQ(loaded->subscriptions.size(), 1u);
    ASSERT_EQ(loaded->offline_queue.size(), 1u);
    ASSERT_EQ(loaded->offline_queue.front().payload, std::string("21.5"));

    auto loaded_retained = store.load_retained();
    ASSERT_TRUE(loaded_retained.has_value());
    auto* retained_msg = loaded_retained->find("status/node-a");
    ASSERT_TRUE(retained_msg != nullptr);
    ASSERT_EQ(retained_msg->payload, std::string("online"));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

RUN_TESTS()
