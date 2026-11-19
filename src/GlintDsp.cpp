#include "GlintDsp.h"

#include <utility>

namespace glint
{

//==============================================================================
// GainComputer

void GainComputer::prepare (double sampleRate) noexcept
{
    fs = sampleRate;
    gainSmoothCoef = 1.0f - static_cast<float> (std::exp (-1.0 / (0.001 * fs)));
    reset();
}

void GainComputer::reset() noexcept
{
    for (auto& s : stages)
        s.z = 0.0f;
    gSmooth = 1.0f;
    lastGrDb = 0.0f;
}

void GainComputer::setThresholdDb (float db) noexcept
{
    thresholdLin = static_cast<float> (std::pow (10.0, db / 20.0));
}

void GainComputer::setTimeMode (int mode1to6) noexcept
{
    const int mode = juce::jlimit (1, numTimeModes, mode1to6);
    if (mode == timeMode)
        return;

    struct TC
    {
        float atkMs;
        float rel[3];
        int n;
    };

    static constexpr TC table[numTimeModes] =
    {
        { 0.2f, { 0.3f,  0.0f,  0.0f }, 1 },
        { 0.2f, { 0.8f,  0.0f,  0.0f }, 1 },
        { 0.4f, { 2.0f,  0.0f,  0.0f }, 1 },
        { 0.8f, { 5.0f,  0.0f,  0.0f }, 1 },
        { 0.2f, { 2.0f, 10.0f,  0.0f }, 2 },
        { 0.4f, { 0.3f, 10.0f, 25.0f }, 3 },
    };

    const auto& tc = table[mode - 1];
    numStages = tc.n;

    for (int j = 0; j < numStages; ++j)
    {
        const float relS = tc.rel[j];
        // Slow stages (release > 1 s) only charge on sustained level.
        const float atkS = relS > 1.0f ? relS / 50.0f : tc.atkMs * 0.001f;
        stages[static_cast<size_t> (j)].atkCoef
            = 1.0f - static_cast<float> (std::exp (-1.0 / (static_cast<double> (atkS) * fs)));
        stages[static_cast<size_t> (j)].relCoef
            = 1.0f - static_cast<float> (std::exp (-1.0 / (static_cast<double> (relS) * fs)));
    }

    timeMode = mode;
}

float GainComputer::advance (float rectifiedOutput) noexcept
{
    // Full-wave rectified output level -> linear excess ratio above threshold.
    float e = rectifiedOutput / thresholdLin - 1.0f;
    if (e < 0.0f)
        e = 0.0f;

    float cv = 0.0f;
    for (int j = 0; j < numStages; ++j)
    {
        auto& s = stages[static_cast<size_t> (j)];
        const float coef = e > s.z ? s.atkCoef : s.relCoef;
        s.z += coef * (e - s.z);
        cv = juce::jmax (cv, s.z);
    }

    const float c = cv / gainCellK;
    const float t = c * std::sqrt (c);
    const float g = 1.0f / (1.0f + t);

    // Extra ~1 ms smoothing on top of the RC envelope to avoid zipper noise.
    gSmooth += gainSmoothCoef * (g - gSmooth);

    lastGrDb = -20.0f * std::log10 (juce::jmax (gSmooth, 1.0e-6f));
    return gSmooth;
}

//==============================================================================
// ColourStage

void GainComputer::copyStateFrom (const GainComputer& other) noexcept
{
    stages = other.stages;
    gSmooth = other.gSmooth;
    lastGrDb = other.lastGrDb;
}

ColourStage::ColourStage()
    // Linear-phase FIR half-band (non-maximum-quality design): the round
    // trip is a pure integer-sample delay at every frequency, so the dry
    // path can be latency-aligned exactly and mix=0 nulls against the
    // delayed input. The polyphase IIR alternative has a frequency-dependent,
    // non-integer group delay (and JUCE's allpass-based integer-latency
    // compensation for it is only approximate), which smears the dry/wet mix.
    : overs (1, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, false)
{
}

void ColourStage::prepare (double baseSampleRate, int maxBlockSize)
{
    overs.initProcessing (static_cast<size_t> (juce::jmax (1, maxBlockSize)));

    const double fsO = 2.0 * baseSampleRate; // oversampled rate

    *lfLP.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (fsO, 120.0, 0.7071);
    *hpAir1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (fsO, 7000.0, 0.7071);
    *hpAir2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (fsO, 7000.0, 0.7071);

    // One-pole HF rolloff, ~1 dB @ 18 kHz (absolute frequency, any sample rate).
    hfCoef = 1.0f - static_cast<float> (
        std::exp (-2.0 * juce::MathConstants<double>::pi * 35400.0 / fsO));

    // Character smoothing: one-pole, ~10 ms at the oversampled rate.
    charCoef = 1.0f - static_cast<float> (std::exp (-1.0 / (0.01 * fsO)));

    reset();
}

void ColourStage::reset()
{
    overs.reset();
    lfLP.reset();
    hpAir1.reset();
    hpAir2.reset();
    hfZ = 0.0f;
    biasTarget = 0.0f;
    sparkleTarget = 0.0f;
    bias = 0.0f;
    sparkle = 0.0f;
}

void ColourStage::setCharacter (float normGR01, float sparkle01) noexcept
{
    // Block rate: only set the targets.  The applied values are smoothed
    // per oversampled sample in inner(), so block-sized parameter steps
    // cannot zipper the tube bias or the sparkle amount.
    // Bias shift grows with gain reduction -> a little 2nd harmonic when
    // the compressor works hard.
    biasTarget = 0.15f * juce::jlimit (0.0f, 1.0f, normGR01);
    // Sparkle opens up as the compressor works.
    sparkleTarget = sparkle01 * (0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, normGR01));
}

