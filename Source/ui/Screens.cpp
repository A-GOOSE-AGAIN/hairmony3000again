#include "Screens.h"

namespace hm::ui
{
using namespace juce;
using namespace Palette;

String formatTime (double s)
{
    s = jmax (0.0, s);
    const int m = (int) (s / 60.0);
    const double sec = s - m * 60.0;
    return String (m) + ":" + String (sec, 1).paddedLeft ('0', 4);
}

void drawCassette (Graphics& g, Rectangle<float> r, Colour accent)
{
    auto body = r.reduced (1.0f);
    g.setGradientFill (ColourGradient (Colour (0xff2a2350), body.getX(), body.getY(), Colour (0xff0d0a22), body.getX(), body.getBottom(), false));
    g.fillRoundedRectangle (body, 3.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (body, 3.0f, 1.3f);
    auto label = body.reduced (body.getWidth() * 0.12f, body.getHeight() * 0.18f).withHeight (body.getHeight() * 0.38f);
    g.setGradientFill (ColourGradient (hotPink, label.getX(), 0.0f, cyan, label.getRight(), 0.0f, false));
    g.fillRoundedRectangle (label, 2.0f);
    const float rr = body.getHeight() * 0.14f;
    const float cy = label.getCentreY();
    for (float cx : { label.getX() + label.getWidth() * 0.28f, label.getX() + label.getWidth() * 0.72f })
    {
        g.setColour (Colour (0xff0d0a22));
        g.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
        g.setColour (Colours::white.withAlpha (0.8f));
        g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 1.0f);
    }
    auto window = Rectangle<float> (body.getCentreX() - body.getWidth() * 0.18f, body.getBottom() - body.getHeight() * 0.3f,
                                    body.getWidth() * 0.36f, body.getHeight() * 0.18f);
    g.setColour (accent.withAlpha (0.6f));
    g.drawRect (window, 1.0f);
}

//==============================================================================
DropZone::DropZone (String t, String s, bool c) : title (std::move (t)), subtitle (std::move (s)), compact (c)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    startTimerHz (24);
}

void DropZone::timerCallback()
{
    if (! isShowing()) return;
    dash = std::fmod (dash + (dragOver ? 2.0f : 0.6f), 24.0f);
    repaint();
}

void DropZone::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    const auto accent = dragOver ? magenta : (hover ? cyan : laserBlue.brighter (0.4f));
    g.setColour (night.withAlpha (compact ? 0.35f : 0.5f));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour ((dragOver ? magenta : cyan).withAlpha (dragOver ? 0.16f : (hover ? 0.08f : 0.04f)));
    g.fillRoundedRectangle (r, 14.0f);

    Path outline;
    outline.addRoundedRectangle (r, 14.0f);
    const float dashes[] = { 12.0f, 8.0f };
    Path dashed;
    PathStrokeType (2.2f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (outline, PathStrokeType (8.0f));
    g.setColour (accent);
    g.fillPath (dashed);
    // a spark that races around the border
    {
        const float len = outline.getLength();
        const float at = std::fmod (dash / 24.0f * len * 0.25f + (float) Time::getMillisecondCounter() * (dragOver ? 0.5f : 0.12f), len);
        const auto p = outline.getPointAlongPath (at);
        g.setColour (Colours::white);
        g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
        g.setColour (accent.withAlpha (0.4f));
        g.fillEllipse (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f);
    }

    if (compact)
    {
        drawCassette (g, Rectangle<float> (r.getX() + 16.0f, r.getCentreY() - 14.0f, 44.0f, 28.0f), accent);
        auto tr = r.withTrimmedLeft (74.0f);
        g.setColour (text);
        g.setFont (displayFont (18.0f));
        g.drawText (title, tr.removeFromTop (r.getHeight() * 0.55f).toNearestInt(), Justification::bottomLeft);
        g.setColour (dim);
        g.setFont (bodyFont (13.0f));
        g.drawText (subtitle, tr.toNearestInt(), Justification::topLeft);
        return;
    }

    auto top = r.reduced (20.0f).withHeight (r.getHeight() * 0.46f);
    drawCassette (g, Rectangle<float> (120.0f, 76.0f).withCentre ({ r.getCentreX(), top.getY() + 58.0f }), accent);
    drawLightning (g, Rectangle<float> (34.0f, 60.0f).withCentre ({ r.getCentreX() - 110.0f, top.getY() + 58.0f }), gold);
    drawLightning (g, Rectangle<float> (34.0f, 60.0f).withCentre ({ r.getCentreX() + 110.0f, top.getY() + 58.0f }), gold);
    drawChromeText (g, dragOver ? "LET IT RIP!" : title, Rectangle<float> (r.getX() + 40.0f, top.getY() + 110.0f, r.getWidth() - 80.0f, 54.0f),
                    Justification::centred, 0.2f, dragOver ? magenta : cyan);
    g.setColour (text.withAlpha (0.85f));
    g.setFont (bodyFont (16.0f));
    g.drawText (subtitle, Rectangle<float> (r.getX(), top.getY() + 170.0f, r.getWidth(), 24.0f).toNearestInt(), Justification::centred);
}

void DropZone::mouseUp (const MouseEvent& e)
{
    if (e.mouseWasClicked() && onClick) onClick();
}

bool DropZone::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (isSupportedAudioFile (File (f))) return true;
    return false;
}

void DropZone::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    Array<File> fs;
    for (auto& f : files) fs.add (File (f));
    if (onFiles) onFiles (fs);
}

//==============================================================================
RecordPanel::RecordPanel (HairmonyProcessor& p, bool c) : proc (p), compact (c)
{
    button.getProperties().set ("style", "rec");
    button.setButtonText ("REC");
    button.setTooltip ("Record from this track: arm, then press play in your DAW. Stopping the DAW imports the take.");
    button.onClick = [this] { click(); };
    addAndMakeVisible (button);
    startTimerHz (20);
}

void RecordPanel::click()
{
    const int st = proc.getCaptureState();
    if (st == HairmonyProcessor::captureIdle) proc.armCapture();
    else proc.stopCapture();
    timerCallback();
}

void RecordPanel::timerCallback()
{
    const int st = proc.getCaptureState();
    button.setToggleState (st != HairmonyProcessor::captureIdle, dontSendNotification);
    button.setButtonText (st == HairmonyProcessor::captureRecording ? "STOP" : st == HairmonyProcessor::captureArmed ? "ARMED" : "REC");
    ++frame;
    if (isShowing()) repaint();
}

