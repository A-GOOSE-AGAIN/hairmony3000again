#include "MetalLookAndFeel.h"

namespace hm::ui
{
using namespace juce;

Font displayFont (float height)
{
    return Font (FontOptions().withName ("Impact").withFallbacks ({ "Haettenschweiler", "Arial Black", "DejaVu Sans" })
                              .withHeight (height));
}

Font bodyFont (float height, bool bold)
{
    auto o = FontOptions().withHeight (height);
    if (bold) o = o.withStyle ("Bold");
    return Font (o);
}

Font lcdFont (float height)
{
    return Font (FontOptions().withName (Font::getDefaultMonospacedFontName()).withHeight (height).withStyle ("Bold"));
}

//==============================================================================
void drawChromeText (Graphics& g, const String& text, Rectangle<float> area, Justification just, float slant, Colour glow)
{
    if (text.isEmpty() || area.isEmpty()) return;

    GlyphArrangement ga;
    ga.addLineOfText (displayFont (100.0f), text, 0.0f, 0.0f);
    Path p;
    ga.createPath (p);
    if (p.isEmpty()) return;

    p.applyTransform (AffineTransform::shear (-slant, 0.0f));
    auto b = p.getBounds();
    const float scale = jmin (area.getHeight() / b.getHeight(), area.getWidth() / b.getWidth());
    p.applyTransform (AffineTransform::translation (-b.getX(), -b.getY()).scaled (scale));
    b = p.getBounds();
    auto target = b.withPosition (area.getX(), area.getY());
    if (just.testFlags (Justification::horizontallyCentred)) target.setX (area.getCentreX() - b.getWidth() * 0.5f);
    else if (just.testFlags (Justification::right)) target.setX (area.getRight() - b.getWidth());
    if (just.testFlags (Justification::verticallyCentred)) target.setY (area.getCentreY() - b.getHeight() * 0.5f);
    else if (just.testFlags (Justification::bottom)) target.setY (area.getBottom() - b.getHeight());
    p.applyTransform (AffineTransform::translation (target.getX(), target.getY()));
    b = p.getBounds();

    const float h = b.getHeight();
    // Neon halo
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (glow.withAlpha (0.07f * (float) (5 - i)));
        g.strokePath (p, PathStrokeType (h * 0.05f * (float) i, PathStrokeType::curved, PathStrokeType::rounded));
    }
    // Dark extrusion
    g.setColour (Colour (0xff12002a));
    g.fillPath (p, AffineTransform::translation (h * 0.035f, h * 0.05f));

    // Chrome fill: sky above the horizon line, sunset below.
    ColourGradient chrome (Colour (0xffffffff), 0.0f, b.getY(), Colour (0xfffff4c2), 0.0f, b.getBottom(), false);
    chrome.addColour (0.18, Colour (0xffd8f3ff));
    chrome.addColour (0.46, Colour (0xff4fa8ff));
    chrome.addColour (0.50, Colour (0xff10184a));
    chrome.addColour (0.56, Colour (0xff4a2a7a));
    chrome.addColour (0.78, Colour (0xffff8a3d));
    g.setGradientFill (chrome);
    g.fillPath (p);

    g.setColour (Colour (0xff0a0014));
    g.strokePath (p, PathStrokeType (jmax (1.0f, h * 0.025f)));
    g.setColour (Colours::white.withAlpha (0.55f));
    g.strokePath (p, PathStrokeType (jmax (0.5f, h * 0.008f)), AffineTransform::translation (0.0f, -h * 0.01f));

    drawSparkle (g, { b.getX() + b.getWidth() * 0.12f, b.getY() + h * 0.12f }, h * 0.22f, Colours::white);
    drawSparkle (g, { b.getRight() - b.getWidth() * 0.08f, b.getY() + h * 0.55f }, h * 0.16f, Colours::white);
}

