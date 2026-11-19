// Plain test executable for the Glint DSP slice. No test framework:
// each check prints PASS/FAIL and the process exit code is the failure count.
#include "GlintDsp.h"
#include "PluginProcessor.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>

namespace
{

constexpr double fs = 48000.0;
constexpr int blockSize = 256;

int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok)
        ++failures;
}

glint::EngineParameters baseParams()
{
    glint::EngineParameters p; // defaults, except sparkle off for clean GR reads
    p.sparkle01 = 0.0f;
    return p;
}

float noiseSample (std::uint32_t& s)
{
    s = s * 1664525u + 1013904223u;
    return static_cast<float> (s >> 8) / 8388608.0f - 1.0f; // [-1, 1)
}

// Deterministic per-(channel, sample) noise, so multi-channel fills are identical
// across channels when they need to be, and reproducible run to run.
float noiseAt (int c, int i)
{
    std::uint32_t h = static_cast<std::uint32_t> (c) * 747796405u
                    + static_cast<std::uint32_t> (i) * 668265263u + 2891336453u;
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    return static_cast<float> (h >> 8) / 8388608.0f - 1.0f; // [-1, 1)
}

struct Capture
{
    std::vector<float> ch[2];
};

template <typename FillFn>
void run (glint::Engine& engine, int numSamples, int numChannels, FillFn&& fill, Capture& out)
{
    juce::AudioBuffer<float> buf (numChannels, blockSize);
    int produced = 0;
    while (produced < numSamples)
    {
        const int n = juce::jmin (blockSize, numSamples - produced);
        buf.setSize (numChannels, n, false, false, false);
        for (int c = 0; c < numChannels; ++c)
        {
            auto* w = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
                w[i] = fill (c, produced + i);
        }
        engine.process (buf);
        for (int c = 0; c < numChannels; ++c)
        {
            const auto* r = buf.getReadPointer (c);
            out.ch[c].insert (out.ch[c].end(), r, r + n);
        }
        produced += n;
    }
}

auto sineFill (double freq, float amp)
{
    return [freq, amp] (int, int i)
    {
        const double t = static_cast<double> (i) / fs;
        return amp * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * freq * t));
    };
}

// Sample i of a -6 dBFS sine at an arbitrary sample rate (stress sweep).
float sineAt (double freq, int i, double sr)
{
    constexpr float amp = 0.501187f;   // -6 dBFS
    return amp * static_cast<float> (
        std::sin (2.0 * juce::MathConstants<double>::pi * freq * static_cast<double> (i) / sr));
}

double peakOf (const std::vector<float>& v, int from, int to)
{
    double p = 0.0;
    for (int i = from; i < to && i < static_cast<int> (v.size()); ++i)
        p = juce::jmax (p, std::abs (static_cast<double> (v[static_cast<size_t> (i)])));
    return p;
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite (x))
            return false;
    return true;
}

