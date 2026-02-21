/// cnetmod example — MQTT Broker + Client 完整功能演示
/// 演示：
///   1. Broker 启动（带 security/ACL）
///   2. 异步 Client: connect → subscribe → publish (QoS 0/1/2) → 收消息
///   3. Retained 消息
///   4. Will 消息 (遗嘱)
///   5. 同步 Client (sync_client) 简易 pub/sub
///   6. Auto-Reconnect
///   7. Security/ACL — 认证失败 + 授权拒绝
///   8. Unsubscribe — 取消订阅后不再收到消息
///   9. Retained 消息检索 — 新订阅者收到已有 retained
///  10. Session 恢复 + 离线队列 — clean_session=false
///  11. Shared Subscription — $share/group/ 负载均衡
///  12. v5 Properties — User Property / Message Expiry / Response Topic
///  13. v3.1.1 客户端 — 向后兼容
/// 全部在单进程内通过协程完成

#include <cnetmod/config.hpp>
#include <exec/static_thread_pool.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.mqtt;
import cnetmod.coro.bridge;

namespace cn = cnetmod;
namespace mqtt = cnetmod::mqtt;

constexpr std::uint16_t MQTT_PORT = 11883;

// =============================================================================
// 1. Broker 启动协程
// =============================================================================

auto run_broker(cn::io_context& ctx, mqtt::broker& brk,
                std::atomic<bool>& broker_ready) -> cn::task<void>
{
    brk.set_options({
        .port                = MQTT_PORT,
        .host                = "127.0.0.1",
        .max_keep_alive      = 300,
        .topic_alias_maximum = 10,
        .receive_maximum     = 100,
    });

    // 配置安全：添加用户 + ACL
    auto& sec = brk.security();
    sec.add_user("alice", "pass123", {"admin"});
    sec.add_user("bob",   "pass456", {"viewer"});
    sec.allow_all("#", {"admin"});              // admin 可读写所有
    sec.allow_subscribe("sensor/#", {"viewer"}); // viewer 只能订阅 sensor/

    auto r = brk.listen("127.0.0.1", MQTT_PORT);
    if (!r) {
        logger::error("  [Broker] listen failed: {}", r.error().message());
        co_return;
    }

    logger::info("  [Broker] Listening on 127.0.0.1{}", static_cast<unsigned>(MQTT_PORT));
    broker_ready.store(true);
    co_await brk.run();
}

// =============================================================================
// 2. 异步 Client — Subscriber
// =============================================================================

auto run_subscriber(cn::io_context& ctx, std::atomic<bool>& broker_ready,
                    std::atomic<int>& msg_count,
                    std::atomic<bool>& sub_ready) -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    mqtt::client sub(ctx);

    // 注册消息回调
    sub.on_message([&](const mqtt::publish_message& msg) {
        logger::info("  [Subscriber] topic={} payload={} qos={} retain={}",
            msg.topic, msg.payload, mqtt::to_string(msg.qos_value), msg.retain);
        msg_count.fetch_add(1);
    });

    sub.on_disconnect([](std::string reason) {
        logger::info("  [Subscriber] disconnected: {}", reason);
    });

    // 连接
    mqtt::connect_options opts;
    opts.host           = "127.0.0.1";
    opts.port           = MQTT_PORT;
    opts.client_id      = "subscriber-1";
    opts.clean_session  = true;
    opts.keep_alive_sec = 30;
    opts.version        = mqtt::protocol_version::v5;
    opts.username       = "alice";
    opts.password       = "pass123";

    auto cr = co_await sub.connect(opts);
    if (!cr) {
        logger::error("  [Subscriber] connect failed: {}", cr.error());
        co_return;
    }
    logger::info("  [Subscriber] Connected (v5)");

    // 订阅多个 topic
    std::vector<mqtt::subscribe_entry> entries = {
        {"sensor/temperature", mqtt::qos::exactly_once},
        {"sensor/humidity",    mqtt::qos::at_least_once},
        {"device/+/status",   mqtt::qos::at_most_once},
    };
    auto sr = co_await sub.subscribe(entries);
    if (!sr) {
        logger::error("  [Subscriber] subscribe failed: {}", sr.error());
        co_return;
    }
    logger::info("  [Subscriber] Subscribed to {} topics", entries.size());
    sub_ready.store(true);

    // 等待消息（publisher 会发 6 条）
    while (msg_count.load() < 6)
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});

    auto dr = co_await sub.disconnect();
    logger::info("  [Subscriber] Disconnected");
}

