#include <gtest/gtest.h>

#include "GatingLogic.hpp"

#include <vector>

// ── GatingLogic::rms ────────────────────────────────────────────────────────

TEST(GatingLogic_Rms, KnownSignals)
{
    const std::vector<float> zeros(1000, 0.0f);
    EXPECT_FLOAT_EQ(GatingLogic::rms(zeros.data(), 1000), 0.0f);

    const std::vector<float> half(1000, 0.5f);
    EXPECT_NEAR(GatingLogic::rms(half.data(), 1000), 0.5f, 1e-6);

    EXPECT_FLOAT_EQ(GatingLogic::rms(nullptr, 0), 0.0f); // empty is safe
}

// ── Silence gate ────────────────────────────────────────────────────────────

TEST(GatingLogic_SilenceGate, WakesOnSoundHoldsThenSleeps)
{
    GatingLogic g;
    const int64_t t = 1'000'000; // large clock so the initial hold has expired

    EXPECT_FALSE(g.shouldEvaluate(1e-6f, t));       // silent → don't run
    EXPECT_TRUE(g.shouldEvaluate(1e-3f, t + 10));   // loud burst wakes it
    EXPECT_TRUE(g.shouldEvaluate(1e-3f, t + 20));   // stays awake while loud
    EXPECT_TRUE(g.shouldEvaluate(1e-6f, t + 30));   // just went quiet → held (holdMs)
    EXPECT_FALSE(g.shouldEvaluate(1e-6f, t + 430)); // hold expired → sleep
}

TEST(GatingLogic_SilenceGate, LoudSteadyInputStillEvaluates)
{
    // A loud but steady signal must not be mistaken for silence: the adaptive
    // floor rises but stays well below the signal.
    GatingLogic g;
    int64_t t    = 1'000'000;
    bool anyEval = false;
    for (int i = 0; i < 500; ++i)
    {
        anyEval |= g.shouldEvaluate(0.2f, t += 16);
    }
    EXPECT_TRUE(anyEval);
}

// ── Send gate ───────────────────────────────────────────────────────────────

TEST(GatingLogic_Send, ChangeOrKeepalive)
{
    GatingLogic g;
    const int64_t t = 1'000'000;

    EXPECT_TRUE(g.shouldSend(1.0f, t));       // first value always sends
    EXPECT_FALSE(g.shouldSend(1.0f, t + 10)); // unchanged, within keepalive
    EXPECT_TRUE(g.shouldSend(0.0f, t + 20));  // changed
    EXPECT_FALSE(g.shouldSend(0.0f, t + 30)); // unchanged
    EXPECT_TRUE(g.shouldSend(0.0f, t + 600)); // keepalive (> sendKeepaliveMs)
}

// ── ScoreLowPass ────────────────────────────────────────────────────────────

TEST(ScoreLowPass, ConvergesAndRejectsBriefSpikes)
{
    ScoreLowPass f;
    const float baji[5] = {1, 0, 0, 0, 0};
    for (int i = 0; i < 50; ++i)
    {
        f.update(baji, 5);
    }
    EXPECT_GT(f[0], 0.95f);
    EXPECT_LT(f[1], 0.05f);

    const float ru[5] = {0, 1, 0, 0, 0};
    f.update(ru, 5);       // a single ru frame
    EXPECT_GT(f[0], f[1]); // baji still dominant

    for (int i = 0; i < 50; ++i)
    {
        f.update(ru, 5);
    }
    EXPECT_GT(f[1], f[0]); // sustained ru takes over
}

TEST(ScoreLowPass, OutOfRangeIndexIsSafe)
{
    ScoreLowPass f;
    EXPECT_FLOAT_EQ(f[-1], 0.0f);
    EXPECT_FLOAT_EQ(f[999], 0.0f);
}

// ── Hysteresis ──────────────────────────────────────────────────────────────

TEST(Hysteresis, FlipsOnlyOnDecisiveCrossings)
{
    Hysteresis h({0.8f, 0.2f});
    EXPECT_FALSE(h.update(0.0f));
    EXPECT_FALSE(h.update(0.79f)); // below high → stays off
    EXPECT_TRUE(h.update(0.80f));  // reaches high → on
    EXPECT_TRUE(h.update(0.50f));  // in the band → holds on
    EXPECT_TRUE(h.update(0.21f));  // above low → holds on
    EXPECT_FALSE(h.update(0.20f)); // reaches low → off
    EXPECT_FALSE(h.update(0.79f)); // below high → holds off
    EXPECT_TRUE(h.update(0.90f));  // on again

    h.reset();
    EXPECT_FALSE(h.value());
}
