#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace glint
{

/** Shared faceplate palette.  Our own look: graphite, warm brass and cream —
    deliberately NOT the colours of any vintage hardware unit. */
struct Palette
{
    static inline const juce::Colour panelTop     { 0xff31302d };
    static inline const juce::Colour panelBottom  { 0xff21201d };
    static inline const juce::Colour brass        { 0xffc2a061 };
    static inline const juce::Colour brassDim     { 0xff8a6f42 };
    static inline const juce::Colour cream        { 0xffeee3c8 };
    static inline const juce::Colour creamDim     { 0xffc4b899 };
    static inline const juce::Colour knobBodyLight { 0xff504a42 };
    static inline const juce::Colour knobBodyDark  { 0xff16130f };
    static inline const juce::Colour skirt        { 0xff6b6152 };
    static inline const juce::Colour amber        { 0xffe2a63c };
    static inline const juce::Colour meterFace    { 0xfff2e6c4 };
    static inline const juce::Colour needle       { 0xff8a2f1e };
    static inline const juce::Colour tickDark     { 0xff4a3a26 };
};

/** Glint faceplate LookAndFeel: hardware-style rotary knobs and a
    two-position toggle switch.  Everything is drawn with juce::Graphics. */
class GlintLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlintLookAndFeel() = default;

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    /** Slider value box: faceplate-styled label with a compact font. */
    juce::Label* createSliderTextBox (juce::Slider& slider) override;

    /** Value-box editor keeps the thin brass outline (never default blue). */
    void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                juce::TextEditor& editor) override;

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    /** Preset combo box: dark inset panel, thin brass outline, cream arrow. */
    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;
    juce::Font getComboBoxFont (juce::ComboBox& box) override;
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;

    /** Preset popup menu: dark panel with a brass outline, cream items and
        an amber highlight. */
    void drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                             const juce::PopupMenu::Options& options) override;
    void drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                       bool isHighlighted, const juce::PopupMenu::Item& item,
                                       const juce::PopupMenu::Options& options) override;
    juce::Font getPopupMenuFont() override;

private:
    /** Point on a circle, using the JUCE convention: angle 0 = 12 o'clock,
        increasing clockwise. */
    static juce::Point<float> pointAt (juce::Point<float> centre, float radius,
                                       float angleRadians) noexcept;

    /** Square centred on `centre` with the given radius (for ellipse calls). */
    static juce::Rectangle<float> ellipse (juce::Point<float> centre, float radius) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlintLookAndFeel)
};

} // namespace glint