// =============================================================================
// 3. 异步 Client — Publisher (QoS 0/1/2 + retained)
// =============================================================================

auto run_publisher(cn::io_context& ctx, std::atomic<bool>& sub_ready)
    -> cn::task<void>
{
    while (!sub_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    mqtt::client pub(ctx);

    mqtt::connect_options opts;
    opts.host           = "127.0.0.1";
    opts.port           = MQTT_PORT;
    opts.client_id      = "publisher-1";
    opts.clean_session  = true;
    opts.keep_alive_sec = 30;
    opts.version        = mqtt::protocol_version::v5;
    opts.username       = "alice";
    opts.password       = "pass123";

    auto cr = co_await pub.connect(opts);
    if (!cr) {
        logger::error("  [Publisher] connect failed: {}", cr.error());
        co_return;
    }
    logger::info("  [Publisher] Connected");

    // QoS 0
    co_await pub.publish("sensor/temperature", "22.5°C", mqtt::qos::at_most_once);
    logger::info("  [Publisher] sent sensor/temperature (QoS 0)");

    // QoS 1
    auto r1 = co_await pub.publish("sensor/humidity", "65%", mqtt::qos::at_least_once);
    if (r1)
        logger::info("  [Publisher] sent sensor/humidity (QoS 1) — ACK received");

    // QoS 2
    auto r2 = co_await pub.publish("sensor/temperature", "23.1°C",
                                   mqtt::qos::exactly_once);
    if (r2)
        logger::info("  [Publisher] sent sensor/temperature (QoS 2) — complete");

    // Retained 消息
    co_await pub.publish("device/thermostat/status", "online",
                         mqtt::qos::at_least_once, true);
    logger::info("  [Publisher] sent device/thermostat/status (retained)");

    // 多条消息
    co_await pub.publish("sensor/temperature", "23.5°C", mqtt::qos::at_most_once);
    co_await pub.publish("sensor/humidity",    "62%",    mqtt::qos::at_most_once);

    co_await cn::async_sleep(ctx, std::chrono::milliseconds{200});
    co_await pub.disconnect();
    logger::info("  [Publisher] Disconnected");
}

// =============================================================================
// 4. Will 消息演示
// =============================================================================

auto run_will_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready,
                   std::atomic<int>& msg_count) -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    // Subscriber 订阅 will topic
    mqtt::client will_sub(ctx);
    will_sub.on_message([&](const mqtt::publish_message& msg) {
        logger::info("  [WillSub] got will: topic={} payload={}",
            msg.topic, msg.payload);
        msg_count.fetch_add(1);
    });

    mqtt::connect_options sub_opts;
    sub_opts.host = "127.0.0.1"; sub_opts.port = MQTT_PORT;
    sub_opts.client_id = "will-subscriber";
    sub_opts.username = "alice"; sub_opts.password = "pass123";
    sub_opts.version = mqtt::protocol_version::v5;
    co_await will_sub.connect(sub_opts);
    co_await will_sub.subscribe("device/sensor-1/will", mqtt::qos::at_least_once);

    // 带 Will 的客户端
    mqtt::client will_client(ctx);
    mqtt::connect_options wc_opts;
    wc_opts.host = "127.0.0.1"; wc_opts.port = MQTT_PORT;
    wc_opts.client_id = "sensor-1";
    wc_opts.username = "alice"; wc_opts.password = "pass123";
    wc_opts.version = mqtt::protocol_version::v5;
    wc_opts.will_msg = mqtt::will{
        .topic   = "device/sensor-1/will",
        .message = "sensor-1 offline unexpectedly",
        .qos_value = mqtt::qos::at_least_once,
    };

    co_await will_client.connect(wc_opts);
    logger::info("  [WillClient] Connected with will message");

    // 模拟异常断连（直接 close 不发 DISCONNECT → broker 发布 will）
    will_client.close();
    logger::info("  [WillClient] Abrupt close (will should be published)");

    // 等待 will 到达
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{500});
    co_await will_sub.disconnect();
}

