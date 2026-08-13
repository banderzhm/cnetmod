# QUIC Reliability Layer - Technical Implementation Notes

## Algorithm Details (Per RFC 9002)

### Loss Detection - RTT Estimation

The RTT sampling algorithm implements the hybrid approach described in RFC 9002 §7.2:

#### Step 1: Min RTT Selection
```cpp
min_rtt = min(min_rtt, raw_rtt)
```
This removes outlier samples that are too large or corrupted by ACK delay.

#### Step 2: Filtered RTT Calculation
```cpp
filtered_rtt = (min_rtt <= raw_rtt - ack_delay) ? (raw_rtt - ack_delay) : min_rtt
```
Removes ACK delay contribution before updating smoothed RTT.

#### Step 3: Exponential Weighted Moving Average (EWMA)
```cpp
// With α = β = 1/8 (spec constants)
smoothed_rtt = (7 * smoothed_rtt + filtered_rtt) / 8
rtt_var = (7 * rtt_var + |smoothed_rtt - filtered_rtt|) / 8
```

#### Benefits:
- Robust against network jitter
- Filters out retransmissions (higher RRs are ignored)
- Handles ACK compression by discounting delays

---

### Loss Detection - PTO Formula

```cpp
PTO = smoothed_rtt + max(4 * rtt_var, granularity) + max_ack_delay
```

Where:
- `granularity` = 1ms (constant)
- `max_ack_delay` = peer-reported value (default 25ms if unknown)

**Purpose**: When no packets are outstanding but receiver might be waiting for more data

**Behavior**: 
1. Send 1 probe packet every PTO duration
2. Increment probe count
3. If still no ACK after 3 probes, assume connection dead

---

### Congestion Control - Slow Start vs Congestion Avoidance

#### Slow Start Phase:
```cpp
while (bytes_in_flight < cwnd):
    can_send = true
    cwnd += bytes_acked_per_ack
```
- **Growth**: Exponential (doubles every RTT)
- **Trigger**: Enter on connection start / after recovery exits
- **Exit Condition**: `bytes_in_flight >= cwnd`

#### Congestion Avoidance Phase:
```cpp
// AIMD approximation per ACK
cwnd += mtu * (cwnd / bytes_in_flight)
```
- **Growth**: Linear (increases ~1 MSS per RTT)
- **Trigger**: Entered from slow start when `bytes_in_flight >= cwnd`
- **Exit Condition**: Trigger congestion event

#### Recovery Behavior:
```cpp
ssthresh = max(cwnd / 2, 2 * MTU)
cwnd = ssthresh  // or 2 * MTU if ssthresh < 2 * MTU
enter_recovery()
```

During recovery:
- All losses trigger NO reduction in cwnd
- Exit on ACK of highest outstanding packet
- Transition to congestion avoidance (not slow start!)

---

### Flow Control - Auto-Update Threshold

The spec recommends triggering MAX_DATA updates when offset exceeds new limit.

Implementation uses 50% threshold:
```cpp
consumed = current_limit - remaining_window
threshold = max_data * 0.5
should_update = consumed >= threshold && consumed > 0
```

**Rationale**: 
- 50% is the sweet spot between buffer utilization and update frequency
- Updates at 50% leave 50% headroom for unexpected bursts
- Prevents receiver from overflowing its receive buffer

**Example**:
- Initial: limit=1MB, consumed=0, window=1MB
- After sending 600KB: limit=1MB, consumed=600KB, window=400KB
- Update triggered (600KB ≥ 500KB threshold)
- Send MAX_DATA(limit=1.5MB)
- Next: limit=1.5MB, window=900KB

---

## Error Handling Strategy

### Loss Detection Errors: None
- Pure computation, returns void or std::vector<T>
- Failed detection = empty vector

### Congestion Control Errors: None
- All state transitions are valid
- Degraded performance (not errors)

### Flow Control Errors:
```cpp
auto result = fc->consume_send(bytes);
if (!result) {
    auto ec = result.error();
    switch(ec) {
        case std::errc::no_buffer_space:
            // Peer cannot accept more data - pause sends
            break;
        case std::errc::not_supported:
            // Would exceed total limit - close stream/connection
            break;
    }
}
```

Use custom error codes via `quic_errc`:
- `flow_control_error` = 0x03
- Connection error code when flow control violations occur

---

## Integration Checklist

Before integrating these modules into production:

☐ Verify packet number space transitions work correctly
☐ Add metrics collection for debugging (RTT history, cwnd trends)
☐ Test with real network conditions (packet loss, reordering)
☐ Implement ECN support (optional per RFC)
☐ Consider BBRv1 congestion controller as alternative
☐ Add unit tests for edge cases:
   - Large RTT variance (>3x mean)
   - Packet reordering beyond kPacketThreshold
   - Back-to-back losses during recovery
   - High-throughput scenarios (TB/s class)

---

## Performance Benchmarks

Based on typical internet conditions:

| Scenario | RTT | CWND Growth | Convergence Time |
|----------|-----|-------------|------------------|
| LAN | 1ms | 50kpps | Instant |
| WAN | 100ms | 1.5kpps | 10-20s to 1Gbps |
| Intercontinental | 200ms | 750pps | 20-40s to 1Gbps |

Note: These assume single TCP-like stream behavior. QUIC multiplexing affects this.

---

## Thread Safety Design

**All three modules are intentionally NOT thread-safe:**

Reasons:
1. Simpler implementation
2. Better performance (no locks)
3. Caller manages concurrent access naturally

**Recommended usage pattern:**
```cpp
// Single-threaded event loop model
void run_loop() {
    while (running) {
        // Lock-free operations on loss_detector
        detector->on_packet_sent(...);
        
        // Check timers atomically
        auto event = detector->get_loss_time_and_space(space);
        
        // CC and FC calls are all synchronous
        cc->on_congestion_event(...);
        fc->consume_send(...);
    }
}
```

For multi-threaded usage, wrap caller logic:
```cpp
std::mutex detector_mutex;
{
    std::lock_guard lock(detector_mutex);
    detector->on_packet_sent(...);
}
```

---

## Memory Usage

Per-module estimates:

**Loss Detector**:
- O(N) where N = unacked packets per packet number space
- Typical: 100-1000 entries (~few KB)
- Cleanup: Remove packets after 1 second idle time recommended

**Congestion Controller**:
- O(1) - constant memory
- 128 bytes total state machine

**Flow Controllers**:
- Connection level: 48 bytes
- Stream level: 32 bytes × (active streams)

Total peak memory: ~few MBs even with heavy load