void drawSparkle (Graphics& g, Point<float> c, float r, Colour col)
{
    Path s;
    s.startNewSubPath (c.x, c.y - r);
    s.quadraticTo (c.x, c.y, c.x + r, c.y);
    s.quadraticTo (c.x, c.y, c.x, c.y + r);
    s.quadraticTo (c.x, c.y, c.x - r, c.y);
    s.quadraticTo (c.x, c.y, c.x, c.y - r);
    s.closeSubPath();
    g.setColour (col.withAlpha (0.9f));
    g.fillPath (s);
    g.setColour (col.withAlpha (0.25f));
    g.fillEllipse (c.x - r * 0.35f, c.y - r * 0.35f, r * 0.7f, r * 0.7f);
}

void drawNeonRect (Graphics& g, Rectangle<float> r, Colour c, float corner, float thickness, float glow)
{
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (c.withAlpha (0.08f * glow * (float) (4 - i)));
        g.drawRoundedRectangle (r, corner, thickness + 3.0f * (float) i);
    }
    g.setColour (c);
    g.drawRoundedRectangle (r, corner, thickness);
    g.setColour (Colours::white.withAlpha (0.35f * glow));
    g.drawRoundedRectangle (r, corner, thickness * 0.4f);
}

void drawNeonLine (Graphics& g, Line<float> l, Colour c, float thickness)
{
    g.setColour (c.withAlpha (0.18f)); g.drawLine (l, thickness * 4.0f);
    g.setColour (c.withAlpha (0.35f)); g.drawLine (l, thickness * 2.0f);
    g.setColour (c); g.drawLine (l, thickness);
}

Path lightningPath (Rectangle<float> a)
{
    Path p;
    const float x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();
    p.startNewSubPath (x + w * 0.62f, y);
    p.lineTo (x + w * 0.10f, y + h * 0.56f);
    p.lineTo (x + w * 0.46f, y + h * 0.56f);
    p.lineTo (x + w * 0.30f, y + h);
    p.lineTo (x + w * 0.92f, y + h * 0.38f);
    p.lineTo (x + w * 0.55f, y + h * 0.38f);
    p.closeSubPath();
    return p;
}

void drawLightning (Graphics& g, Rectangle<float> a, Colour c)
{
    auto p = lightningPath (a);
    g.setColour (c.withAlpha (0.25f));
    g.strokePath (p, PathStrokeType (a.getWidth() * 0.18f, PathStrokeType::mitered));
    g.setGradientFill (ColourGradient (Colours::white, a.getX(), a.getY(), c, a.getX(), a.getBottom(), false));
    g.fillPath (p);
    g.setColour (Colour (0xff12002a));
    g.strokePath (p, PathStrokeType (jmax (1.0f, a.getWidth() * 0.04f)));
}