void RecordPanel::resized()
{
    auto r = getLocalBounds();
    if (compact) button.setBounds (r.removeFromLeft (jmin (r.getHeight() * 2, 96)).reduced (2));
    else button.setBounds (Rectangle<int> (110, 110).withCentre ({ r.getCentreX(), r.getY() + 160 }));
}

void RecordPanel::paint (Graphics& g)
{
    const int st = proc.getCaptureState();
    const bool rec = st == HairmonyProcessor::captureRecording, armed = st == HairmonyProcessor::captureArmed;
    const auto blink = ((frame / 10) % 2) == 0;
    String status = rec ? "RECORDING  " + formatTime (proc.getCaptureSeconds())
                  : armed ? (blink ? "ARMED - PRESS PLAY IN YOUR DAW" : "")
                  : "READY";

    if (compact)
    {
        auto r = getLocalBounds().withTrimmedLeft (button.getRight() + 8);
        g.setColour (rec ? danger : (armed ? gold : dim));
        g.setFont (lcdFont (13.0f));
        g.drawText (rec ? status : armed ? "ARMED - PLAY YOUR DAW" : "RECORD A TAKE", r, Justification::centredLeft);
        return;
    }

    auto r = getLocalBounds().toFloat().withTrimmedTop (12.0f);
    drawPanel (g, r.reduced (2.0f), "RECORD FROM TRACK", danger);
    drawChromeText (g, "LIVE TAKE", Rectangle<float> (r.getX() + 30.0f, r.getY() + 24.0f, r.getWidth() - 60.0f, 44.0f), Justification::centred, 0.2f, danger);

    // level meter
    auto meter = Rectangle<float> (r.getX() + 34.0f, r.getY() + 230.0f, r.getWidth() - 68.0f, 14.0f);
    g.setColour (Colour (0xff12002a));
    g.fillRoundedRectangle (meter, 3.0f);
    const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (proc.getCaptureLevel(), -60.0f) + 60.0f) / 60.0f);
    auto lit = meter.reduced (2.0f).withWidth ((meter.getWidth() - 4.0f) * lvl);
    g.setGradientFill (ColourGradient (cyan, meter.getX(), 0.0f, danger, meter.getRight(), 0.0f, false));
    g.fillRoundedRectangle (lit, 2.0f);
    g.setColour (Colour (0xff12002a));
    for (float x = meter.getX() + 8.0f; x < meter.getRight(); x += 9.0f) g.fillRect (x, meter.getY(), 2.0f, meter.getHeight());
    drawNeonRect (g, meter, laserBlue, 3.0f, 1.0f, 0.4f);

    g.setColour (rec ? danger : (armed ? gold : cyan));
    g.setFont (lcdFont (15.0f));
    g.drawText (status, Rectangle<float> (r.getX(), meter.getBottom() + 10.0f, r.getWidth(), 22.0f).toNearestInt(), Justification::centred);

    g.setColour (dim);
    g.setFont (bodyFont (13.0f));
    g.drawFittedText ("Put HAIRMONY on your vocal track, hit REC,\nthen press play in your DAW. Stop the DAW\nand the take drops straight in - the choir\nwill play back in sync with your timeline.",
                      Rectangle<float> (r.getX() + 20.0f, meter.getBottom() + 40.0f, r.getWidth() - 40.0f, 80.0f).toNearestInt(), Justification::centredTop, 5);
}

//==============================================================================
ArrowButton2089::ArrowButton2089 (bool pointsLeft) : Button (pointsLeft ? "previous" : "next"), left (pointsLeft) {}

void ArrowButton2089::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const float d = jmin (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (d, d);
    g.setGradientFill (ColourGradient (Colour (down ? 0xff8b97b6 : 0xfff8fbff), 0, c.getY(), Colour (0xff5b6688), 0, c.getBottom(), false));
    g.fillEllipse (c);
    drawNeonRect (g, c, over ? magenta : cyan, d * 0.5f, 1.2f, over ? 0.9f : 0.4f);
    Path tri;
    const float s = d * 0.22f, cx = c.getCentreX() + (left ? -1.0f : 1.0f), cy = c.getCentreY();
    if (left) tri.addTriangle (cx + s * 0.8f, cy - s, cx + s * 0.8f, cy + s, cx - s, cy);
    else      tri.addTriangle (cx - s * 0.8f, cy - s, cx - s * 0.8f, cy + s, cx + s, cy);
    g.setColour (Colour (0xff12002a));
    g.fillPath (tri);
}

TransportButton::TransportButton (float hint) : Button ("play"), sizeHint (hint) {}

void TransportButton::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float d = jmin (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (d, d);
    g.setGradientFill (ColourGradient (Colour (down ? 0xff8b97b6 : 0xfff8fbff), 0, c.getY(), Colour (0xff5b6688), 0, c.getBottom(), false));
    g.fillEllipse (c);
    drawNeonRect (g, c, stop ? magenta : (over ? cyan : laserBlue), d * 0.5f, 1.3f, stop ? 1.2f : 0.6f);
    const float s = d * 0.2f * sizeHint / 0.9f;
    g.setColour (stop ? hotPink : Colour (0xff12002a));
    if (stop) g.fillRoundedRectangle (Rectangle<float> (s * 2.0f, s * 2.0f).withCentre (c.getCentre()), 2.0f);
    else
    {
        Path tri;
        tri.addTriangle (c.getCentreX() - s * 0.75f, c.getCentreY() - s * 1.1f, c.getCentreX() - s * 0.75f, c.getCentreY() + s * 1.1f,
                         c.getCentreX() + s * 1.2f, c.getCentreY());
        g.fillPath (tri);
    }
}

//==============================================================================
ArrowSelector::ArrowSelector (RangedAudioParameter& p, String cap)
    : param (p), caption (std::move (cap)),
      attachment (p, [this] (float v) { update (v); }, nullptr)
{
    choices = param.getAllValueStrings();
    if (auto* c = dynamic_cast<AudioParameterChoice*> (&param)) choices = c->choices;
    addAndMakeVisible (prev);
    addAndMakeVisible (next);
    addAndMakeVisible (name);
    name.getProperties().set ("style", "ghost");
    name.setTooltip ("Click to pick from the list");
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    name.onClick = [this] { showMenu(); };
    attachment.sendInitialUpdate();
}

void ArrowSelector::update (float value)
{
    index = jlimit (0, jmax (0, choices.size() - 1), roundToInt (value));
    name.setButtonText (choices[index].toUpperCase());
    repaint();
}

void ArrowSelector::step (int delta)
{
    if (choices.isEmpty()) return;
    const int n = choices.size();
    const int target = ((index + delta) % n + n) % n;
    attachment.setValueAsCompleteGesture ((float) target);
}

