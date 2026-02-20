/// cnetmod example — Async File I/O
/// 演示异步文件写入、读取、刷新

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

using namespace cnetmod;

auto run_file_demo(io_context& ctx) -> task<void> {
    constexpr auto path = "cnetmod_test_file.bin";

    // ---- 写入 ----
    std::println("  [1] Writing file...");
    {
        auto f = file::open(path, open_mode::write | open_mode::create | open_mode::truncate);
        if (!f) {
            std::println("  open(write) failed: {}", f.error().message());
            co_return;
        }

        std::string data = "Hello, cnetmod async file I/O!\nLine 2: C++23 modules are awesome.\n";
        auto wr = co_await async_file_write(ctx, *f,
            const_buffer{data.data(), data.size()}, 0);
        if (wr)
            std::println("  wrote {} bytes", *wr);
        else
            std::println("  write error: {}", wr.error().message());

        // 刷新
        auto fr = co_await async_file_flush(ctx, *f);
        if (fr)
            std::println("  flush OK");
        else
            std::println("  flush error: {}", fr.error().message());
    }

    // ---- 追加写入 ----
    std::println("  [2] Appending...");
    {
        auto f = file::open(path, open_mode::write | open_mode::create);
        if (!f) { std::println("  open(append) failed"); co_return; }

        // 获取文件大小以确定追加偏移
        auto sz = f->size();
        std::uint64_t offset = sz ? *sz : 0;

        std::string extra = "Line 3: appended content.\n";
        auto wr = co_await async_file_write(ctx, *f,
            const_buffer{extra.data(), extra.size()}, offset);
        if (wr)
            std::println("  appended {} bytes at offset {}", *wr, offset);

        (void)co_await async_file_flush(ctx, *f);
    }

    // ---- 读取全部 ----
    std::println("  [3] Reading back...");
    {
        auto f = file::open(path, open_mode::read);
        if (!f) { std::println("  open(read) failed"); co_return; }

        std::array<char, 4096> buf{};
        auto rr = co_await async_file_read(ctx, *f,
            mutable_buffer{buf.data(), buf.size()}, 0);
        if (rr) {
            std::string_view content(buf.data(), *rr);
            std::println("  read {} bytes:\n{}", *rr, content);
        } else {
            std::println("  read error: {}", rr.error().message());
        }
    }

    // 清理临时文件
    std::filesystem::remove(path);
    std::println("  Temp file removed.");
}

auto run_and_stop(io_context& ctx) -> task<void> {
    co_await run_file_demo(ctx);
    ctx.stop();
}

auto main() -> int {
    std::println("=== cnetmod: Async File I/O ===");

    auto ctx = make_io_context();
    spawn(*ctx, run_and_stop(*ctx));
    ctx->run();

    std::println("Done.");
    return 0;
}
