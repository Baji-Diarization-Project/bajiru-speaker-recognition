#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// AVX path for the inner dot product, with runtime detection + a scalar fallback so
// the binary still runs on CPUs without AVX.
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define LINKJIRU_X86_SIMD 1
#include <immintrin.h>
#include <intrin.h>
#endif

// One rational-ratio polyphase FIR resampling stage: outputRate / inputRate = L/M.
// The prototype is a Kaiser-windowed sinc low-pass whose cutoff sits at the lower
// of the input / output Nyquist, so it doubles as the anti-image filter when
// upsampling and the anti-alias filter when downsampling: the stopband lands
// above the surviving Nyquist and folds nothing back in.
//
// Real-time safe: design() (called off the audio thread) builds the coefficient
// table and buffers; process() only does multiply-adds and never allocates. It is
// stateful: the filter history and commutator phase carry across process() calls,
// so block boundaries are seamless.
class PolyphaseFir
{
public:
    struct Block
    {
        const float* data = nullptr;
        int numSamples    = 0;
    };

    // L/M is the reduced output:input ratio. tapsPerPhase sets the per-phase filter
    // length (quality/steepness). maxInputSamples bounds the preallocated output.
    void design(const int upFactor, const int downFactor, const int tapsPerPhaseIn, const int maxInputSamples)
    {
        L            = upFactor;
        M            = downFactor;
        tapsPerPhase = tapsPerPhaseIn;

        // Cutoff in cycles/sample of the L-upsampled rate: min(input, output) Nyquist.
        const double cutoff = std::min(0.5 / L, 0.5 / M);
        buildPrototype(L * tapsPerPhase, cutoff);

        // Mirrored history (2x) so the taps read contiguously (see pushHistory).
        history.assign(2 * static_cast<std::size_t>(tapsPerPhase), 0.0f);
        const int maxOut = maxInputSamples * L / M + 2;
        output.assign(static_cast<std::size_t>(std::max(maxOut, 1)), 0.0f);

#if LINKJIRU_X86_SIMD
        // AVX pays off here (per-tap contiguous mul-add); the tap count is a multiple
        // of 8 so the whole loop vectorizes, with a scalar tail for any remainder.
        useAvx = cpuHasAvx();
#endif
        reset();
    }

    void reset()
    {
        std::fill(history.begin(), history.end(), 0.0f);
        histWrite = 0;
        phase     = 0;
        inNeeded  = 0;
        pushed    = 0;
    }

    // Push n input samples, pull as many outputs as the ratio yields. The returned
    // view is valid until the next process() call.
    Block process(const float* in, const int n)
    {
        int outCount  = 0;
        int pos       = 0;
        const int cap = static_cast<int>(output.size());

        while (outCount < cap)
        {
            // Advance input until the newest sample this output needs is in history.
            while (pushed <= inNeeded)
            {
                if (pos >= n)
                {
                    return {output.data(), outCount};
                }
                pushHistory(in[pos++]);
            }
            output[static_cast<std::size_t>(outCount++)] = dot();

            phase += M;
            inNeeded += phase / L;
            phase %= L;
        }
        return {output.data(), outCount};
    }

    void* storage() noexcept { return output.data(); }
    [[nodiscard]] std::size_t byteSize() const noexcept { return output.size() * sizeof(float); }
    [[nodiscard]] bool usesSimd() const noexcept { return useAvx; }

private:
    // Mirrored history: a 2*tapsPerPhase ring where each sample is written twice, so
    // the newest tapsPerPhase samples (newest-first) are always a contiguous run at
    // [histWrite, histWrite + tapsPerPhase): no wrap in the inner loop, so the dot
    // vectorizes. histWrite decrements (newest at the front).
    void pushHistory(const float x)
    {
        histWrite                                    = (histWrite == 0) ? tapsPerPhase - 1 : histWrite - 1;
        history[static_cast<std::size_t>(histWrite)] = x;
        history[static_cast<std::size_t>(histWrite) + static_cast<std::size_t>(tapsPerPhase)] = x;
        ++pushed;
    }