void ArrowSelector::showMenu()
{
    PopupMenu m;
    for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i].toUpperCase(), true, i == index);
    SafePointer<ArrowSelector> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&name).withMinimumWidth (name.getWidth()),
                     [safe] (int r) { if (safe != nullptr && r > 0) safe->attachment.setValueAsCompleteGesture ((float) (r - 1)); });
}

void ArrowSelector::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (110);
    prev.setBounds (r.removeFromLeft (36));
    next.setBounds (r.removeFromRight (36));
    name.setBounds (r.reduced (6, 0));
}

void ArrowSelector::paint (Graphics& g)
{
    g.setColour (dim);
    g.setFont (displayFont (16.0f));
    g.drawText (caption, getLocalBounds().withWidth (106), Justification::centredLeft);
}

//==============================================================================
DragTile::DragTile (String l, bool big) : label (std::move (l)), large (big)
{
    setMouseCursor (MouseCursor::DraggingHandCursor);
    setTooltip (large ? "Drag all stems into your DAW at once" : "Drag this stem into your DAW");
}

void DragTile::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto accent = hover ? magenta : cyan;
    if (large)
    {
        g.setColour (accent.withAlpha (hover ? 0.14f : 0.06f));
        g.fillRoundedRectangle (r, 8.0f);
        drawNeonRect (g, r, accent, 8.0f, 1.3f, hover ? 1.0f : 0.5f);
        auto icon = r.reduced (14.0f).withHeight (r.getHeight() * 0.48f);
        for (int i = 2; i >= 0; --i)
            drawCassette (g, icon.withSizeKeepingCentre (icon.getHeight() * 1.5f, icon.getHeight()).translated ((float) i * 7.0f - 7.0f, (float) -i * 5.0f + 5.0f), accent);
        g.setColour (text);
        g.setFont (displayFont (19.0f));
        g.drawText (label, r.withTrimmedTop (r.getHeight() * 0.62f).toNearestInt(), Justification::centred);
    }
    else
    {
        drawCassette (g, r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight() * 1.5f), jmin (r.getHeight(), r.getWidth() / 1.5f)), accent);
    }
}

StringArray DragTile::prepareFiles()
{
    StringArray paths;
    if (getFiles)
        for (auto& f : getFiles()) if (f.existsAsFile()) paths.add (f.getFullPathName());
    return paths;
}

void DragTile::mouseDrag (const MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    const auto paths = prepareFiles();
    if (paths.isEmpty()) { if (onDragFinished) onDragFinished (false); return; }
    SafePointer<DragTile> safe (this);
    const bool started = DragAndDropContainer::performExternalDragDropOfFiles (paths, false, this, [safe]
    {
        if (safe != nullptr) { safe->dragging = false; if (safe->onDragFinished) safe->onDragFinished (true); }
    });
    if (! started) { dragging = false; if (onDragFinished) onDragFinished (false); }
}

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& s, const String& id, const String& cap, Colour a) : caption (cap), accent (a)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 90, 20);
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setDoubleClickReturnValue (true, s.getParameter (id)->convertFrom0to1 (s.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (22);
    slider.setBounds (r);
}

void Knob::paint (Graphics& g)
{
    g.setColour (accent.brighter (0.3f));
    g.setFont (displayFont (17.0f));
    g.drawText (caption, getLocalBounds().removeFromTop (22), Justification::centred);
}

//==============================================================================
TrackRow::TrackRow (int i, const HairmonyProcessor::TrackInfo& t, double r) : index (i), info (t), rate (r)
{
    mute.setClickingTogglesState (true);
    mute.getProperties().set ("style", "toggle");
    mute.setToggleState (info.muted, dontSendNotification);
    mute.setTooltip ("Mute this vocal (muted vocals are skipped when the choir is created)");
    mute.onClick = [this] { if (onMute) onMute (index, mute.getToggleState()); };
    addAndMakeVisible (mute);
}

void TrackRow::resized() { mute.setBounds (getLocalBounds().removeFromRight (64).reduced (4, 7)); }

void TrackRow::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Colours::white.withAlpha (index % 2 ? 0.03f : 0.06f));
    g.fillRoundedRectangle (r, 6.0f);
    drawCassette (g, Rectangle<float> (r.getX() + 8.0f, r.getCentreY() - 11.0f, 34.0f, 22.0f), info.muted ? dim : cyan);
    g.setColour (info.muted ? dim : text);
    g.setFont (displayFont (17.0f));
    g.drawFittedText (info.name.toUpperCase(), r.withTrimmedLeft (52.0f).withTrimmedRight (info.hostStart >= 0 ? 230.0f : 140.0f).toNearestInt(), Justification::centredLeft, 1, 0.7f);
    g.setColour (cyan.withAlpha (0.8f));
    g.setFont (lcdFont (13.0f));
    g.drawText (formatTime (info.seconds), r.withTrimmedRight (70.0f).removeFromRight (70.0f).toNearestInt(), Justification::centredRight);
    if (info.hostStart >= 0)
    {
        g.setColour (danger.withAlpha (0.9f));
        g.setFont (lcdFont (11.0f));
        g.drawText ("REC @ " + formatTime ((double) info.hostStart / jmax (1.0, rate)), r.withTrimmedRight (140.0f).removeFromRight (90.0f).toNearestInt(), Justification::centredRight);
    }
}

//==============================================================================
OutputRow::OutputRow (int i, const HairmonyProcessor::TrackInfo& t, std::vector<float> pk) : index (i), info (t), peaks (std::move (pk))
{
    mute.setClickingTogglesState (true);
    mute.getProperties().set ("style", "toggle");
    mute.setToggleState (info.muted, dontSendNotification);
    mute.onClick = [this] { info.muted = mute.getToggleState(); repaint(); if (onMute) onMute (index, mute.getToggleState()); };
    play.onClick = [this] { if (onPlay) onPlay (index); };
    play.setTooltip ("Audition this output");
    drag.getFiles = [this] { return getFile ? getFile (index) : Array<File>(); };
    addAndMakeVisible (play);
    addAndMakeVisible (mute);
    addAndMakeVisible (drag);
}

juce::Rectangle<int> OutputRow::laneBounds() const
{
    return getLocalBounds().reduced (4).withTrimmedLeft (300).withTrimmedRight (6);
}

void OutputRow::setPlayhead (float f, bool playingThis)
{
    if (std::abs (f - playhead) > 1.0e-4f || playingThis != rowPlaying)
    {
        playhead = f; rowPlaying = playingThis;
        play.setShowsStop (playingThis);
        repaint (laneBounds());
    }
}

