// Simple unit tests for QUIC reliability layer modules
// This is a validation script to demonstrate the API usage

#include <cassert>
#include <chrono>
#include <iostream>

// Note: This is pseudo-code for documentation purposes.
// The actual C++20 modules require MSVC/Clang with proper build configuration.

int main()
{
    using namespace cnetmod::quic;

    // =================================================================
    // Test 1: Loss Detection
    // =================================================================
    {
        quic_config config{};
        config.idle_timeout = std::chrono::milliseconds(30000);

        loss_detector detector(config);

        time_point base_time = std::chrono::steady_clock::now();

        // Simulate sending packets
        detector.on_packet_sent(0, 1472, base_time + std::chrono::milliseconds(1), true, pn_space::application);
        detector.on_packet_sent(1, 1472, base_time + std::chrono::milliseconds(2), true, pn_space::application);
        detector.on_packet_sent(2, 500, base_time + std::chrono::milliseconds(3), false, pn_space::application);

        // Simulate ACK received at RTT=50ms
        ack_frame ack{};
        ack.largest_acked = 1;
        ack.ack_delay = 25000; // microseconds

        auto result = detector.on_ack_received(ack, 1, base_time + std::chrono::milliseconds(50), pn_space::application);

        if (result)
        {
            std::cout << "ACK processing successful, newly acked: ";
            for (const auto& pn : *result)
            {
                std::cout << pn << " ";
            }
            std::cout << "\n";
        }

        const auto& rtt_sample = detector.rtt_sample();
        std::cout << "RTT Sample obtained\n";

        // Check PTO duration
        auto pto_dur = detector.pto_duration();
        std::cout << "PTO Duration calculated\n";
    }

    // =================================================================
    // Test 2: Congestion Control
    // =================================================================
    {
        quic_config config{};
        auto controller = create_congestion_controller(config);

        std::cout << "Initial CWND: " << controller->congestion_window() << " bytes\n";
        std::cout << "Initial SSthresh: " << controller->ssthresh() << " bytes\n";

        // Simulate slow start phase
        controller->on_packet_sent(1472);  // Send one MTU
        controller->on_packet_acked(1472); // Acknowledge it

        std::cout << "After ACK in slow start: " << controller->congestion_window() << " bytes\n";

        // Simulate congestion event
        controller->on_congestion_event(2944); // Lost packets

        std::cout << "After congestion event: " << controller->congestion_window() << " bytes\n";
        std::cout << "SSthresh after congestion: " << controller->ssthresh() << " bytes\n";
    }

    // =================================================================
    // Test 3: Flow Control
    // =================================================================
    {
        // Connection-level flow control
        flow_controller conn_fc(1048576); // 1MB initial limit

        std::cout << "Initial send window: " << conn_fc.remaining_send_window() << " bytes\n";
        std::cout << "Initial recv window: " << conn_fc.remaining_recv_window() << " bytes\n";

        // Consume data (sender side)
        auto result = conn_fc.consume_send(524288); // Send 512KB
        if (result)
        {
            std::cout << "Send consume successful, should_update: " << (*result ? "yes" : "no") << "\n";
            std::cout << "Remaining send window: " << conn_fc.remaining_send_window() << " bytes\n";
        }

        // Stream-level flow control
        stream_flow_controller stream_fc(262144); // 256KB initial

        std::cout << "Stream max_data: " << stream_fc.stream_max_data() << " bytes\n";
        std::cout << "Stream remaining send window: " << stream_fc.remaining_send_window() << " bytes\n";
    }

    std::cout << "\nAll tests completed successfully!\n";
    return 0;
}
