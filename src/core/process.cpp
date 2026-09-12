/// cnetmod.core.process — implementations

module;

#include <cnetmod/config.hpp>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <csignal>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

module cnetmod.core.process;

import std;

namespace cnetmod {

struct child_process::implementation
{
#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE input = nullptr;
    HANDLE output = nullptr;
#else
    pid_t process = -1;
    int input = -1;
    int output = -1;
#endif
    std::string buffered_output;
};

namespace {
#ifdef _WIN32
    auto last_process_error() -> std::error_code
    {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }

    auto quote_argument(const std::wstring& argument) -> std::wstring
    {
        if (argument.empty())
            return L"\"\"";
        if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
            return argument;
        std::wstring result{L'\"'};
        std::size_t backslashes = 0;
        for (const auto character : argument)
        {
            if (character == L'\\')
            {
                ++backslashes;
                continue;
            }
            if (character == L'\"')
            {
                result.append(backslashes * 2 + 1, L'\\');
                result.push_back(L'\"');
                backslashes = 0;
                continue;
            }
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(character);
        }
        result.append(backslashes * 2, L'\\');
        result.push_back(L'\"');
        return result;
    }

    void close_handle(HANDLE& handle) noexcept
    {
        if (handle && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
        handle = nullptr;
    }
#else
    auto last_process_error() -> std::error_code
    {
        return {errno, std::generic_category()};
    }

    void close_descriptor(int& descriptor) noexcept
    {
        if (descriptor >= 0)
            ::close(descriptor);
        descriptor = -1;
    }
#endif
} // namespace

child_process::child_process() noexcept = default;

child_process::child_process(std::unique_ptr<implementation> state) noexcept
    : state_(std::move(state))
{
}

child_process::~child_process()
{
    terminate();
}

child_process::child_process(child_process&&) noexcept = default;

auto child_process::operator=(child_process&& other) noexcept -> child_process&
{
    if (this != &other)
    {
        terminate();
        state_ = std::move(other.state_);
    }
    return *this;
}

auto child_process::launch(process_options options)
    -> std::expected<child_process, std::error_code>
{
    if (options.executable.empty())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    auto state = std::make_unique<implementation>();
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE child_input = nullptr;
    HANDLE child_output = nullptr;
    if (!CreatePipe(&state->output, &child_output, &security, 0))
        return std::unexpected(last_process_error());
    if (!SetHandleInformation(state->output, HANDLE_FLAG_INHERIT, 0))
    {
        close_handle(child_output);
        close_handle(state->output);
        return std::unexpected(last_process_error());
    }
    if (!CreatePipe(&child_input, &state->input, &security, 0))
    {
        close_handle(child_output);
        close_handle(state->output);
        return std::unexpected(last_process_error());
    }
    if (!SetHandleInformation(state->input, HANDLE_FLAG_INHERIT, 0))
    {
        close_handle(child_output);
        close_handle(child_input);
        close_handle(state->output);
        close_handle(state->input);
        return std::unexpected(last_process_error());
    }

    auto command = quote_argument(options.executable.native());
    for (const auto& argument : options.arguments)
    {
        command.push_back(L' ');
        command += quote_argument(argument.native());
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_input;
    startup.hStdOutput = child_output;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    const auto working_directory = options.working_directory
        ? options.working_directory->native()
        : std::wstring{};
    const auto created = CreateProcessW(nullptr, command.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup, &process);
    const auto creation_error = created ? std::error_code{}
                                        : last_process_error();
    close_handle(child_input);
    close_handle(child_output);
    if (!created)
    {
        close_handle(state->input);
        close_handle(state->output);
        return std::unexpected(creation_error);
    }
    CloseHandle(process.hThread);
    state->process = process.hProcess;
#else
    int input_pipe[2]{};
    int output_pipe[2]{};
    if (::pipe(input_pipe) != 0)
        return std::unexpected(last_process_error());
    if (::pipe(output_pipe) != 0)
    {
        ::close(input_pipe[0]);
        ::close(input_pipe[1]);
        return std::unexpected(last_process_error());
    }
    const auto process = ::fork();
    if (process < 0)
    {
        const auto error = last_process_error();
        ::close(input_pipe[0]);
        ::close(input_pipe[1]);
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        return std::unexpected(error);
    }
    if (process == 0)
    {
        ::dup2(input_pipe[0], STDIN_FILENO);
        ::dup2(output_pipe[1], STDOUT_FILENO);
        ::close(input_pipe[0]);
        ::close(input_pipe[1]);
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        if (options.working_directory)
            (void)::chdir(options.working_directory->c_str());
        std::vector<std::string> storage;
        storage.reserve(options.arguments.size() + 1);
        storage.push_back(options.executable.string());
        for (const auto& argument : options.arguments)
            storage.push_back(argument.string());
        std::vector<char*> arguments;
        arguments.reserve(storage.size() + 1);
        for (auto& argument : storage)
            arguments.push_back(argument.data());
        arguments.push_back(nullptr);
        ::execvp(arguments.front(), arguments.data());
        ::_exit(127);
    }
    ::close(input_pipe[0]);
    ::close(output_pipe[1]);
    state->process = process;
    state->input = input_pipe[1];
    state->output = output_pipe[0];
#endif
    return child_process{std::move(state)};
}

auto child_process::write(std::string_view bytes)
    -> std::expected<void, std::error_code>
{
    if (!state_)
        return std::unexpected(
            std::make_error_code(std::errc::bad_file_descriptor));
    std::size_t written = 0;
    while (written < bytes.size())
    {
#ifdef _WIN32
        DWORD count = 0;
        const auto remaining = std::min<std::size_t>(
            bytes.size() - written, std::numeric_limits<DWORD>::max());
        if (!WriteFile(state_->input, bytes.data() + written,
                static_cast<DWORD>(remaining), &count, nullptr))
            return std::unexpected(last_process_error());
#else
        const auto count = ::write(state_->input, bytes.data() + written,
            bytes.size() - written);
        if (count < 0)
            return std::unexpected(last_process_error());
#endif
        if (count == 0)
            return std::unexpected(
                std::make_error_code(std::errc::broken_pipe));
        written += static_cast<std::size_t>(count);
    }
    return {};
}

auto child_process::read_line()
    -> std::expected<std::string, std::error_code>
{
    if (!state_)
        return std::unexpected(
            std::make_error_code(std::errc::bad_file_descriptor));
    for (;;)
    {
        if (const auto newline = state_->buffered_output.find('\n');
            newline != std::string::npos)
        {
            auto line = state_->buffered_output.substr(0, newline);
            state_->buffered_output.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return line;
        }
        std::array<char, 4096> buffer{};
#ifdef _WIN32
        DWORD count = 0;
        if (!ReadFile(state_->output, buffer.data(),
                static_cast<DWORD>(buffer.size()), &count, nullptr))
            return std::unexpected(last_process_error());
#else
        const auto count = ::read(state_->output, buffer.data(), buffer.size());
        if (count < 0)
            return std::unexpected(last_process_error());
#endif
        if (count == 0)
            return std::unexpected(
                std::make_error_code(std::errc::broken_pipe));
        state_->buffered_output.append(
            buffer.data(), static_cast<std::size_t>(count));
    }
}

void child_process::close_input() noexcept
{
    if (!state_)
        return;
#ifdef _WIN32
    close_handle(state_->input);
#else
    close_descriptor(state_->input);
#endif
}

void child_process::terminate() noexcept
{
    if (!state_)
        return;
    close_input();
#ifdef _WIN32
    close_handle(state_->output);
    if (state_->process)
    {
        if (WaitForSingleObject(state_->process, 0) == WAIT_TIMEOUT)
            TerminateProcess(state_->process, 1);
        CloseHandle(state_->process);
        state_->process = nullptr;
    }
#else
    close_descriptor(state_->output);
    if (state_->process > 0)
    {
        int status = 0;
        if (::waitpid(state_->process, &status, WNOHANG) == 0)
        {
            ::kill(state_->process, SIGTERM);
            bool exited = false;
            for (std::size_t attempt = 0; attempt < 50; ++attempt)
            {
                if (::waitpid(state_->process, &status, WNOHANG) != 0)
                {
                    exited = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!exited)
            {
                ::kill(state_->process, SIGKILL);
                (void)::waitpid(state_->process, &status, 0);
            }
        }
        state_->process = -1;
    }
#endif
    state_.reset();
}

auto child_process::running() const noexcept -> bool
{
    if (!state_)
        return false;
#ifdef _WIN32
    return state_->process &&
        WaitForSingleObject(state_->process, 0) == WAIT_TIMEOUT;
#else
    if (state_->process <= 0)
        return false;
    return ::kill(state_->process, 0) == 0;
#endif
}

} // namespace cnetmod
