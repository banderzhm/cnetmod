#pragma once

#include <csignal>

namespace mongodb_example {

class shutdown_signal
{
public:
    void install() noexcept
    {
        requested_ = 0;
        std::signal(SIGINT, &shutdown_signal::handle);
        std::signal(SIGTERM, &shutdown_signal::handle);
    }

    [[nodiscard]] auto is_requested() const noexcept -> bool
    {
        return requested_ != 0;
    }

private:
    static void handle(int) noexcept
    {
        requested_ = 1;
    }

    inline static volatile std::sig_atomic_t requested_ = 0;
};

} // namespace mongodb_example
