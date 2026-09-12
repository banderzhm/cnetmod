/// cnetmod.core.process — Cross-platform child process with stdio pipes

module;

#include <cnetmod/config.hpp>

export module cnetmod.core.process;

import std;

namespace cnetmod {

export struct process_options
{
    std::filesystem::path executable;
    std::vector<std::filesystem::path> arguments;
    std::optional<std::filesystem::path> working_directory;
};

/// Move-only RAII child process used by protocol transports and tooling.
export class child_process
{
public:
    child_process() noexcept;
    ~child_process();
    child_process(child_process&&) noexcept;
    auto operator=(child_process&&) noexcept -> child_process&;
    child_process(const child_process&) = delete;
    auto operator=(const child_process&) -> child_process& = delete;

    [[nodiscard]] static auto launch(process_options options)
        -> std::expected<child_process, std::error_code>;
    [[nodiscard]] auto write(std::string_view bytes)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto read_line()
        -> std::expected<std::string, std::error_code>;
    void close_input() noexcept;
    void terminate() noexcept;
    [[nodiscard]] auto running() const noexcept -> bool;

private:
    struct implementation;
    explicit child_process(std::unique_ptr<implementation> state) noexcept;
    std::unique_ptr<implementation> state_;
};

} // namespace cnetmod
