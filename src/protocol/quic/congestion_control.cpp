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

void new_reno_congestion_controller::on_packets_discarded(std::uint64_t bytes)
{
    bytes_in_flight_ -= std::min(bytes, bytes_in_flight_);
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

void cubic_congestion_controller::on_packets_discarded(std::uint64_t bytes)
{
    bytes_in_flight_ -= std::min(bytes, bytes_in_flight_);
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

bbr_congestion_controller::bbr_congestion_controller(quic_config)
{
    update_pacing_rate();
}

void bbr_congestion_controller::on_packet_sent(std::uint64_t bytes)
{
    bytes_in_flight_ += bytes;
}

void bbr_congestion_controller::on_packet_acked(std::uint64_t bytes)
{
    bytes_in_flight_ -= std::min(bytes, bytes_in_flight_);
    update_model(bytes, std::chrono::steady_clock::now());
}

void bbr_congestion_controller::on_packets_discarded(std::uint64_t bytes)
{
    bytes_in_flight_ -= std::min(bytes, bytes_in_flight_);
}

void bbr_congestion_controller::on_congestion_event(std::uint64_t lost_bytes)
{
    bytes_in_flight_ -= std::min(lost_bytes, bytes_in_flight_);
    // BBR does not use loss as its bandwidth signal.  A modest safety clamp
    // prevents a burst after severe loss while preserving its model-driven
    // probing behaviour.
    cwnd_ = std::max(min_window_, (cwnd_ * 7U) / 8U);
    update_pacing_rate();
}

void bbr_congestion_controller::update_rtt(
    std::chrono::steady_clock::duration smoothed_rtt)
{
    if (smoothed_rtt <= std::chrono::steady_clock::duration::zero())
        return;
    smoothed_rtt_ = smoothed_rtt;
    min_rtt_ = std::min(min_rtt_, smoothed_rtt);
    update_pacing_rate();
}

auto bbr_congestion_controller::can_send(std::uint64_t bytes_in_flight) const noexcept -> bool
{
    return bytes_in_flight < cwnd_;
}

auto bbr_congestion_controller::can_send_datagram(std::uint64_t bytes) const noexcept -> bool
{
    return bytes <= cwnd_ - std::min(bytes_in_flight_, cwnd_);
}

auto bbr_congestion_controller::bytes_in_flight() const noexcept -> std::uint64_t
{
    return bytes_in_flight_;
}

auto bbr_congestion_controller::congestion_window() const noexcept -> std::uint64_t
{
    return cwnd_;
}

auto bbr_congestion_controller::ssthresh() const noexcept -> std::uint64_t
{
    // BBR does not have a slow-start threshold; expose the active cwnd as the
    // useful sending bound for code shared with loss-based controllers.
    return cwnd_;
}

auto bbr_congestion_controller::pacing_rate() const noexcept -> std::optional<double>
{
    return pacing_rate_;
}

void bbr_congestion_controller::update_model(std::uint64_t acked_bytes,
    std::chrono::steady_clock::time_point now)
{
    if (last_ack_)
    {
        const auto interval = now - *last_ack_;
        if (interval > std::chrono::steady_clock::duration::zero())
        {
            const auto sample = static_cast<double>(acked_bytes) /
                std::chrono::duration<double>(interval).count();
            bandwidth_samples_[bandwidth_sample_cursor_] = sample;
            bandwidth_sample_cursor_ = (bandwidth_sample_cursor_ + 1U) % bandwidth_samples_.size();
            bandwidth_sample_count_ = std::min(bandwidth_sample_count_ + 1U,
                bandwidth_samples_.size());
            bandwidth_bytes_per_second_ = *std::max_element(bandwidth_samples_.begin(),
                bandwidth_samples_.begin() + static_cast<std::ptrdiff_t>(bandwidth_sample_count_));
        }
    }
    last_ack_ = now;

    const auto bandwidth = bandwidth_bytes_per_second_ > 0.0
        ? bandwidth_bytes_per_second_
        : static_cast<double>(cwnd_) / std::chrono::duration<double>(min_rtt_).count();
    const auto bdp = std::max<double>(static_cast<double>(min_window_),
        bandwidth * std::chrono::duration<double>(min_rtt_).count());
    const auto target = static_cast<std::uint64_t>(std::ceil(bdp * cwnd_gain_));

    if (mode_ == mode::startup)
    {
        const auto before = cwnd_;
        cwnd_ += acked_bytes;
        startup_rounds_without_growth_ = cwnd_ > before ? 0U : startup_rounds_without_growth_ + 1U;
        if (cwnd_ >= target || startup_rounds_without_growth_ >= 3U)
            mode_ = mode::drain;
    }
    else if (mode_ == mode::drain)
    {
        if (bytes_in_flight_ <= target)
            mode_ = mode::probe_bandwidth;
        cwnd_ = std::max(min_window_, target);
    }
    else
        cwnd_ = std::max(min_window_, target);
    update_pacing_rate();
}

void bbr_congestion_controller::update_pacing_rate()
{
    const auto gain = mode_ == mode::startup ? startup_gain_
        : mode_ == mode::drain               ? drain_gain_
                                             : probe_gain_;
    const auto bandwidth = bandwidth_bytes_per_second_ > 0.0
        ? bandwidth_bytes_per_second_
        : static_cast<double>(cwnd_) /
            std::chrono::duration<double>(std::max(min_rtt_, smoothed_rtt_)).count();
    pacing_rate_ = bandwidth * gain;
}

congestion_controller::congestion_controller(quic_config config)
    : algorithm_(config.congestion_algorithm), controller_(std::in_place_type<new_reno_congestion_controller>, config)
{
    if (algorithm_ == quic_congestion_algorithm::cubic)
        controller_.emplace<cubic_congestion_controller>(std::move(config));
    else if (algorithm_ == quic_congestion_algorithm::bbr)
        controller_.emplace<bbr_congestion_controller>(std::move(config));
}

void congestion_controller::on_packet_sent(std::uint64_t bytes)
{
    std::visit([bytes](auto& controller)
        {
            controller.on_packet_sent(bytes);
        },
        controller_);
}

void congestion_controller::on_packet_acked(std::uint64_t bytes)
{
    std::visit([bytes](auto& controller)
        {
            controller.on_packet_acked(bytes);
        },
        controller_);
}

void congestion_controller::on_packets_discarded(std::uint64_t bytes)
{
    std::visit([bytes](auto& controller)
        {
            controller.on_packets_discarded(bytes);
        },
        controller_);
}

void congestion_controller::on_congestion_event(std::uint64_t lost_bytes)
{
    std::visit([lost_bytes](auto& controller)
        {
            controller.on_congestion_event(lost_bytes);
        },
        controller_);
}

void congestion_controller::update_rtt(std::chrono::steady_clock::duration smoothed_rtt)
{
    std::visit([smoothed_rtt](auto& controller)
        {
            controller.update_rtt(smoothed_rtt);
        },
        controller_);
}

auto congestion_controller::can_send(std::uint64_t bytes_in_flight) const noexcept -> bool
{
    return std::visit([bytes_in_flight](const auto& controller)
        {
            return controller.can_send(bytes_in_flight);
        },
        controller_);
}

auto congestion_controller::can_send_datagram(std::uint64_t bytes) const noexcept -> bool
{
    return std::visit([bytes](const auto& controller)
        {
            return controller.can_send_datagram(bytes);
        },
        controller_);
}

auto congestion_controller::bytes_in_flight() const noexcept -> std::uint64_t
{
    return std::visit([](const auto& controller)
        {
            return controller.bytes_in_flight();
        },
        controller_);
}

auto congestion_controller::congestion_window() const noexcept -> std::uint64_t
{
    return std::visit([](const auto& controller)
        {
            return controller.congestion_window();
        },
        controller_);
}

auto congestion_controller::ssthresh() const noexcept -> std::uint64_t
{
    return std::visit([](const auto& controller)
        {
            return controller.ssthresh();
        },
        controller_);
}

auto congestion_controller::pacing_rate() const noexcept -> std::optional<double>
{
    return std::visit([](const auto& controller)
        {
            return controller.pacing_rate();
        },
        controller_);
}

auto congestion_controller::algorithm() const noexcept -> quic_congestion_algorithm
{
    return algorithm_;
}

auto create_congestion_controller(quic_config config)
    -> congestion_controller
{
    return congestion_controller{std::move(config)};
}

} // namespace cnetmod::quic
