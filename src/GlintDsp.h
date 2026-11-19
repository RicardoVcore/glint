#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace glint
{

/** Number of detector time-constant positions (parameter values 1..6). */
constexpr int numTimeModes = 6;

/**
    Gain-cell calibration constant.

    The detector cv is a linear "excess ratio": rectified cell output level
    divided by the detector threshold, minus 1 (clamped at 0).  With the
    feedback loop settled (sustained tone), cv/k exponent 1.5 with k = 0.15
    gives roughly 5 dB of gain reduction for a sustained sine 6 dB above
    threshold, and the ratio rises smoothly with level (soft knee, no fixed
    ratio).
*/
constexpr float gainCellK = 0.15f;

/** Gain reduction in dB that maps to the maximum "working hard" value (1.0)
    when driving the tube bias and the sparkle amount. */
constexpr float maxNormGrDb = 12.0f;

/** Tube stage soft-clip steepness (small-signal gain stays 1). */
constexpr float tubeK = 1.5f;

/** Sparkle "air" generator gain.  The generator is degree-1 homogeneous
    (full-wave rectification of the >7 kHz band), so the added air scales
    linearly with the band level.  airGain is calibrated so that at
    sparkle=100 the added air sits about 12-14 dB below the >7 kHz band
    level (-14.8 dB on programme-like noise, -10.7 dB on a loud 9 kHz
    sine), and a loud sine adds well under +1 dB of peak. */
constexpr float airGain = 2.0f;

/** Maps the 0..10 threshold knob to the detector threshold in dBFS
    (3 dB per knob position: 0 -> -6 dBFS, 10 -> -36 dBFS). */
inline float thresholdKnobToDb (float knob) noexcept
{
    return -6.0f - 3.0f * juce::jlimit (0.0f, 10.0f, knob);
}

/** Snapshot of the user parameters, evaluated once per audio block. */
struct EngineParameters
{
    float inputDb = 0.0f;        // -20..+20
    float outputDb = 0.0f;       // -20..+20
    float thresholdKnob = 3.0f;  // 0..10
    int timeMode = 2;            // 1..6
    bool midSide = false;        // false = stereo linked
    float sparkle01 = 0.3f;      // 0..1
    float mix01 = 1.0f;          // 0..1
};

//==============================================================================
/**
    Variable-mu gain computer, feedback topology.

    The detector reads the gain cell's OUTPUT (the caller feeds the full-wave
    rectified output level of the previous sample).  The detector subtracts
    the threshold in the LINEAR domain: excess ratio
    r = max (0, |out| / thresholdLin - 1), which is 0 at threshold and 1 for
    a level 6 dB above it.  r is then RC-smoothed.

    Time positions (attack ms / release s):
      1: 0.2 / 0.3        2: 0.2 / 0.8        3: 0.4 / 2.0
      4: 0.8 / 5.0        5: 0.2 / program dependent: parallel stages, fast
                             2 s + slow 10 s, cv = max of both
      6: 0.4 / program dependent: three stages, 0.3 s, 10 s, 25 s

    For the program-dependent positions, any stage with release > 1 s uses
    attack = release / 50, so slow stages only charge on sustained level
    while fast stages keep the listed attack.

    Gain from the smoothed control voltage cv (>= 0):
        g = 1 / (1 + (cv / k)^1.5),  k = gainCellK
    The result is additionally smoothed with a ~1 ms one-pole to avoid
    zipper/clicks.  No fixed ratio: the ratio rises smoothly with level.
*/
class GainComputer
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setThresholdDb (float db) noexcept;
    void setTimeMode (int mode1to6) noexcept;

    /** Feeds one full-wave rectified sample of the cell output (linear) into
        the detector envelope and returns the smoothed linear gain to apply
        for this sample. */
    float advance (float rectifiedOutput) noexcept;

    /** Copies the detector envelope state from another instance (used when
        the stereo-link mode changes so a channel that was idle in linked
        mode resumes mid-programme instead of from a stale envelope). */
    void copyStateFrom (const GainComputer& other) noexcept;

    /** Current gain reduction in dB (positive number = amount of reduction). */
    float getCurrentGainReductionDb() const noexcept { return lastGrDb; }

private:
    struct Stage
    {
        float z = 0.0f;        // envelope state (excess ratio)
        float atkCoef = 0.0f;
        float relCoef = 0.0f;
    };

    static constexpr int maxStages = 3;

    double fs = 48000.0;
    float thresholdLin = 0.1778f;   // -15 dBFS default
    int timeMode = 0;
    int numStages = 1;
    std::array<Stage, maxStages> stages {};
    float gSmooth = 1.0f;
    float gainSmoothCoef = 0.0f;
    float lastGrDb = 0.0f;
};

