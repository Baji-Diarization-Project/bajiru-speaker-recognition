#include <gtest/gtest.h>

#include "CadenceGate.h"

// windowSamples, hopSamples, requireFullWindow.

TEST(CadenceGate, WarmupWithholdsUntilFullWindow)
{
    CadenceGate<> g{100, 10, /*requireFullWindow=*/true};
    EXPECT_FALSE(g.poll(50).evaluate); // fewer than window samples written
    EXPECT_FALSE(g.poll(99).evaluate);
    EXPECT_TRUE(g.poll(100).evaluate); // window reached
}

TEST(CadenceGate, HopPacing)
{
    CadenceGate<> g{10, 10, /*requireFullWindow=*/false};
    EXPECT_TRUE(g.poll(10).evaluate);  // first eval → schedules next at +hop
    EXPECT_FALSE(g.poll(15).evaluate); // not enough new samples yet
    EXPECT_TRUE(g.poll(20).evaluate);  // hop reached
    EXPECT_FALSE(g.poll(25).evaluate);
    EXPECT_TRUE(g.poll(30).evaluate);
}

TEST(CadenceGate, ContinuousModeEvaluatesEveryTick)
{
    CadenceGate<> g{10, 0, /*requireFullWindow=*/false}; // hop 0 = continuous
    EXPECT_TRUE(g.poll(5).evaluate);
    EXPECT_TRUE(g.poll(6).evaluate);
    EXPECT_TRUE(g.poll(7).evaluate);
}

TEST(CadenceGate, DetectsSkipWhenBehindByMoreThanOneHop)
{
    CadenceGate<> g{10, 10, /*requireFullWindow=*/false};
    EXPECT_FALSE(g.poll(10).skipped); // first eval is never "skipped"

    const auto d = g.poll(100); // jumped far ahead: many hops missed
    EXPECT_TRUE(d.evaluate);
    EXPECT_TRUE(d.skipped);
}

TEST(CadenceGate, OnTimeIsNotFlaggedAsSkipped)
{
    CadenceGate<> g{10, 10, /*requireFullWindow=*/false};
    EXPECT_FALSE(g.poll(10).skipped);
    EXPECT_FALSE(g.poll(20).skipped); // exactly one hop later
    EXPECT_FALSE(g.poll(30).skipped);
}