void OutputRow::resized()
{
    auto r = getLocalBounds().reduced (4);
    play.setBounds (r.removeFromLeft (44).reduced (2));
    r.removeFromLeft (6);
    auto infoArea = r.removeFromLeft (246);
    mute.setBounds (infoArea.removeFromRight (58).reduced (2, 14));
    drag.setBounds (infoArea.removeFromRight (48).reduced (4, 12));
}

void OutputRow::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Colours::white.withAlpha (index % 2 ? 0.03f : 0.06f));
    g.fillRoundedRectangle (r, 8.0f);

    const int nameW = jmax (60, drag.getX() - 62);
    g.setColour (info.muted ? dim : text);
    g.setFont (displayFont (18.0f));
    g.drawFittedText (info.name.toUpperCase(), Rectangle<int> (58, 6, nameW, getHeight() / 2 - 2), Justification::bottomLeft, 1, 0.65f);
    g.setColour (cyan.withAlpha (0.75f));
    g.setFont (lcdFont (12.0f));
    g.drawText (formatTime (info.seconds), Rectangle<int> (58, getHeight() / 2 + 2, nameW, 18), Justification::topLeft);

    auto lane = laneBounds().toFloat();
    g.setColour (Colour (0xff0b0420));
    g.fillRoundedRectangle (lane, 6.0f);
    g.setColour (laserBlue.withAlpha (0.25f));
    for (float x = lane.getX(); x < lane.getRight(); x += 24.0f) g.drawVerticalLine ((int) x, lane.getY() + 2.0f, lane.getBottom() - 2.0f);
    g.drawHorizontalLine ((int) lane.getCentreY(), lane.getX(), lane.getRight());

    if (! peaks.empty())
    {
        Path wave;
        const float mid = lane.getCentreY(), hh = lane.getHeight() * 0.46f;
        const int n = (int) peaks.size();
        float maxP = 1.0e-6f; for (float p : peaks) maxP = jmax (maxP, p);
        const float norm = 1.0f / jmax (0.25f, maxP);
        wave.startNewSubPath (lane.getX(), mid);
        for (int i = 0; i < n; ++i) wave.lineTo (lane.getX() + lane.getWidth() * (float) i / (float) (n - 1), mid - peaks[(size_t) i] * norm * hh);
        for (int i = n - 1; i >= 0; --i) wave.lineTo (lane.getX() + lane.getWidth() * (float) i / (float) (n - 1), mid + peaks[(size_t) i] * norm * hh);
        wave.closeSubPath();
        const auto c1 = info.muted ? dim.darker (0.4f) : (index % 2 ? cyan : hotPink);
        const auto c2 = info.muted ? dim.darker (0.6f) : (index % 2 ? laserBlue : magenta);
        g.setGradientFill (ColourGradient (c1, 0.0f, lane.getY(), c2, 0.0f, lane.getBottom(), false));
        g.fillPath (wave);
        g.setColour (c1.brighter (0.4f).withAlpha (0.6f));
        g.strokePath (wave, PathStrokeType (0.8f));
    }
    drawNeonRect (g, lane, (rowPlaying ? magenta : laserBlue).withAlpha (0.7f), 6.0f, 1.0f, rowPlaying ? 0.8f : 0.2f);

    if (playhead >= 0.0f)
    {
        const float x = lane.getX() + lane.getWidth() * jlimit (0.0f, 1.0f, playhead);
        drawNeonLine (g, { x, lane.getY() + 2.0f, x, lane.getBottom() - 2.0f }, gold, 1.5f);
    }
}

//==============================================================================
RowList::RowList (int h) : rowHeight (h)
{
    viewport.setViewedComponent (&inner, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
}

void RowList::setRows (std::vector<std::unique_ptr<Component>> r)
{
    rows = std::move (r);
    inner.removeAllChildren();
    for (auto& c : rows) inner.addAndMakeVisible (*c);
    resized();
    repaint();
}

void RowList::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = getWidth() - (rows.size() * (size_t) rowHeight > (size_t) getHeight() ? viewport.getScrollBarThickness() + 2 : 0);
    inner.setSize (jmax (1, w), jmax (1, (int) rows.size() * rowHeight));
    for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, (int) i * rowHeight, w, rowHeight);
}

void RowList::paint (Graphics& g)
{
    if (rows.empty() && paintEmpty) paintEmpty (g, getLocalBounds());
}

//==============================================================================
void Screen::browseForAudio()
{
    chooser = std::make_unique<FileChooser> ("Choose vocal files", File::getSpecialLocation (File::userMusicDirectory), supportedWildcards());
    SafePointer<Screen> safe (this);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                          [safe] (const FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              auto files = fc.getResults();
                              if (! files.isEmpty()) safe->proc.importFiles (files);
                          });
}

//==============================================================================
ImportScreen::ImportScreen (HairmonyProcessor& p)
    : Screen (p), zone ("DROP YOUR VOCAL HERE", "drag a vocal in from your DAW, or browse for a file", false), record (p, false)
{
    addAndMakeVisible (record);
    zone.onFiles = [this] (const Array<File>& f) { proc.importFiles (f); };
    zone.onClick = [this] { browseForAudio(); };
    browse.onClick = [this] { browseForAudio(); };
    browse.setTooltip ("Pick one or more WAV / AIFF / FLAC / MP3 / OGG files");
    message.setFont (bodyFont (14.0f));
    message.setColour (Label::textColourId, hotPink);
    message.setJustificationType (Justification::centred);
    addAndMakeVisible (zone);
    addAndMakeVisible (browse);
    addAndMakeVisible (message);
}

void ImportScreen::resized()
{
    auto r = getLocalBounds();
    auto main = r.withTrimmedBottom (70).reduced (0, 10);
    record.setBounds (main.removeFromRight (330));
    main.removeFromRight (20);
    zone.setBounds (main);
    browse.setBounds (Rectangle<int> (220, 46).withCentre ({ zone.getBounds().getCentreX(), zone.getY() + 320 }));
    message.setBounds (r.removeFromBottom (64).reduced (20, 0).removeFromTop (30));
}

void ImportScreen::paint (Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (34).reduced (20, 0);
    g.setColour (dim);
    g.setFont (bodyFont (13.0f));
    g.drawText ("WAV  /  AIFF  /  FLAC  /  MP3  /  OGG   -   up to 10 minutes   -   dry, solo lead vocals make the mightiest choirs",
                r, Justification::centred);
}

void ImportScreen::refresh() { message.setText (proc.getLastMessage(), dontSendNotification); }

