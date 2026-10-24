#include "sim/runtime/runner.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <type_traits>
using namespace sim;
using namespace std::chrono_literals;

TEST(Queue, RejectsOverflowPreservesFifoAndDrainsAfterClose) {
    runtime::BoundedQueue<int> queue{2};
    EXPECT_TRUE(queue.try_push(1));
    EXPECT_TRUE(queue.try_push(2));
    EXPECT_FALSE(queue.try_push(3));
    queue.close();
    EXPECT_FALSE(queue.try_push(4));
    EXPECT_EQ(queue.pop({}), 1);
    EXPECT_EQ(queue.pop({}), 2);
    EXPECT_FALSE(queue.pop({}));
}
TEST(Queue, StopWakesBlockedConsumerWithoutSleep) {
    runtime::BoundedQueue<int> queue{1};
    std::promise<void> entered;
    std::promise<bool> finished;
    auto done = finished.get_future();
    std::jthread thread([&](std::stop_token stop) {
        entered.set_value();
        finished.set_value(!queue.pop(stop).has_value());
    });
    entered.get_future().wait();
    thread.request_stop();
    ASSERT_EQ(done.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(done.get());
}
TEST(Queue, ZeroCapacityIsRejected) {
    EXPECT_THROW(runtime::BoundedQueue<int>{0}, std::invalid_argument);
}
TEST(Runner, DedicatedExecutionMatchesPureCoreAndPublishesConstData) {
    static_assert(std::is_const_v<runtime::ResultPtr::element_type>);
    runtime::Runner runner;
    const auto expected = core::simulate(core::demo_scenario());
    for (int i = 0; i < 20; ++i) {
        auto result = runner.submit(core::demo_scenario());
        ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
        EXPECT_EQ(*result.get(), expected);
    }
}
TEST(Runner, InvalidSubmissionDoesNotPoisonScheduler) {
    runtime::Runner runner;
    auto invalid = core::demo_scenario();
    invalid.ticks = 0;
    EXPECT_THROW(runner.submit(invalid), std::invalid_argument);
    EXPECT_NO_THROW(runner.submit(core::demo_scenario()).get());
}
TEST(Runner, DestructionSettlesAllAcceptedFutures) {
    std::vector<std::future<runtime::ResultPtr>> futures;
    {
        runtime::Runner runner{8};
        auto spec = core::demo_scenario();
        spec.ticks = 100000;
        for (int i = 0; i < 8; ++i) { futures.push_back(runner.submit(spec)); }
    }
    for (auto& result : futures) {
        ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
        try { EXPECT_FALSE(result.get()->trace.empty()); }
        catch (const core::RunCancelled&) { SUCCEED(); }
    }
}
TEST(Runner, CapacityIsBounded) {
    EXPECT_THROW(runtime::Runner{0}, std::invalid_argument);
    EXPECT_THROW(runtime::Runner{9}, std::invalid_argument);
}