    // Convolve the phase's polyphase subfilter with the newest tapsPerPhase inputs.
    // poly[phase][k] multiplies x[inBase-k] = history[histWrite+k] (both contiguous).
    float dot() const
    {
        const float* c  = &poly[static_cast<std::size_t>(phase) * tapsPerPhase];
        const float* in = &history[static_cast<std::size_t>(histWrite)];

#if LINKJIRU_X86_SIMD
        if (useAvx)
        {
            return dotAvx(c, in, tapsPerPhase);
        }
#endif
        double acc = 0.0;
        for (int k = 0; k < tapsPerPhase; ++k)
        {
            acc += static_cast<double>(c[k]) * in[k];
        }
        return static_cast<float>(acc);
    }

#if LINKJIRU_X86_SIMD
    // 256-bit AVX dot (plain mul+add, so only the AVX feature is required, no FMA).
    // n is a multiple of 8; a scalar tail covers any remainder.
    static float dotAvx(const float* c, const float* in, const int n)
    {
        __m256 acc = _mm256_setzero_ps();
        int k      = 0;
        for (; k + 8 <= n; k += 8)
        {
            acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_loadu_ps(c + k), _mm256_loadu_ps(in + k)));
        }

        __m128 v = _mm_add_ps(_mm256_castps256_ps128(acc), _mm256_extractf128_ps(acc, 1));
        v        = _mm_add_ps(v, _mm_movehl_ps(v, v));
        v        = _mm_add_ss(v, _mm_shuffle_ps(v, v, 1));
        float r  = _mm_cvtss_f32(v);
        for (; k < n; ++k)
        {
            r += c[k] * in[k];
        }
        return r;
    }

    // True only if the CPU has AVX and the OS saves YMM state (OSXSAVE + XCR0).
    static bool cpuHasAvx()
    {
        int info[4];
        __cpuid(info, 1);
        const bool osxsave = (info[2] & (1 << 27)) != 0;
        const bool avx     = (info[2] & (1 << 28)) != 0;
        if (!osxsave || !avx)
        {
            return false;
        }
        return (_xgetbv(0) & 0x6) == 0x6; // XMM (bit1) + YMM (bit2) state enabled
    }
#endif

    // Modified Bessel function of the first kind, order 0 (for the Kaiser window).
    static double i0(const double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x * x) / (4.0 * k * k);
            sum += term;
            if (term < 1e-12 * sum)
            {
                break;
            }
        }
        return sum;
    }

    // Kaiser-windowed sinc, normalized so each polyphase subfilter has unity DC gain
    // (total taps sum to L), then split into L phase subfilters of tapsPerPhase each.
    void buildPrototype(const int nTaps, const double cutoff)
    {
        constexpr double kPi  = 3.14159265358979323846;
        constexpr double beta = 9.0; // ~ -80 dB stopband
        const double center   = (nTaps - 1) / 2.0;
        const double i0beta   = i0(beta);

        std::vector<double> h(static_cast<std::size_t>(nTaps));
        double sum = 0.0;
        for (int i = 0; i < nTaps; ++i)
        {
            const double m                 = i - center;
            const double sinc              = (m == 0.0) ? 2.0 * cutoff : std::sin(2.0 * kPi * cutoff * m) / (kPi * m);
            const double r                 = 2.0 * i / (nTaps - 1) - 1.0;
            const double win               = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0beta;
            h[static_cast<std::size_t>(i)] = sinc * win;
            sum += h[static_cast<std::size_t>(i)];
        }
        const double gain = L / sum; // DC gain per phase -> 1
        for (double& c : h)
        {
            c *= gain;
        }

        poly.assign(static_cast<std::size_t>(L) * tapsPerPhase, 0.0f);
        for (int p = 0; p < L; ++p)
        {
            for (int k = 0; k < tapsPerPhase; ++k)
            {
                poly[static_cast<std::size_t>(p) * tapsPerPhase + k] =
                    static_cast<float>(h[static_cast<std::size_t>(p) + static_cast<std::size_t>(k) * L]);
            }
        }
    }

    std::vector<float> poly;    // L * tapsPerPhase coefficients
    std::vector<float> history; // mirrored ring, 2 * tapsPerPhase
    std::vector<float> output;

    int L = 1, M = 1, tapsPerPhase = 0;
    int histWrite = 0, phase = 0;
    long long inNeeded = 0, pushed = 0; // absolute input indices (only their diff is used)
    bool useAvx = false;
};