void drawPanel (Graphics& g, Rectangle<float> r, const String& title, Colour accent, bool opaque)
{
    if (opaque)
    {
        g.setGradientFill (ColourGradient (Colour (0xff160530), 0.0f, r.getY(), Colour (0xff0a0118), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.0f);
    }
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (r, 10.0f);
    // faint scanlines
    g.setColour (Colours::white.withAlpha (0.025f));
    for (float y = r.getY() + 2.0f; y < r.getBottom(); y += 3.0f) g.drawHorizontalLine ((int) y, r.getX() + 4.0f, r.getRight() - 4.0f);
    drawNeonRect (g, r, accent.withAlpha (0.85f), 10.0f, 1.3f, 0.8f);

    if (title.isNotEmpty())
    {
        auto f = displayFont (19.0f);
        const float tw = GlyphArrangement::getStringWidth (f, title) + 24.0f;
        auto tag = Rectangle<float> (r.getX() + 18.0f, r.getY() - 11.0f, tw, 22.0f);
        g.setColour (Palette::night);
        g.fillRoundedRectangle (tag, 4.0f);
        drawNeonRect (g, tag, accent, 4.0f, 1.2f, 0.6f);
        g.setColour (accent.brighter (0.4f));
        g.setFont (f);
        g.drawText (title, tag, Justification::centred);
    }
}

//==============================================================================
MetalLookAndFeel::MetalLookAndFeel()
{
    using namespace Palette;
    setColour (TextButton::buttonColourId, Colour (0xff2b1a4a));
    setColour (TextButton::textColourOffId, Colour (0xff0d0620));
    setColour (TextButton::textColourOnId, Colours::white);
    setColour (Label::textColourId, text);
    setColour (Slider::textBoxTextColourId, cyan);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::backgroundColourId, Colour (0xff12002a));
    setColour (ComboBox::textColourId, text);
    setColour (ComboBox::outlineColourId, cyan);
    setColour (ComboBox::arrowColourId, cyan);
    setColour (PopupMenu::backgroundColourId, Colour (0xf0100022));
    setColour (PopupMenu::textColourId, text);
    setColour (PopupMenu::highlightedBackgroundColourId, magenta.withAlpha (0.6f));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (ScrollBar::thumbColourId, magenta);
    setColour (TooltipWindow::backgroundColourId, Colour (0xf0100022));
    setColour (TooltipWindow::textColourId, text);
    setColour (TooltipWindow::outlineColourId, cyan);
    setColour (AlertWindow::backgroundColourId, Colour (0xff12002a));
    setColour (AlertWindow::textColourId, text);
    setColour (AlertWindow::outlineColourId, magenta);
    setColour (ResizableWindow::backgroundColourId, night);
}

static String styleOf (const Component& c) { return c.getProperties().getWithDefault ("style", "chrome").toString(); }

static void fillChrome (Graphics& g, Rectangle<float> r, float corner, bool down)
{
    ColourGradient cg (Colour (down ? 0xffb8c4dc : 0xfff8fbff), 0.0f, r.getY(), Colour (down ? 0xff5a6688 : 0xff8f9bbd), 0.0f, r.getBottom(), false);
    cg.addColour (0.48, Colour (down ? 0xff8b97b6 : 0xffcdd7ec));
    cg.addColour (0.52, Colour (down ? 0xff46506e : 0xff6b7899));
    cg.addColour (0.80, Colour (down ? 0xff6a7696 : 0xffb2bdd6));
    g.setGradientFill (cg);
    g.fillRoundedRectangle (r, corner);
    g.setColour (Colours::white.withAlpha (down ? 0.2f : 0.7f));
    g.drawRoundedRectangle (r.reduced (1.0f), corner, 1.0f);
    g.setColour (Colour (0xff0a0014));
    g.drawRoundedRectangle (r, corner, 1.2f);
}

void MetalLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    using namespace Palette;
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const auto style = styleOf (b);
    const bool on = b.getToggleState();
    const float alpha = b.isEnabled() ? 1.0f : 0.4f;
    g.setOpacity (alpha);

    if (style == "toggle")
    {
        const float corner = r.getHeight() * 0.5f;
        if (on)
        {
            g.setGradientFill (ColourGradient (magenta, r.getX(), r.getY(), hotPink.darker (0.3f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);
            drawNeonRect (g, r, magenta.brighter (0.3f), corner, 1.4f, 1.2f);
        }
        else
        {
            g.setColour (Colour (0xff160830));
            g.fillRoundedRectangle (r, corner);
            drawNeonRect (g, r, (over ? cyan : dim).withAlpha (0.8f), corner, 1.1f, over ? 0.7f : 0.25f);
        }
        // LED
        auto led = Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + corner * 0.9f + 2.0f, r.getCentreY() });
        g.setColour (on ? Colours::white : Colour (0xff3a2860));
        g.fillEllipse (led);
        if (on) { g.setColour (Colours::white.withAlpha (0.3f)); g.fillEllipse (led.expanded (4.0f)); }
        return;
    }

    if (style == "rec")
    {
        auto c = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight()));
        const float pulse = on ? 0.6f + 0.4f * std::sin ((float) Time::getMillisecondCounter() * 0.008f) : 0.0f;
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (danger.withAlpha ((on ? 0.12f + 0.1f * pulse : 0.05f) * (float) (4 - i)));
            g.fillEllipse (c.expanded (3.0f * (float) i));
        }
        g.setGradientFill (ColourGradient (on ? Colour (0xffff8a8a) : Colour (0xff6a1020), c.getX(), c.getY(),
                                           on ? Colour (0xffb00018) : Colour (0xff2a0610), c.getX(), c.getBottom(), false));
        g.fillEllipse (c);
        drawNeonRect (g, c, over ? Colours::white : danger, c.getWidth() * 0.5f, 1.5f, on ? 1.0f : 0.5f);
        return;
    }

    if (style == "ghost")
    {
        g.setColour (Colour (over ? 0x40ff2bd6 : 0x20ffffff));
        g.fillRoundedRectangle (r, 6.0f);
        drawNeonRect (g, r, over ? magenta : cyan, 6.0f, 1.2f, over ? 0.9f : 0.4f);
        return;
    }

    if (style == "round")
    {
        auto c = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight()));
        fillChrome (g, c, c.getWidth() * 0.5f, down);
        drawNeonRect (g, c.expanded (1.0f), on ? magenta : (over ? cyan : laserBlue), c.getWidth() * 0.5f, 1.4f, on ? 1.2f : 0.6f);
        return;
    }

    if (style == "hero")
    {
        const float corner = 8.0f;
        // Skewed slab like a band logo plate
        Path slab;
        const float skew = r.getHeight() * 0.3f;
        slab.startNewSubPath (r.getX() + skew, r.getY());
        slab.lineTo (r.getRight(), r.getY());
        slab.lineTo (r.getRight() - skew, r.getBottom());
        slab.lineTo (r.getX(), r.getBottom());
        slab.closeSubPath();
        slab = slab.createPathWithRoundedCorners (corner);

        for (int i = 4; i >= 1; --i)
        {
            g.setColour ((over ? cyan : magenta).withAlpha (0.08f * (float) (5 - i)));
            g.strokePath (slab, PathStrokeType (4.0f * (float) i));
        }
        ColourGradient fill (Colour (0xffff5ee6), 0.0f, r.getY(), Colour (0xff4a0c8a), 0.0f, r.getBottom(), false);
        fill.addColour (0.49, Colour (down ? 0xffb0108f : 0xffd61ab8));
        fill.addColour (0.51, Colour (0xff5d0f9e));
        g.setGradientFill (fill);
        g.fillPath (slab);
        g.setColour (Colours::white.withAlpha (0.7f));
        g.strokePath (slab, PathStrokeType (1.5f));
        g.setColour (Colour (0xff0a0014));
        g.strokePath (slab, PathStrokeType (1.0f), AffineTransform::translation (0.0f, 1.5f));
        drawLightning (g, Rectangle<float> (r.getX() + skew + 10.0f, r.getY() + 7.0f, r.getHeight() * 0.55f, r.getHeight() - 14.0f), gold);
        drawLightning (g, Rectangle<float> (r.getRight() - skew - 10.0f - r.getHeight() * 0.55f, r.getY() + 7.0f, r.getHeight() * 0.55f, r.getHeight() - 14.0f), gold);
        return;
    }

    // chrome plate
    fillChrome (g, r, 6.0f, down || on);
    if (over || on) drawNeonRect (g, r.expanded (1.0f), on ? magenta : cyan, 6.0f, 1.2f, 0.7f);
}

Font MetalLookAndFeel::getTextButtonFont (TextButton& b, int h)
{
    const auto style = styleOf (b);
    if (style == "hero") return displayFont ((float) h * 0.55f);
    return displayFont (jmin (20.0f, (float) h * 0.55f));
}

void MetalLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    using namespace Palette;
    const auto style = styleOf (b);
    auto r = b.getLocalBounds().toFloat().reduced (4.0f, 2.0f);
    g.setOpacity (b.isEnabled() ? 1.0f : 0.4f);

    if (style == "rec")
    {
        g.setColour (Colours::white.withAlpha (b.getToggleState() ? 1.0f : 0.85f));
        g.setFont (displayFont (jmin (26.0f, (float) b.getHeight() * 0.28f + 6.0f)));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4), Justification::centred, 1, 0.6f);
        return;
    }

    if (style == "hero")
    {
        drawChromeText (g, b.getButtonText(), r.reduced (r.getHeight() * 1.2f, r.getHeight() * 0.16f),
                        Justification::centred, 0.18f, gold);
        return;
    }

    g.setFont (getTextButtonFont (b, b.getHeight()));
    Colour c;
    if (style == "toggle")
    {
        c = b.getToggleState() ? Colours::white : text.withAlpha (0.75f);
        r.removeFromLeft (r.getHeight() * 0.5f + 6.0f);
    }
    else if (style == "ghost") c = text;
    else if (style == "round") c = b.getToggleState() ? hotPink : Colour (0xff12002a);
    else c = Colour (0xff12002a);

    g.setColour (c);
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), Justification::centred, 1, 0.8f);
}

//==============================================================================
void MetalLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    using namespace Palette;
    auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto accent = s.findColour (Slider::rotarySliderFillColourId) == Colour() ? magenta : s.findColour (Slider::rotarySliderFillColourId);

    // tick ring
    for (int i = 0; i <= 20; ++i)
    {
        const float a = start + (end - start) * (float) i / 20.0f;
        const bool lit = (float) i / 20.0f <= pos + 1.0e-4f;
        const float r0 = radius * 0.98f, r1 = radius * (i % 5 == 0 ? 0.84f : 0.89f);
        g.setColour (lit ? accent : dim.withAlpha (0.35f));
        g.drawLine ({ c.getPointOnCircumference (r1, a), c.getPointOnCircumference (r0, a) }, i % 5 == 0 ? 2.0f : 1.2f);
    }

    // neon arc
    Path track, value;
    const float arcR = radius * 0.76f;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, angle, true);
    g.setColour (Colour (0xff22103e));
    g.strokePath (track, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (value, PathStrokeType (12.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (value, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // chrome dome
    const float domeR = radius * 0.58f;
    auto dome = Rectangle<float> (domeR * 2.0f, domeR * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillEllipse (dome.translated (0.0f, 3.0f).expanded (1.0f));
    ColourGradient cg (Colours::white, dome.getX() + domeR * 0.6f, dome.getY() + domeR * 0.4f, Colour (0xff3e4566), dome.getRight(), dome.getBottom(), true);
    cg.addColour (0.35, Colour (0xffc9d3ea));
    cg.addColour (0.7, Colour (0xff6c7698));
    g.setGradientFill (cg);
    g.fillEllipse (dome);
    // machined rings
    g.setColour (Colours::white.withAlpha (0.12f));
    for (float rr = domeR * 0.3f; rr < domeR; rr += domeR * 0.18f)
        g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.8f);
    g.setColour (Colour (0xff0a0014));
    g.drawEllipse (dome, 1.4f);

    // pointer
    const auto p0 = c.getPointOnCircumference (domeR * 0.25f, angle);
    const auto p1 = c.getPointOnCircumference (domeR * 0.92f, angle);
    g.setColour (accent.withAlpha (0.4f));
    g.drawLine ({ p0, p1 }, 6.0f);
    g.setColour (Colour (0xff12002a));
    g.drawLine ({ p0, p1 }, 3.0f);
    g.setColour (accent.brighter (0.5f));
    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (p1));
}

void MetalLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         Slider::SliderStyle style, Slider& s)
{
    using namespace Palette;
    if (style != Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
    auto r = Rectangle<int> (x, y, w, h).toFloat();
    auto track = r.withSizeKeepingCentre (r.getWidth(), 6.0f);
    g.setColour (Colour (0xff22103e));
    g.fillRoundedRectangle (track, 3.0f);
    auto lit = track.withRight (pos);
    g.setGradientFill (ColourGradient (cyan, track.getX(), 0.0f, magenta, track.getRight(), 0.0f, false));
    g.fillRoundedRectangle (lit, 3.0f);
    g.setColour (magenta.withAlpha (0.25f));
    g.fillRoundedRectangle (lit.expanded (0.0f, 4.0f), 5.0f);

    auto thumb = Rectangle<float> (14.0f, r.getHeight() * 0.8f).withCentre ({ pos, r.getCentreY() });
    fillChrome (g, thumb, 3.0f, false);
    g.setColour (magenta);
    g.drawLine (thumb.getCentreX(), thumb.getY() + 4.0f, thumb.getCentreX(), thumb.getBottom() - 4.0f, 2.0f);
}

Label* MetalLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (lcdFont (14.0f));
    l->setColour (Label::textColourId, Palette::cyan);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    return l;
}

//==============================================================================
void MetalLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    using namespace Palette;
    auto r = Rectangle<int> (w, h).toFloat().reduced (1.5f);
    g.setColour (Colour (0xff12002a));
    g.fillRoundedRectangle (r, 6.0f);
    drawNeonRect (g, r, box.isMouseOver (true) ? magenta : cyan, 6.0f, 1.2f, 0.5f);
    Path arrow;
    const float ax = r.getRight() - 16.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 5.0f, ay - 3.0f, ax + 5.0f, ay - 3.0f, ax, ay + 4.0f);
    g.setColour (cyan);
    g.fillPath (arrow);
}

