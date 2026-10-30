// HAIRMONY 3000 - editor: synthwave backdrop, chrome header and the three workflow screens.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/MetalLookAndFeel.h"
#include "ui/Screens.h"

class HairmonyEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int baseWidth = 1000, baseHeight = 660;

    explicit HairmonyEditor (HairmonyProcessor&);
    ~HairmonyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    // Exposed for the test harness.
    void update() { timerCallback(); }
    int getShownScreen() const noexcept { return shownScreen; }
    hm::ui::ImportScreen& getImportScreen() { return importScreen; }
    hm::ui::CraftScreen& getCraftScreen() { return craftScreen; }
    hm::ui::ReviewScreen& getReviewScreen() { return reviewScreen; }
    hm::ui::SettingsOverlay& getSettings() { return settings; }
    juce::Button& getSettingsButton() { return settingsButton; }
    juce::Component& getContent() { return content; }

private:
    void timerCallback() override;
    void showScreen (int s);
    void useSoftwareRenderer();

    class GearButton : public juce::Button
    {
    public:
        GearButton() : juce::Button ("settings") {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

    struct Content : juce::Component
    {
        explicit Content (HairmonyEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        HairmonyEditor& owner;
        juce::Image backdrop;
        float backdropScale = 0.0f;
    };

    HairmonyProcessor& proc;
    hm::ui::MetalLookAndFeel laf;
    Content content { *this };
    hm::ui::ImportScreen importScreen;
    hm::ui::CraftScreen craftScreen;
    hm::ui::ReviewScreen reviewScreen;
    hm::ui::SettingsOverlay settings;
    GearButton settingsButton;
    juce::TooltipWindow tooltips { &content, 700 };

    juce::ComponentPeer* softwarePeer = nullptr;
    int shownScreen = -1;
    int lastVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HairmonyEditor)
};
