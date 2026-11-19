#pragma once

#include "GlintLookAndFeel.h"
#include "PluginProcessor.h"

namespace glint
{

// Knob-row geometry below the knobs: the name labels and the always-visible
// value box underneath each knob (fixed rows, independent of knob size).
inline constexpr int knobLabelY     = 332; // top of the knob name labels
inline constexpr int knobValueBoxY  = 348; // top of the value boxes
inline constexpr int valueBoxHeight = 18;

/** Rotary knob with an always-visible, editable value box underneath
    (Slider::TextBoxBelow).  A single click on the box opens the in-place
    text editor (Enter applies, Escape cancels, focus loss applies); the
    typed text is converted by the attached parameter's own text<->value
    functions through the SliderAttachment, so out-of-range input is clamped
    and the change reaches the host as a proper gesture (automation and undo
    work).  Double-click on the knob body still resets to the parameter
    default, also courtesy of the attachment. */
class KnobSlider : public juce::Slider
{
public:
    KnobSlider()
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, valueBoxHeight);
        setTextBoxIsEditable (true);

        // Faceplate-styled value box: cream text on a dark inset panel with
        // a thin brass outline (never the L&F default white/blue).
        setColour (juce::Slider::textBoxTextColourId,       Palette::cream);
        setColour (juce::Slider::textBoxBackgroundColourId, Palette::knobBodyDark);
        setColour (juce::Slider::textBoxOutlineColourId,    Palette::brassDim);
        setColour (juce::Slider::textBoxHighlightColourId,  Palette::amber.withAlpha (0.35f));
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobSlider)
};

/** Glint custom editor: a fixed 760 x 430 hardware faceplate with rotary
    knobs and an analog gain-reduction meter. */
class GlintEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit GlintEditor (GlintAudioProcessor& p);
    ~GlintEditor() override;

    void paint (juce::Graphics&) override;

    /** Runs one meter update; exposed so headless snapshot tools can drive
        the needle without a running message loop. */
    void tickMeter();

private:
    void timerCallback() override;

    void drawPanel (juce::Graphics& g);
    void drawWordmark (juce::Graphics& g);
    void drawMeter (juce::Graphics& g);

    /** Meter angle (JUCE convention, 0 = 12 o'clock clockwise) for a gain
        reduction in dB.  The tick values are spaced evenly around the arc,
        classic compressed GR-meter style. */
    float meterAngleFor (float grDb) const noexcept;

    GlintAudioProcessor& processor;
    GlintLookAndFeel lookAndFeel;

    struct Knob
    {
        KnobSlider slider;
        juce::Label label;
    };

    std::array<Knob, 6> knobs {};
    juce::Label linkLabel;          // "LINK" caption above the switch plate
    juce::ToggleButton linkSwitch;  // bounds = the recessed plate itself

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        inputAttach, thresholdAttach, timeAttach, sparkleAttach, mixAttach, outputAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttach;

    // Meter geometry (0 dB rests at the left of the arc).
    static constexpr float meterStartAngle = 5.06145f; // 290 deg
    static constexpr float meterEndAngle   = 7.50492f; // 430 deg (= 70 deg + 360)
    static constexpr float meterMaxDb = 20.0f;
    float grSmoothedDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlintEditor)
};

} // namespace glint