// =============================================================================
// 5. 同步 Client 演示
// =============================================================================

void run_sync_demo(std::atomic<bool>& broker_ready) {
    // 避免占满 CPU：自旋等待时稍作休眠
    while (!broker_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    mqtt::sync_client sc;

    mqtt::connect_options opts;
    opts.host = "127.0.0.1"; opts.port = MQTT_PORT;
    opts.client_id = "sync-client-1";
    opts.username = "alice"; opts.password = "pass123";

    auto cr = sc.connect_sync(opts);
    if (!cr) {
        logger::error("  [Sync] connect failed: {}", cr.error());
        return;
    }
    logger::info("  [Sync] Connected");

    sc.on_message([](const mqtt::publish_message& msg) {
        logger::info("  [Sync] recv: topic={} payload={}", msg.topic, msg.payload);
    });

    sc.subscribe_sync("test/sync", mqtt::qos::at_least_once);
    sc.publish_sync("test/sync", "hello from sync_client", mqtt::qos::at_least_once);

    // poll 一下让回调触发
    for (int i = 0; i < 50; ++i) {
        sc.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    sc.disconnect_sync();
    logger::info("  [Sync] Disconnected");
}

// =============================================================================
// 6. Auto-Reconnect 演示
// =============================================================================

auto run_reconnect_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready)
    -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    mqtt::client rc(ctx);

    // 配置自动重连
    rc.set_reconnect({
        .enabled              = true,
        .max_retries          = 3,
        .initial_delay        = std::chrono::seconds(1),
        .max_delay            = std::chrono::seconds(5),
        .backoff_multiplier   = 2.0,
        .restore_subscriptions = true,
    });

    rc.on_message([](const mqtt::publish_message& msg) {
        logger::info("  [Reconnect] recv: {}", msg.payload);
    });
    rc.on_disconnect([](std::string reason) {
        logger::info("  [Reconnect] disconnected: {} (auto-reconnect will retry)",
            reason);
    });

    mqtt::connect_options opts;
    opts.host = "127.0.0.1"; opts.port = MQTT_PORT;
    opts.client_id = "reconnect-client";
    opts.clean_session = false;
    opts.username = "alice"; opts.password = "pass123";
    opts.version = mqtt::protocol_version::v5;

    auto cr = co_await rc.connect(opts);
    if (!cr) {
        logger::error("  [Reconnect] connect failed: {}", cr.error());
        co_return;
    }
    logger::info("  [Reconnect] Connected (auto-reconnect enabled)");

    co_await rc.subscribe("reconnect/test", mqtt::qos::at_least_once);
    logger::info("  [Reconnect] Subscribed to reconnect/test");

    co_await rc.disconnect();
    logger::info("  [Reconnect] Demo done");
}

// =============================================================================
// 7. Security/ACL 演示 — 认证失败 + 授权拒绝
// =============================================================================

