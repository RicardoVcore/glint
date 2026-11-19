#include "GlintLookAndFeel.h"

namespace glint
{

void GlintLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    // When the slider rect is taller than wide (value box + label strip live
    // below), the knob itself occupies the top square of the rect.  The
    // "knobSize" property (set by the editor) pins that square so the value
    // box can be wider than the knob.
    const int knobSize = (int) slider.getProperties().getWithDefault ("knobSize", 0);
    const float radius = knobSize > 0 ? 0.5f * (float) knobSize
                                      : 0.5f * juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = knobSize > 0
        ? juce::Point<float> (bounds.getCentreX(), bounds.getY() + radius)
        : bounds.getCentre();
    const float span = rotaryEndAngle - rotaryStartAngle;
    const float angle = rotaryStartAngle + sliderPosProportional * span;

    const auto props = slider.getProperties();
    const auto style = props.getWithDefault ("style", juce::var()).toString();
    const bool isTime = style == "time";
    const bool isSparkle = style == "sparkle";
    // The time knob shrinks its body to leave room for the detent labels
    // (and drops the skirt ring so the labels stay clear of it).
    const float bodyR = radius * (isTime ? 0.60f : 0.78f);

    // Tick marks around the knob.
    const int numTicks = (int) props.getWithDefault ("numTicks", 11);
    for (int i = 0; i < numTicks; ++i)
    {
        const float t = numTicks > 1 ? (float) i / (float) (numTicks - 1) : 0.0f;
        const float a = rotaryStartAngle + t * span;
        g.setColour (Palette::creamDim.withAlpha (0.8f));
        g.drawLine ({ pointAt (centre, radius * 0.94f, a), pointAt (centre, radius, a) }, 1.4f);

        if (isTime) // detent labels 1..6 printed at each tick
        {
            g.setColour (Palette::cream);
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (juce::String (i + 1),
                        juce::Rectangle<float> (12.0f, 12.0f).withCentre (pointAt (centre, radius * 0.78f, a)),
                        juce::Justification::centred);
        }
    }

    // Sparkle signature: amber accent ring that brightens with the value.
    if (isSparkle)
    {
        const float brightness = 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, sliderPosProportional);
        g.setColour (juce::Colour::fromHSV (Palette::amber.getHue(), Palette::amber.getSaturation(),
                                            brightness, 1.0f));
        juce::Path ring;
        ring.addCentredArc (centre.x, centre.y, bodyR + 2.5f, bodyR + 2.5f, 0.0f,
                            rotaryStartAngle, angle, true);
        g.strokePath (ring, juce::PathStrokeType (3.2f));
    }

    // Lighter skirt ring, then the dark bakelite body with a radial sheen.
    // (Time knob skips the skirt so the detent labels stay clear of it.)
    if (! isTime)
    {
        g.setColour (Palette::skirt);
        g.fillEllipse (ellipse (centre, bodyR + 4.0f));
    }

    juce::ColourGradient sheen (Palette::knobBodyLight, centre.x - bodyR * 0.35f, centre.y - bodyR * 0.40f,
                                Palette::knobBodyDark, centre.x + bodyR * 0.35f, centre.y + bodyR * 0.40f, true);
    sheen.addColour (0.55, Palette::knobBodyDark.brighter (0.06f));
    g.setGradientFill (sheen);
    g.fillEllipse (ellipse (centre, bodyR));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (ellipse (centre, bodyR - 0.5f), 1.0f);

    // Bright pointer line and centre cap.
    g.setColour (Palette::cream);
    g.drawLine ({ pointAt (centre, bodyR * 0.18f, angle), pointAt (centre, bodyR * 0.82f, angle) }, 2.6f);
    g.setColour (Palette::knobBodyDark);
    g.fillEllipse (ellipse (centre, bodyR * 0.20f));
    g.setColour (Palette::brassDim);
    g.drawEllipse (ellipse (centre, bodyR * 0.20f), 1.0f);
}

void GlintLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsDown);
    // The button bounds are the recessed mounting plate: dark inset rounded
    // rect with a thin brass outline, matching the knob value boxes.
    const auto plate = button.getLocalBounds().toFloat();
    const auto centre = plate.getCentre();
    const bool midSide = button.getToggleState();

    g.setColour (Palette::knobBodyDark);
    g.fillRoundedRectangle (plate, 5.0f);
    g.setColour (Palette::brassDim);
    g.drawRoundedRectangle (plate.reduced (1.0f), 5.0f, 1.0f);

    // Vertical two-position switch slot, centred on the plate.
    const auto slot = juce::Rectangle<float> (12.0f, 24.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (slot, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (slot.reduced (0.5f), 6.0f, 1.0f);

    // Metal lever, top = stereo, bottom = mid/side.
    const float leverY = midSide ? slot.getBottom() - 6.5f : slot.getY() + 6.5f;
    const auto leverCentre = juce::Point<float> (centre.x, leverY);
    juce::ColourGradient metal (juce::Colours::white.withAlpha (shouldDrawButtonAsHighlighted ? 0.55f : 0.40f),
                                leverCentre.x, leverY - 6.0f,
                                juce::Colours::black.withAlpha (0.5f),
                                leverCentre.x, leverY + 6.0f, false);
    g.setGradientFill (metal);
    g.fillEllipse (ellipse (leverCentre, 6.5f));
    g.setColour (Palette::brassDim);
    g.drawEllipse (ellipse (leverCentre, 6.5f), 1.0f);

    // Labels: active position cream, inactive dimmed, nudged clear of the
    // plate outline.
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold).withKerningFactor (0.10f)));
    auto labelArea = plate.reduced (6.0f, 2.0f);
    g.setColour (midSide ? Palette::creamDim : Palette::cream);
    g.drawText ("STEREO", labelArea.removeFromTop (12.0f), juce::Justification::centred);
    g.setColour (midSide ? Palette::cream : Palette::creamDim);
    g.drawText ("M-S", labelArea.removeFromBottom (12.0f), juce::Justification::centred);
}

juce::Label* GlintLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::Font (juce::FontOptions (11.0f)));
    return label;
}

void GlintLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                              juce::TextEditor& editor)
{
    // Keep the value-box editor in the faceplate look: thin brass outline
    // (the colour copied from the slider text box), never the default blue.
    g.setColour (editor.findColour (juce::TextEditor::outlineColourId));
    g.drawRect (0, 0, width, height);
}

//==============================================================================
// Preset combo box + popup menu

void GlintLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                     int, int, int, int, juce::ComboBox&)
{
    const auto box = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    // Dark inset panel with a thin brass outline, matching the value boxes.
    g.setColour (Palette::knobBodyDark);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (box, 3.0f, 2.0f);   // inner shadow rim
    g.setColour (Palette::brassDim);
    g.drawRoundedRectangle (box.reduced (0.75f), 3.0f, 1.0f);

    // Small downward triangle on the right.
    const float cx = (float) width - 13.0f;
    const float cy = 0.5f * (float) height;
    juce::Path arrow;
    arrow.addTriangle (cx, cy - 2.5f, cx + 7.0f, cy - 2.5f, cx + 3.5f, cy + 2.5f);
    g.setColour (Palette::cream);
    g.fillPath (arrow);
}

juce::Font GlintLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return { withDefaultMetrics (juce::FontOptions (12.0f)) };
}

void GlintLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (2, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

void GlintLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width,
                                                           int height,
                                                           const juce::PopupMenu::Options&)
{
    g.setColour (Palette::knobBodyDark);
    g.fillRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 3.0f);
    g.setColour (Palette::brassDim);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 3.0f, 1.0f);
}

void GlintLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g,
                                                     const juce::Rectangle<int>& area,
                                                     bool isHighlighted,
                                                     const juce::PopupMenu::Item& item,
                                                     const juce::PopupMenu::Options&)
{
    auto r = area.reduced (1);

    if (item.isSeparator)
    {
        g.setColour (Palette::brassDim.withAlpha (0.5f));
        g.fillRect (r.removeFromTop (1));
        return;
    }

    if (isHighlighted && item.isEnabled)
    {
        g.setColour (Palette::amber.withAlpha (0.30f));
        g.fillRect (r);
    }

    g.setColour (! item.isEnabled
                     ? Palette::creamDim.withAlpha (0.4f)
                     : (isHighlighted ? Palette::cream : Palette::creamDim));
    // Mark the active preset with a small brass dot in the left gutter.
    auto gutter = r.removeFromLeft (14);
    if (item.isTicked)
    {
        g.setColour (Palette::amber);
        g.fillEllipse (gutter.toFloat().withSizeKeepingCentre (5.0f, 5.0f));
        g.setColour (Palette::cream);
    }

    g.setFont (getPopupMenuFont());
    g.drawFittedText (item.text, r.reduced (2, 0), juce::Justification::centredLeft, 1);
}

juce::Font GlintLookAndFeel::getPopupMenuFont()
{
    return { withDefaultMetrics (juce::FontOptions (13.0f)) };
}

juce::Point<float> GlintLookAndFeel::pointAt (juce::Point<float> centre, float radius,
                                              float angleRadians) noexcept
{
    // 0 radians = 12 o'clock, angles increase clockwise (JUCE rotary convention).
    return { centre.x + radius * std::sin (angleRadians),
             centre.y - radius * std::cos (angleRadians) };
}

juce::Rectangle<float> GlintLookAndFeel::ellipse (juce::Point<float> centre, float radius) noexcept
{
    return juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (centre);
}

} // namespace glint
