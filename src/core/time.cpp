module cnetmod.core.time;

namespace cnetmod {

auto unix_time_seconds() noexcept -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace cnetmod
