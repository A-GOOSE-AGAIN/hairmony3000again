#include "PluginEditor.h"

using namespace juce;
using namespace hm::ui;
using namespace hm::ui::Palette;

//==============================================================================
static void drawScene (Graphics& g, float w, float h)
{
    const float horizon = h * 0.712f;

    // Sky
    ColourGradient sky (Colour (0xff04000c), 0.0f, 0.0f, Colour (0xffb0148f), 0.0f, horizon, false);
    sky.addColour (0.45, Colour (0xff1c0036));
    sky.addColour (0.82, Colour (0xff4a0866));
    g.setGradientFill (sky);
    g.fillRect (0.0f, 0.0f, w, horizon);

    // Stars
    Random rng (2089);
    for (int i = 0; i < 170; ++i)
    {
        const float x = rng.nextFloat() * w, y = rng.nextFloat() * horizon * 0.8f;
        const float s = rng.nextFloat() < 0.08f ? 2.4f : 1.0f + rng.nextFloat();
        g.setColour (Colours::white.withAlpha (0.25f + rng.nextFloat() * 0.6f));
        g.fillEllipse (x, y, s, s);
        if (s > 2.0f) drawSparkle (g, { x + s * 0.5f, y + s * 0.5f }, 6.0f, Colour (0xffd8f3ff));
    }

    // Laser sun with slits
    {
        const float r = h * 0.27f;
        const Point<float> c (w * 0.5f, horizon + r * 0.15f);
        g.setColour (magenta.withAlpha (0.12f));
        g.fillEllipse (Rectangle<float> (r * 2.8f, r * 2.8f).withCentre (c));
        g.saveState();
        g.reduceClipRegion (Rectangle<float> (0.0f, 0.0f, w, horizon).toNearestInt());
        g.setGradientFill (ColourGradient (Colour (0xfffff07a), 0.0f, c.y - r, Colour (0xffff2bd6), 0.0f, c.y + r * 0.2f, false));
        g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        float y = c.y - r * 0.18f, gap = 2.0f;
        g.setColour (Colour (0xff3a0757));
        while (y > c.y - r * 0.85f) { g.fillRect (c.x - r, y, r * 2.0f, gap); gap *= 0.8f; y -= r * 0.13f; }
        y = c.y - r * 0.18f; gap = 2.0f;
        while (y < horizon) { g.fillRect (c.x - r, y, r * 2.0f, gap); gap += 1.6f; y += r * 0.11f; }
        g.restoreState();
    }

    // Wireframe mountains
    auto ridge = [&] (std::initializer_list<Point<float>> pts, Colour edge)
    {
        Path p;
        bool first = true;
        for (auto pt : pts) { if (first) p.startNewSubPath (pt.x * w, pt.y * horizon); else p.lineTo (pt.x * w, pt.y * horizon); first = false; }
        Path fill (p);
        fill.lineTo (w * pts.end()[-1].x, horizon);
        fill.lineTo (w * pts.begin()->x, horizon);
        fill.closeSubPath();
        g.setGradientFill (ColourGradient (Colour (0xff12002a), 0.0f, horizon * 0.7f, Colour (0xff250040), 0.0f, horizon, false));
        g.fillPath (fill);
        g.setColour (edge.withAlpha (0.25f));
        g.strokePath (p, PathStrokeType (5.0f));
        g.setColour (edge);
        g.strokePath (p, PathStrokeType (1.4f));
    };
    ridge ({ { 0.0f, 0.86f }, { 0.07f, 0.74f }, { 0.13f, 0.82f }, { 0.21f, 0.66f }, { 0.29f, 0.84f }, { 0.36f, 0.93f }, { 0.42f, 1.0f } }, cyan);
    ridge ({ { 0.58f, 1.0f }, { 0.66f, 0.88f }, { 0.73f, 0.70f }, { 0.80f, 0.83f }, { 0.88f, 0.68f }, { 0.94f, 0.80f }, { 1.0f, 0.74f } }, cyan);

    // Grid floor
    g.setGradientFill (ColourGradient (Colour (0xff1a0030), 0.0f, horizon, Colour (0xff05000c), 0.0f, h, false));
    g.fillRect (0.0f, horizon, w, h - horizon);
    const float depth = h - horizon;
    for (int i = 0; i < 14; ++i)
    {
        const float t = (float) i / 13.0f;
        const float y = horizon + depth * t * t;
        g.setColour (magenta.withAlpha (0.18f + 0.5f * t));
        g.drawLine (0.0f, y, w, y, 1.0f + t * 1.5f);
    }
    for (int i = -16; i <= 16; ++i)
    {
        const float xb = w * 0.5f + (float) i * w * 0.09f;
        g.setColour (magenta.withAlpha (0.45f));
        g.drawLine (w * 0.5f + (float) i * w * 0.012f, horizon, xb, h, 1.2f);
    }
    g.setColour (Colour (0xffff7ae8));
    g.drawLine (0.0f, horizon, w, horizon, 2.0f);
    g.setColour (magenta.withAlpha (0.3f));
    g.drawLine (0.0f, horizon, w, horizon, 8.0f);

    // Vignette
    g.setGradientFill (ColourGradient (Colours::transparentBlack, w * 0.5f, h * 0.5f, Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true));
    g.fillRect (0.0f, 0.0f, w, h);
}