auto run_security_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready)
    -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    // --- 7a: 错误密码连接 → 应被拒绝 ---
    {
        mqtt::client c(ctx);
        mqtt::connect_options opts;
        opts.host = "127.0.0.1"
                    ; opts.port = MQTT_PORT;
        opts.client_id = "bad-auth";
        opts.username = "alice"; opts.password = "wrong_password";
        opts.version = mqtt::protocol_version::v5;

        auto cr = co_await c.connect(opts);
        if (!cr)
            logger::info("  [Security] Auth rejected (wrong password): {}", cr.error());
        else
            logger::error("  [Security] ERROR: should have been rejected!");
    }

    // --- 7b: bob 尝试发布到 device/# → ACL 拒绝 ---
    {
        mqtt::client bob(ctx);
        mqtt::connect_options opts;
        opts.host = "127.0.0.1"
                    ; opts.port = MQTT_PORT;
        opts.client_id = "bob-acl-test";
        opts.username = "bob"; opts.password = "pass456";
        opts.version = mqtt::protocol_version::v5;

        auto cr = co_await bob.connect(opts);
        if (!cr) {
            logger::error("  [Security] bob connect failed: {}", cr.error());
            co_return;
        }
        logger::info("  [Security] bob connected (viewer group)");

        // bob 订阅 sensor/# → 应成功 (viewer 有 sensor/# 订阅权限)
        auto sr = co_await bob.subscribe("sensor/temperature", mqtt::qos::at_most_once);
        if (sr)
            logger::info("  [Security] bob subscribed sensor/temperature — OK");

        // bob 发布到 device/cmd → ACL 应拒绝 (viewer 无发布权限)
        // 注意: broker 侧拒绝发布不会给 QoS 0 返回错误
        auto pr = co_await bob.publish("device/cmd", "reboot", mqtt::qos::at_least_once);
        if (pr)
            logger::info("  [Security] bob publish device/cmd — broker accepted (ACL checked server-side)");
        else
            logger::info("  [Security] bob publish device/cmd — rejected: {}", pr.error());

        co_await bob.disconnect();
        logger::info("  [Security] bob disconnected");
    }
}

// =============================================================================
// 8. Unsubscribe 演示 — 取消订阅后不再收消息
// =============================================================================

auto run_unsubscribe_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready)
    -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    std::atomic<int> recv_count{0};

    mqtt::client c(ctx);
    c.on_message([&](const mqtt::publish_message& msg) {
        logger::info("  [Unsub] recv: topic={} payload={}", msg.topic, msg.payload);
        recv_count.fetch_add(1);
    });

    mqtt::connect_options opts;
    opts.host = "127.0.0.1"
                ; opts.port = MQTT_PORT;
    opts.client_id = "unsub-client";
    opts.username = "alice"; opts.password = "pass123";
    opts.version = mqtt::protocol_version::v5;
    co_await c.connect(opts);

    // 订阅
    co_await c.subscribe("unsub/test", mqtt::qos::at_least_once);
    logger::info("  [Unsub] Subscribed to unsub/test");

    // 用另一个 client 发消息
    mqtt::client pub(ctx);
    mqtt::connect_options pub_opts;
    pub_opts.host = "127.0.0.1"
                    ; pub_opts.port = MQTT_PORT;
    pub_opts.client_id = "unsub-publisher";
    pub_opts.username = "alice"; pub_opts.password = "pass123";
    pub_opts.version = mqtt::protocol_version::v5;
    co_await pub.connect(pub_opts);

    // 发第 1 条 → 应收到
    co_await pub.publish("unsub/test", "msg-before-unsub", mqtt::qos::at_most_once);
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{100});
    logger::info("  [Unsub] recv_count after 1st publish: {}", recv_count.load());

    // 取消订阅
    auto ur = co_await c.unsubscribe({"unsub/test"});
    if (ur)
        logger::info("  [Unsub] Unsubscribed from unsub/test");

    // 发第 2 条 → 不应收到
    co_await pub.publish("unsub/test", "msg-after-unsub", mqtt::qos::at_most_once);
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{100});
    logger::info("  [Unsub] recv_count after 2nd publish: {} (should still be 1)",
        recv_count.load());

    co_await pub.disconnect();
    co_await c.disconnect();
}

// =============================================================================
// 9. Retained 消息检索 — 新订阅者收到已有 retained
// =============================================================================

