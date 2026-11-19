/**
    Factory-preset calibration tool (run manually, NOT a CTest test).

    For each preset the engine is driven with the shared reference signal
    (tests/ReferenceSignal.h: stereo pink noise, -18 dBFS RMS, 0.7 L/R
    correlation, 48 kHz, 20 s; measured over the last 10 s):

      * input starts at 0 dB; the threshold knob is bisected over 0..10 until
        the mean gain reduction hits the preset's target (+/- 0.25 dB).  If
        threshold 10 cannot reach the target, input rises in 0.5 dB steps.
      * output is loudness-matched: the gain (in dB) that makes the processed
        RMS (after mix) equal to the input RMS, clamped to the parameter
        range.

    The printed table is pasted into src/GlintPresets.h.

    Build: the `glint_calibrate` target.  Run from the repo root:
      ./build/glint_calibrate
*/

#include "GlintDsp.h"
#include "GlintPresets.h"
#include "ReferenceSignal.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using glint::refsig::StereoSignal;

/** Mean gain reduction (dB) of one engine pass at the given knob settings. */
double meanGrAt (const glint::Preset& preset, const StereoSignal& sig,
                 float inputDb, float thresholdKnob)
{
    glint::EngineParameters p;
    p.inputDb = inputDb;
    p.outputDb = 0.0f;   // output gain sits after the mix; irrelevant to GR
    p.thresholdKnob = thresholdKnob;
    p.timeMode = preset.timeMode;
    p.midSide = preset.midSide;
    p.sparkle01 = preset.sparklePct / 100.0f;
    p.mix01 = preset.mixPct / 100.0f;
    return glint::refsig::measure (p, sig).meanGrDb;
}

struct Calibration
{
    float inputDb = 0.0f;
    float thresholdKnob = 0.0f;
    float outputDb = 0.0f;
    double grErrDb = 0.0;
};

Calibration calibrate (const glint::Preset& preset, const StereoSignal& sig)
{
    const double target = preset.targetGrDb;
    constexpr double tolerance = 0.25;

    // Threshold bisection at input 0 dB.  GR rises monotonically with the
    // threshold knob, so: f(knob) = gr(knob) - target, f(0) < 0 < f(10).
    float inputDb = 0.0f;
    double fHi = meanGrAt (preset, sig, inputDb, 10.0f) - target;
    while (fHi < -tolerance && inputDb < 20.0f)
    {
        inputDb += 0.5f;
        fHi = meanGrAt (preset, sig, inputDb, 10.0f) - target;
    }

    float lo = 0.0f, hi = 10.0f;
    double fLo = meanGrAt (preset, sig, inputDb, lo) - target;

    float threshold = 10.0f;
    double grErr = std::abs (fHi);

    if (fLo >= 0.0)
    {
        // GR already exceeds the target at the lowest threshold: knob 0 is
        // as low as the parameter goes.
        threshold = 0.0f;
        grErr = fLo;
    }
    else if (fHi < -tolerance)
    {
        // Even threshold 10 at the input ceiling cannot reach the target.
        grErr = -fHi;
    }
    else
    {
        while (hi - lo > 0.002f && grErr > 0.05)
        {
            const float mid = 0.5f * (lo + hi);
            const double f = meanGrAt (preset, sig, inputDb, mid) - target;
            if (f < 0.0)
                lo = mid;
            else
                hi = mid;
            grErr = std::abs (f);
            threshold = mid;
        }

        // Snap to the parameter's 0.01 grid and pick the best candidate
        // around the bracket (parameter values must be expressible exactly).
        const int kLo = static_cast<int> (std::floor (lo * 100.0f)) - 1;
        const int kHi = static_cast<int> (std::ceil (hi * 100.0f)) + 1;
        for (int k = kLo; k <= kHi; ++k)
        {
            const float t = static_cast<float> (k) / 100.0f;
            if (t < 0.0f || t > 10.0f)
                continue;
            const double f = std::abs (meanGrAt (preset, sig, inputDb, t) - target);
            if (f < grErr)
            {
                grErr = f;
                threshold = t;
            }
        }
    }

    // Output loudness match: with output at 0 dB the engine's processed RMS
    // (after mix) is m.outRmsDb; the output gain path is strictly linear, so
    // the needed gain is the difference to the input RMS.
    glint::EngineParameters p;
    p.inputDb = inputDb;
    p.outputDb = 0.0f;
    p.thresholdKnob = threshold;
    p.timeMode = preset.timeMode;
    p.midSide = preset.midSide;
    p.sparkle01 = preset.sparklePct / 100.0f;
    p.mix01 = preset.mixPct / 100.0f;
    const auto m = glint::refsig::measure (p, sig);

    double outputDb = m.inRmsDb - m.outRmsDb;
    outputDb = juce::jlimit (-20.0, 20.0, outputDb);
    outputDb = std::round (outputDb * 100.0) / 100.0;   // parameter step 0.01

    Calibration c;
    c.inputDb = inputDb;
    c.thresholdKnob = threshold;
    c.outputDb = static_cast<float> (outputDb);
    c.grErrDb = grErr;
    return c;
}

} // namespace