// Simple RBJ biquads, used offline to measure energy in a frequency band.
struct RbjBiquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    static RbjBiquad highpass (double f, double Q = 0.7071)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double cw = std::cos (w0), sw = std::sin (w0), alpha = sw / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        RbjBiquad bq;
        bq.b0 = (1.0 + cw) / 2.0 / a0;
        bq.b1 = -(1.0 + cw) / a0;
        bq.b2 = bq.b0;
        bq.a1 = -2.0 * cw / a0;
        bq.a2 = (1.0 - alpha) / a0;
        return bq;
    }

    static RbjBiquad lowpass (double f, double Q = 0.7071)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double cw = std::cos (w0), sw = std::sin (w0), alpha = sw / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        RbjBiquad bq;
        bq.b0 = (1.0 - cw) / 2.0 / a0;
        bq.b1 = (1.0 - cw) / a0;
        bq.b2 = bq.b0;
        bq.a1 = -2.0 * cw / a0;
        bq.a2 = (1.0 - alpha) / a0;
        return bq;
    }

    double process (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// rms of v inside [fLo, fHi] (fHi <= 0: no upper bound).
double rmsInBand (const std::vector<float>& v, double fLo, double fHi)
{
    RbjBiquad hp = RbjBiquad::highpass (fLo);
    RbjBiquad lp = RbjBiquad::lowpass (fHi > 0.0 ? fHi : 23000.0);
    double sum = 0.0;
    for (float xIn : v)
    {
        const double y = lp.process (hp.process (xIn));
        sum += y * y;
    }
    return std::sqrt (sum / static_cast<double> (v.size()));
}

double rmsOf (const std::vector<float>& v)
{
    double sum = 0.0;
    for (float x : v)
        sum += static_cast<double> (x) * static_cast<double> (x);
    return std::sqrt (sum / static_cast<double> (v.size()));
}

//------------------------------------------------------------------------------

void testSilenceAndFinite()
{
    std::puts ("test 1: silence in -> silence out; finite on full-scale noise");

    {
        glint::Engine e;
        e.prepare (fs, blockSize);
        e.setParameters (baseParams());

        Capture cap;
        run (e, 3 * blockSize, 2, [] (int, int) { return 0.0f; }, cap);
        bool silent = true;
        for (int c = 0; c < 2; ++c)
            for (float x : cap.ch[c])
                if (std::abs (x) > 1.0e-9f)
                    silent = false;
        check (silent, "silence gives silence (|y| < 1e-9)");
    }

    {
        glint::Engine e;
        e.prepare (fs, blockSize);
        e.setParameters (baseParams());

        std::uint32_t seed = 0x1234567u;
        Capture cap;
        run (e, fs, 2, [&seed] (int c, int i) { return noiseAt (c, i); }, cap);

        bool finite = true, bounded = true;
        for (int c = 0; c < 2; ++c)
        {
            if (! allFinite (cap.ch[c]))
                finite = false;
            if (peakOf (cap.ch[c], 0, (int) cap.ch[c].size()) > 4.0)
                bounded = false;
        }
        check (finite, "full-scale noise: no NaN/Inf");
        check (bounded, "full-scale noise: output stays bounded");
    }
}

void testGrOrdering()
{
    std::puts ("test 2: GR rises with input level (1 kHz sustained sine)");

    auto grFor = [] (float inputDb)
    {
        glint::Engine e;
        e.prepare (fs, blockSize);
        auto p = baseParams();
        p.inputDb = inputDb;
        p.timeMode = 1;
        e.setParameters (p);

        const int numSamples = static_cast<int> (2.0 * fs);
        Capture cap;
        run (e, numSamples, 1, sineFill (1000.0, 0.25f), cap);

        const int from = static_cast<int> (1.5 * fs);
        const double inPeak = 0.25 * std::pow (10.0, inputDb / 20.0);
        const double outPeak = peakOf (cap.ch[0], from, numSamples);
        return 20.0 * std::log10 (inPeak / outPeak);
    };

    const double gr12 = grFor (12.0f);
    const double gr0 = grFor (0.0f);
    const double grM20 = grFor (-20.0f);

    std::printf ("    GR: +12 dB -> %.2f dB, 0 dB -> %.2f dB, -20 dB -> %.2f dB\n",
                 gr12, gr0, grM20);

    check (gr12 > gr0 + 1.0, "GR(+12) > GR(0) + 1 dB");
    check (gr0 > grM20 + 0.5, "GR(0) > GR(-20) + 0.5 dB");
    check (grM20 < 0.3, "GR(-20) ~ 0 (< 0.3 dB)");
}

void testReleaseOrdering()
{
    std::puts ("test 3: release ordering (time=4 releases slower than time=1)");

    auto residualGr = [] (int timeMode)
    {
        glint::Engine e;
        e.prepare (fs, blockSize);
        auto p = baseParams();
        p.timeMode = timeMode;
        e.setParameters (p);

        Capture cap;
        run (e, static_cast<int> (fs), 1, sineFill (1000.0, 1.0f), cap);   // burst
        run (e, static_cast<int> (0.2 * fs), 1, [] (int, int) { return 0.0f; }, cap);
        return static_cast<double> (e.gainReductionDb.load());
    };

    const double gr1 = residualGr (1);
    const double gr4 = residualGr (4);
    std::printf ("    residual GR @200ms: time=1 -> %.2f dB, time=4 -> %.2f dB\n", gr1, gr4);

    check (gr4 > gr1 + 2.0, "residual GR(time=4) > residual GR(time=1) + 2 dB");
    check (gr1 > 0.1, "time=1 still shows residual GR after 200 ms");
}

void testMixZeroNull()
{
    std::puts ("test 4: mix=0 equals latency-delayed input (null < -80 dB)");

    glint::Engine e;
    e.prepare (fs, blockSize);
    auto p = baseParams();
    p.mix01 = 0.0f;
    e.setParameters (p);

    std::uint32_t seed = 0xdeadbeefu;
    std::vector<float> in;
    in.reserve (static_cast<size_t> (fs));
    for (int i = 0; i < static_cast<int> (fs); ++i)
        in.push_back (noiseSample (seed));

    Capture cap;
    run (e, static_cast<int> (fs), 2, [&in] (int, int i) { return in[static_cast<size_t> (i)]; }, cap);

    const int delay = e.getLatencySamples();
    std::printf ("    reported latency: %d samples\n", delay);

    double sigSum = 0.0, errSum = 0.0;
    for (int i = delay; i < static_cast<int> (in.size()); ++i)
    {
        const double d = static_cast<double> (cap.ch[0][static_cast<size_t> (i)])
                       - static_cast<double> (in[static_cast<size_t> (i - delay)]);
        errSum += d * d;
        sigSum += static_cast<double> (in[static_cast<size_t> (i)]) * in[static_cast<size_t> (i)];
    }
    const double nullDb = -10.0 * std::log10 (errSum / sigSum);
    std::printf ("    null: %.1f dB\n", nullDb);
    check (nullDb > 80.0, "null against delayed input better than -80 dB");
}

void testMidSideIdentical()
{
    std::puts ("test 5: mid/side with identical L and R gives L == R");

    glint::Engine e;
    e.prepare (fs, blockSize);
    auto p = baseParams();
    p.midSide = true;
    e.setParameters (p);

    Capture cap;
    run (e, static_cast<int> (fs), 2,
         [] (int, int i) { return 0.4f * noiseAt (0, i); }, cap);

    double maxDiff = 0.0;
    const size_t count = juce::jmin (cap.ch[0].size(), cap.ch[1].size());
    for (size_t i = 0; i < count; ++i)
        maxDiff = juce::jmax (maxDiff, static_cast<double> (std::abs (cap.ch[0][i] - cap.ch[1][i])));
    std::printf ("    max |L-R| = %.3g\n", maxDiff);
    check (maxDiff < 1.0e-9f, "L == R (max diff < 1e-9)");
}

void testSparkleAir()
{
    std::puts ("test 6: sparkle=100 audibility (5-12 kHz lift), air level, loud-HF peak guard");

    // Runs the test-6 signal (0.5 amplitude white noise, +6 dB input ->
    // heavy compression) at a given sparkle and returns the output tail.
    auto runWith = [] (float sparkle, double& grOut)
    {
        glint::Engine e;
        e.prepare (fs, blockSize);
        auto p = baseParams();
        p.sparkle01 = sparkle;
        p.inputDb = 6.0f;
        e.setParameters (p);

        Capture cap;
        run (e, static_cast<int> (fs), 2,
             [] (int c, int i) { return 0.5f * noiseAt (c, i); }, cap);
        grOut = static_cast<double> (e.gainReductionDb.load());
        const int skip = static_cast<int> (0.1 * fs);
        return std::vector<float> (cap.ch[0].begin() + skip, cap.ch[0].end());
    };

    double gr0 = 0.0, gr100 = 0.0;
    const auto wet0 = runWith (0.0f, gr0);
    const auto wet100 = runWith (1.0f, gr100);

    // sparkle=0 is an exact bypass, so the difference IS the added air.
    std::vector<float> air (wet0.size());
    for (size_t i = 0; i < wet0.size(); ++i)
        air[i] = wet100[i] - wet0[i];

    // Offline 2nd-order highpass at 3.5 kHz matches the engine's band split
    // (the oversampling round trip is transparent below the FIR transition).
    const double bandRms = rmsInBand (wet0, 3500.0, 0.0);
    const double airRelDb = 20.0 * std::log10 (rmsOf (air) / bandRms);
    const double liftDb = 20.0 * std::log10 (rmsInBand (wet100, 5000.0, 12000.0)
                                            / rmsInBand (wet0, 5000.0, 12000.0));

    std::printf ("    band >3.5 kHz rms: %.2f dBFS; added air: %+.2f dB rel band "
                 "(airGain %.2f)\n",
                 20.0 * std::log10 (bandRms), airRelDb, glint::airGain);
    std::printf ("    5-12 kHz lift, sparkle=100 vs 0 under compression: %+.2f dB "
                 "(GR %.1f / %.1f dB)\n",
                 liftDb, gr0, gr100);

    check (gr0 > 1.0 && gr100 > 1.0, "both runs are under compression (GR > 1 dB)");
    // Target: 5-12 kHz lift = 2.5 dB +/- 0.5 dB, tuned via airGain only.
    // The airGain sweep used to be capped by the stress sweep (its worst-case
    // peak grew 1.106 + 0.497 * airGain with the air added linearly, so
    // airGain <= ~1.79 and lift ~+0.9 dB).  The air is now soft-limited
    // (tanh around airCeiling) before the add-back, so the ceiling — not the
    // gain — absorbs the extreme input and airGain = 3.20 hits the target.
    check (liftDb >= 2.0 && liftDb <= 3.0, "5-12 kHz lift within [2.0, 3.0] dB");

    // Light compression: ~1.5 dB GR at input -7 dB.  The amount remap
    // (0.6 + 0.4 * normGR instead of 0.3 + 0.7 * normGR) keeps sparkle
    // clearly present when the compressor is only tickling.
    auto runLight = [] (float sparkle, double& grOut)
    {
        glint::Engine e;
        e.prepare (fs, blockSize);
        auto p = baseParams();
        p.sparkle01 = sparkle;
        p.inputDb = -7.0f;
        e.setParameters (p);
        Capture cap;
        run (e, static_cast<int> (fs), 2,
             [] (int c, int i) { return 0.5f * noiseAt (c, i); }, cap);
        grOut = static_cast<double> (e.gainReductionDb.load());
        const int skip = static_cast<int> (0.1 * fs);
        return std::vector<float> (cap.ch[0].begin() + skip, cap.ch[0].end());
    };

    double grl0 = 0.0, grl100 = 0.0;
    const auto light0 = runLight (0.0f, grl0);
    const auto light100 = runLight (1.0f, grl100);
    const double liftLightDb = 20.0 * std::log10 (rmsInBand (light100, 5000.0, 12000.0)
                                                 / rmsInBand (light0, 5000.0, 12000.0));
    std::printf ("    light compression (GR %.2f dB): 5-12 kHz lift %+.2f dB\n",
                 grl0, liftLightDb);
    check (grl0 > 0.8 && grl0 < 2.5, "light run is under ~1-2 dB GR");
    // Target: light-GR lift >= 1.0 dB.  Measured +1.14 dB at airGain = 3.20
    // (see the heavy-lift comment above for why the ceiling unblocked the
    // tuning).
    check (liftLightDb >= 1.0, "light-GR 5-12 kHz lift >= 1.0 dB");

    // Regression: the old band^2 saturator exploded on loud HF material
    // (near full-scale air once the band hit ~-26 dBFS).  This guard only
    // catches blow-ups — the real guard is the stress sweep's peak < 2.0
    // (test 8).  At the calibrated airGain = 3.20 the 9 kHz sine adds
    // +4.6 dB of peak, still inside +6 dB (the 12 kHz lowpass rotates the
    // harmonics away from the carrier peak, and the air ceiling caps the
    // rest).
    auto sinePeak = [] (float sparkle)
    {
        glint::Engine e;
        e.prepare (fs, blockSize);
        auto p = baseParams();
        p.sparkle01 = sparkle;
        e.setParameters (p);

        Capture cap;
        run (e, static_cast<int> (fs), 2, sineFill (9000.0, 0.5f), cap);
        return peakOf (cap.ch[0], static_cast<int> (0.5 * fs), static_cast<int> (fs));
    };

    const double pk0 = sinePeak (0.0f);
    const double pk100 = sinePeak (1.0f);
    std::printf ("    9 kHz peak: sparkle=0 -> %.4f, sparkle=100 -> %.4f (%+.2f dB)\n",
                 pk0, pk100, 20.0 * std::log10 (pk100 / pk0));
    check (pk100 < pk0 * 1.99526, "loud 9 kHz sine: sparkle=100 peak < +6 dB vs sparkle=0");
}

void testStateRoundTrip()
{
    std::puts ("test 7: state round-trip preserves all parameter values");

    GlintAudioProcessor a;

    auto setFloat = [] (GlintAudioProcessor& p, const char* id, float v)
    {
        auto* pf = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (id));
        jassertquiet (pf != nullptr);
        *pf = v;
    };
    auto setChoice = [] (GlintAudioProcessor& p, const char* id, int v)
    {
        auto* pc = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id));
        jassertquiet (pc != nullptr);
        pc->setValueNotifyingHost (pc->convertTo0to1 (static_cast<float> (v)));
    };

    setFloat (a, "input", 6.5f);
    setFloat (a, "threshold", 7.5f);
    setChoice (a, "time", 4);
    setChoice (a, "link", 1);
    setFloat (a, "sparkle", 77.0f);
    setFloat (a, "mix", 33.0f);
    setFloat (a, "output", -8.25f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    GlintAudioProcessor b;
    b.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    bool ok = true;
    for (const char* id : { "input", "threshold", "sparkle", "mix", "output" })
    {
        const float va = a.apvts.getRawParameterValue (id)->load();
        const float vb = b.apvts.getRawParameterValue (id)->load();
        if (std::abs (va - vb) > 1.0e-6f)
        {
            std::printf ("    mismatch %s: %.6f vs %.6f\n", id, (double) va, (double) vb);
            ok = false;
        }
    }
    for (const char* id : { "time", "link" })
    {
        const auto va = dynamic_cast<juce::AudioParameterChoice*> (a.apvts.getParameter (id))->getCurrentChoiceName();
        const auto vb = dynamic_cast<juce::AudioParameterChoice*> (b.apvts.getParameter (id))->getCurrentChoiceName();
        if (va != vb)
        {
            std::printf ("    mismatch %s: %s vs %s\n", id, va.toRawUTF8(), vb.toRawUTF8());
            ok = false;
        }
    }

    check (ok, "all parameter values survive get/set state");
}

