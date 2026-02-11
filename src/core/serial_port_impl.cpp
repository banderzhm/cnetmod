module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cerrno>
#endif

module cnetmod.core.serial_port;

import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// 生命周期
// =============================================================================

serial_port::~serial_port() {
    close();
}

serial_port::serial_port(serial_port&& other) noexcept
    : handle_(other.handle_), config_(other.config_)
{
    other.handle_ = invalid_file_handle;
}

auto serial_port::operator=(serial_port&& other) noexcept -> serial_port& {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        config_ = other.config_;
        other.handle_ = invalid_file_handle;
    }
    return *this;
}

// =============================================================================
// 关闭
// =============================================================================

void serial_port::close() noexcept {
    if (handle_ == invalid_file_handle) return;
#ifdef CNETMOD_PLATFORM_WINDOWS
    ::CloseHandle(handle_);
#else
    ::close(handle_);
#endif
    handle_ = invalid_file_handle;
}

// =============================================================================
// 打开 — Windows
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS

auto serial_port::open(std::string_view name, const serial_config& config)
    -> std::expected<serial_port, std::error_code>
{
    // 构造设备路径: \\.\COMn
    std::string device_path;
    if (name.starts_with("\\\\.\\")) {
        device_path = std::string(name);
    } else {
        device_path = std::string("\\\\.\\") + std::string(name);
    }

    // 转宽字符
    int wlen = ::MultiByteToWideChar(CP_UTF8, 0,
        device_path.data(), static_cast<int>(device_path.size()),
        nullptr, 0);
    std::wstring wpath(static_cast<std::size_t>(wlen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0,
        device_path.data(), static_cast<int>(device_path.size()),
        wpath.data(), wlen);

    // 打开串口 (FILE_FLAG_OVERLAPPED 用于 IOCP 异步)
    HANDLE h = ::CreateFileW(
        wpath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,              // 不共享
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        int err = static_cast<int>(::GetLastError());
        return std::unexpected(make_error_code(from_native_error(err)));
    }

    // --- 配置 DCB ---
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);

    if (!::GetCommState(h, &dcb)) {
        int err = static_cast<int>(::GetLastError());
        ::CloseHandle(h);
        return std::unexpected(make_error_code(from_native_error(err)));
    }

    dcb.BaudRate = config.baud_rate;
    dcb.ByteSize = config.data_bits;

    // 停止位
    switch (config.stop) {
        case stop_bits::one:      dcb.StopBits = ONESTOPBIT;   break;
        case stop_bits::one_half: dcb.StopBits = ONE5STOPBITS; break;
        case stop_bits::two:      dcb.StopBits = TWOSTOPBITS;  break;
    }

    // 校验
    switch (config.par) {
        case parity::none:  dcb.Parity = NOPARITY;    dcb.fParity = FALSE; break;
        case parity::odd:   dcb.Parity = ODDPARITY;   dcb.fParity = TRUE;  break;
        case parity::even:  dcb.Parity = EVENPARITY;  dcb.fParity = TRUE;  break;
        case parity::mark:  dcb.Parity = MARKPARITY;  dcb.fParity = TRUE;  break;
        case parity::space: dcb.Parity = SPACEPARITY; dcb.fParity = TRUE;  break;
    }

    // 流控
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl  = RTS_CONTROL_DISABLE;
    dcb.fOutX        = FALSE;
    dcb.fInX         = FALSE;

    switch (config.flow) {
        case flow_control::none:
            break;
        case flow_control::hardware:
            dcb.fOutxCtsFlow = TRUE;
            dcb.fRtsControl  = RTS_CONTROL_HANDSHAKE;
            break;
        case flow_control::software:
            dcb.fOutX = TRUE;
            dcb.fInX  = TRUE;
            break;
    }

    // 二进制模式
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fAbortOnError = FALSE;

    if (!::SetCommState(h, &dcb)) {
        int err = static_cast<int>(::GetLastError());
        ::CloseHandle(h);
        return std::unexpected(make_error_code(from_native_error(err)));
    }

    // --- 超时设置 ---
    COMMTIMEOUTS timeouts{};
    // OVERLAPPED 模式下通常设 0 让 IOCP 管理
    // 但设置 ReadTotalTimeoutConstant 可以给读操作一个上限
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier  = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant    = config.read_timeout_ms > 0
                                           ? config.read_timeout_ms : MAXDWORD - 1;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = config.write_timeout_ms;

    if (!::SetCommTimeouts(h, &timeouts)) {
        int err = static_cast<int>(::GetLastError());
        ::CloseHandle(h);
        return std::unexpected(make_error_code(from_native_error(err)));
    }

    // 清空收发缓冲区
    ::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return serial_port{h, config};
}