//==============================================================================
CraftScreen::Overlay::Overlay()
{
    cancel.getProperties().set ("style", "ghost");
    addAndMakeVisible (cancel);
}

void CraftScreen::Overlay::resized()
{
    cancel.setBounds (Rectangle<int> (140, 38).withCentre ({ getWidth() / 2, getHeight() / 2 + 78 }));
}

void CraftScreen::Overlay::paint (Graphics& g)
{
    g.fillAll (Colour (0xd0050010));
    auto panel = Rectangle<float> (520.0f, 230.0f).withCentre (getLocalBounds().toFloat().getCentre());
    drawPanel (g, panel, "RENDERING", magenta, true);
    drawChromeText (g, "SUMMONING THE CHOIR", panel.withHeight (60.0f).translated (0.0f, 28.0f).reduced (40.0f, 4.0f), Justification::centred, 0.2f, magenta);

    auto bar = Rectangle<float> (panel.getX() + 40.0f, panel.getCentreY() + 4.0f, panel.getWidth() - 80.0f, 22.0f);
    g.setColour (Colour (0xff12002a));
    g.fillRoundedRectangle (bar, 4.0f);
    auto lit = bar.reduced (3.0f).withWidth ((bar.getWidth() - 6.0f) * jlimit (0.0f, 1.0f, progress));
    g.setGradientFill (ColourGradient (cyan, lit.getX(), 0.0f, magenta, bar.getRight(), 0.0f, false));
    g.fillRoundedRectangle (lit, 3.0f);
    // segment slits, like an old VU display
    g.setColour (Colour (0xff12002a));
    for (float x = bar.getX() + 10.0f; x < bar.getRight(); x += 12.0f) g.fillRect (x, bar.getY(), 2.0f, bar.getHeight());
    drawNeonRect (g, bar, cyan, 4.0f, 1.2f, 0.6f);

    g.setColour (cyan);
    g.setFont (lcdFont (16.0f));
    const char* phases[] = { "TUNING THE SINGERS", "TEASING THE HAIR", "WARMING UP VOCAL TRACTS", "CHARGING FLUX HARMONIES" };
    g.drawText (String (roundToInt (progress * 100.0f)) + "%   " + phases[(frame / 45) % 4],
                bar.translated (0.0f, 28.0f).toNearestInt(), Justification::centred);
}

CraftScreen::CraftScreen (HairmonyProcessor& p)
    : Screen (p),
      record (p, true),
      choir (*p.getState().getParameter (ParamIDs::choir), "CHOIR"),
      mic (*p.getState().getParameter (ParamIDs::mic), "MICROPHONE")
{
    addAndMakeVisible (record);
    for (auto* b : { &unisonBtn, &octavesBtn, &harmonyBtn })
    {
        b->getProperties().set ("style", "toggle");
        addAndMakeVisible (*b);
    }
    unisonBtn.setTooltip ("Everyone sings your melody");
    octavesBtn.setTooltip ("Your melody plus an octave (below for adult choirs, above for kids)");
    harmonyBtn.setTooltip ("The choir sings in parts: soprano on the tune, alto/tenor/bass harmonising in your key");
    voicingAtt = std::make_unique<ParameterAttachment> (*p.getState().getParameter (ParamIDs::voicing), [this] (float v)
    {
        const int i = roundToInt (v);
        unisonBtn.setToggleState (i == 0, dontSendNotification);
        octavesBtn.setToggleState (i == 1, dontSendNotification);
        harmonyBtn.setToggleState (i == 2, dontSendNotification);
    }, nullptr);
    unisonBtn.onClick = [this] { voicingAtt->setValueAsCompleteGesture (0.0f); };
    octavesBtn.onClick = [this] { voicingAtt->setValueAsCompleteGesture (1.0f); };
    harmonyBtn.onClick = [this] { voicingAtt->setValueAsCompleteGesture (2.0f); };
    voicingAtt->sendInitialUpdate();
    auto& st = proc.getState();
    startOver.getProperties().set ("style", "ghost");
    startOver.setTooltip ("Clear the imported audio and go back to the start");
    startOver.onClick = [this] { proc.startOver(); };
    addAndMakeVisible (startOver);

    list.paintEmpty = [] (Graphics& g, Rectangle<int> r)
    {
        g.setColour (dim); g.setFont (bodyFont (14.0f));
        g.drawText ("No vocals loaded", r, Justification::centred);
    };
    addAndMakeVisible (list);

    importZone.onFiles = [this] (const Array<File>& f) { proc.importFiles (f); };
    importZone.onClick = [this] { browseForAudio(); };
    addAndMakeVisible (importZone);

    play.setTooltip ("Play / stop the imported vocals");
    play.onClick = [this] { if (proc.isPlaying()) proc.stop(); else proc.play(); };
    addAndMakeVisible (play);

    addAndMakeVisible (choir);
    addAndMakeVisible (mic);

    auto setupToggle = [this, &st] (TextButton& b, const char* id, const char* tip)
    {
        b.getProperties().set ("style", "toggle");
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
        addAndMakeVisible (b);
        buttonAtts.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (st, id, b));
    };
    setupToggle (octUp, ParamIDs::octUp, "Adds a choir singing an octave above your vocal");
    setupToggle (octDown, ParamIDs::octDown, "Adds a choir singing an octave below your vocal");
    setupToggle (third, ParamIDs::third, "Adds a choir singing a diatonic third above (set Key and Scale)");
    setupToggle (fifth, ParamIDs::fifth, "Adds a choir singing a diatonic fifth above (set Key and Scale)");
    setupToggle (below, ParamIDs::below, "Adds an octave-below choir under each harmony");

    for (auto* b : { &stereoBtn, &monoBtn })
    {
        b->getProperties().set ("style", "toggle");
        addAndMakeVisible (*b);
    }
    stereoAtt = std::make_unique<ParameterAttachment> (*st.getParameter (ParamIDs::stereo), [this] (float v)
    {
        stereoBtn.setToggleState (v > 0.5f, dontSendNotification);
        monoBtn.setToggleState (v <= 0.5f, dontSendNotification);
    }, nullptr);
    stereoBtn.onClick = [this] { stereoAtt->setValueAsCompleteGesture (1.0f); };
    monoBtn.onClick = [this] { stereoAtt->setValueAsCompleteGesture (0.0f); };
    stereoAtt->sendInitialUpdate();

    int id = 1;
    for (auto& k : keyNames()) key.addItem (k, id++);
    id = 1;
    for (auto& s : scaleNames()) scale.addItem (String (s).toUpperCase(), id++);
    key.setTooltip ("Key of your song - used by the 3rd / 5th harmony generator");
    scale.setTooltip ("Scale of your song - used by the 3rd / 5th harmony generator");
    addAndMakeVisible (key);
    addAndMakeVisible (scale);
    keyAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (st, ParamIDs::key, key);
    scaleAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (st, ParamIDs::scale, scale);

    create.getProperties().set ("style", "hero");
    create.setTooltip ("Render the choir (runs in the background)");
    create.onClick = [this] { proc.startRender(); refresh(); };
    addAndMakeVisible (create);

    message.setFont (bodyFont (14.0f));
    message.setColour (Label::textColourId, hotPink);
    message.setJustificationType (Justification::centred);
    addAndMakeVisible (message);

    overlay.cancel.onClick = [this] { proc.cancelRender(); };
    addChildComponent (overlay);
}