Font MetalLookAndFeel::getComboBoxFont (ComboBox&) { return displayFont (17.0f); }

void MetalLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

Font MetalLookAndFeel::getPopupMenuFont() { return displayFont (17.0f); }

void MetalLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (findColour (PopupMenu::backgroundColourId));
    g.setColour (Palette::cyan.withAlpha (0.7f));
    g.drawRect (0, 0, w, h, 1);
}

void MetalLookAndFeel::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical,
                                      int thumbStart, int thumbSize, bool over, bool)
{
    auto track = Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    g.setColour (Colour (0xff1c0b36));
    g.fillRoundedRectangle (track, 3.0f);
    auto thumb = vertical ? Rectangle<float> ((float) x + 3.0f, (float) thumbStart, (float) w - 6.0f, (float) thumbSize)
                          : Rectangle<float> ((float) thumbStart, (float) y + 3.0f, (float) thumbSize, (float) h - 6.0f);
    g.setColour (over ? Palette::cyan : Palette::magenta);
    g.fillRoundedRectangle (thumb, 3.0f);
}

void MetalLookAndFeel::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.fillAll (findColour (TooltipWindow::backgroundColourId));
    g.setColour (Palette::cyan);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (Palette::text);
    g.setFont (bodyFont (13.0f));
    g.drawFittedText (text, Rectangle<int> (w, h).reduced (6, 3), Justification::centredLeft, 4);
}

Font MetalLookAndFeel::getLabelFont (Label& l)
{
    return l.getFont();
}

void MetalLookAndFeel::drawLabel (Graphics& g, Label& l)
{
    // Slider value boxes render as little neon LCD readouts.
    if (dynamic_cast<Slider*> (l.getParentComponent()) != nullptr && ! l.isBeingEdited())
    {
        auto r = l.getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Colour (0xff0b0420));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Palette::cyan.withAlpha (0.12f));
        g.fillRoundedRectangle (r.reduced (2.0f).withTrimmedBottom (r.getHeight() * 0.5f), 3.0f);
        g.setColour (Palette::cyan.withAlpha (0.45f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (l.findColour (Label::textColourId));
        g.setFont (lcdFont (jmin (15.0f, r.getHeight() * 0.72f)));
        g.drawFittedText (l.getText(), r.toNearestInt(), Justification::centred, 1, 0.8f);
        return;
    }
    LookAndFeel_V4::drawLabel (g, l);
}
} // namespace hm::ui
