namespace {

class confirmation_observer final : public cnetmod::amqp091::publisher_confirm_observer
{
public:
    unsigned confirmed = 0;
    unsigned failed = 0;
    bool throws = false;
    std::function<void()> during_failure;

    void on_confirm(const cnetmod::amqp091::publisher_confirmation&) override
    {
        ++confirmed;
        if (throws)
            throw std::runtime_error("observer failure");
    }

    void on_confirm_failure(const cnetmod::amqp091::error&) override
    {
        ++failed;
        if (during_failure)
            during_failure();
        if (throws)
            throw std::runtime_error("observer failure");
    }
};

} // namespace

TEST(amqp091_confirmation_callbacks_are_isolated_and_reentrant)
{
    cnetmod::amqp091::publisher_confirm_tracker tracker;
    auto first = std::make_shared<confirmation_observer>();
    auto second = std::make_shared<confirmation_observer>();
    auto late = std::make_shared<confirmation_observer>();
    first->throws = true;
    tracker.observe(first);
    tracker.observe(second);
    auto sequence = tracker.reserve_sequence();
    ASSERT_EQ(sequence, 1U);
    ASSERT_THROWS(tracker.settle(sequence, true, false));
    ASSERT_EQ(first->confirmed, 1U);
    ASSERT_EQ(second->confirmed, 1U);
    ASSERT_EQ(tracker.pending(), 0U);
    first->during_failure = [&]
    {
        ASSERT_EQ(tracker.pending(), 0U);
        tracker.observe(late);
    };
    ASSERT_EQ(tracker.reserve_sequence(), 2U);
    tracker.fail_all({.code = cnetmod::amqp091::error_code::connection_closed});
    ASSERT_EQ(first->failed, 1U);
    ASSERT_EQ(second->failed, 1U);
    ASSERT_EQ(late->failed, 0U);
    first->during_failure = {};
    tracker.fail_all({.code = cnetmod::amqp091::error_code::connection_closed});
    ASSERT_EQ(late->failed, 1U);
}