auto run_retained_retrieval_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready)
    -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    // 先发布 retained 消息
    mqtt::client pub(ctx);
    mqtt::connect_options pub_opts;
    pub_opts.host = "127.0.0.1"
                    ; pub_opts.port = MQTT_PORT;
    pub_opts.client_id = "retained-pub";
    pub_opts.username = "alice"; pub_opts.password = "pass123";
    pub_opts.version = mqtt::protocol_version::v5;
    co_await pub.connect(pub_opts);

    co_await pub.publish("status/server", "running", mqtt::qos::at_least_once, true);
    logger::info("  [Retained] Published retained: status/server=running");
    co_await pub.disconnect();

    // 新订阅者连接 → 订阅后应收到 retained 消息
    std::atomic<int> retained_count{0};
    mqtt::client sub(ctx);
    sub.on_message([&](const mqtt::publish_message& msg) {
        logger::info("  [Retained] new sub recv: topic={} payload={} retain={}",
            msg.topic, msg.payload, msg.retain);
        retained_count.fetch_add(1);
    });

    mqtt::connect_options sub_opts;
    sub_opts.host = "127.0.0.1"
                    ; sub_opts.port = MQTT_PORT;
    sub_opts.client_id = "retained-sub";
    sub_opts.username = "alice"; sub_opts.password = "pass123";
    sub_opts.version = mqtt::protocol_version::v5;
    co_await sub.connect(sub_opts);

    co_await sub.subscribe("status/#", mqtt::qos::at_least_once);
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{200});
    logger::info("  [Retained] retained_count={} (should be >=1)", retained_count.load());

    // 删除 retained 消息（空 payload）
    mqtt::client del(ctx);
    mqtt::connect_options del_opts;
    del_opts.host = "127.0.0.1"
                    ; del_opts.port = MQTT_PORT;
    del_opts.client_id = "retained-del";
    del_opts.username = "alice"; del_opts.password = "pass123";
    del_opts.version = mqtt::protocol_version::v5;
    co_await del.connect(del_opts);
    co_await del.publish("status/server", "", mqtt::qos::at_most_once, true);
    logger::info("  [Retained] Deleted retained on status/server");
    co_await del.disconnect();

    co_await sub.disconnect();
}

// =============================================================================
// 10. Session 恢复 + 离线队列 (clean_session=false)
// =============================================================================

auto run_session_resume_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready)
    -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    std::atomic<int> recv_count{0};

    // 第一次连接: clean_session=false, 订阅, 然后断开
    {
        mqtt::client c(ctx);
        mqtt::connect_options opts;
        opts.host = "127.0.0.1"
                    ; opts.port = MQTT_PORT;
        opts.client_id = "session-resume";
        opts.clean_session = false;
        opts.username = "alice"; opts.password = "pass123";
        opts.version = mqtt::protocol_version::v5;

        auto cr = co_await c.connect(opts);
        if (!cr) { logger::error("  [Session] connect failed: {}", cr.error()); co_return; }
        logger::info("  [Session] 1st connect: session_present={}", c.session_present());

        co_await c.subscribe("session/test", mqtt::qos::at_least_once);
        logger::info("  [Session] Subscribed to session/test");

        co_await c.disconnect();
        logger::info("  [Session] 1st disconnect (session persisted on broker)");
    }

    // 在 client 离线期间发布消息
    {
        mqtt::client pub(ctx);
        mqtt::connect_options opts;
        opts.host = "127.0.0.1"
                    ; opts.port = MQTT_PORT;
        opts.client_id = "session-pub";
        opts.username = "alice"; opts.password = "pass123";
        opts.version = mqtt::protocol_version::v5;
        co_await pub.connect(opts);

        co_await pub.publish("session/test", "offline-msg-1", mqtt::qos::at_least_once);
        co_await pub.publish("session/test", "offline-msg-2", mqtt::qos::at_least_once);
        logger::info("  [Session] Published 2 msgs while client offline");

        co_await pub.disconnect();
    }

    // 第二次连接: clean_session=false → 恢复会话 → 收到离线消息
    {
        mqtt::client c(ctx);
        c.on_message([&](const mqtt::publish_message& msg) {
            logger::info("  [Session] recv offline: topic={} payload={}",
                msg.topic, msg.payload);
            recv_count.fetch_add(1);
        });

        mqtt::connect_options opts;
        opts.host = "127.0.0.1"
                    ; opts.port = MQTT_PORT;
        opts.client_id = "session-resume";
        opts.clean_session = false;
        opts.username = "alice"; opts.password = "pass123";
        opts.version = mqtt::protocol_version::v5;

        auto cr = co_await c.connect(opts);
        if (!cr) { logger::error("  [Session] reconnect failed: {}", cr.error()); co_return; }
        logger::info("  [Session] 2nd connect: session_present={}", c.session_present());

        co_await cn::async_sleep(ctx, std::chrono::milliseconds{300});
        logger::info("  [Session] recv_count={} (should be 2)", recv_count.load());

        co_await c.disconnect();
    }
}

