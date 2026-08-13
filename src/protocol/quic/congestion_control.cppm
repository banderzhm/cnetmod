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
    /// Remove packets whose QUIC packet-number space has been discarded. This
    /// is neither an ACK nor a congestion signal, so it must not grow or
    /// reduce the congestion window.
    void on_packets_discarded(std::uint64_t bytes);
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
    void on_packets_discarded(std::uint64_t bytes);
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

/// A compact BBRv1 controller.  QUIC loss recovery supplies byte ACKs and a
/// smoothed RTT, so delivery-rate samples are derived from ACK spacing.  The
/// controller is intentionally self-contained: no allocations, locks, or
/// virtual dispatch occur on the packet path.
export class bbr_congestion_controller
{
public:
    explicit bbr_congestion_controller(quic_config config);

    void on_packet_sent(std::uint64_t bytes);
    void on_packet_acked(std::uint64_t bytes);
    void on_packets_discarded(std::uint64_t bytes);
    void on_congestion_event(std::uint64_t lost_bytes);
    void update_rtt(std::chrono::steady_clock::duration smoothed_rtt);

    [[nodiscard]] auto can_send(std::uint64_t bytes_in_flight) const noexcept -> bool;
    [[nodiscard]] auto can_send_datagram(std::uint64_t bytes) const noexcept -> bool;
    [[nodiscard]] auto bytes_in_flight() const noexcept -> std::uint64_t;
    [[nodiscard]] auto congestion_window() const noexcept -> std::uint64_t;
    [[nodiscard]] auto ssthresh() const noexcept -> std::uint64_t;
    [[nodiscard]] auto pacing_rate() const noexcept -> std::optional<double>;

private:
    enum class mode : std::uint8_t
    {
        startup,
        drain,
        probe_bandwidth
    };
    void update_model(std::uint64_t acked_bytes,
        std::chrono::steady_clock::time_point now);
    void update_pacing_rate();

    static constexpr std::uint64_t mtu_{1472};
    static constexpr std::uint64_t min_window_{2U * mtu_};
    static constexpr double startup_gain_{2.885};
    static constexpr double drain_gain_{1.0 / startup_gain_};
    static constexpr double probe_gain_{1.0};
    static constexpr double cwnd_gain_{2.0};
    mode mode_{mode::startup};
    std::uint64_t cwnd_{10U * mtu_};
    std::uint64_t bytes_in_flight_{};
    std::chrono::steady_clock::duration smoothed_rtt_{std::chrono::milliseconds{100}};
    std::chrono::steady_clock::duration min_rtt_{std::chrono::milliseconds{100}};
    std::array<double, 10> bandwidth_samples_{};
    std::size_t bandwidth_sample_count_{};
    std::size_t bandwidth_sample_cursor_{};
    double bandwidth_bytes_per_second_{};
    double pacing_rate_{static_cast<double>(cwnd_) * 10.0};
    std::optional<std::chrono::steady_clock::time_point> last_ack_;
    std::uint32_t startup_rounds_without_growth_{};
};

/// Runtime-selected, allocation-free congestion controller used by a QUIC
/// connection. The algorithm is selected during construction and the wrapper
/// keeps the selected state in-place; there is no shared lock or heap traffic
/// on the packet path.
export class congestion_controller
{
public:
    explicit congestion_controller(quic_config config);

    void on_packet_sent(std::uint64_t bytes);
    void on_packet_acked(std::uint64_t bytes);
    void on_packets_discarded(std::uint64_t bytes);
    void on_congestion_event(std::uint64_t lost_bytes);
    void update_rtt(std::chrono::steady_clock::duration smoothed_rtt);

    [[nodiscard]] auto can_send(std::uint64_t bytes_in_flight) const noexcept -> bool;
    [[nodiscard]] auto can_send_datagram(std::uint64_t bytes) const noexcept -> bool;
    [[nodiscard]] auto bytes_in_flight() const noexcept -> std::uint64_t;
    [[nodiscard]] auto congestion_window() const noexcept -> std::uint64_t;
    [[nodiscard]] auto ssthresh() const noexcept -> std::uint64_t;
    [[nodiscard]] auto pacing_rate() const noexcept -> std::optional<double>;
    [[nodiscard]] auto algorithm() const noexcept -> quic_congestion_algorithm;

private:
    quic_congestion_algorithm algorithm_;
    std::variant<new_reno_congestion_controller, cubic_congestion_controller,
        bbr_congestion_controller>
        controller_;
};

export [[nodiscard]] auto create_congestion_controller(quic_config config)
    -> congestion_controller;

} // namespace cnetmod::quic