CraftScreen::~CraftScreen() { buttonAtts.clear(); }

void CraftScreen::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (40);
    startOver.setBounds (top.removeFromLeft (150).reduced (0, 4));

    auto bottom = r.removeFromBottom (96);
    create.setBounds (Rectangle<int> (460, 66).withCentre ({ bottom.getCentreX(), bottom.getY() + 56 }));
    message.setBounds (bottom.removeFromTop (22).reduced (100, 0));

    r.removeFromTop (14);
    leftPanel = r.removeFromLeft (430);
    r.removeFromLeft (20);
    rightPanel = r;

    auto lp = leftPanel.reduced (16, 22);
    auto transport = lp.removeFromBottom (44);
    play.setBounds (transport.removeFromLeft (44));
    record.setBounds (transport.removeFromRight (190));
    lp.removeFromBottom (8);
    importZone.setBounds (lp.removeFromBottom (70));
    lp.removeFromBottom (8);
    list.setBounds (lp);

    auto rp = rightPanel.reduced (20, 20);
    const int rowH = 38, gap = 6;
    choir.setBounds (rp.removeFromTop (rowH)); rp.removeFromTop (gap);
    mic.setBounds (rp.removeFromTop (rowH)); rp.removeFromTop (gap + 4);

    auto vrow = rp.removeFromTop (rowH); vrow.removeFromLeft (110);
    unisonBtn.setBounds (vrow.removeFromLeft (104).reduced (2, 3)); vrow.removeFromLeft (4);
    octavesBtn.setBounds (vrow.removeFromLeft (110).reduced (2, 3)); vrow.removeFromLeft (4);
    harmonyBtn.setBounds (vrow.removeFromLeft (150).reduced (2, 3));
    rp.removeFromTop (gap);

    auto row = rp.removeFromTop (rowH); row.removeFromLeft (110);
    octUp.setBounds (row.removeFromLeft (110).reduced (2, 3)); row.removeFromLeft (8);
    octDown.setBounds (row.removeFromLeft (120).reduced (2, 3));
    rp.removeFromTop (gap);

    row = rp.removeFromTop (rowH); row.removeFromLeft (110);
    key.setBounds (row.removeFromLeft (80).reduced (2, 4)); row.removeFromLeft (64);
    scale.setBounds (row.reduced (2, 4));
    rp.removeFromTop (gap);

    row = rp.removeFromTop (rowH); row.removeFromLeft (110);
    third.setBounds (row.removeFromLeft (100).reduced (2, 3)); row.removeFromLeft (6);
    fifth.setBounds (row.removeFromLeft (100).reduced (2, 3)); row.removeFromLeft (6);
    below.setBounds (row.removeFromLeft (118).reduced (2, 3));
    rp.removeFromTop (gap);

    row = rp.removeFromTop (rowH); row.removeFromLeft (110);
    stereoBtn.setBounds (row.removeFromLeft (130).reduced (2, 3)); row.removeFromLeft (6);
    monoBtn.setBounds (row.removeFromLeft (110).reduced (2, 3));

    overlay.setBounds (getLocalBounds());
}

void CraftScreen::paint (Graphics& g)
{
    drawPanel (g, leftPanel.toFloat(), "VOCAL TAPES", cyan);
    drawPanel (g, rightPanel.toFloat(), "CHOIR SETUP", magenta);

    auto label = [&g] (const String& t, Rectangle<int> r)
    {
        g.setColour (dim);
        g.setFont (displayFont (16.0f));
        g.drawText (t, r, Justification::centredLeft);
    };
    label ("VOICING", { unisonBtn.getX() - 110, unisonBtn.getY(), 106, unisonBtn.getHeight() });
    label ("OCTAVE", { octUp.getX() - 110, octUp.getY(), 106, octUp.getHeight() });
    label ("KEY", { key.getX() - 110, key.getY(), 106, key.getHeight() });
    label ("SCALE", { key.getRight() + 10, key.getY(), 60, key.getHeight() });
    label ("ABOVE", { third.getX() - 110, third.getY(), 106, third.getHeight() });
    label ("OUTPUT", { stereoBtn.getX() - 110, stereoBtn.getY(), 106, stereoBtn.getHeight() });

    g.setColour (cyan.withAlpha (0.85f));
    g.setFont (lcdFont (14.0f));
    g.drawText (formatTime (proc.isPlaying() ? proc.getPlayheadSeconds() : 0.0) + " / " + formatTime (proc.getPlaybackLengthSeconds()),
                play.getBounds().withX (play.getRight() + 12).withWidth (150), Justification::centredLeft);
}

void CraftScreen::refresh()
{
    // Only rebuild the rows when the set of vocals changes; otherwise update them in place
    // (keeps the scroll position and the button that was just clicked alive).
    const auto infos = proc.getTrackInfos();
    String sig;
    for (auto& t : infos) sig << t.name << "|" << t.seconds << ";";
    if (sig == rowSignature && list.getNumRows() == (int) infos.size())
    {
        for (int r = 0; r < (int) infos.size(); ++r)
            if (auto* row = dynamic_cast<TrackRow*> (list.getRow (r))) row->setMuted (infos[(size_t) r].muted);
        message.setText (proc.getLastMessage(), dontSendNotification);
        tick();
        return;
    }
    rowSignature = sig;

    std::vector<std::unique_ptr<Component>> rows;
    int i = 0;
    for (auto& t : infos)
    {
        auto row = std::make_unique<TrackRow> (i++, t, proc.getWorkRate());
        row->onMute = [this] (int idx, bool m) { proc.setTrackMuted (idx, m); };
        rows.push_back (std::move (row));
    }
    list.setRows (std::move (rows));
    message.setText (proc.getLastMessage(), dontSendNotification);
    tick();
}

