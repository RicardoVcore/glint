#include "PluginEditor.h"

#include <array>
#include <cmath>

namespace glint
{

namespace
{
    /** Knob row specification: parameter id, label, drawn size in px, number
        of tick marks and an optional style hook for the LookAndFeel. */
    struct KnobSpec
    {
        const char* id;
        const char* name;
        int size;
        int ticks;
        const char* style; // "" | "time" | "sparkle"
    };

    constexpr KnobSpec knobSpecs[]
    {
        { "input",     "INPUT",     90,  9, "" },
        { "threshold", "THRESHOLD", 90, 11, "" },
        { "time",      "TIME",      90,  6, "time" },
        { "sparkle",   "SPARKLE",   70, 11, "sparkle" },
        { "mix",       "MIX",       70, 11, "" },
        { "output",    "OUTPUT",    70,  9, "" },
    };

    /** Gain-reduction meter scale anchors in dB.  The tick marks/labels and
        the needle angle mapping are both derived from this one table, so the
        scale can never drift apart. */
    constexpr float meterAnchorDb[] { 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 15.0f, 20.0f };

    /** Deterministic hash noise for the brushed-metal streaks. */
    float hash01 (int i) noexcept
    {
        const float s = std::sin ((float) i * 12.9898f) * 43758.5453f;
        return s - std::floor (s);
    }
}

GlintEditor::GlintEditor (GlintAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      inputAttach     (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, knobSpecs[0].id, knobs[0].slider)),
      thresholdAttach (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, knobSpecs[1].id, knobs[1].slider)),
      timeAttach      (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, knobSpecs[2].id, knobs[2].slider)),
      sparkleAttach   (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, knobSpecs[3].id, knobs[3].slider)),
      mixAttach       (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, knobSpecs[4].id, knobs[4].slider)),
      outputAttach    (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, knobSpecs[5].id, knobs[5].slider)),
      linkAttach      (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "link", linkSwitch))
{
    setLookAndFeel (&lookAndFeel);
    setSize (760, 400);
    setResizable (false, false);

    const int knobCentreY = 281;
    const int sliderBottom = knobValueBoxY + valueBoxHeight;
    float x = 30.0f;
    for (int i = 0; i < 6; ++i)
    {
        const auto& spec = knobSpecs[i];
        auto& k = knobs[i];

        if (spec.style[0] != '\0')
            k.slider.getProperties().set ("style", juce::var (spec.style));
        k.slider.getProperties().set ("numTicks", spec.ticks);
        k.slider.getProperties().set ("knobSize", spec.size);
        k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, false);
        // The slider component encloses the knob square, the label strip and
        // the value box; the LookAndFeel draws the knob in the top square.
        // The bounds are widened past the knob circle so the value box has
        // room for the longest text ("+20.00 dB").
        const int knobTop = knobCentreY - spec.size / 2;
        k.slider.setBounds (juce::roundToInt (x) - 12, knobTop, spec.size + 24, sliderBottom - knobTop);
        addAndMakeVisible (k.slider);

        k.label.setText (spec.name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold).withKerningFactor (0.12f)));
        k.label.setColour (juce::Label::textColourId, Palette::cream);
        k.label.setBounds (juce::roundToInt (x) - 10, knobLabelY, spec.size + 20, knobValueBoxY - knobLabelY - 2);
        k.label.setInterceptsMouseClicks (false, false); // let knob drags pass through
        addAndMakeVisible (k.label);

        x += (float) spec.size + 44.0f;
    }

    linkSwitch.setBounds (505, 186, 140, 48);
    addAndMakeVisible (linkSwitch);

    // 30 Hz meter polling with editor-side ballistics.
    startTimerHz (30);
}

GlintEditor::~GlintEditor()
{
    setLookAndFeel (nullptr);
}

void GlintEditor::paint (juce::Graphics& g)
{
    drawPanel (g);
    drawWordmark (g);
    drawMeter (g);
}

void GlintEditor::timerCallback()
{
    const float target = processor.getGainReductionDb().load (std::memory_order_relaxed);
    const float dt = 1.0f / 30.0f;
    // GR meters kick up quickly and fall back slowly (~300 ms release).
    const float tau = target > grSmoothedDb ? 0.04f : 0.3f;
    grSmoothedDb += (target - grSmoothedDb) * (1.0f - std::exp (-dt / tau));
    repaint (406, 22, 338, 162); // meter bezel area only
}