inline float ColourStage::inner (float x) noexcept
{
    // Smooth the character values per oversampled sample (~10 ms one-pole).
    bias += charCoef * (biasTarget - bias);
    sparkle += charCoef * (sparkleTarget - sparkle);

    // Push-pull tube: symmetric soft clip + GR-dependent bias shift.
    float y = (std::tanh (tubeK * (x + bias)) - std::tanh (tubeK * bias)) / tubeK;

    // Transformer: gently saturate the LF band and sum it back.
    const float lp = lfLP.processSample (y);
    const float sat = std::tanh (1.3f * lp) * (1.0f / 1.3f);
    y += 0.8f * (sat - lp);

    // Mild HF rolloff (~1 dB @ 18 kHz).
    hfZ += hfCoef * (y - hfZ);
    y = hfZ;

    // Sparkle: highpass, then full-wave rectify the band.  The generator is
    // degree-1 homogeneous, so the added air scales LINEARLY with the band
    // level (the old band^2 saturator was quadratic at low level and
    // saturated to full scale once a loud band hit ~-26 dBFS, blowing loud
    // cymbals up).  airGain keeps the added air about 12-14 dB below the
    // >7 kHz band at sparkle=100; highpass again so only the NEW upper
    // harmonics and intermodulation are added, never cancelling the
    // existing highs.
    // Run the air filters every sample even while the add-back is gated:
    // a filter whose state freezes while sparkle is off produces a short
    // transient on re-engagement.  With the add-back gated, sparkle=0 is
    // still an exact bypass (the filters are linear, zero in -> zero out).
    const float band = hpAir1.processSample (y);
    const float s = hpAir2.processSample (std::abs (band));
    if (sparkle > 1.0e-6f)
        y += sparkle * airGain * s;

    return y;
}

void ColourStage::processBlock (float* samples, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    float* channelPtrs[1] = { samples };
    auto block = juce::dsp::AudioBlock<float> (channelPtrs, 1, static_cast<size_t> (numSamples));
    auto up = overs.processSamplesUp (block);

    auto* upData = up.getChannelPointer (0);
    const int n = static_cast<int> (up.getNumSamples());
    for (int i = 0; i < n; ++i)
        upData[i] = inner (upData[i]);

    overs.processSamplesDown (block);
}

//==============================================================================
// Engine

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    const int maxBlock = juce::jmax (1, maxBlockSize);

    for (auto& c : comps)
        c.prepare (fs);
    for (auto& s : stages)
        s.prepare (fs, maxBlock);

    latencySamples = juce::jmax (1, static_cast<int> (std::lround (stages[0].getLatencyInSamples())));

    for (auto& d : dryDelay)
        d.prepare (latencySamples);

    for (auto& row : scratch)
        row.assign (static_cast<size_t> (maxBlock), 0.0f);

    inDbS.reset (fs, 0.02);
    outDbS.reset (fs, 0.02);
    mixS.reset (fs, 0.05);
    sparkS.reset (fs, 0.05);

    reset();
}