// =============================================================================
// 11. Shared Subscription — $share/group/ 负载均衡
// =============================================================================

auto run_shared_sub_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready)
    -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    std::atomic<int> sub1_count{0};
    std::atomic<int> sub2_count{0};

    // 两个订阅者使用 shared subscription
    mqtt::client s1(ctx), s2(ctx);

    s1.on_message([&](const mqtt::publish_message& msg) {
        logger::info("  [Shared-S1] recv: {}", msg.payload);
        sub1_count.fetch_add(1);
    });
    s2.on_message([&](const mqtt::publish_message& msg) {
        logger::info("  [Shared-S2] recv: {}", msg.payload);
        sub2_count.fetch_add(1);
    });

    auto make_opts = [](const char* cid) {
        mqtt::connect_options o;
        o.host = "127.0.0.1"
                 ; o.port = MQTT_PORT;
        o.client_id = cid;
        o.username = "alice"; o.password = "pass123";
        o.version = mqtt::protocol_version::v5;
        return o;
    };

    co_await s1.connect(make_opts("shared-s1"));
    co_await s2.connect(make_opts("shared-s2"));

    // 共享订阅: $share/workers/job/+
    co_await s1.subscribe("$share/workers/job/+", mqtt::qos::at_least_once);
    co_await s2.subscribe("$share/workers/job/+", mqtt::qos::at_least_once);
    logger::info("  [Shared] Both subscribers joined $share/workers/job/+");

    // 发布多条消息
    mqtt::client pub(ctx);
    co_await pub.connect(make_opts("shared-pub"));

    for (int i = 0; i < 4; ++i) {
        auto topic = "job/task" + std::to_string(i);
        auto payload = "work-" + std::to_string(i);
        co_await pub.publish(topic, payload, mqtt::qos::at_least_once);
    }
    logger::info("  [Shared] Published 4 messages to job/task*");

    co_await cn::async_sleep(ctx, std::chrono::milliseconds{300});
    logger::info("  [Shared] S1 recv={}, S2 recv={} (round-robin)",
        sub1_count.load(), sub2_count.load());

    co_await pub.disconnect();
    co_await s1.disconnect();
    co_await s2.disconnect();
}

// =============================================================================
// 12. v5 Properties 演示
// =============================================================================