void GlintEditor::drawPanel (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();

    g.fillAll (Palette::panelBottom);
    juce::ColourGradient face (Palette::panelTop, 0.0f, full.getY(),
                               Palette::panelBottom, 0.0f, full.getBottom(), false);
    g.setGradientFill (face);
    g.fillAll();

    // Subtle vertical brushed streaks, clipped to the faceplate.
    const auto frame = full.reduced (10.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (frame.toNearestInt());
        for (int i = 0; i < 64; ++i)
        {
            const float px = frame.getX() + hash01 (i) * frame.getWidth();
            const float a = 0.035f * hash01 (i + 100);
            g.setColour ((i % 2) == 0 ? juce::Colours::white.withAlpha (a)
                                      : juce::Colours::black.withAlpha (a));
            g.drawLine ({ px, frame.getY(), px, frame.getBottom() }, 1.0f);
        }
    }

    // Warm-brass outline.
    g.setColour (Palette::brass);
    g.drawRoundedRectangle (frame, 3.0f, 1.5f);

    // Corner screws.
    for (const auto c : { juce::Point<float> (frame.getX() + 12, frame.getY() + 12),
                          juce::Point<float> (frame.getRight() - 12, frame.getY() + 12),
                          juce::Point<float> (frame.getX() + 12, frame.getBottom() - 12),
                          juce::Point<float> (frame.getRight() - 12, frame.getBottom() - 12) })
    {
        juce::ColourGradient screw (juce::Colours::grey, c.x, c.y - 5.0f,
                                    juce::Colours::black, c.x, c.y + 5.0f, true);
        g.setGradientFill (screw);
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));
        g.setColour (Palette::brassDim);
        g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c), 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine ({ c.x - 3.0f, c.y - 3.0f, c.x + 3.0f, c.y + 3.0f }, 1.4f);
    }
}

void GlintEditor::drawWordmark (juce::Graphics& g)
{
    g.setColour (Palette::cream);
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)).withExtraKerningFactor (0.45f));
    g.drawText ("GLINT", juce::Rectangle<float> (30, 22, 260, 36), juce::Justification::centredLeft);

    g.setColour (Palette::creamDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f).withKerningFactor (0.08f)));
    g.drawText ("variable-mu tube compressor", juce::Rectangle<int> (32, 58, 300, 14),
                juce::Justification::centredLeft);
}

void GlintEditor::drawMeter (juce::Graphics& g)
{
    const auto housing = juce::Rectangle<float> (410, 26, 330, 150);

    // Dark surround and brass bezel around the warm-cream face.
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (housing, 6.0f);
    g.setColour (Palette::brass);
    g.drawRoundedRectangle (housing.reduced (1.5f), 5.0f, 1.2f);

    const auto face = housing.reduced (4.0f);
    g.setColour (Palette::meterFace);
    g.fillRoundedRectangle (face, 4.0f);

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (face.toNearestInt());

    const auto pivot = juce::Point<float> (575.0f, 162.0f);
    auto pointOnArc = [pivot] (float r, float a)
    {
        return juce::Point<float> (pivot.x + r * std::sin (a), pivot.y - r * std::cos (a));
    };

    // Scale arc and ticks (0,1,2,3,5,7,10,15,20 evenly around the arc).
    // The radii are chosen so the top tick labels stay clear of the title
    // band drawn along the top edge of the face below.
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, 93.0f, 93.0f, 0.0f, meterStartAngle, meterEndAngle, true);
    g.setColour (Palette::tickDark);
    g.strokePath (arc, juce::PathStrokeType (1.2f));

    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    for (const float db : meterAnchorDb)
    {
        const float a = meterAngleFor (db);
        g.setColour (Palette::tickDark);
        g.drawLine ({ pointOnArc (93.0f, a), pointOnArc (103.0f, a) }, 1.4f);
        g.drawText (juce::String (db, 0),
                    juce::Rectangle<float> (24.0f, 12.0f).withCentre (pointOnArc (112.0f, a)),
                    juce::Justification::centred);
    }

    g.setColour (Palette::tickDark.withAlpha (0.85f));
    g.drawText ("GAIN REDUCTION (dB)", face.withY (face.getY() + 1.0f).withHeight (12.0f),
                juce::Justification::centredTop);

    // Needle, driven by the ballistic smoothed value; 0 dB rests at the left.
    const float needleAngle = meterAngleFor (grSmoothedDb);
    g.setColour (Palette::needle);
    g.drawLine ({ pivot, pointOnArc (84.0f, needleAngle) }, 2.2f);
    g.setColour (Palette::tickDark);
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (pivot));
    g.setColour (Palette::meterFace);
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (pivot));
}

float GlintEditor::meterAngleFor (float grDb) const noexcept
{
    const float db = juce::jlimit (0.0f, meterMaxDb, grDb);

    for (size_t i = 0; i < std::size (meterAnchorDb) - 1; ++i)
    {
        if (db <= meterAnchorDb[i + 1])
        {
            const float t = (db - meterAnchorDb[i]) / (meterAnchorDb[i + 1] - meterAnchorDb[i]);
            const float frac = ((float) i + t) / (float) (std::size (meterAnchorDb) - 1);
            return meterStartAngle + frac * (meterEndAngle - meterStartAngle);
        }
    }
    return meterEndAngle;
}

/** Runs one meter update; exposed so headless snapshot tools can drive the
    needle without a running message loop. */
void GlintEditor::tickMeter()
{
    timerCallback();
}

} // namespace glint
