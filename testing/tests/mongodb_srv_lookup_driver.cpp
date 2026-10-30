import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    cnetmod::net_init net;
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    int outcome = 1;
    const std::string hostname{argv[1]};
    auto work = [&]() -> cnetmod::task<void>
    {
        auto records = co_await cnetmod::mongodb::system_srv_lookup(pool, *io, hostname);
        if (records)
        {
            auto seeds = cnetmod::mongodb::validate_srv_seedlist(hostname, std::move(*records));
            if (seeds)
            {
                std::println("SRV/TXT resolved and validated: {} seeds, TTL={}s", seeds->seeds.size(), seeds->ttl.count());
                outcome = 0;
            }
            else
                std::println("{}", seeds.error().message);
        }
        else
            std::println("{}", records.error().message);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
    return outcome;
}
