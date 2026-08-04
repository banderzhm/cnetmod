module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import :congestion_control;

namespace cnetmod::quic {

namespace {

    constexpr auto min_congestion_window = std::uint64_t{2} * 1472U;

    auto pacing_rate_for(std::uint64_t cwnd, std::chrono::steady_clock::duration rtt,
        double gain) -> double
    {
        if (rtt <= std::chrono::steady_clock::duration::zero())
            return std::numeric_limits<double>::infinity();
        return gain * static_cast<double>(cwnd) /
            std::chrono::duration<double>(rtt).count();
    }

} // namespace

new_reno_congestion_controller::new_reno_congestion_controller(quic_config)
{
    update_pacing_rate_estimator();
}

void new_reno_congestion_controller::on_packet_sent(std::uint64_t bytes)
{
    bytes_in_flight_ += bytes;
}

void new_reno_congestion_controller::on_packet_acked(std::uint64_t bytes)
{
    bytes_in_flight_ -= std::min(bytes, bytes_in_flight_);
    if (current_state_ == state_type::recovery)
        current_state_ = state_type::congestion_avoidance;
    else if (current_state_ == state_type::slow_start)
        cwnd_ += bytes;
    else
        cwnd_ += std::max<std::uint64_t>(1U, (mtu_ * bytes) / cwnd_);
    update_pacing_rate_estimator();
}

void new_reno_congestion_controller::on_congestion_event(std::uint64_t lost_bytes)
{
    if (current_state_ == state_type::recovery)
        return;
    bytes_in_flight_ -= std::min(lost_bytes, bytes_in_flight_);
    ssthresh_ = std::max(cwnd_ / 2U, min_congestion_window);
    cwnd_ = ssthresh_;
    current_state_ = state_type::recovery;
    update_pacing_rate_estimator();
}

void new_reno_congestion_controller::update_rtt(
    std::chrono::steady_clock::duration smoothed_rtt)
{
    if (smoothed_rtt > std::chrono::steady_clock::duration::zero())
        smoothed_rtt_ = smoothed_rtt;
    update_pacing_rate_estimator();
}

auto new_reno_congestion_controller::can_send(std::uint64_t bytes_in_flight) const noexcept -> bool
{
    return bytes_in_flight < cwnd_;
}

auto new_reno_congestion_controller::can_send_datagram(std::uint64_t bytes) const noexcept -> bool
{
    return bytes <= cwnd_ - std::min(bytes_in_flight_, cwnd_);
}

auto new_reno_congestion_controller::bytes_in_flight() const noexcept -> std::uint64_t
{
    return bytes_in_flight_;
}

auto new_reno_congestion_controller::congestion_window() const noexcept -> std::uint64_t
{
    return cwnd_;
}

auto new_reno_congestion_controller::ssthresh() const noexcept -> std::uint64_t
{
    return ssthresh_;
}

auto new_reno_congestion_controller::pacing_rate() const noexcept -> std::optional<double>
{
    return pacing_rate_;
}

void new_reno_congestion_controller::update_pacing_rate_estimator()
{
    const auto gain = current_state_ == state_type::slow_start ? 1.25 : 1.0;
    pacing_rate_ = pacing_rate_for(cwnd_, smoothed_rtt_, gain);
}

cubic_congestion_controller::cubic_congestion_controller(quic_config)
{
    update_pacing_rate_estimator();
}

void cubic_congestion_controller::on_packet_sent(std::uint64_t bytes)
{
    bytes_in_flight_ += bytes;
}

void cubic_congestion_controller::on_packet_acked(std::uint64_t bytes)
{
    bytes_in_flight_ -= std::min(bytes, bytes_in_flight_);
    if (cwnd_ < ssthresh_)
    {
        cwnd_ += bytes;
        update_pacing_rate_estimator();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!epoch_start_)
        epoch_start_ = now;
    const auto elapsed = std::chrono::duration<double>(now - *epoch_start_).count();
    const auto k = std::cbrt(static_cast<double>(w_max_) * (1.0 - beta_) / cubic_c_);
    const auto target = cubic_c_ * std::pow(elapsed - k, 3.0) + static_cast<double>(w_max_);
    const auto reno_increment = std::max<std::uint64_t>(1U, (mtu_ * bytes) / cwnd_);
    if (target > static_cast<double>(cwnd_))
    {
        const auto delta = static_cast<std::uint64_t>(target - static_cast<double>(cwnd_));
        cwnd_ += std::max<std::uint64_t>(reno_increment, std::min(delta, bytes));
    }
    else
        cwnd_ += reno_increment;
    update_pacing_rate_estimator();
}

void cubic_congestion_controller::on_congestion_event(std::uint64_t lost_bytes)
{
    bytes_in_flight_ -= std::min(lost_bytes, bytes_in_flight_);
    w_max_ = cwnd_;
    cwnd_ = std::max<std::uint64_t>(
        static_cast<std::uint64_t>(static_cast<double>(cwnd_) * beta_), min_congestion_window);
    ssthresh_ = cwnd_;
    epoch_start_.reset();
    update_pacing_rate_estimator();
}

void cubic_congestion_controller::update_rtt(std::chrono::steady_clock::duration smoothed_rtt)
{
    if (smoothed_rtt > std::chrono::steady_clock::duration::zero())
        smoothed_rtt_ = smoothed_rtt;
    update_pacing_rate_estimator();
}

auto cubic_congestion_controller::can_send(std::uint64_t bytes_in_flight) const noexcept -> bool
{
    return bytes_in_flight < cwnd_;
}

auto cubic_congestion_controller::can_send_datagram(std::uint64_t bytes) const noexcept -> bool
{
    return bytes <= cwnd_ - std::min(bytes_in_flight_, cwnd_);
}

auto cubic_congestion_controller::bytes_in_flight() const noexcept -> std::uint64_t
{
    return bytes_in_flight_;
}

auto cubic_congestion_controller::congestion_window() const noexcept -> std::uint64_t
{
    return cwnd_;
}

auto cubic_congestion_controller::ssthresh() const noexcept -> std::uint64_t
{
    return ssthresh_;
}

auto cubic_congestion_controller::pacing_rate() const noexcept -> std::optional<double>
{
    return pacing_rate_;
}

void cubic_congestion_controller::update_pacing_rate_estimator()
{
    pacing_rate_ = pacing_rate_for(cwnd_, smoothed_rtt_, cwnd_ < ssthresh_ ? 1.25 : 1.0);
}

auto create_congestion_controller(quic_config config)
    -> std::unique_ptr<new_reno_congestion_controller>
{
    return std::make_unique<new_reno_congestion_controller>(std::move(config));
}

} // namespace cnetmod::quic
