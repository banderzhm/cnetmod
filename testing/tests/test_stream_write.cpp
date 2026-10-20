#include <cnetmod/config.hpp>
#ifdef CNETMOD_PLATFORM_LINUX
#include <sys/socket.h>
#include <cerrno>
#include <pthread.h>
#include <sched.h>
#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)
#include <liburing.h>
#endif
#endif

import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.coro.wait_group;
#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && defined(IORING_RECV_MULTISHOT)
import cnetmod.io.platform.io_uring;
import cnetmod.io.platform.io_uring_recv_service;
import cnetmod.io.platform.io_uring_multishot_recv;
#endif

namespace {
std::size_t dispatch_failures{};

void observe_dispatch_failure(std::exception_ptr error)
{
    if (error)
        ++dispatch_failures;
}

auto mark_started(bool& started) -> cnetmod::task<void>
{
    started = true;
    co_return;
}

auto fail_dispatched() -> cnetmod::task<void>
{
    throw std::runtime_error("expected dispatch failure");
    co_return;
}

void require(bool value, std::string_view message)
{
    if (!value)
        throw std::runtime_error(std::string(message));
}

#ifdef CNETMOD_PLATFORM_LINUX
auto check_worker_processor(unsigned expected, bool& valid,
    cnetmod::async_wait_group& completed) -> cnetmod::task<void>
{
    cpu_set_t actual;
    CPU_ZERO(&actual);
    valid = ::pthread_getaffinity_np(::pthread_self(), sizeof(actual), &actual) == 0 &&
        CPU_COUNT(&actual) == 1 && CPU_ISSET(expected, &actual);
    completed.done();
    co_return;
}

void check_auto_affinity()
{
    cpu_set_t original;
    require(::pthread_getaffinity_np(::pthread_self(), sizeof(original), &original) == 0,
        "query eligible processors");
    std::vector<unsigned> eligible;
    for (unsigned i = 0; i < CPU_SETSIZE; ++i)
        if (CPU_ISSET(i, &original))
            eligible.push_back(i);
    require(!eligible.empty(), "eligible processor list");
    struct restore_affinity
    {
        cpu_set_t original;
        ~restore_affinity() { (void)::pthread_setaffinity_np(::pthread_self(), sizeof(original), &original); }
    } restore{original};
    cpu_set_t sparse;
    CPU_ZERO(&sparse);
    CPU_SET(eligible.front(), &sparse);
    CPU_SET(eligible.back(), &sparse);
    require(::pthread_setaffinity_np(::pthread_self(), sizeof(sparse), &sparse) == 0,
        "select sparse eligible processors");
    cnetmod::server_context context(2, 1, {.enabled = true, .auto_assign_workers = true});
    std::array<bool, 2> valid{};
    cnetmod::async_wait_group completed;
    completed.add(2);
    const auto work = [&]() -> cnetmod::task<void>
    {
        auto workers = context.worker_ios();
        cnetmod::spawn(*workers[0], check_worker_processor(eligible.front(), valid[0], completed));
        cnetmod::spawn(*workers[1], check_worker_processor(eligible.back(), valid[1], completed));
        co_await completed.wait();
        context.stop();
    };
    cnetmod::spawn(context.accept_io(), work());
    context.run();
    require(valid[0] && valid[1], "automatic affinity honors sparse calling-thread mask");
}

auto pair() -> std::array<cnetmod::socket, 2>
{
    int descriptors[2];
    require(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0, descriptors) == 0, "socketpair");
    const int size = 4096;
    require(::setsockopt(descriptors[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0,
        "bounded send buffer");
    return {cnetmod::socket::from_native(descriptors[0]),
        cnetmod::socket::from_native(descriptors[1])};
}

auto run(cnetmod::io_context& io) -> cnetmod::task<void>
{
    bool immediate = false;
    cnetmod::dispatch_guarded<observe_dispatch_failure>(io, mark_started(immediate));
    require(immediate, "owner dispatch starts inline");
    cnetmod::dispatch_guarded<observe_dispatch_failure>(io, fail_dispatched());
    require(dispatch_failures == 1, "dispatch observes exceptions once");
    bool deferred = false;
    cnetmod::spawn_guarded<observe_dispatch_failure>(io, mark_started(deferred));
    require(!deferred, "spawn retains deferred ordering");
    while (!deferred)
        (void)co_await cnetmod::async_sleep(io, std::chrono::milliseconds{1});
    auto sockets = pair();
    const std::string byte = "x";
    cnetmod::cancel_token cancelled;
    cancelled.cancel();
    auto rejected = co_await cnetmod::async_write(io, sockets[0],
        cnetmod::const_buffer{byte.data(), byte.size()}, cancelled);
    require(!rejected && rejected.error() == cnetmod::make_error_code(cnetmod::errc::operation_aborted),
        "pre-cancelled write");
    char probe;
    require(::recv(sockets[1].native_handle(), &probe, 1, MSG_DONTWAIT) < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK), "cancelled write sent no bytes");

    std::vector<std::byte> payload(2 * 1024 * 1024);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::byte>(i % 251);
    auto first = co_await cnetmod::async_write(io, sockets[0],
        cnetmod::const_buffer{payload.data(), payload.size()});
    require(first && *first > 0 && *first < payload.size(), "partial send is reported");

    bool reader_done = false;
    std::exception_ptr reader_error;
    const auto read_all = [&]() -> cnetmod::task<void>
    {
        try
        {
            std::array<std::byte, 16384> chunk;
            std::size_t total = 0;
            while (total < payload.size())
            {
                const auto read = co_await cnetmod::async_read(io, sockets[1], cnetmod::buffer(chunk));
                require(read && *read > 0, "read after backpressure");
                require(std::equal(chunk.begin(), chunk.begin() + *read, payload.begin() + total),
                    "partial sends preserve exact byte order");
                total += *read;
            }
        }
        catch (...)
        {
            reader_error = std::current_exception();
        }
        reader_done = true;
    };
    cnetmod::spawn(io, read_all());
    const auto rest = co_await cnetmod::async_write_all(io, sockets[0],
        cnetmod::const_buffer{payload.data() + *first, payload.size() - *first});
    require(static_cast<bool>(rest), "backpressured write_all completes");
    while (!reader_done)
        (void)co_await cnetmod::async_sleep(io, std::chrono::milliseconds{1});
    if (reader_error)
        std::rethrow_exception(reader_error);

    // Force a pending write without a reader, then cancel after suspension.
    auto stalled = pair();
    const std::array<std::byte, 4096> fill{};
    while (::send(stalled[0].native_handle(), fill.data(), fill.size(), MSG_DONTWAIT | MSG_NOSIGNAL) > 0) {}
    cnetmod::cancel_token pending;
    bool timer_done = false;
    const auto cancel_later = [&]() -> cnetmod::task<void>
    {
        (void)co_await cnetmod::async_sleep(io, std::chrono::milliseconds{5});
        pending.cancel();
        timer_done = true;
    };
    cnetmod::spawn(io, cancel_later());
    const auto interrupted = co_await cnetmod::async_write(io, stalled[0],
        cnetmod::const_buffer{byte.data(), byte.size()}, pending);
    require(!interrupted && interrupted.error() == cnetmod::make_error_code(cnetmod::errc::operation_aborted),
        "pending write cancellation");
    require(timer_done, "cancellation timer retired");
    sockets[1].close();
    const auto broken = co_await cnetmod::async_write(io, sockets[0],
        cnetmod::const_buffer{byte.data(), byte.size()});
    require(!broken, "closed peer reports an error without SIGPIPE");
#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && defined(IORING_RECV_MULTISHOT)
    // EOF resumes the owner from inside the terminal completion callback.
    // Destroying the joined receiver here must not leave callback member reads.
    auto ending = pair();
    cnetmod::io_uring_recv_service service(
        static_cast<cnetmod::io_uring_context&>(io), 62000, 4096, 2);
    auto receiver = service.make_receiver(ending[0]);
    require(::send(ending[1].native_handle(), "hello", 5, MSG_NOSIGNAL) == 5,
        "multishot terminal probe send");
    ending[1].close();
    {
        auto message = co_await receiver->async_next();
        require(message && message->size() == 5, "multishot terminal probe data");
    }
    auto eof = co_await receiver->async_next();
    require(eof && eof->size() == 0, "multishot terminal probe EOF");
    require(static_cast<bool>(co_await receiver->async_stop()), "join terminal receiver");
    receiver.reset();
#endif
}
#endif
}

auto main() -> int
{
    cnetmod::net_init net;
    int result = 0;
#ifdef CNETMOD_PLATFORM_LINUX
    try
    {
        check_auto_affinity();
    }
    catch (const std::exception& error)
    {
        logger::error{"{}", error.what()};
        logger::shutdown();
        return 1;
    }
    auto io = cnetmod::make_io_context();
    bool outside_started = false;
    cnetmod::dispatch_guarded<observe_dispatch_failure>(*io, mark_started(outside_started));
    require(!outside_started, "foreign dispatch remains queued");
    const auto work = [&]() -> cnetmod::task<void>
    {
        try
        {
            co_await run(*io);
            logger::info{"stream partial writes, backpressure, cancellation and closed peer passed"};
        }
        catch (const std::exception& error)
        {
            logger::error{"{}", error.what()};
            result = 1;
        }
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
#endif
    logger::shutdown();
    return result;
}
