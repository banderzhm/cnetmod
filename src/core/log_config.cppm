/// Public logger configuration and extension contracts.
export module cnetmod.core.log:config;

import std;

export namespace logger
{
    enum class level { trace, debug, info, warn, error, critical, off };

    enum class output_format { text, json };

    struct rotation_options
    {
        std::size_t max_file_size = 0; // 0 disables size-based rotation
        std::size_t max_files = 0; // 0 retains all rotated files
        bool daily = false;
    };

    using sink = std::function<void(std::string_view)>;
}