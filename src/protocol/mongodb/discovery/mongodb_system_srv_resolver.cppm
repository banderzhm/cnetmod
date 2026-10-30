export module cnetmod.protocol.mongodb:system_srv_resolver;
import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :error;
import :srv_seedlist;

export namespace cnetmod::mongodb {
/// Uses the OS resolver off the I/O thread; the caller owns pool/io lifetimes.
auto system_srv_lookup(thread_pool& pool, io_context& io, std::string hostname,
    std::string service = "mongodb") -> task<result<srv_dns_records>>;
} // namespace cnetmod::mongodb