void HairmonyEditor::Content::paint (Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (backdrop.isNull() || std::abs (scale - backdropScale) > 0.01f)
    {
        backdropScale = scale;
        // CPU-side image: drawing into it never opens a second GPU (Direct2D) context mid-paint.
        backdrop = Image (Image::RGB, roundToInt (baseWidth * scale), roundToInt (baseHeight * scale), false, SoftwareImageType());
        Graphics bg (backdrop);
        bg.addTransform (AffineTransform::scale (scale));
        drawScene (bg, (float) baseWidth, (float) baseHeight);
    }
    g.drawImage (backdrop, Rectangle<float> ((float) baseWidth, (float) baseHeight));

    // Header strip
    auto header = Rectangle<float> (0.0f, 0.0f, (float) baseWidth, 104.0f);
    g.setGradientFill (ColourGradient (Colour (0xe0060012), 0.0f, 0.0f, Colour (0x80060012), 0.0f, header.getBottom(), false));
    g.fillRect (header);
    drawNeonLine (g, { 0.0f, header.getBottom(), (float) baseWidth, header.getBottom() }, cyan, 1.2f);

    drawLightning (g, { 22.0f, 16.0f, 30.0f, 62.0f }, gold);
    drawChromeText (g, "HAIRMONY 3000", { 58.0f, 12.0f, 500.0f, 62.0f }, Justification::centredLeft, 0.22f, magenta);
    g.setColour (cyan);
    g.setFont (lcdFont (13.0f));
    g.drawText ("* *  THE CHOIR MACHINE FROM THE YEAR 2089  * *", Rectangle<int> (64, 78, 500, 18), Justification::centredLeft);

    // Step indicator
    const char* steps[] = { "LOAD", "CRAFT", "REVIEW" };
    const int scr = owner.proc.getScreen();
    float x = 590.0f;
    for (int i = 0; i < 3; ++i)
    {
        auto pill = Rectangle<float> (x, 34.0f, 98.0f, 34.0f);
        const bool active = i == scr, done = i < scr;
        g.setColour (active ? magenta.withAlpha (0.85f) : Colour (0xff12002a));
        g.fillRoundedRectangle (pill, 17.0f);
        drawNeonRect (g, pill, active ? magenta : (done ? cyan : dim.withAlpha (0.6f)), 17.0f, 1.2f, active ? 1.0f : 0.3f);
        g.setColour (active ? Colours::white : (done ? cyan : dim));
        g.setFont (displayFont (17.0f));
        g.drawText (String (i + 1).paddedLeft ('0', 2) + " " + steps[i], pill.toNearestInt(), Justification::centred);
        if (i < 2)
        {
            Path tri;
            tri.addTriangle (x + 103.0f, 46.0f, x + 103.0f, 56.0f, x + 109.0f, 51.0f);
            g.setColour (done ? cyan : dim);
            g.fillPath (tri);
        }
        x += 112.0f;
    }
}