auto run_v5_props_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready)
    -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    std::atomic<int> recv_count{0};

    mqtt::client sub(ctx);
    sub.on_message([&](const mqtt::publish_message& msg) {
        logger::info("  [v5Props] recv: topic={} payload={}", msg.topic, msg.payload);
        // 打印 user properties
        for (auto& p : msg.props) {
            if (p.id == mqtt::property_id::user_property) {
                if (auto* kv = std::get_if<std::pair<std::string, std::string>>(&p.value))
                    logger::info("  [v5Props]   user_property: {}={}", kv->first, kv->second);
            }
            if (p.id == mqtt::property_id::content_type) {
                if (auto* s = std::get_if<std::string>(&p.value))
                    logger::info("  [v5Props]   content_type: {}", *s);
            }
            if (p.id == mqtt::property_id::response_topic) {
                if (auto* s = std::get_if<std::string>(&p.value))
                    logger::info("  [v5Props]   response_topic: {}", *s);
            }
            if (p.id == mqtt::property_id::correlation_data) {
                if (auto* s = std::get_if<std::string>(&p.value))
                    logger::info("  [v5Props]   correlation_data: {}", *s);
            }
        }
        recv_count.fetch_add(1);
    });

    auto make_opts = [](const char* cid) {
        mqtt::connect_options o;
        o.host = "127.0.0.1"
                 ; o.port = MQTT_PORT;
        o.client_id = cid;
        o.username = "alice"; o.password = "pass123";
        o.version = mqtt::protocol_version::v5;
        return o;
    };

    co_await sub.connect(make_opts("v5props-sub"));
    co_await sub.subscribe("v5test/#", mqtt::qos::at_least_once);

    mqtt::client pub(ctx);
    co_await pub.connect(make_opts("v5props-pub"));

    // 带 User Property + Content-Type + Response Topic + Correlation Data
    mqtt::properties pub_props;
    pub_props.push_back(mqtt::mqtt_property::string_pair_prop(
        mqtt::property_id::user_property, "trace-id", "abc-123"));
    pub_props.push_back(mqtt::mqtt_property::string_prop(
        mqtt::property_id::content_type, "application/json"));
    pub_props.push_back(mqtt::mqtt_property::string_prop(
        mqtt::property_id::response_topic, "v5test/reply"));
    pub_props.push_back(mqtt::mqtt_property::string_prop(
        mqtt::property_id::correlation_data, "req-001"));

    co_await pub.publish("v5test/request", R"({"cmd":"status"})",
        mqtt::qos::at_least_once, false, pub_props);
    logger::info("  [v5Props] Published with user_property + content_type + response_topic");

    // 带 Message Expiry Interval
    mqtt::properties expiry_props;
    expiry_props.push_back(mqtt::mqtt_property::u32_prop(
        mqtt::property_id::message_expiry_interval, 60));
    co_await pub.publish("v5test/expiry", "expires in 60s",
        mqtt::qos::at_most_once, false, expiry_props);
    logger::info("  [v5Props] Published with message_expiry_interval=60");

    co_await cn::async_sleep(ctx, std::chrono::milliseconds{200});
    logger::info("  [v5Props] recv_count={}", recv_count.load());

    co_await pub.disconnect();
    co_await sub.disconnect();
}

// =============================================================================
// 13. v3.1.1 客户端 — 向后兼容
// =============================================================================

auto run_v311_demo(cn::io_context& ctx, std::atomic<bool>& broker_ready)
    -> cn::task<void>
{
    while (!broker_ready.load())
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{5});

    std::atomic<int> recv_count{0};

    mqtt::client sub(ctx);
    sub.on_message([&](const mqtt::publish_message& msg) {
        logger::info("  [v3.1.1] recv: topic={} payload={} qos={}",
            msg.topic, msg.payload, mqtt::to_string(msg.qos_value));
        recv_count.fetch_add(1);
    });

    // v3.1.1 连接
    mqtt::connect_options sub_opts;
    sub_opts.host = "127.0.0.1"; sub_opts.port = MQTT_PORT;
    sub_opts.client_id = "v311-sub";
    sub_opts.username = "alice"; sub_opts.password = "pass123";
    sub_opts.version = mqtt::protocol_version::v3_1_1;

    auto cr = co_await sub.connect(sub_opts);
    if (!cr) { logger::error("  [v3.1.1] sub connect failed: {}", cr.error()); co_return; }
    logger::info("  [v3.1.1] Subscriber connected (v3.1.1)");

    co_await sub.subscribe("compat/#", mqtt::qos::at_least_once);

    // v3.1.1 publisher
    mqtt::client pub(ctx);
    mqtt::connect_options pub_opts;
    pub_opts.host = "127.0.0.1"; pub_opts.port = MQTT_PORT;
    pub_opts.client_id = "v311-pub";
    pub_opts.username = "alice"; pub_opts.password = "pass123";
    pub_opts.version = mqtt::protocol_version::v3_1_1;

    co_await pub.connect(pub_opts);
    logger::info("  [v3.1.1] Publisher connected (v3.1.1)");

    // QoS 0
    co_await pub.publish("compat/q0", "hello-v311-q0", mqtt::qos::at_most_once);
    // QoS 1
    co_await pub.publish("compat/q1", "hello-v311-q1", mqtt::qos::at_least_once);
    // QoS 2
    co_await pub.publish("compat/q2", "hello-v311-q2", mqtt::qos::exactly_once);

    co_await cn::async_sleep(ctx, std::chrono::milliseconds{200});
    logger::info("  [v3.1.1] recv_count={} (should be 3)", recv_count.load());

    co_await pub.disconnect();
    co_await sub.disconnect();
}

