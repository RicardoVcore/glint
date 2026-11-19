#pragma once

/**
    Shared reference signal for factory-preset calibration (tests/PresetCalibrate.cpp)
    and the preset accuracy test (test 10 in tests/GlintTests.cpp).

    The reference is 20 s of stereo pink noise at 48 kHz: two Voss-McCartney
    generators (12 rows, deterministic LCG white noise, fixed seeds) combined
    so L and R have a correlation of 0.7 — enough side content for the
    mid/side presets to bite.  Each channel is normalised to -18 dBFS RMS.
    Gain reduction and RMS are measured over the LAST 10 s (settling window
    excluded).

    This header must stay the single source of truth: the calibrator that
    produced the values in src/GlintPresets.h and the test that verifies them
    both run the exact same signal and measurement.
*/

#include "GlintDsp.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace glint::refsig
{

inline constexpr double sampleRate = 48000.0;
inline constexpr int blockSize = 256;
inline constexpr double totalSeconds = 20.0;
inline constexpr double measureSeconds = 10.0;
inline constexpr double targetRmsDb = -18.0;   // per channel, over the full signal
inline constexpr double channelCorrelation = 0.7;

/** Stereo reference signal, generated once and reused by every run. */
struct StereoSignal
{
    std::vector<float> left, right;
};

/** Deterministic LCG white noise in [-1, 1). */
class Random
{
public:
    explicit Random (std::uint32_t seed) noexcept : state (seed) {}

    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (state >> 8) / 8388608.0f - 1.0f;
    }

private:
    std::uint32_t state;
};

/** Voss-McCartney pink noise: `numRows` running values, row j refreshed
    every 2^j samples; the sum is approximately pink (-3 dB/oct) below
    fs / 2^1 and rolls off above fs / 2^numRows (~12 Hz here). */
class PinkNoise
{
public:
    explicit PinkNoise (std::uint32_t seed) noexcept : rng (seed) {}

    float next() noexcept
    {
        const std::uint32_t c = ++counter;
        const int updateCount = juce::jmin (static_cast<int> (std::countr_zero (c) + 1), numRows);
        for (int j = 0; j < updateCount; ++j)
            rows[static_cast<size_t> (j)] = rng.next();

        float sum = 0.0f;
        for (int j = 0; j < numRows; ++j)
            sum += rows[static_cast<size_t> (j)];
        return sum * rowScale;
    }

private:
    static constexpr int numRows = 12;
    static constexpr float rowScale = 0.288675135f;  // 1 / sqrt (numRows)

    Random rng;
    std::uint32_t counter = 0;
    float rows[numRows] {};
};

/** Builds the reference signal: L = pink A, R = 0.7 * A + sqrt (1 - 0.49) * B
    (correlation exactly 0.7), then each channel normalised to -18 dBFS RMS
    (per-channel scaling leaves the correlation unchanged). */
inline StereoSignal makeReferenceSignal()
{
    StereoSignal sig;
    const int numSamples = static_cast<int> (totalSeconds * sampleRate);
    sig.left.assign (static_cast<size_t> (numSamples), 0.0f);
    sig.right.assign (static_cast<size_t> (numSamples), 0.0f);

    PinkNoise a (0x51ed270bu), b (0x9e3779b9u);
    const double rho = channelCorrelation;
    const double indep = std::sqrt (1.0 - rho * rho);
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = a.next();
        const float r = static_cast<float> (rho) * l + static_cast<float> (indep) * b.next();
        sig.left[static_cast<size_t> (i)] = l;
        sig.right[static_cast<size_t> (i)] = r;
    }

    for (auto* ch : { &sig.left, &sig.right })
    {
        double sum = 0.0;
        for (float v : *ch)
            sum += static_cast<double> (v) * static_cast<double> (v);
        const double rms = std::sqrt (sum / static_cast<double> (numSamples));
        const float scale = static_cast<float> (std::pow (10.0, targetRmsDb / 20.0) / rms);
        for (float& v : *ch)
            v *= scale;
    }

    return sig;
}

/** Result of one engine pass over the reference signal. */
struct Measurement
{
    double meanGrDb = 0.0;    // block-rate gain-reduction average over the window
    double inRmsDb = -1000.0; // input RMS of the window (both channels pooled)
    double outRmsDb = -1000.0;// output RMS of the window (both channels pooled)
};

/** Runs the signal through a fresh engine (48 kHz, 256-sample blocks) and
    measures mean gain reduction plus input/output RMS over the last 10 s.
    The reported gain reduction is the engine's own detector value, sampled
    per audio block and averaged sample-weighted over the window. */
inline Measurement measure (const EngineParameters& p, const StereoSignal& sig)
{
    Engine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setParameters (p);

    const int total = static_cast<int> (totalSeconds * sampleRate);
    const int measureStart = total - static_cast<int> (measureSeconds * sampleRate);

    juce::AudioBuffer<float> buffer (2, blockSize);
    int produced = 0;

    double grSum = 0.0;
    int grWeight = 0;
    double outSum = 0.0;
    long long outCount = 0;

    while (produced < total)
    {
        const int n = juce::jmin (blockSize, total - produced);
        buffer.setSize (2, n, false, false, false);
        for (int c = 0; c < 2; ++c)
        {
            const auto& src = c == 0 ? sig.left : sig.right;
            auto* w = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i)
                w[i] = src[static_cast<size_t> (produced + i)];
        }

        engine.process (buffer);
        produced += n;

        if (produced >= measureStart)
        {
            grSum += static_cast<double> (
                engine.gainReductionDb.load (std::memory_order_relaxed)) * n;
            grWeight += n;
            for (int c = 0; c < 2; ++c)
            {
                const auto* r = buffer.getReadPointer (c);
                for (int i = 0; i < n; ++i)
                    outSum += static_cast<double> (r[i]) * static_cast<double> (r[i]);
            }
            outCount += 2 * n;
        }
    }

    double inSum = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        const auto& src = c == 0 ? sig.left : sig.right;
        for (int i = measureStart; i < total; ++i)
            inSum += static_cast<double> (src[static_cast<size_t> (i)])
                   * static_cast<double> (src[static_cast<size_t> (i)]);
    }

    Measurement m;
    m.meanGrDb = grSum / static_cast<double> (grWeight);
    m.inRmsDb = 20.0 * std::log10 (std::sqrt (
        inSum / static_cast<double> (2 * (total - measureStart))));
    m.outRmsDb = 20.0 * std::log10 (std::sqrt (
        static_cast<double> (outSum) / static_cast<double> (outCount)));
    return m;
}

} // namespace glint::refsig
