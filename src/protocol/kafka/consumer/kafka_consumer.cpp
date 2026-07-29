module cnetmod.protocol.kafka.kafka_consumer;
import std;

namespace cnetmod::kafka {
class consumer::impl
{
public:
    impl(std::shared_ptr<consumer_backend> b, consumer_options o)
        : backend(std::move(b)), options(std::move(o)) {}

    std::shared_ptr<consumer_backend> backend;
    consumer_options options;
};

consumer::consumer(std::shared_ptr<consumer_backend> b, consumer_options o)
    : impl_(std::make_unique<impl>(std::move(b), std::move(o))) {}

consumer::~consumer() = default;
consumer::consumer(consumer&&) noexcept = default;
auto consumer::operator=(consumer&&) noexcept -> consumer& = default;

auto consumer::subscribe(std::vector<std::string> t, cancel_token* c)
    -> task<result<void>>
{
    co_return co_await impl_->backend->subscribe(t, c);
}

auto consumer::assign(std::vector<topic_partition> p, cancel_token* c)
    -> task<result<void>>
{
    co_return co_await impl_->backend->assign(p, c);
}

auto consumer::poll(cancel_token* c)
    -> task<result<std::vector<consumed_record>>>
{
    co_return co_await impl_->backend->poll(impl_->options.max_poll_records, c);
}

auto consumer::assignment() const -> std::vector<topic_partition>
{
    return impl_->backend->assignment();
}

auto consumer::commit(const consumed_record& r, cancel_token* c)
    -> task<result<void>>
{
    std::map<topic_partition, offset_and_metadata> m{
        {r.source, {r.offset + 1, r.leader_epoch, {}}}};
    co_return co_await impl_->backend->commit(m, c);
}

auto consumer::seek(topic_partition t, std::int64_t o, cancel_token* c)
    -> task<result<void>>
{
    co_return co_await impl_->backend->seek(t, o, c);
}

auto consumer::close(cancel_token* c) -> task<result<void>>
{
    co_return co_await impl_->backend->close(c);
}
} // namespace cnetmod::kafka
