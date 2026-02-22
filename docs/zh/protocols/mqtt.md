# MQTT

完整的 MQTT v3.1.1 和 v5.0 代理和客户端实现，用于物联网消息传递。

## MQTT 代理

### 基础代理

```cpp
import cnetmod;
import cnetmod.protocol.mqtt;
using namespace cnetmod;

int main() {
    net_init guard;
    io_context ctx;
    
    mqtt::broker broker(ctx);
    
    broker.set_options({
        .port = 1883,
        .max_connections = 10000,
        .enable_persistence = true,
        .persistence_path = "./mqtt_data"
    });
    
    broker.listen();
    std::println("MQTT broker listening on port 1883");
    
    spawn(ctx, broker.run());
    ctx.run();
}
```

### 代理特性

- **QoS 0/1/2**：所有服务质量级别
- **保留消息**：最后消息持久化
- **遗嘱消息**：遗嘱和遗言
- **会话持久化**：断开后恢复
- **共享订阅**：负载均衡（v5.0）
- **主题别名**：减少带宽（v5.0）
- **认证**：用户名/密码，自定义认证

### 代理配置

```cpp
mqtt::broker_options opts{
    .port = 1883,
    .max_connections = 10000,
    .max_packet_size = 256 * 1024,
    .enable_persistence = true,
    .persistence_path = "./mqtt_data",
    .session_expiry_interval = 3600,  // seconds
    .max_qos = mqtt::qos::exactly_once,
    .retain_available = true,
    .wildcard_subscription_available = true,
    .shared_subscription_available = true
};

broker.set_options(opts);
```

## MQTT 客户端

### 基础客户端

```cpp
task<void> mqtt_client_example(io_context& ctx) {
    mqtt::client client(ctx);
    
    // Connect
    mqtt::connect_options opts{
        .host = "127.0.0.1",
        .port = 1883,
        .client_id = "my_client",
        .username = "user",
        .password = "pass",
        .clean_session = true,
        .keep_alive = 60,
        .version = mqtt::protocol_version::v5
    };
    
    co_await client.connect(opts);
    std::println("Connected to MQTT broker");
    
    // Subscribe
    co_await client.subscribe("sensor/#", mqtt::qos::at_least_once);
    
    // Set message handler
    client.on_message([](const mqtt::publish_message& msg) {
        std::println("Topic: {}, Payload: {}", msg.topic, msg.payload);
    });
    
    // Publish
    co_await client.publish("sensor/temp", "22.5", mqtt::qos::exactly_once);
    
    // Wait for messages
    co_await async_sleep(ctx, 60s);
    
    // Disconnect
    co_await client.disconnect();
}
```

### QoS 级别

```cpp
// QoS 0: At most once (fire and forget)
co_await client.publish("topic", "data", mqtt::qos::at_most_once);

// QoS 1: At least once (acknowledged)
co_await client.publish("topic", "data", mqtt::qos::at_least_once);

// QoS 2: Exactly once (assured delivery)
co_await client.publish("topic", "data", mqtt::qos::exactly_once);
```

### 保留消息

```cpp
// Publish retained message
mqtt::publish_options opts{
    .qos = mqtt::qos::at_least_once,
    .retain = true
};

co_await client.publish("status/online", "true", opts);

// New subscribers immediately receive last retained message
```

### 遗嘱消息

```cpp
mqtt::connect_options opts{
    .host = "127.0.0.1",
    .port = 1883,
    .will_topic = "status/offline",
    .will_payload = "client_disconnected",
    .will_qos = mqtt::qos::at_least_once,
    .will_retain = true
};

co_await client.connect(opts);
// If client disconnects unexpectedly, broker publishes will message
```

### 主题通配符

```cpp
// Single-level wildcard (+)
co_await client.subscribe("sensor/+/temperature", mqtt::qos::at_least_once);
// Matches: sensor/room1/temperature, sensor/room2/temperature

// Multi-level wildcard (#)
co_await client.subscribe("sensor/#", mqtt::qos::at_least_once);
// Matches: sensor/temp, sensor/room1/temp, sensor/room1/humidity
```

