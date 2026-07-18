#include <gtest/gtest.h>

#include "Resampler.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

// Resampler quality: the artifacts a bare interpolator introduces. We drive the
// production Resampler class (host<->48k) with pure tones and measure how far the
// output drifts from a clean sine: fundamental droop, distortion residual, and
// aliasing when downsampling. These are the numbers the "beef up" has to beat, so
// the thresholds are quality targets, not just "current behaviour".

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> makeSine(const double freq, const double rate, const int n, const float amp = 0.5f)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        v[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(2.0 * kPi * freq * i / rate));
    }
    return v;
}

// Quadrature fit of a single tone: the in-phase / quadrature components at `freq`,
// from which both the amplitude and a clean reference sinusoid can be rebuilt.
struct ToneFit
{
    double amp = 0.0, inPhase = 0.0, quad = 0.0;
};

ToneFit fitTone(const float* x, const int n, const double freq, const double rate)
{
    const double w = 2.0 * kPi * freq / rate;
    double iAcc = 0.0, qAcc = 0.0;
    for (int i = 0; i < n; ++i)
    {
        iAcc += static_cast<double>(x[i]) * std::cos(w * i);
        qAcc += static_cast<double>(x[i]) * std::sin(w * i);
    }
    iAcc *= 2.0 / n;
    qAcc *= 2.0 / n;
    return {std::sqrt(iAcc * iAcc + qAcc * qAcc), iAcc, qAcc};
}

// RMS of what's left after subtracting the fitted tone: the distortion + noise +
// aliasing the interpolator added (phase handled by the fit, so this isn't just delay).
double residualRms(const float* x, const int n, const double freq, const double rate, const ToneFit& f)
{
    const double w = 2.0 * kPi * freq / rate;
    double sumSq   = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double fitted = f.inPhase * std::cos(w * i) + f.quad * std::sin(w * i);
        const double e      = static_cast<double>(x[i]) - fitted;
        sumSq += e * e;
    }
    return std::sqrt(sumSq / n);
}

double db(const double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1e-12));
}

// Steady-state tone measurement: fundamental amplitude + distortion floor over the
// middle of the signal, skipping a guard at each end so the interpolator's startup
// ramp and accumulated group delay don't pollute the reading.
struct Meas
{
    double amp         = 0.0; // fundamental amplitude
    double residualRel = 0.0; // residual RMS relative to the fundamental (THD+N)
};

Meas measure(const std::vector<float>& x, const double freq, const double rate, const double guardFrac = 0.1)
{
    const int n     = static_cast<int>(x.size());
    const int guard = static_cast<int>(n * guardFrac);
    const int m     = n - 2 * guard;
    const float* p  = x.data() + guard;
    const ToneFit f = fitTone(p, m, freq, rate);
    return {f.amp, residualRms(p, m, freq, rate, f) / f.amp};
}

// Resample a whole mono block fromRate -> toRate via the production Resampler.
std::vector<float> resampleFull(const double fromRate, const double toRate, const std::vector<float>& in)
{
    Resampler r(static_cast<int>(toRate), static_cast<int>(in.size()) + 8);
    r.prepare(fromRate);
    const auto b = r.process(in.data(), static_cast<int>(in.size()));
    return std::vector<float>(b.data, b.data + b.numSamples);
}

// One generation of loss: down to 44.1k and back to 48k.
std::vector<float> roundTrip48(const std::vector<float>& sig48)
{
    return resampleFull(44100.0, 48000.0, resampleFull(48000.0, 44100.0, sig48));
}
} // namespace

// Report whether the AVX dot path is active on this machine (scalar fallback is
// still correct; the quality tests below run whichever path is selected).
TEST(ResamplerQuality, SimdPathReport)
{
    Resampler r(48000, 4096);
    r.prepare(44100.0);
    std::printf("  [simd] AVX dot in use: %s\n", r.usesSimd() ? "yes" : "no");
    SUCCEED();
}

// A single 48k -> 44.1k -> 48k round trip should barely touch a mid-band tone.
TEST(ResamplerQuality, RoundTripPreservesMidBandTone)
{
    constexpr double freq = 1000.0;
    const auto ref        = makeSine(freq, 48000.0, 48000);
    const double a0       = measure(ref, freq, 48000.0).amp;

    const Meas m      = measure(roundTrip48(ref), freq, 48000.0);
    const double drop = db(m.amp / a0);
    const double thdn = db(m.residualRel);

    std::printf("  [round-trip 1kHz] droop=%.3f dB  residual=%.1f dB\n", drop, thdn);
    EXPECT_GT(drop, -0.05) << "1 kHz should survive one round trip nearly untouched";
    EXPECT_LT(thdn, -90.0) << "one round trip should add almost no distortion at 1 kHz";
}

