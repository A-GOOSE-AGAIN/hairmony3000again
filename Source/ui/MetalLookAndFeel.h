// HAIRMONY 3000 - "hair metal from the future" look: chrome lettering, neon tubes, lightning.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace hm::ui
{
namespace Palette
{
    const juce::Colour night     { 0xff07000f };
    const juce::Colour deepPurple{ 0xff1a0033 };
    const juce::Colour panel     { 0xd0120024 };
    const juce::Colour magenta   { 0xffff2bd6 };
    const juce::Colour hotPink   { 0xffff3c8e };
    const juce::Colour cyan      { 0xff22f3ff };
    const juce::Colour laserBlue { 0xff3d5afe };
    const juce::Colour gold      { 0xffffc94d };
    const juce::Colour text      { 0xfff4ecff };
    const juce::Colour dim       { 0xff9a88bf };
    const juce::Colour danger    { 0xffff5a5a };
}

juce::Font displayFont (float height);          // heavy condensed (Impact on Windows)
juce::Font bodyFont (float height, bool bold = false);
juce::Font lcdFont (float height);

// Glossy 80s chrome lettering with a sky/horizon split, black outline and neon glow.
void drawChromeText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                     juce::Justification just = juce::Justification::centred, float slant = 0.2f,
                     juce::Colour glow = Palette::magenta);

void drawNeonRect (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float corner, float thickness = 1.6f, float glow = 1.0f);
void drawNeonLine (juce::Graphics& g, juce::Line<float> l, juce::Colour c, float thickness = 1.6f);
juce::Path lightningPath (juce::Rectangle<float> area);
void drawLightning (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c);
void drawSparkle (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour c);
void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent = Palette::cyan, bool opaque = false);

// Button styles (set with button.getProperties().set ("style", ...)):
//   "chrome" (default) - polished chrome plate
//   "toggle"           - neon pill that lights up when on
//   "hero"             - the big CREATE VOCALS slab
//   "ghost"            - outline only
//   "round"            - circular transport button
class MetalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MetalLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
};
} // namespace hm::ui
