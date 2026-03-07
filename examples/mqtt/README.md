# MQTT Examples

MQTT client implementation with full protocol support.

## Examples

### mqtt_demo.cpp
Complete MQTT client demonstration:
- **Connection**: CONNECT/CONNACK with clean session
- **Publishing**: PUBLISH with QoS 0, 1, 2
- **Subscribing**: SUBSCRIBE/UNSUBSCRIBE to topics
- **Retained messages**: Message persistence
- **Last Will and Testament (LWT)**: Automatic notification on disconnect
- **Keep-alive**: Automatic PINGREQ/PINGRESP
- **Topic wildcards**: Single-level (+) and multi-level (#)

## MQTT Features

### Quality of Service (QoS)

- **QoS 0**: At most once delivery (fire and forget)
- **QoS 1**: At least once delivery (acknowledged)
- **QoS 2**: Exactly once delivery (assured)

### Topic Patterns

```
home/livingroom/temperature    # Specific topic
home/+/temperature            # Single-level wildcard
home/#                        # Multi-level wildcard
```

### Message Retention

```cpp
// Publish retained message
client.publish("status/online", "true", qos::at_least_once, true);

// New subscribers receive last retained message immediately
```

### Last Will and Testament

```cpp
mqtt::connect_options opts;
opts.will_topic = "status/client1";
opts.will_message = "offline";
opts.will_qos = qos::at_least_once;
opts.will_retain = true;

// Broker publishes will message if client disconnects unexpectedly
```

## Building

```bash
cd build
cmake --build . --target mqtt_demo
```

## Running

```bash
# Start MQTT broker first (e.g., mosquitto)
mosquitto -v

# Or use public broker
# broker.hivemq.com:1883
# test.mosquitto.org:1883

# Run example
./mqtt/mqtt_demo
```

## Testing with mosquitto_sub/pub

```bash
# Subscribe to topics
mosquitto_sub -h localhost -t "test/#" -v

# Publish messages
mosquitto_pub -h localhost -t "test/topic" -m "Hello MQTT"

# Subscribe with QoS
mosquitto_sub -h localhost -t "test/qos" -q 2

# Publish retained message
mosquitto_pub -h localhost -t "status" -m "online" -r
```

## Configuration

```cpp
mqtt::client_config config;
config.broker_host = "localhost";
config.broker_port = 1883;
config.client_id = "cnetmod_client";
config.username = "";  // Optional
config.password = "";  // Optional
config.keep_alive = 60;  // seconds
config.clean_session = true;
```

## Protocol Support

- MQTT 3.1.1 specification
- TCP transport
- TLS/SSL support (optional)
- Automatic reconnection
- Message queuing
- Session persistence

## Use Cases

- IoT device communication
- Real-time messaging
- Sensor data collection
- Remote monitoring and control
- Pub/Sub architecture