int main()
{
    const auto sig = glint::refsig::makeReferenceSignal();

    const auto dateText = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
    const char* dateStr = dateText.toRawUTF8();

    std::vector<Calibration> calibrations (static_cast<size_t> (glint::numPresets));
    bool allOk = true;

    for (int i = 0; i < glint::numPresets; ++i)
    {
        const auto& preset = glint::factoryPresets[static_cast<size_t> (i)];
        std::printf ("preset %d \"%s\" (target %.1f dB GR) ... ",
                     i, preset.name, (double) preset.targetGrDb);
        std::fflush (stdout);

        if (i == 0)
        {
            // Init keeps the factory defaults, not calibrated.
            std::printf ("defaults (input 0, threshold 3, output 0)\n");
            continue;
        }

        const auto c = calibrate (preset, sig);
        calibrations[static_cast<size_t> (i)] = c;
        std::printf ("input %+.1f dB, threshold %.2f, output %+.2f dB, GR error %+.2f dB\n",
                     (double) c.inputDb, (double) c.thresholdKnob,
                     (double) c.outputDb, c.grErrDb);
        if (c.grErrDb > 0.25)
            allOk = false;
    }

    std::printf ("\nMeasurement window: last 10 s of the 20 s reference; mean GR is the\n"
                 "sample-weighted average of the engine's per-block detector value.\n");
    if (! allOk)
        std::printf ("WARNING: at least one preset missed the +/- 0.25 dB GR tolerance.\n");

    std::printf ("\nReady-to-paste table for src/GlintPresets.h:\n\n");
    std::printf ("    // Factory preset calibration — measured %s.\n", dateStr);
    std::printf ("    // Reference signal: stereo Voss-McCartney pink noise (12 rows, fixed\n"
                 "    // seeds), -18 dBFS RMS per channel, L/R correlation 0.7, 48 kHz, 20 s;\n"
                 "    // mean gain reduction and RMS measured over the last 10 s.\n");
    std::printf ("    inline constexpr Preset factoryPresets[numPresets]\n    {\n");
    for (int i = 0; i < glint::numPresets; ++i)
    {
        const auto& preset = glint::factoryPresets[static_cast<size_t> (i)];
        const auto& c = calibrations[static_cast<size_t> (i)];
        std::printf ("        { \"%s\",\n"
                     "          %d, %s, %.0ff, %.0ff, %.1ff,   // time, link, sparkle, mix, target GR\n"
                     "          %.1ff, %.2ff, %.2ff },   // measured: input, threshold, output\n",
                     preset.name, preset.timeMode, preset.midSide ? "true" : "false",
                     (double) preset.sparklePct, (double) preset.mixPct,
                     (double) preset.targetGrDb,
                     i == 0 ? 0.0 : (double) c.inputDb,
                     i == 0 ? 3.0 : (double) c.thresholdKnob,
                     i == 0 ? 0.0 : (double) c.outputDb);
    }
    std::printf ("    };\n");

    return allOk ? 0 : 1;
}