#else

// =============================================================================
// 打开 — Linux / POSIX
// =============================================================================

namespace {

auto to_posix_baud(std::uint32_t baud) noexcept -> speed_t {
    switch (baud) {
        case 50:     return B50;
        case 75:     return B75;
        case 110:    return B110;
        case 134:    return B134;
        case 150:    return B150;
        case 200:    return B200;
        case 300:    return B300;
        case 600:    return B600;
        case 1200:   return B1200;
        case 1800:   return B1800;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default:     return B9600;
    }
}

} // anonymous namespace

auto serial_port::open(std::string_view name, const serial_config& config)
    -> std::expected<serial_port, std::error_code>
{
    std::string path(name);
    int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return std::unexpected(make_error_code(from_native_error(errno)));

    // 取消 O_NONBLOCK (后续由 epoll/io_uring 管理)
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    // --- termios 配置 ---
    struct termios tty{};
    if (::tcgetattr(fd, &tty) != 0) {
        int err = errno;
        ::close(fd);
        return std::unexpected(make_error_code(from_native_error(err)));
    }

    // 波特率
    auto baud = to_posix_baud(config.baud_rate);
    ::cfsetispeed(&tty, baud);
    ::cfsetospeed(&tty, baud);

    // 数据位
    tty.c_cflag &= ~CSIZE;
    switch (config.data_bits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        default: tty.c_cflag |= CS8; break;
    }

    // 校验
    switch (config.par) {
        case parity::none:
            tty.c_cflag &= ~PARENB;
            break;
        case parity::odd:
            tty.c_cflag |= PARENB | PARODD;
            break;
        case parity::even:
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            break;
        case parity::mark:
            tty.c_cflag |= PARENB | PARODD;
#ifdef CMSPAR
            tty.c_cflag |= CMSPAR;
#endif
            break;
        case parity::space:
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
#ifdef CMSPAR
            tty.c_cflag |= CMSPAR;
#endif
            break;
    }

    // 停止位
    switch (config.stop) {
        case stop_bits::one:
        case stop_bits::one_half: // POSIX 不支持 1.5, fallback 1
            tty.c_cflag &= ~CSTOPB;
            break;
        case stop_bits::two:
            tty.c_cflag |= CSTOPB;
            break;
    }

    // 流控
    switch (config.flow) {
        case flow_control::none:
            tty.c_cflag &= ~CRTSCTS;
            tty.c_iflag &= ~(IXON | IXOFF | IXANY);
            break;
        case flow_control::hardware:
            tty.c_cflag |= CRTSCTS;
            tty.c_iflag &= ~(IXON | IXOFF | IXANY);
            break;
        case flow_control::software:
            tty.c_cflag &= ~CRTSCTS;
            tty.c_iflag |= IXON | IXOFF;
            break;
    }

    // 启用接收, 本地模式
    tty.c_cflag |= CLOCAL | CREAD;

    // 原始模式 (无回显, 无信号, 无规范处理)
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    // 最小字符数和超时
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = config.read_timeout_ms > 0
                       ? static_cast<cc_t>(config.read_timeout_ms / 100)
                       : 0;

    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        int err = errno;
        ::close(fd);
        return std::unexpected(make_error_code(from_native_error(err)));
    }

    // 清空收发缓冲区
    ::tcflush(fd, TCIOFLUSH);

    return serial_port{fd, config};
}

#endif // platform

} // namespace cnetmod