void testParameterTextRoundTrip()
{
    std::puts ("test 9: parameter text round-trips and out-of-range input clamps");

    GlintAudioProcessor p;

    // displayStep = the smallest change the formatted text can express.
    struct Spec { const char* id; float displayStep; };
    const Spec specs[] { { "input",     0.01f },
                         { "threshold", 0.1f  },
                         { "sparkle",   1.0f  },
                         { "mix",       1.0f  },
                         { "output",    0.01f } };

    for (const auto& spec : specs)
    {
        auto* pf = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (spec.id));
        jassertquiet (pf != nullptr);
        if (pf == nullptr)
        {
            check (false, "float parameter found");
            continue;
        }

        // getText/getValueForText are private overrides on AudioParameterFloat;
        // call them through the public AudioProcessorParameter interface.
        auto& param = static_cast<juce::AudioProcessorParameter&> (*pf);

        // getText(value) -> getValueForText(text) must land within one
        // display step of the original value.
        const float v = pf->get();
        const juce::String text = param.getText (pf->convertTo0to1 (v), 64);
        const float back = pf->convertFrom0to1 (param.getValueForText (text));

        char msg[128];
        std::snprintf (msg, sizeof (msg), "%s: \"%s\" -> %.3f (was %.3f)",
                       spec.id, text.toRawUTF8(), (double) back, (double) v);
        check (std::abs (back - v) <= spec.displayStep + 1.0e-4f, msg);

        // getValueForText("999") must clamp to the parameter maximum.
        const float clamped = pf->convertFrom0to1 (param.getValueForText ("999"));
        const float rangeMax = pf->getNormalisableRange().end;
        std::snprintf (msg, sizeof (msg), "%s: \"999\" clamps to max %.2f",
                       spec.id, (double) rangeMax);
        check (std::abs (clamped - rangeMax) < 1.0e-4f, msg);
    }

    // Typo-tolerant parsing on a dB parameter: bare numbers, units and
    // explicit signs all resolve to the same value.
    auto* input = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter ("input"));
    jassertquiet (input != nullptr);
    auto& inputParam = static_cast<juce::AudioProcessorParameter&> (*input);
    auto parsesTo = [&inputParam, &input] (const char* text)
    {
        return input->convertFrom0to1 (inputParam.getValueForText (text));
    };
    check (std::abs (parsesTo ("3") - 3.0f) < 0.01f, "input \"3\" -> 3 dB");
    check (std::abs (parsesTo ("3 dB") - 3.0f) < 0.01f, "input \"3 dB\" -> 3 dB");
    check (std::abs (parsesTo ("+3.0") - 3.0f) < 0.01f, "input \"+3.0\" -> 3 dB");

    // Same round-trip at one non-default value per parameter (the value box
    // has to survive what the user typed, not just the default), including
    // a negative dB value.
    struct NonDefaultSpec { const char* id; float value; float displayStep; };
    const NonDefaultSpec nonDefaults[] { { "input",     -3.567f, 0.01f },
                                         { "threshold",  7.5f,   0.1f  },
                                         { "sparkle",    77.0f,  1.0f  },
                                         { "mix",        33.0f,  1.0f  },
                                         { "output",     -8.25f, 0.01f } };

    for (const auto& spec : nonDefaults)
    {
        auto* pf = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (spec.id));
        jassertquiet (pf != nullptr);
        if (pf == nullptr)
        {
            check (false, "float parameter found");
            continue;
        }

        *pf = spec.value;
        auto& param = static_cast<juce::AudioProcessorParameter&> (*pf);
        const juce::String text = param.getText (pf->convertTo0to1 (spec.value), 64);
        const float back = pf->convertFrom0to1 (param.getValueForText (text));

        char msg[128];
        std::snprintf (msg, sizeof (msg), "%s: %+.3f renders \"%s\" -> %.3f",
                       spec.id, (double) spec.value, text.toRawUTF8(), (double) back);
        check (std::abs (back - spec.value) <= spec.displayStep + 1.0e-4f, msg);
    }

    // The negative dB case must format with an explicit sign and two decimals.
    {
        auto* pf = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter ("input"));
        jassertquiet (pf != nullptr);
        *pf = -3.567f;
        auto& param = static_cast<juce::AudioProcessorParameter&> (*pf);
        const juce::String text = param.getText (pf->convertTo0to1 (-3.567f), 64);
        check (text == "-3.57 dB", "input -3.567 shows \"-3.57 dB\"");
    }
}