void HairmonyEditor::GearButton::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const auto c = r.getCentre();
    const float R = jmin (r.getWidth(), r.getHeight()) * 0.5f;
    Path gear;
    const int teeth = 8;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float a0 = MathConstants<float>::twoPi * (float) i / (float) (teeth * 2);
        const float a1 = MathConstants<float>::twoPi * (float) (i + 1) / (float) (teeth * 2);
        const float rr = (i % 2 == 0) ? R : R * 0.78f;
        if (i == 0) gear.startNewSubPath (c.getPointOnCircumference (rr, a0));
        else gear.lineTo (c.getPointOnCircumference (rr, a0));
        gear.lineTo (c.getPointOnCircumference (rr, a1));
    }
    gear.closeSubPath();
    gear.addEllipse (Rectangle<float> (R * 0.7f, R * 0.7f).withCentre (c));
    gear.setUsingNonZeroWinding (false);
    g.setColour ((over ? magenta : cyan).withAlpha (0.3f));
    g.strokePath (gear, PathStrokeType (5.0f));
    g.setGradientFill (ColourGradient (Colours::white, 0.0f, r.getY(), Colour (down ? 0xff3e4566 : 0xff8f9bbd), 0.0f, r.getBottom(), false));
    g.fillPath (gear);
    g.setColour (Colour (0xff12002a));
    g.strokePath (gear, PathStrokeType (1.2f));
}

//==============================================================================
HairmonyEditor::HairmonyEditor (HairmonyProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      importScreen (p), craftScreen (p), reviewScreen (p), settings (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (content);
    for (auto* s : { (Component*) &importScreen, (Component*) &craftScreen, (Component*) &reviewScreen })
        content.addChildComponent (*s);
    settingsButton.setTooltip ("Settings, about and tips");
    settingsButton.onClick = [this] { settings.refresh(); settings.setVisible (true); settings.toFront (false); };
    content.addAndMakeVisible (settingsButton);
    content.addChildComponent (settings);

    setResizable (true, true);
    setResizeLimits (baseWidth * 7 / 10, baseHeight * 7 / 10, baseWidth * 8 / 5, baseHeight * 8 / 5);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    timerCallback();
    startTimerHz (30);
}

HairmonyEditor::~HairmonyEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void HairmonyEditor::paint (Graphics& g) { g.fillAll (night); }

void HairmonyEditor::resized()
{
    const float s = (float) getWidth() / (float) baseWidth;
    content.setTransform (AffineTransform::scale (s));
    content.setBounds (0, 0, baseWidth, baseHeight);

    const auto area = Rectangle<int> (24, 116, baseWidth - 48, baseHeight - 128);
    importScreen.setBounds (area);
    craftScreen.setBounds (area);
    reviewScreen.setBounds (area);
    settingsButton.setBounds (baseWidth - 64, 30, 42, 42);
    settings.setBounds (0, 0, baseWidth, baseHeight);
}

void HairmonyEditor::showScreen (int s)
{
    shownScreen = s;
    importScreen.setVisible (s == HairmonyProcessor::importScreen);
    craftScreen.setVisible (s == HairmonyProcessor::craftScreen);
    reviewScreen.setVisible (s == HairmonyProcessor::reviewScreen);
    content.repaint (0, 0, baseWidth, 110);
}

void HairmonyEditor::useSoftwareRenderer()
{
   #if JUCE_WINDOWS
    // JUCE 8 renders through Direct2D on Windows. The software renderer is the most robust
    // choice across hosts and GPU-less machines (CI runners, VMs, remote desktops), and this UI
    // is mostly cached artwork, so it costs nothing noticeable.
    if (auto* peer = getPeer())
    {
        if (peer == softwarePeer) return;
        const auto engines = peer->getAvailableRenderingEngines();
        const int idx = engines.indexOf ("Software Renderer");
        if (idx >= 0 && peer->getCurrentRenderingEngine() != idx)
            peer->setCurrentRenderingEngine (idx);
        softwarePeer = peer;
    }
   #endif
}

void HairmonyEditor::parentHierarchyChanged() { useSoftwareRenderer(); }

void HairmonyEditor::timerCallback()
{
    useSoftwareRenderer();
    proc.pollRender();

    const int scr = proc.getScreen();
    bool needsRefresh = false;
    if (scr != shownScreen) { showScreen (scr); needsRefresh = true; }
    const int v = proc.getSessionVersion();
    if (v != lastVersion) { lastVersion = v; needsRefresh = true; }

    Screen* current = scr == HairmonyProcessor::importScreen ? (Screen*) &importScreen
                    : scr == HairmonyProcessor::craftScreen  ? (Screen*) &craftScreen
                                                             : (Screen*) &reviewScreen;
    if (needsRefresh) { current->refresh(); content.repaint (0, 0, baseWidth, 110); }
    current->tick();
}
