#include "PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>

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
}

juce::AudioProcessorValueTreeState::ParameterLayout GlintAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    using Attribute = juce::AudioParameterFloatAttributes;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input", 1 }, "Input",
        juce::NormalisableRange<float> (-20.0f, 20.0f, 0.01f), 0.0f,
        Attribute().withLabel (" dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "threshold", 1 }, "Threshold",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "time", 1 }, "Time",
        juce::StringArray { "1", "2", "3", "4", "5", "6" }, 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "link", 1 }, "Link",
        juce::StringArray { "Stereo", "Mid/Side" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sparkle", 1 }, "Sparkle",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,
        Attribute().withLabel (" %")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attribute().withLabel (" %")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (-20.0f, 20.0f, 0.01f), 0.0f,
        Attribute().withLabel (" dB")));

    return layout;
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
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GlintAudioProcessor();
}
