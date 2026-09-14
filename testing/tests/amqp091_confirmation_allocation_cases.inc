namespace {

class allocation_confirmation_observer final : public cnetmod::amqp091::publisher_confirm_observer
{
public:
    unsigned failures = 0;

    void on_confirm(const cnetmod::amqp091::publisher_confirmation&) override {}

    void on_confirm_failure(const cnetmod::amqp091::error&) override
    {
        ++failures;
    }
};

} // namespace

TEST(amqp091_confirmation_reservation_allocation_failure_preserves_sequence)
{
    cnetmod::amqp091::publisher_confirm_tracker tracker;
    bool rejected = false;
    fail_after = 0;
    try
    {
        (void)tracker.reserve_sequence();
    }
    catch (const std::bad_alloc&)
    {
        rejected = true;
    }
    fail_after.reset();
    ASSERT_TRUE(rejected);
    ASSERT_EQ(tracker.pending(), 0U);
    ASSERT_EQ(tracker.reserve_sequence(), 1U);
    ASSERT_EQ(tracker.pending(), 1U);
}

TEST(amqp091_confirmation_registration_allocation_failure_preserves_snapshot)
{
    bool reached_success = false;
    unsigned failures = 0;
    for (std::size_t index = 0; index != 8 && !reached_success; ++index)
    {
        cnetmod::amqp091::publisher_confirm_tracker tracker;
        auto retained = std::make_shared<allocation_confirmation_observer>();
        auto candidate = std::make_shared<allocation_confirmation_observer>();
        tracker.observe(retained);
        bool rejected = false;
        fail_after = index;
        try
        {
            tracker.observe(candidate);
        }
        catch (const std::bad_alloc&)
        {
            rejected = true;
        }
        fail_after.reset();
        tracker.fail_all({.code = cnetmod::amqp091::error_code::connection_closed});
        ASSERT_EQ(retained->failures, 1U);
        ASSERT_EQ(candidate->failures, rejected ? 0U : 1U);
        if (rejected)
            ++failures;
        else
            reached_success = true;
    }
    ASSERT_TRUE(reached_success);
    ASSERT_GT(failures, 0U);
}

TEST(amqp091_confirmation_failure_notification_does_not_allocate)
{
    cnetmod::amqp091::publisher_confirm_tracker tracker;
    auto observer = std::make_shared<allocation_confirmation_observer>();
    tracker.observe(observer);
    (void)tracker.reserve_sequence();
    const cnetmod::amqp091::error reason{.code = cnetmod::amqp091::error_code::connection_closed};
    fail_after = 0;
    tracker.fail_all(reason);
    const bool allocation_not_attempted = fail_after && *fail_after == 0;
    fail_after.reset();
    ASSERT_TRUE(allocation_not_attempted);
    ASSERT_EQ(tracker.pending(), 0U);
    ASSERT_EQ(observer->failures, 1U);
}
