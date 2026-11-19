#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace
{
    /** Value-box text for a dB parameter: two decimals with an explicit sign
        ("+3.50 dB"), so the plug-in UI and the host display agree. */
    juce::String dBText (float v, int)
    {
        const float rounded = std::round (v * 100.0f) / 100.0f;
        const char* sign = rounded < 0.0f ? "-" : (rounded > 0.0f ? "+" : "");
        return juce::String (sign) + juce::String (std::abs (rounded), 2) + " dB";
    }

    /** Value-box text factory: fixed decimals plus an optional unit suffix
        (Threshold "3.0", Sparkle/Mix "30 %"). */
    auto fixedPointText (int decimals, const char* unit)
    {
        return [decimals, unit] (float v, int)
        {
            if (decimals == 0)
                return juce::String (juce::roundToInt (v)) + unit;
            return juce::String (v, decimals) + unit;
        };
    }

    /** Text->value conversion for a value box: a tolerant float parse (so
        "3", "3 dB" and "+3.0" all work), clamped to the parameter range and
        snapped to its step size. */
    auto textToValue (float rangeStart, float rangeEnd, float interval)
    {
        return [rangeStart, rangeEnd, interval] (const juce::String& text)
        {
            const auto range = juce::NormalisableRange<float> (rangeStart, rangeEnd, interval);
            return range.snapToLegalValue (juce::jlimit (rangeStart, rangeEnd, text.getFloatValue()));
        };
    }
}

GlintAudioProcessor::GlintAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createParameterLayout())
{
    inputParam = apvts.getRawParameterValue ("input");
    thresholdParam = apvts.getRawParameterValue ("threshold");
    timeParam = apvts.getRawParameterValue ("time");
    linkParam = apvts.getRawParameterValue ("link");
    sparkleParam = apvts.getRawParameterValue ("sparkle");
    mixParam = apvts.getRawParameterValue ("mix");
    outputParam = apvts.getRawParameterValue ("output");

    // Restore the persisted preset index (default 0 = Init for fresh states).
    const auto stored = apvts.state.getProperty (presetIndexPropertyId, -1);
    if (static_cast<int> (stored) >= 0 && static_cast<int> (stored) < glint::numPresets)
        currentProgram = static_cast<int> (stored);
    else
        apvts.state.setProperty (presetIndexPropertyId, 0, nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout GlintAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    using Attribute = juce::AudioParameterFloatAttributes;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input", 1 }, "Input",
        juce::NormalisableRange<float> (-20.0f, 20.0f, 0.01f), 0.0f,
        Attribute().withLabel (" dB")
                   .withStringFromValueFunction (dBText)
                   .withValueFromStringFunction (textToValue (-20.0f, 20.0f, 0.01f))));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "threshold", 1 }, "Threshold",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f,
        Attribute().withStringFromValueFunction (fixedPointText (1, ""))
                   .withValueFromStringFunction (textToValue (0.0f, 10.0f, 0.01f))));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "time", 1 }, "Time",
        juce::StringArray { "1", "2", "3", "4", "5", "6" }, 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "link", 1 }, "Link",
        juce::StringArray { "Stereo", "Mid/Side" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sparkle", 1 }, "Sparkle",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,
        Attribute().withLabel (" %")
                   .withStringFromValueFunction (fixedPointText (0, " %"))
                   .withValueFromStringFunction (textToValue (0.0f, 100.0f, 0.1f))));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attribute().withLabel (" %")
                   .withStringFromValueFunction (fixedPointText (0, " %"))
                   .withValueFromStringFunction (textToValue (0.0f, 100.0f, 0.1f))));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (-20.0f, 20.0f, 0.01f), 0.0f,
        Attribute().withLabel (" dB")
                   .withStringFromValueFunction (dBText)
                   .withValueFromStringFunction (textToValue (-20.0f, 20.0f, 0.01f))));

    return layout;
}

juce::AudioProcessorEditor* GlintAudioProcessor::createEditor()
{
    return new glint::GlintEditor (*this);
}

const juce::String GlintAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, glint::numPresets)
               ? juce::String (glint::factoryPresets[static_cast<size_t> (index)].name)
               : juce::String();
}

void GlintAudioProcessor::setCurrentProgram (int index)
{
    applyPreset (index);
}

void GlintAudioProcessor::applyPreset (int index)
{
    index = juce::jlimit (0, glint::numPresets - 1, index);
    currentProgram = index;
    apvts.state.setProperty (presetIndexPropertyId, index, nullptr);

    const auto& preset = glint::factoryPresets[static_cast<size_t> (index)];

    // One begin/setValueNotifyingHost/end pair per parameter, so the host
    // sees exactly one gesture (one undo step / automation event per knob).
    auto set = [] (juce::RangedAudioParameter* p, float raw)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (raw));
        p->endChangeGesture();
    };

    set (apvts.getParameter ("input"),     preset.inputDb);
    set (apvts.getParameter ("threshold"), preset.thresholdKnob);
    set (apvts.getParameter ("time"),      static_cast<float> (preset.timeMode - 1));
    set (apvts.getParameter ("link"),      preset.midSide ? 1.0f : 0.0f);
    set (apvts.getParameter ("sparkle"),   preset.sparklePct);
    set (apvts.getParameter ("mix"),       preset.mixPct);
    set (apvts.getParameter ("output"),    preset.outputDb);
}

void GlintAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);

    // Start the stream at the current parameter values (no ramp from
    // internal defaults).
    glint::EngineParameters p;
    p.inputDb = inputParam->load (std::memory_order_relaxed);
    p.outputDb = outputParam->load (std::memory_order_relaxed);
    p.thresholdKnob = thresholdParam->load (std::memory_order_relaxed);
    p.timeMode = 1 + static_cast<int> (timeParam->load (std::memory_order_relaxed));
    p.midSide = linkParam->load (std::memory_order_relaxed) > 0.5f;
    p.sparkle01 = juce::jlimit (0.0f, 1.0f, sparkleParam->load (std::memory_order_relaxed) / 100.0f);
    p.mix01 = juce::jlimit (0.0f, 1.0f, mixParam->load (std::memory_order_relaxed) / 100.0f);
    engine.setParameters (p);

    setLatencySamples (engine.getLatencySamples());
    prepared = true;
}

void GlintAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (! prepared)
    {
        buffer.clear();
        return;
    }

    glint::EngineParameters p;
    p.inputDb = inputParam->load (std::memory_order_relaxed);
    p.outputDb = outputParam->load (std::memory_order_relaxed);
    p.thresholdKnob = thresholdParam->load (std::memory_order_relaxed);
    p.timeMode = 1 + static_cast<int> (timeParam->load (std::memory_order_relaxed));
    p.midSide = linkParam->load (std::memory_order_relaxed) > 0.5f;
    p.sparkle01 = juce::jlimit (0.0f, 1.0f, sparkleParam->load (std::memory_order_relaxed) / 100.0f);
    p.mix01 = juce::jlimit (0.0f, 1.0f, mixParam->load (std::memory_order_relaxed) / 100.0f);

    engine.setParameters (p);
    engine.process (buffer);
}

bool GlintAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void GlintAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GlintAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes);
        xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

        // The restored state carries its own parameter values — only read
        // back the preset index, never re-apply the preset on top of them.
        const int restored = static_cast<int> (
            apvts.state.getProperty (presetIndexPropertyId, currentProgram));
        currentProgram = juce::jlimit (0, glint::numPresets - 1, restored);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GlintAudioProcessor();
}
