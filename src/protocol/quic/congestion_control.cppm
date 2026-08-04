module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:congestion_control;

import std;
import :types;

namespace cnetmod::quic {

export class new_reno_congestion_controller
{
public:
    explicit new_reno_congestion_controller(quic_config config);

    void on_packet_sent(std::uint64_t bytes);
    void on_packet_acked(std::uint64_t bytes);
    void on_congestion_event(std::uint64_t lost_bytes);
    void update_rtt(std::chrono::steady_clock::duration smoothed_rtt);

    [[nodiscard]] auto can_send(std::uint64_t bytes_in_flight) const noexcept -> bool;
    [[nodiscard]] auto can_send_datagram(std::uint64_t bytes) const noexcept -> bool;
    [[nodiscard]] auto bytes_in_flight() const noexcept -> std::uint64_t;
    [[nodiscard]] auto congestion_window() const noexcept -> std::uint64_t;
    [[nodiscard]] auto ssthresh() const noexcept -> std::uint64_t;
    [[nodiscard]] auto pacing_rate() const noexcept -> std::optional<double>;

private:
    enum class state_type
    {
        slow_start,
        congestion_avoidance,
        recovery
    };

    void update_pacing_rate_estimator();

    state_type current_state_{state_type::slow_start};
    std::uint64_t cwnd_{14720};
    std::uint64_t ssthresh_{(std::numeric_limits<std::uint64_t>::max)()};
    std::uint64_t bytes_in_flight_{};
    static constexpr std::uint64_t mtu_{1472};
    std::chrono::steady_clock::duration smoothed_rtt_{std::chrono::milliseconds{100}};
    double pacing_rate_{static_cast<double>(cwnd_) * 10.0};
};

/// CUBIC congestion control following RFC 9438.  It uses the same byte-based
/// packet accounting API as NewReno so callers can select it without changing
/// QUIC loss recovery or flow-control code.
export class cubic_congestion_controller
{
public:
    explicit cubic_congestion_controller(quic_config config);

    void on_packet_sent(std::uint64_t bytes);
    void on_packet_acked(std::uint64_t bytes);
    void on_congestion_event(std::uint64_t lost_bytes);
    void update_rtt(std::chrono::steady_clock::duration smoothed_rtt);

    [[nodiscard]] auto can_send(std::uint64_t bytes_in_flight) const noexcept -> bool;
    [[nodiscard]] auto can_send_datagram(std::uint64_t bytes) const noexcept -> bool;
    [[nodiscard]] auto bytes_in_flight() const noexcept -> std::uint64_t;
    [[nodiscard]] auto congestion_window() const noexcept -> std::uint64_t;
    [[nodiscard]] auto ssthresh() const noexcept -> std::uint64_t;
    [[nodiscard]] auto pacing_rate() const noexcept -> std::optional<double>;

private:
    void update_pacing_rate_estimator();

    std::uint64_t cwnd_{14720};
    std::uint64_t ssthresh_{(std::numeric_limits<std::uint64_t>::max)()};
    std::uint64_t bytes_in_flight_{};
    std::uint64_t w_max_{cwnd_};
    static constexpr std::uint64_t mtu_{1472};
    static constexpr double beta_{0.7};
    static constexpr double cubic_c_{0.4};
    std::chrono::steady_clock::duration smoothed_rtt_{std::chrono::milliseconds{100}};
    std::optional<std::chrono::steady_clock::time_point> epoch_start_;
    double pacing_rate_{static_cast<double>(cwnd_) * 10.0};
};

[[nodiscard]] auto create_congestion_controller(quic_config config)
    -> std::unique_ptr<new_reno_congestion_controller>;

} // namespace cnetmod::quic
