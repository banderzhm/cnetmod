#pragma once

namespace mongodb_example {
struct event_job
{
    std::size_t sequence_number{};
};

class worker_service
{
public:
    worker_service(event_repository& repository, cnetmod::channel<event_job>& jobs,
        std::size_t worker_id, std::string run_id, std::atomic_size_t& completed)
        : repository_(repository), jobs_(jobs), worker_id_(worker_id), run_id_(std::move(run_id)), completed_(completed) {}

    auto run() -> cnetmod::task<void>
    {
        while (auto job = co_await jobs_.receive())
            if (co_await repository_.record_processed(worker_id_, job->sequence_number, run_id_))
                completed_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    event_repository& repository_;
    cnetmod::channel<event_job>& jobs_;
    std::size_t worker_id_;
    std::string run_id_;
    std::atomic_size_t& completed_;
};
} // namespace mongodb_example
