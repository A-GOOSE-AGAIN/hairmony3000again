// HAIRMONY 3000 - the three workflow screens (Import, Craft, Review) and the settings panel.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "MetalLookAndFeel.h"

namespace hm::ui
{
void drawCassette (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent);
juce::String formatTime (double seconds);

//==============================================================================
class DropZone : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    DropZone (juce::String title, juce::String subtitle, bool compact);
    std::function<void (const juce::Array<juce::File>&)> onFiles;
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    bool isDragOver() const noexcept { return dragOver; }

private:
    void timerCallback() override;
    juce::String title, subtitle;
    bool compact, hover = false, dragOver = false;
    float dash = 0.0f;
};

//==============================================================================
// Big neon record button + status for "record from track".
class RecordPanel : public juce::Component, private juce::Timer
{
public:
    RecordPanel (HairmonyProcessor& p, bool compact);
    ~RecordPanel() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void click();                 // same as pressing REC / STOP (exposed for tests)
    juce::TextButton button;
private:
    void timerCallback() override;
    HairmonyProcessor& proc;
    bool compact;
    int frame = 0;
};

//==============================================================================
class ArrowButton2089 : public juce::Button
{
public:
    explicit ArrowButton2089 (bool pointsLeft);
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    bool left;
};

class TransportButton : public juce::Button
{
public:
    explicit TransportButton (float sizeHint = 1.0f);
    void setShowsStop (bool s) { if (s != stop) { stop = s; repaint(); } }
    bool showsStop() const noexcept { return stop; }
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    bool stop = false;
    float sizeHint;
};

// "<  Gospel Choir  >" selector bound to a choice parameter (arrows cycle, the name opens a menu).
class ArrowSelector : public juce::Component
{
public:
    ArrowSelector (juce::RangedAudioParameter& param, juce::String caption);
    void resized() override;
    void paint (juce::Graphics&) override;
    int getIndex() const noexcept { return index; }
    void step (int delta);
    void showMenu();

    ArrowButton2089 prev { true }, next { false };
    juce::TextButton name;

private:
    void update (float value);
    juce::RangedAudioParameter& param;
    juce::StringArray choices;
    juce::String caption;
    int index = 0;
    juce::ParameterAttachment attachment;
};

class DragTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragTile (juce::String label, bool large);
    std::function<juce::Array<juce::File>()> getFiles;
    std::function<void (bool ok)> onDragFinished;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    // Prepares the files and starts the external drag (exposed so tests can check file creation).
    juce::StringArray prepareFiles();
private:
    juce::String label;
    bool large, hover = false, dragging = false;
};

struct Knob : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& caption, juce::Colour accent);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
    juce::String caption;
    juce::Colour accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class TrackRow : public juce::Component
{
public:
    TrackRow (int index, const HairmonyProcessor::TrackInfo& info, double rate = 48000.0);
    std::function<void (int, bool)> onMute;
    void setMuted (bool m) { if (info.muted != m) { info.muted = m; mute.setToggleState (m, juce::dontSendNotification); repaint(); } }
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::TextButton mute { "M" };
private:
    int index;
    HairmonyProcessor::TrackInfo info;
    double rate;
};

class OutputRow : public juce::Component
{
public:
    OutputRow (int index, const HairmonyProcessor::TrackInfo& info, std::vector<float> peaks);
    std::function<void (int, bool)> onMute;
    std::function<void (int)> onPlay;
    std::function<juce::Array<juce::File>(int)> getFile;
    void setPlayhead (float fraction, bool thisRowPlaying);
    void setMuted (bool m) { if (info.muted != m) { info.muted = m; mute.setToggleState (m, juce::dontSendNotification); repaint(); } }
    void resized() override;
    void paint (juce::Graphics&) override;
    TransportButton play { 0.8f };
    juce::TextButton mute { "M" };
    DragTile drag { "", false };
private:
    juce::Rectangle<int> laneBounds() const;
    int index;
    HairmonyProcessor::TrackInfo info;
    std::vector<float> peaks;
    float playhead = -1.0f;
    bool rowPlaying = false;
};

// Vertical stack of rows inside a viewport.
class RowList : public juce::Component
{
public:
    explicit RowList (int rowHeight);
    void setRows (std::vector<std::unique_ptr<juce::Component>> rows);
    int getNumRows() const noexcept { return (int) rows.size(); }
    juce::Component* getRow (int i) { return juce::isPositiveAndBelow (i, (int) rows.size()) ? rows[(size_t) i].get() : nullptr; }
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> paintEmpty;
private:
    struct Inner : juce::Component { };
    juce::Viewport viewport;
    Inner inner;
    std::vector<std::unique_ptr<juce::Component>> rows;
    int rowHeight;
};

//==============================================================================
class Screen : public juce::Component
{
public:
    explicit Screen (HairmonyProcessor& p) : proc (p) {}
    virtual void refresh() {}        // session changed
    virtual void tick() {}           // ~30 Hz animation / playhead
protected:
    void browseForAudio();
    HairmonyProcessor& proc;
    std::unique_ptr<juce::FileChooser> chooser;
};

class ImportScreen : public Screen
{
public:
    explicit ImportScreen (HairmonyProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
    DropZone zone;
    juce::TextButton browse { "BROWSE FILES" };
    juce::Label message;
    RecordPanel record;
};

class CraftScreen : public Screen
{
public:
    explicit CraftScreen (HairmonyProcessor&);
    ~CraftScreen() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
    void tick() override;

    juce::TextButton startOver { "START OVER" };
    RowList list { 44 };
    DropZone importZone { "+ IMPORT AUDIO", "drop more vocals or click to browse", true };
    TransportButton play;
    RecordPanel record;
    ArrowSelector choir, mic;
    juce::TextButton unisonBtn { "UNISON" }, octavesBtn { "OCTAVES" }, harmonyBtn { "FULL HARMONY" };
    juce::TextButton octUp { "UP" }, octDown { "DOWN" }, third { "3RD" }, fifth { "5TH" }, below { "BELOW" };
    juce::TextButton stereoBtn { "STEREO" }, monoBtn { "MONO" };
    juce::ComboBox key, scale;
    juce::TextButton create { "CREATE VOCALS" };
    juce::Label message;

    struct Overlay : juce::Component
    {
        Overlay();
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::TextButton cancel { "CANCEL" };
        float progress = 0.0f;
        int frame = 0;
    } overlay;

private:
    juce::Rectangle<int> leftPanel, rightPanel;
    juce::String rowSignature;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt;
    std::unique_ptr<juce::ParameterAttachment> stereoAtt, voicingAtt;
};

class ReviewScreen : public Screen
{
public:
    explicit ReviewScreen (HairmonyProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
    void tick() override;

    juce::TextButton back { "BACK" };
    TransportButton play;
    juce::TextButton follow { "PLAY WITH DAW" };
    juce::Slider gain;
    RowList list { 62 };
    Knob loCut, brightness, reverb;
    DragTile stems { "STEMS", true };
    juce::TextButton save { "SAVE STEMS..." };
    juce::Label message;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followAtt;
    juce::Rectangle<int> listPanel, fxPanel;
    juce::String rowSignature;
};

class SettingsOverlay : public juce::Component
{
public:
    explicit SettingsOverlay (HairmonyProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
    juce::TextButton close { "CLOSE" }, change { "CHANGE..." }, open { "OPEN" }, clear { "CLEAR OLD RENDERS" };
    juce::Label path;
private:
    HairmonyProcessor& proc;
    juce::Rectangle<int> panel;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace hm::ui
