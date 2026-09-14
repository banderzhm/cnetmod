module cnetmod.protocol.kafka.kafka_consumer;
import std;
import cnetmod.coro.mutex;
import cnetmod.protocol.kafka.protocol_constants;

namespace cnetmod::kafka {
class consumer::impl
{
public:
    impl(std::shared_ptr<consumer_backend> b, consumer_options o)
        : backend(std::move(b)), options(std::move(o)) {}

    std::shared_ptr<consumer_backend> backend;
    consumer_options options;
    bool closing = false;
    bool closed = false;
    async_mutex close_mutex;
};

consumer::consumer(std::shared_ptr<consumer_backend> b, consumer_options o)
    : impl_(std::make_unique<impl>(std::move(b), std::move(o))) {}

consumer::~consumer() = default;
consumer::consumer(consumer&&) noexcept = default;
auto consumer::operator=(consumer&&) noexcept -> consumer& = default;

auto consumer::subscribe(std::vector<std::string> t, cancel_token* c)
    -> task<result<void>>
{
    if (impl_->closing)
        co_return std::unexpected(make_error(error_code::configuration, "consumer is closed"));
    co_return co_await impl_->backend->subscribe(t, c);
}

auto consumer::assign(std::vector<topic_partition> p, cancel_token* c)
    -> task<result<void>>
{
    if (impl_->closing)
        co_return std::unexpected(make_error(error_code::configuration, "consumer is closed"));
    co_return co_await impl_->backend->assign(p, c);
}

auto consumer::poll(cancel_token* c)
    -> task<result<std::vector<consumed_record>>>
{
    if (impl_->closing)
        co_return std::unexpected(make_error(error_code::configuration, "consumer is closed"));
    co_return co_await impl_->backend->poll(impl_->options.max_poll_records, c);
}

auto consumer::assignment() const -> std::vector<topic_partition>
{
    return impl_->backend->assignment();
}

auto consumer::commit(const consumed_record& r, cancel_token* c)
    -> task<result<void>>
{
    if (impl_->closing)
        co_return std::unexpected(make_error(error_code::configuration, "consumer is closed"));
    std::map<topic_partition, offset_and_metadata> m{
        {r.source, {r.offset + 1, r.leader_epoch, {}}}};
    co_return co_await impl_->backend->commit(m, c);
}

auto consumer::seek(topic_partition t, std::int64_t o, cancel_token* c)
    -> task<result<void>>
{
    if (impl_->closing)
        co_return std::unexpected(make_error(error_code::configuration, "consumer is closed"));
    co_return co_await impl_->backend->seek(t, o, c);
}

auto consumer::close(cancel_token* c) -> task<result<void>>
{
    impl_->closing = true;
    co_await impl_->close_mutex.lock();
    async_lock_guard guard{impl_->close_mutex, std::adopt_lock};
    if (impl_->closed)
        co_return result<void>{};
    auto outcome = co_await impl_->backend->close(c);
    if (outcome)
        impl_->closed = true;
    co_return outcome;
}
} // namespace cnetmod::kafka