void Engine::reset()
{
    for (auto& c : comps)
        c.reset();
    for (auto& s : stages)
        s.reset();
    for (auto& d : dryDelay)
    {
        std::fill (d.buf.begin(), d.buf.end(), 0.0f);
        d.pos = 0;
    }
    prevRect = { 0.0f, 0.0f };

    inDbS.setCurrentAndTargetValue (params.inputDb);
    outDbS.setCurrentAndTargetValue (params.outputDb);
    mixS.setCurrentAndTargetValue (params.mix01);
    sparkS.setCurrentAndTargetValue (params.sparkle01);

    gainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void Engine::setParameters (const EngineParameters& p) noexcept
{
    // Stereo link runs only comps[0], so comps[1]'s detector envelope goes
    // stale; when the link mode changes, resume the newly independent
    // channel from comps[0]'s live state instead of its stale one.
    const bool msChanged = (p.midSide != msMode);

    params = p;
    msMode = p.midSide;

    // On the first call after prepare(), start the smoothing from the actual
    // parameter values instead of ramping away from the internal defaults.
    const bool snap = std::exchange (snapSmoothersOnNextSet, false);

    if (snap)
    {
        inDbS.setCurrentAndTargetValue (p.inputDb);
        outDbS.setCurrentAndTargetValue (p.outputDb);
        mixS.setCurrentAndTargetValue (p.mix01);
        sparkS.setCurrentAndTargetValue (p.sparkle01);
    }
    else
    {
        inDbS.setTargetValue (p.inputDb);
        outDbS.setTargetValue (p.outputDb);
        mixS.setTargetValue (p.mix01);
        sparkS.setTargetValue (p.sparkle01);
    }

    const float threshDb = thresholdKnobToDb (p.thresholdKnob);
    for (auto& c : comps)
    {
        c.setThresholdDb (threshDb);
        c.setTimeMode (p.timeMode);
    }

    if (msChanged)
        comps[1].copyStateFrom (comps[0]);
}

void Engine::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    if (n <= 0 || numCh <= 0 || fs <= 0.0)
        return;

    float* ch[2] = { buffer.getWritePointer (0),
                     numCh > 1 ? buffer.getWritePointer (1) : nullptr };

    float* comp[2] = { scratch[0].data(), scratch[1].data() };
    float* dry[2]  = { scratch[2].data(), scratch[3].data() };
    float* ogArr   = scratch[4].data();
    float* mxArr   = scratch[5].data();
    float* sparkArr = scratch[6].data();

    for (int i = 0; i < n; ++i)
    {
        const float ig = std::pow (10.0f, inDbS.getNextValue() / 20.0f);
        ogArr[i] = std::pow (10.0f, outDbS.getNextValue() / 20.0f);
        mxArr[i] = mixS.getNextValue();
        sparkArr[i] = sparkS.getNextValue();

        const float raw[2] = { ch[0][i], numCh > 1 ? ch[1][i] : 0.0f };

        float x[2] = { raw[0], raw[1] };
        if (msMode && numCh > 1)
        {
            x[0] = 0.5f * (raw[0] + raw[1]);
            x[1] = 0.5f * (raw[0] - raw[1]);
        }

        float g[2];
        if (! msMode && numCh > 1)
        {
            // Stereo link: one detector on max(|L|, |R|) drives both channels.
            g[0] = g[1] = comps[0].advance (juce::jmax (prevRect[0], prevRect[1]));
        }
        else
        {
            for (int c = 0; c < numCh; ++c)
                g[c] = comps[c].advance (prevRect[c]);
        }

        for (int c = 0; c < numCh; ++c)
        {
            x[c] = x[c] * ig * g[c];
            prevRect[c] = std::abs (x[c]);   // detector reads the cell output
        }

        dry[0][i] = dryDelay[0].process (raw[0]);
        if (numCh > 1)
            dry[1][i] = dryDelay[1].process (raw[1]);

        comp[0][i] = x[0];
        if (numCh > 1)
            comp[1][i] = x[1];
    }

    // Tube / transformer / sparkle colour (2x oversampled).
    const float grDb = msMode && numCh > 1
        ? juce::jmax (comps[0].getCurrentGainReductionDb(), comps[1].getCurrentGainReductionDb())
        : comps[0].getCurrentGainReductionDb();
    const float normGR = juce::jlimit (0.0f, 1.0f, grDb / maxNormGrDb);

    // Sparkle amount is applied at block rate (smoothed across blocks); the
    // detector/gain smoothing is per sample.
    const float sparkBlock = sparkArr[static_cast<size_t> (n - 1)];

    for (int c = 0; c < numCh; ++c)
    {
        stages[c].setCharacter (normGR, sparkBlock);
        stages[c].processBlock (comp[c], n);
    }

    // Mid/side decode, mix with the latency-aligned dry path, output gain.
    for (int i = 0; i < n; ++i)
    {
        float wet[2] = { comp[0][i], numCh > 1 ? comp[1][i] : 0.0f };
        if (msMode && numCh > 1)
        {
            const float m = wet[0];
            const float s = wet[1];
            wet[0] = m + s;
            wet[1] = m - s;
        }

        for (int c = 0; c < numCh; ++c)
        {
            const float y = mxArr[i] * wet[c] + (1.0f - mxArr[i]) * dry[c][i];
            ch[c][i] = y * ogArr[i];
        }
    }

    gainReductionDb.store (grDb, std::memory_order_relaxed);
}

} // namespace glint