//------------------------------------------------------------------------------
// Stress sweep: one compact table-driven loop over every parameter combination
// x signal x sample rate.  Guards against nonlinear-stage saturation (the
// historical bug: sparkle blew loud highs up to near full scale).

void testStressSweep()
{
    std::puts ("test 8: stress sweep (time x link x sparkle x input x threshold x signal x rate)");

    struct Sig
    {
        const char* name;
        bool isSilence;
        float (*fill) (int c, int i, double sr);
    };

    const Sig sigs[] =
    {
        { "silence",       true,  [] (int, int, double) { return 0.0f; } },
        { "100 Hz L=R",    false, [] (int, int i, double sr) { return sineAt (100.0, i, sr); } },
        { "1 kHz L=R",     false, [] (int, int i, double sr) { return sineAt (1000.0, i, sr); } },
        { "9 kHz L=R",     false, [] (int, int i, double sr) { return sineAt (9000.0, i, sr); } },
        { "100 Hz L-only", false, [] (int c, int i, double sr) { return c == 0 ? sineAt (100.0, i, sr) : 0.0f; } },
        { "1 kHz L-only",  false, [] (int c, int i, double sr) { return c == 0 ? sineAt (1000.0, i, sr) : 0.0f; } },
        { "9 kHz L-only",  false, [] (int c, int i, double sr) { return c == 0 ? sineAt (9000.0, i, sr) : 0.0f; } },
        { "noise FS",      false, [] (int c, int i, double) { return noiseAt (c, i); } },
    };
    constexpr int numSigs = static_cast<int> (std::size (sigs));
    constexpr int skipSamples = 4096;      // settling: attack transients excluded
    constexpr double silenceLimit = 1.0e-9;
    constexpr double peakLimit = 2.0;      // +6 dBFS

    std::vector<std::string> worstWhere (numSigs);
    std::vector<double> worstPeak (numSigs, 0.0);
    int stressFails = 0;
    int runs = 0;

    auto runOne = [&] (glint::Engine& engine, double sr, const Sig& sig,
                       int totalSamples, int blockSize, const char* where)
    {
        juce::AudioBuffer<float> buf (2, blockSize);
        double peak = 0.0;
        bool finite = true;
        int produced = 0;
        while (produced < totalSamples)
        {
            const int n = juce::jmin (blockSize, totalSamples - produced);
            buf.setSize (2, n, false, false, false);
            for (int c = 0; c < 2; ++c)
            {
                auto* w = buf.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                    w[i] = sig.fill (c, produced + i, sr);
            }
            engine.process (buf);
            for (int c = 0; c < 2; ++c)
            {
                const auto* r = buf.getReadPointer (c);
                for (int i = 0; i < n; ++i)
                {
                    if (! std::isfinite (r[i]))
                        finite = false;
                    if (produced + i >= skipSamples)
                        peak = juce::jmax (peak, static_cast<double> (std::abs (r[i])));
                }
            }
            produced += n;
        }

        ++runs;
        const double limit = sig.isSilence ? silenceLimit : peakLimit;
        const bool ok = finite && peak < limit;
        const size_t si = static_cast<size_t> (&sig - sigs);
        if (peak > worstPeak[si])
        {
            worstPeak[si] = peak;
            worstWhere[si] = where;
        }
        if (! ok)
        {
            std::printf ("    FAIL: %s | %s | peak %.6g (limit %.1g)%s\n",
                         where, sig.name, peak, limit, finite ? "" : " | non-finite output");
            ++stressFails;
        }
    };

    for (const double sr : { 44100.0, 96000.0 })
        for (int timeMode = 1; timeMode <= glint::numTimeModes; ++timeMode)
            for (const int msInt : { 0, 1 })
                for (const float sparkle : { 0.0f, 1.0f })
                    for (const float inputDb : { 0.0f, 20.0f })
                        for (const float threshold : { 0.0f, 10.0f })
                        {
                            char where[160];
                            std::snprintf (where, sizeof (where),
                                           "t%d %s spark%.0f in%+.0f thr%.0f @%.0fk",
                                           timeMode, msInt ? "M/S" : "ST", sparkle * 100.0f,
                                           inputDb, threshold, sr * 0.001);

                            glint::Engine e;
                            e.prepare (sr, 512);
                            glint::EngineParameters p;   // mix 100, output 0 dB
                            p.inputDb = inputDb;
                            p.thresholdKnob = threshold;
                            p.timeMode = timeMode;
                            p.midSide = msInt != 0;
                            p.sparkle01 = sparkle;
                            e.setParameters (p);

                            const int total = static_cast<int> (0.5 * sr);
                            for (const auto& sig : sigs)
                            {
                                e.reset();
                                runOne (e, sr, sig, total, 512, where);
                            }
                        }

    // Tiny-block spot check: worst-case combo, 1 kHz L=R sine, block size 1.
    {
        glint::Engine e;
        e.prepare (44100.0, 512);
        glint::EngineParameters p;
        p.inputDb = 20.0f;
        p.thresholdKnob = 0.0f;
        p.timeMode = 6;
        p.sparkle01 = 1.0f;
        e.setParameters (p);
        e.reset();
        runOne (e, 44100.0, sigs[2], static_cast<int> (0.5 * 44100.0), 1, "t6 ST spark100 in+20 thr0 bs1 @44.1k");
    }

    std::printf ("    %d runs. Worst-case output peak per signal (after %d-sample settling):\n",
                 runs, skipSamples);
    for (int si = 0; si < numSigs; ++si)
        std::printf ("      %-14s %.4f  (%s)\n", sigs[si].name, worstPeak[si],
                     worstWhere[si].c_str());

    char msg[64];
    std::snprintf (msg, sizeof (msg), "stress sweep: %d runs, all finite and bounded", runs);
    check (stressFails == 0, msg);
}

} // namespace

int main()
{
    testSilenceAndFinite();
    testGrOrdering();
    testReleaseOrdering();
    testMixZeroNull();
    testMidSideIdentical();
    testSparkleAir();
    testStateRoundTrip();
    testParameterTextRoundTrip();
    testStressSweep();

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