// =============================================================================
// 主协程 — 编排全部演示
// =============================================================================

auto run_mqtt_demo(cn::io_context& ctx) -> cn::task<void> {
    mqtt::broker brk(ctx);
    std::atomic<bool> broker_ready{false};
    std::atomic<bool> sub_ready{false};
    std::atomic<int>  msg_count{0};
    std::atomic<int>  will_count{0};

    // 启动 Broker
    cn::spawn(ctx, run_broker(ctx, brk, broker_ready));

    // === Demo 1: Pub/Sub with QoS ===
    logger::info("--- Demo 1: Async Pub/Sub (QoS 0/1/2 + Retained) ---");
    cn::spawn(ctx, run_subscriber(ctx, broker_ready, msg_count, sub_ready));
    cn::spawn(ctx, run_publisher(ctx, sub_ready));

    while (msg_count.load() < 6)
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{200});

    // === Demo 2: Will Message ===
    logger::info("--- Demo 2: Will Message ---");
    co_await run_will_demo(ctx, broker_ready, will_count);

    // === Demo 3: Sync Client ===
    logger::info("--- Demo 3: Sync Client ---");
    {
        exec::static_thread_pool pool(1);
        co_await cn::blocking_invoke(pool, ctx, [&] {
            run_sync_demo(broker_ready);
        });
    }

    // === Demo 4: Auto-Reconnect ===
    logger::info("--- Demo 4: Auto-Reconnect ---");
    co_await run_reconnect_demo(ctx, broker_ready);

    // === Demo 5: Security/ACL ===
    logger::info("--- Demo 5: Security/ACL ---");
    co_await run_security_demo(ctx, broker_ready);

    // === Demo 6: Unsubscribe ===
    logger::info("--- Demo 6: Unsubscribe ---");
    co_await run_unsubscribe_demo(ctx, broker_ready);

    // === Demo 7: Retained Retrieval ===
    logger::info("--- Demo 7: Retained Message Retrieval ---");
    co_await run_retained_retrieval_demo(ctx, broker_ready);

    // === Demo 8: Session Resume + Offline Queue ===
    logger::info("--- Demo 8: Session Resume + Offline Queue ---");
    co_await run_session_resume_demo(ctx, broker_ready);

    // === Demo 9: Shared Subscription ===
    logger::info("--- Demo 9: Shared Subscription ---");
    co_await run_shared_sub_demo(ctx, broker_ready);

    // === Demo 10: v5 Properties ===
    logger::info("--- Demo 10: v5 Properties ---");
    co_await run_v5_props_demo(ctx, broker_ready);

    // === Demo 11: v3.1.1 Backward Compat ===
    logger::info("--- Demo 11: v3.1.1 Backward Compatibility ---");
    co_await run_v311_demo(ctx, broker_ready);

    co_await cn::async_sleep(ctx, std::chrono::milliseconds{200});

    brk.stop();
    logger::info("[Broker] Stopped");
    logger::info("  MQTT demo complete.");
    ctx.stop();
}

// =============================================================================
// main
// =============================================================================

auto main() -> int {
    logger::info("=== cnetmod: MQTT Broker + Client Demo ===");

    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_mqtt_demo(*ctx));
    ctx->run();

    logger::info("Done.");
    return 0;
}