void CraftScreen::tick()
{
    const bool rendering = proc.isRendering();
    if (overlay.isVisible() != rendering) { overlay.setVisible (rendering); if (rendering) overlay.toFront (false); }
    if (rendering) { overlay.progress = proc.getRenderProgress(); ++overlay.frame; overlay.repaint(); }
    create.setEnabled (! rendering);
    play.setShowsStop (proc.isPlaying());
    repaint (play.getBounds().withWidth (160));
}

//==============================================================================
ReviewScreen::ReviewScreen (HairmonyProcessor& p)
    : Screen (p),
      loCut (p.getState(), ParamIDs::loCut, "LO-CUT", cyan),
      brightness (p.getState(), ParamIDs::brightness, "BRIGHTNESS", gold),
      reverb (p.getState(), ParamIDs::reverb, "REVERB", magenta)
{
    back.getProperties().set ("style", "ghost");
    back.setTooltip ("Back to the choir setup (clears these outputs)");
    back.onClick = [this] { proc.backToCraft(); };
    addAndMakeVisible (back);

    follow.getProperties().set ("style", "toggle");
    follow.setClickingTogglesState (true);
    follow.setTooltip ("Recorded takes: the choir plays along with your DAW at the spot you recorded it");
    addAndMakeVisible (follow);
    followAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.getState(), ParamIDs::follow, follow);
    play.setTooltip ("Play / stop all outputs");
    play.onClick = [this] { if (proc.isPlaying() && proc.getSoloIndex() < 0) proc.stop(); else { proc.stop(); proc.play (-1); } };
    addAndMakeVisible (play);

    gain.setSliderStyle (Slider::LinearHorizontal);
    gain.setTextBoxStyle (Slider::TextBoxRight, false, 76, 22);
    gain.setTextValueSuffix (" dB");
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setTooltip ("Playback volume");
    addAndMakeVisible (gain);
    gainAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.getState(), ParamIDs::gain, gain);

    list.paintEmpty = [] (Graphics& g, Rectangle<int> r)
    {
        g.setColour (dim); g.setFont (bodyFont (14.0f));
        g.drawText ("No outputs yet", r, Justification::centred);
    };
    addAndMakeVisible (list);
    addAndMakeVisible (loCut);
    addAndMakeVisible (brightness);
    addAndMakeVisible (reverb);

    stems.getFiles = [this] { return proc.createAllExportFiles(); };
    addAndMakeVisible (stems);

    save.getProperties().set ("style", "ghost");
    save.setTooltip ("Write every output (with Lo-Cut / Brightness / Reverb) to a folder");
    save.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Save stems to folder", File::getSpecialLocation (File::userMusicDirectory));
        SafePointer<ReviewScreen> safe (this);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [safe] (const FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == File()) return;
            StringArray written;
            const bool ok = safe->proc.exportAllTo (fc.getResult(), written);
            safe->message.setText (ok ? "Saved " + String (written.size()) + " stems to " + fc.getResult().getFullPathName()
                                      : "Couldn't save the stems there", dontSendNotification);
        });
    };
    addAndMakeVisible (save);

    message.setFont (bodyFont (13.0f));
    message.setColour (Label::textColourId, cyan);
    addAndMakeVisible (message);
}

void ReviewScreen::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (40);
    back.setBounds (top.removeFromLeft (120).reduced (0, 4));
    top.removeFromLeft (20);
    play.setBounds (top.removeFromLeft (40));
    top.removeFromLeft (150);
    follow.setBounds (top.removeFromLeft (170).reduced (0, 4));
    gain.setBounds (top.removeFromRight (360).reduced (60, 6).withTrimmedLeft (0));
    gain.setBounds (gain.getBounds().withLeft (gain.getX()));

    r.removeFromTop (14);
    fxPanel = r.removeFromBottom (150);
    r.removeFromBottom (18);
    listPanel = r;
    list.setBounds (listPanel.reduced (14, 20));

    auto fx = fxPanel.reduced (20, 18);
    for (auto* k : { &loCut, &brightness, &reverb }) { k->setBounds (fx.removeFromLeft (130)); fx.removeFromLeft (14); }
    auto right = fx.removeFromRight (360);
    stems.setBounds (right.removeFromRight (130));
    right.removeFromRight (16);
    save.setBounds (right.removeFromTop (60).removeFromBottom (40).withTrimmedLeft (40));
    message.setBounds (right.withTrimmedLeft (0).removeFromTop (60));
}

void ReviewScreen::paint (Graphics& g)
{
    drawPanel (g, listPanel.toFloat(), "CHOIR STEMS", magenta);
    drawPanel (g, fxPanel.toFloat(), "FINISH", cyan);

    g.setColour (dim);
    g.setFont (displayFont (16.0f));
    g.drawText ("GAIN", gain.getBounds().withX (gain.getX() - 56).withWidth (52), Justification::centredRight);

    g.setColour (cyan.withAlpha (0.85f));
    g.setFont (lcdFont (14.0f));
    const bool synced = proc.isSyncedPlaying();
    g.drawText ((proc.isPlaying() || synced ? formatTime (proc.getPlayheadSeconds()) : formatTime (0.0)) + " / " + formatTime (proc.getPlaybackLengthSeconds()),
                play.getBounds().withX (play.getRight() + 8).withWidth (140), Justification::centredLeft);
    if (synced)
    {
        g.setColour (gold);
        g.setFont (lcdFont (11.0f));
        g.drawText ("FOLLOWING YOUR DAW", follow.getBounds().translated (0, 26).withHeight (14), Justification::centred);
    }

    g.setColour (dim);
    g.setFont (bodyFont (12.5f));
    g.drawFittedText ("Drag a tape icon into your DAW, or the STEMS tile for all of them.\nLo-Cut, Brightness and Reverb are baked into exports.",
                      save.getBounds().withY (save.getBottom() + 6).withHeight (40).withX (save.getX() - 40).withWidth (save.getWidth() + 40),
                      Justification::topLeft, 3);
}