//==============================================================================
/**
    Tube + transformer colour and the "sparkle" air generator.

    Runs inside 2x oversampling (linear-phase FIR half-band, integer latency).
    Per oversampled sample:
      * push-pull tube: symmetric tanh soft clip, plus a small positive bias
        that grows with gain reduction (a little 2nd harmonic when working
        hard);
      * transformer: the ~120 Hz low-passed band is softly saturated and
        summed back (gentle LF saturation), plus a one-pole HF rolloff
        (~1 dB @ 18 kHz);
      * sparkle: high-pass at ~7 kHz (2nd order), full-wave rectify the band
        (degree-1 homogeneous: only genuinely NEW upper harmonics and
        intermodulation, scaling with the band level), high-pass again to
        keep the new air, and add back scaled by
        amount = sparkle * (0.3 + 0.7 * normalisedGR) so it opens up as the
        compressor works. Exactly bypassed when sparkle is zero.
      * bias and sparkle amount are smoothed per oversampled sample with a
        one-pole (~10 ms), so block-rate parameter changes cannot zipper.

    Subtle at rest: at -18 dBFS input the colouration stays below ~0.5% THD.
*/
class ColourStage
{
public:
    ColourStage();

    void prepare (double baseSampleRate, int maxBlockSize);
    void reset();

    /** normGR01: normalised gain reduction (0..1, see maxNormGrDb).
        sparkle01: sparkle parameter, 0..1.
        Block rate: only sets the character TARGETS; the applied values are
        smoothed per oversampled sample inside processBlock (see inner). */
    void setCharacter (float normGR01, float sparkle01) noexcept;

    double getLatencyInSamples() const noexcept { return overs.getLatencyInSamples(); }

    void processBlock (float* samples, int numSamples) noexcept;

private:
    float inner (float x) noexcept;

    juce::dsp::Oversampling<float> overs;
    juce::dsp::IIR::Filter<float> lfLP;     // transformer LF band
    juce::dsp::IIR::Filter<float> hpAir1;   // sparkle pre-emphasis
    juce::dsp::IIR::Filter<float> hpAir2;   // sparkle "new air" selection
    float hfCoef = 0.0f;
    float hfZ = 0.0f;
    // Character targets (block rate) and one-pole-smoothed applied values
    // (per oversampled sample, ~10 ms).
    float biasTarget = 0.0f;
    float sparkleTarget = 0.0f;
    float bias = 0.0f;
    float sparkle = 0.0f;
    float charCoef = 0.0f;
};

//==============================================================================
/**
    Two-channel Glint engine.

    Signal flow per sample:
      input gain -> [mid/side encode] -> variable-mu gain cell (stereo linked:
      one detector on max(|L|,|R|); mid/side: two independent channels)
      -> oversampled tube/transformer/sparkle colour -> [mid/side decode]
      -> mix with latency-aligned dry -> output gain.

    Real-time safe: no allocation, locks or logging inside process(); all
    buffers are allocated in prepare(). Denormal flushing is the caller's
    job (the processor uses juce::ScopedNoDenormals).
*/
class Engine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setParameters (const EngineParameters& p) noexcept;

    void process (juce::AudioBuffer<float>& buffer);

    /** Reported latency in samples (integer, from the oversampler). */
    int getLatencySamples() const noexcept { return latencySamples; }

    /** Current gain reduction in dB (max of both channels), for metering. */
    std::atomic<float> gainReductionDb { 0.0f };

private:
    // Ring buffer: returns the sample written `size` calls ago.
    struct DelayLine
    {
        std::vector<float> buf;
        int pos = 0;

        void prepare (int delaySamples)
        {
            buf.assign (static_cast<size_t> (juce::jmax (1, delaySamples)), 0.0f);
            pos = 0;
        }

        float process (float v) noexcept
        {
            if (buf.size() <= 1)
                return v;
            const float out = buf[static_cast<size_t> (pos)];
            buf[static_cast<size_t> (pos)] = v;
            pos = pos + 1 == static_cast<int> (buf.size()) ? 0 : pos + 1;
            return out;
        }
    };

    double fs = 0.0;
    int latencySamples = 0;
    bool msMode = false;

    EngineParameters params;
    std::array<GainComputer, 2> comps;
    std::array<ColourStage, 2> stages;
    std::array<DelayLine, 2> dryDelay;
    std::array<float, 2> prevRect {};

    juce::SmoothedValue<float> inDbS;    // input gain, smoothed in dB
    juce::SmoothedValue<float> outDbS;   // output gain, smoothed in dB
    juce::SmoothedValue<float> mixS;
    juce::SmoothedValue<float> sparkS;

    // Per-block scratch, allocated in prepare() only. Rows: compressed L,
    // compressed R, dry L, dry R, output gain, mix, sparkle amount.
    std::array<std::vector<float>, 7> scratch;

    bool snapSmoothersOnNextSet = true;
};

} // namespace glint