// Generation loss: the tone through many round trips. Droop and distortion must
// stay bounded: a bare interpolator's HF roll-off compounds each pass.
TEST(ResamplerQuality, GenerationLoss)
{
    constexpr double freq     = 8000.0; // high enough that interpolation roll-off shows
    constexpr int generations = 10;

    const auto ref  = makeSine(freq, 48000.0, 48000);
    const double a0 = measure(ref, freq, 48000.0).amp;

    std::vector<float> sig = ref;
    std::printf("  [generation loss @ %.0f Hz]\n", freq);
    for (int g = 1; g <= generations; ++g)
    {
        sig           = roundTrip48(sig);
        const Meas mg = measure(sig, freq, 48000.0);
        if (g == 1 || g == 5 || g == generations)
        {
            std::printf("    gen %2d: droop=%7.3f dB  residual=%6.1f dB\n", g, db(mg.amp / a0), db(mg.residualRel));
        }
    }

    const Meas m      = measure(sig, freq, 48000.0);
    const double drop = db(m.amp / a0);
    const double thdn = db(m.residualRel);

    // Targets for a resampler good enough to survive repeated conversion:
    EXPECT_GT(drop, -0.1) << "8 kHz lost more than 0.1 dB over " << generations << " round trips";
    EXPECT_LT(thdn, -80.0) << "distortion floor rose above -80 dB after " << generations << " round trips";
}

// Passband flatness: map the roll-off across the spectrum for one round trip. Core
// speech band (<= 4 kHz) must stay flat; higher bins are characterised, not gated.
TEST(ResamplerQuality, PassbandFlatness)
{
    std::printf("  [passband — one 48->44.1->48 round trip]\n");
    for (const double freq : {100.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0, 16000.0, 20000.0})
    {
        const auto ref    = makeSine(freq, 48000.0, 48000);
        const double a0   = measure(ref, freq, 48000.0).amp;
        const double drop = db(measure(roundTrip48(ref), freq, 48000.0).amp / a0);
        std::printf("    %6.0f Hz: %7.3f dB\n", freq, drop);

        if (freq <= 4000.0)
        {
            EXPECT_GT(drop, -0.05) << freq << " Hz droops too much for the speech band";
        }
    }
}

// Downsampling aliasing: 96 kHz -> 48 kHz with a tone above the 48 kHz Nyquist.
// A 30 kHz tone folds to 18 kHz; a proper anti-alias filter suppresses it, a bare
// interpolator lets it through. This is the artifact the beef-up must address.
TEST(ResamplerQuality, DownsamplingRejectsAliasing)
{
    constexpr double inFreq    = 30000.0; // valid at 96k, above 48k's 24k Nyquist
    constexpr double aliasFreq = 18000.0; // |30000 - 48000| after decimation to 48k
    constexpr float amp        = 0.5f;

    const auto ref          = makeSine(inFreq, 96000.0, 96000, amp);
    const auto out          = resampleFull(96000.0, 48000.0, ref);
    const double aliasLevel = db(measure(out, aliasFreq, 48000.0).amp / amp);

    std::printf("  [downsample alias] 30kHz -> 18kHz fold: %.1f dB below input\n", aliasLevel);
    EXPECT_LT(aliasLevel, -70.0) << "aliased image only " << aliasLevel << " dB down — anti-alias filter too weak";
}

// The plugin feeds the resampler in small, ragged audio blocks; the per-call state
// (filter history + commutator phase) must make that seamless. Resampling a tone in
// varying chunks has to stay a clean tone; any discontinuity at a block seam shows
// up as broadband residual.
TEST(ResamplerQuality, StreamingIsSeamlessAcrossBlocks)
{
    constexpr double freq = 6000.0;
    const auto in         = makeSine(freq, 44100.0, 44100, 0.5f); // 1 s at 44.1 kHz

    Resampler r(48000, 512);
    r.prepare(44100.0);

    std::vector<float> out;
    const int blocks[] = {512, 512, 100, 333, 512, 480, 7};
    int pos = 0, bi = 0;
    while (pos < static_cast<int>(in.size()))
    {
        const int nb = std::min(blocks[bi++ % 7], static_cast<int>(in.size()) - pos);
        const auto b = r.process(in.data() + pos, nb);
        out.insert(out.end(), b.data, b.data + b.numSamples);
        pos += nb;
    }

    const double thdn = db(measure(out, freq, 48000.0).residualRel);
    std::printf("  [streaming ragged blocks] out=%zu residual=%.1f dB\n", out.size(), thdn);
    EXPECT_NEAR(static_cast<double>(out.size()), 48000.0, 64.0) << "wrong output length from block streaming";
    EXPECT_LT(thdn, -80.0) << "block seams introduced discontinuities";
}

// Heavy downsampling (192 kHz -> 48 kHz, a >2x drop) must use the staged 2:1
// cascade: a speech-band tone passes through clean, an ultrasonic tone is rejected
// instead of folding into the audible band the model sees.
TEST(ResamplerQuality, MultistageDownsamplePreservesAndRejects)
{
    constexpr float amp = 0.5f;

    const auto keep      = resampleFull(192000.0, 48000.0, makeSine(5000.0, 192000.0, 192000, amp));
    const double keepDb  = db(measure(keep, 5000.0, 48000.0).amp / amp);
    const auto ultra     = resampleFull(192000.0, 48000.0, makeSine(40000.0, 192000.0, 192000, amp));
    const double aliasDb = db(measure(ultra, 8000.0, 48000.0).amp / amp); // 40k folds to |40-48|=8k
    std::printf("  [multistage 192->48] 5kHz kept=%.3f dB  40kHz->8kHz alias=%.1f dB\n", keepDb, aliasDb);

    EXPECT_GT(keepDb, -0.1) << "staged decimation lost the speech-band tone";
    EXPECT_LT(aliasDb, -70.0) << "staged decimation let an ultrasonic tone alias in";
}
