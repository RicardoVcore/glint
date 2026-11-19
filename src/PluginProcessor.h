#pragma once

#include "GlintDsp.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** Glint — variable-mu tube compressor (Glint Audio).

    VST3 + Standalone; UI for this slice is juce::GenericAudioProcessorEditor.
*/
class GlintAudioProcessor : public juce::AudioProcessor
{
public:
    GlintAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override
    {
        return new juce::GenericAudioProcessorEditor (*this);
    }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Glint"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Current gain reduction in dB, for a future meter. */
    std::atomic<float>& getGainReductionDb() noexcept { return engine.gainReductionDb; }

    // Exposed for tests and a future custom UI.
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    glint::Engine engine;
    bool prepared = false;

    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* linkParam = nullptr;
    std::atomic<float>* sparkleParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlintAudioProcessor)
};