### 共享订阅（v5.0）

```cpp
// Multiple clients share subscription load
co_await client1.subscribe("$share/group1/sensor/#", mqtt::qos::at_least_once);
co_await client2.subscribe("$share/group1/sensor/#", mqtt::qos::at_least_once);

// Messages distributed round-robin between client1 and client2
```

### 主题别名（v5.0）

```cpp
// First publish with alias
mqtt::publish_options opts{
    .topic_alias = 1
};
co_await client.publish("very/long/topic/name", "data", opts);

// Subsequent publishes use alias (empty topic)
co_await client.publish("", "data", opts);  // Uses alias 1
```

## 自动重连

```cpp
mqtt::client client(ctx);

client.set_reconnect_options({
    .enable = true,
    .initial_delay = 1s,
    .max_delay = 60s,
    .backoff_multiplier = 2.0
});

client.on_connect([]() {
    std::println("Connected");
});

client.on_disconnect([]() {
    std::println("Disconnected, will auto-reconnect");
});

co_await client.connect(opts);
// Client automatically reconnects on connection loss
```

## 同步客户端

用于非协程上下文：

```cpp
mqtt::sync_client client;

// Blocking connect
client.connect({
    .host = "127.0.0.1",
    .port = 1883
});

// Blocking publish
client.publish("topic", "data", mqtt::qos::at_least_once);

// Blocking subscribe
client.subscribe("topic/#", mqtt::qos::at_least_once);

// Set callback
client.on_message([](const mqtt::publish_message& msg) {
    std::println("Received: {}", msg.payload);
});

// Wait for messages
std::this_thread::sleep_for(60s);

client.disconnect();
```

## 物联网传感器示例

```cpp
task<void> temperature_sensor(io_context& ctx) {
    mqtt::client client(ctx);
    
    co_await client.connect({
        .host = "mqtt.example.com",
        .port = 1883,
        .client_id = "temp_sensor_001",
        .clean_session = false  // Resume session
    });
    
    while (true) {
        // Read sensor
        float temp = read_temperature();
        
        // Publish reading
        auto payload = std::format("{{\"temp\":{:.1f}}}", temp);
        co_await client.publish("sensors/temp/001", payload,
            mqtt::qos::at_least_once);
        
        // Wait 10 seconds
        co_await async_sleep(ctx, 10s);
    }
}
```

## 命令与控制示例

```cpp
task<void> device_controller(io_context& ctx) {
    mqtt::client client(ctx);
    
    co_await client.connect({
        .host = "mqtt.example.com",
        .port = 1883,
        .client_id = "controller"
    });
    
    // Subscribe to command topic
    co_await client.subscribe("devices/+/command", mqtt::qos::at_least_once);
    
    client.on_message([&](const mqtt::publish_message& msg) -> task<void> {
        // Parse command
        auto json = nlohmann::json::parse(msg.payload);
        auto action = json["action"].get<std::string>();
        
        // Execute command
        if (action == "turn_on") {
            turn_on_device();
        } else if (action == "turn_off") {
            turn_off_device();
        }
        
        // Publish status
        auto status = std::format("{{\"status\":\"{}\"}}",  action);
        co_await client.publish("devices/status", status,
            mqtt::qos::at_least_once);
    });
    
    // Keep running
    co_await async_sleep(ctx, std::chrono::hours(24));
}
```

## 性能提示

1. **使用 QoS 0** 处理非关键数据（较低开销）
2. **启用 clean_session=false** 以恢复会话
3. **使用主题别名**（v5.0）处理重复主题
4. **尽可能批量发布**
5. **使用共享订阅**进行负载均衡
6. **设置适当的 keep_alive**（通常 30-60 秒）
7. **启用持久化**以提高代理可靠性

## 下一步

- **[HTTP 服务器](http.md)** - MQTT 控制的 REST API
- **[WebSocket](websocket.md)** - WebSocket-MQTT 桥接
- **[Redis](redis.md)** - 缓存 MQTT 消息