void ReviewScreen::refresh()
{
    const auto infos = proc.getOutputInfos();
    String sig;
    for (auto& t : infos) sig << t.name << "|" << t.seconds << ";";
    if (sig == rowSignature && list.getNumRows() == (int) infos.size())
    {
        for (int r = 0; r < (int) infos.size(); ++r)
            if (auto* row = dynamic_cast<OutputRow*> (list.getRow (r))) row->setMuted (infos[(size_t) r].muted);
        if (proc.getLastMessage().isNotEmpty()) message.setText (proc.getLastMessage(), dontSendNotification);
        tick();
        return;
    }
    rowSignature = sig;

    std::vector<std::unique_ptr<Component>> rows;
    for (int i = 0; i < (int) infos.size(); ++i)
    {
        auto row = std::make_unique<OutputRow> (i, infos[(size_t) i], proc.getOutputPeaks (i, 400));
        row->onMute = [this] (int idx, bool m) { proc.setOutputMuted (idx, m); };
        row->onPlay = [this] (int idx)
        {
            if (proc.isPlaying() && proc.getSoloIndex() == idx) proc.stop();
            else { proc.stop(); proc.play (idx); }
        };
        row->getFile = [this] (int idx) { Array<File> a; if (auto f = proc.createExportFile (idx); f.existsAsFile()) a.add (f); return a; };
        rows.push_back (std::move (row));
    }
    list.setRows (std::move (rows));
    if (proc.getLastMessage().isNotEmpty()) message.setText (proc.getLastMessage(), dontSendNotification);
    tick();
}

void ReviewScreen::tick()
{
    const bool playing = proc.isPlaying();
    const int soloIdx = proc.getSoloIndex();
    const double len = proc.getPlaybackLengthSeconds();
    const float frac = playing && len > 0.0 ? (float) (proc.getPlayheadSeconds() / len) : -1.0f;
    play.setShowsStop ((playing && soloIdx < 0) || proc.isSyncedPlaying());
    if (proc.isSyncedPlaying() && len > 0.0)
    {
        const float f2 = (float) (proc.getPlayheadSeconds() / len);
        for (int i = 0; i < list.getNumRows(); ++i)
            if (auto* row = dynamic_cast<OutputRow*> (list.getRow (i))) row->setPlayhead (f2, false);
        repaint (play.getBounds().withWidth (400).withHeight (44));
        return;
    }
    for (int i = 0; i < list.getNumRows(); ++i)
        if (auto* row = dynamic_cast<OutputRow*> (list.getRow (i)))
            row->setPlayhead ((soloIdx < 0 || soloIdx == i) ? frac : -1.0f, playing && soloIdx == i);
    repaint (play.getBounds().withWidth (400).withHeight (44));
}

//==============================================================================
SettingsOverlay::SettingsOverlay (HairmonyProcessor& p) : proc (p)
{
    for (auto* b : { &close, &change, &open, &clear }) { b->getProperties().set ("style", "ghost"); addAndMakeVisible (*b); }
    close.onClick = [this] { setVisible (false); };
    change.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Temp file storage", proc.getTempFolder());
        SafePointer<SettingsOverlay> safe (this);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [safe] (const FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult() != File()) { safe->proc.setTempFolder (fc.getResult()); safe->refresh(); }
        });
    };
    open.onClick = [this] { proc.getTempFolder().createDirectory(); proc.getTempFolder().startAsProcess(); };
    clear.onClick = [this] { proc.clearTempFiles(); refresh(); };
    path.setFont (lcdFont (13.0f));
    path.setColour (Label::textColourId, cyan);
    path.setColour (Label::backgroundColourId, Colour (0xff0b0420));
    path.setColour (Label::outlineColourId, laserBlue.withAlpha (0.5f));
    addAndMakeVisible (path);
    refresh();
}

void SettingsOverlay::refresh() { path.setText (proc.getTempFolder().getFullPathName(), dontSendNotification); }

void SettingsOverlay::mouseUp (const MouseEvent& e)
{
    if (! panel.contains (e.getPosition())) setVisible (false);
}

void SettingsOverlay::resized()
{
    panel = Rectangle<int> (700, 500).withCentre (getLocalBounds().getCentre());
    auto r = panel.reduced (28, 30);
    close.setBounds (panel.getRight() - 128, panel.getY() + 18, 104, 34);
    r.removeFromTop (78);
    auto row = r.removeFromTop (30);
    path.setBounds (row);
    r.removeFromTop (8);
    row = r.removeFromTop (34);
    change.setBounds (row.removeFromLeft (130)); row.removeFromLeft (10);
    open.setBounds (row.removeFromLeft (90)); row.removeFromLeft (10);
    clear.setBounds (row.removeFromLeft (200));
}

void SettingsOverlay::paint (Graphics& g)
{
    g.fillAll (Colour (0xc0050010));
    drawPanel (g, panel.toFloat(), "SETTINGS", cyan, true);
    auto r = panel.reduced (28, 30);
    drawChromeText (g, "BACKSTAGE", r.removeFromTop (44).toFloat().withWidth (300.0f), Justification::centredLeft, 0.2f, cyan);

    g.setColour (dim);
    g.setFont (displayFont (16.0f));
    g.drawText ("TEMP FILE STORAGE", r.withY (path.getY() - 24).withHeight (22), Justification::centredLeft);

    auto body = r.withTop (clear.getBottom() + 22);
    g.setColour (magenta);
    g.setFont (displayFont (18.0f));
    g.drawText ("ABOUT", body.removeFromTop (24), Justification::centredLeft);
    g.setColour (text);
    g.setFont (bodyFont (13.5f));
    g.drawFittedText ("HAIRMONY 3000 v1.0 turns one sung vocal into a full choir that follows your words, melody and timing. "
                      "Each choir model spawns 8-12 virtual singers with their own vocal-tract size, pitch drift, vibrato, "
                      "breath and timing. All processing happens on your computer - no audio ever leaves it.",
                      body.removeFromTop (64), Justification::topLeft, 4);
    body.removeFromTop (8);
    g.setColour (magenta);
    g.setFont (displayFont (18.0f));
    g.drawText ("TIPS", body.removeFromTop (24), Justification::centredLeft);
    g.setColour (text);
    g.setFont (bodyFont (13.5f));
    const char* tips[] = {
        "REC records straight off the track HAIRMONY sits on - the choir then plays back in sync with your DAW.",
        "Feed it a dry, solo lead vocal - reverb, doubles and backing vocals confuse the pitch tracker.",
        "Set KEY and SCALE to match your song before switching on the 3rd / 5th harmonies.",
        "FULL HARMONY splits the choir into parts in your KEY / SCALE; UNISON keeps everyone on the tune.",
        "Gang Vocals love shouted, punchy takes. Tuck them under the lead and pan them wide.",
        "Cyborg Choir hard-tunes to your key - great for robotic stacks.",
        "Drag single tapes into your DAW, or the STEMS tile to drop every output at once." };
    for (auto* t : tips)
    {
        auto line = body.removeFromTop (22);
        drawLightning (g, line.removeFromLeft (16).toFloat().reduced (2.0f, 3.0f), gold);
        line.removeFromLeft (6);
        g.setColour (text);
        g.setFont (bodyFont (13.5f));
        g.drawText (t, line, Justification::centredLeft, true);
    }
}
} // namespace hm::ui
