#include <cnetmod/config.hpp>

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)
#include <liburing.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

import std;

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.platform.io_uring;
import cnetmod.io.platform.io_uring_recv_service;
import cnetmod.protocol.http;
#endif

namespace cn = cnetmod;

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)

auto verify_recv(cn::io_uring_context& context,
                 cn::io_uring_recv_service& service,
                 cn::socket& receiver,
                 int sender_fd,
                 bool& passed) -> cn::task<void>
{
    auto stream = service.make_receiver(receiver);
    constexpr std::string_view payload =
        "GET /uring HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (::send(sender_fd, payload.data(), payload.size(), 0) !=
        static_cast<ssize_t>(payload.size())) {
        context.stop();
        co_return;
    }

    auto received = co_await stream->async_next();
    if (received && received->size() == payload.size()) {
        const auto view = received->data();
        cnetmod::http::request_parser parser;
        auto parsed = parser.consume(static_cast<const char*>(view.data), view.size);
        passed = parsed && parser.ready() && parser.method() == "GET" &&
                 parser.uri() == "/uring";
    }
    if (received)
        received->release();
    (void)co_await stream->async_stop();
    context.stop();
}

int main() {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0)
        return 2;

    try {
        cn::io_uring_context context;
        cn::io_uring_recv_service service(context, 1, 4096, 16);
        auto receiver = cn::socket::from_native(fds[0]);
        bool passed = false;
        cn::spawn(context, verify_recv(context, service, receiver, fds[1], passed));
        context.run();
        ::close(fds[1]);
        std::println("io_uring multishot HTTP recv: {}", passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        ::close(fds[0]);
        ::close(fds[1]);
        std::println("io_uring multishot HTTP recv: FAIL ({})", error.what());
        return 1;
    }
}

#else

int main() {
    std::println("io_uring multishot HTTP recv: SKIP (unsupported build)");
    return 0;
}

#endif
