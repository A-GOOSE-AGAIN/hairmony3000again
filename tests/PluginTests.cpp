// HAIRMONY 3000 - plugin test harness: drives the real processor and editor through the
// whole workflow (import -> craft -> render -> review -> export -> recall) and renders UI snapshots.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>

using namespace juce;

static int failures = 0, checks = 0;
static String section;
#define EXPECT(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FAIL [" << section << "] " << (msg) << "  (" #cond ", line " << __LINE__ << ")\n"; } } while (0)

static constexpr double PI = 3.14159265358979323846;

static File testDir()
{
    static File d = File::getSpecialLocation (File::tempDirectory).getChildFile ("hairmony-plugin-tests");
    return d;
}

static File writeVocal (const String& name, double sr, double seconds, int channels = 1, std::initializer_list<int> notes = { 60, 64, 67, 72 })
{
    const int len = (int) (sr * seconds);
    AudioBuffer<float> buf (channels, len);
    buf.clear();
    const int perNote = len / (int) notes.size();
    int n = 0;
    for (int midi : notes)
    {
        const double f0 = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        for (int i = 0; i < perNote; ++i)
        {
            const double t = (double) i / sr;
            double s = 0.0;
            for (int h = 1; h * f0 < 8000.0; ++h)
            {
                const double hz = h * f0;
                const double env = 1.0 / (1.0 + std::pow ((hz - 700.0) / 150.0, 2.0)) + 0.5 / (1.0 + std::pow ((hz - 1200.0) / 200.0, 2.0)) + 0.03;
                s += env * std::sin (2.0 * PI * hz * t + h);
            }
            const double fade = std::min (1.0, std::min ((double) i, (double) (perNote - i)) / (0.01 * sr));
            for (int c = 0; c < channels; ++c) buf.setSample (c, n * perNote + i, (float) (s * 0.15 * fade));
        }
        ++n;
    }
    auto f = testDir().getChildFile (name);
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os = std::make_unique<FileOutputStream> (f);
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (channels).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (buf, 0, len);
    return f;
}

static float processAndMeasure (HairmonyProcessor& p, int blocks, int blockSize, bool* finite = nullptr)
{
    AudioBuffer<float> b (2, blockSize);
    MidiBuffer m;
    double e = 0.0; long n = 0; bool ok = true;
    for (int i = 0; i < blocks; ++i)
    {
        b.clear();
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < blockSize; ++s) { const float v = b.getSample (c, s); ok &= std::isfinite (v); e += (double) v * v; ++n; }
    }
    if (finite) *finite = ok;
    return (float) std::sqrt (e / (double) std::max (1L, n));
}

static void setParam (HairmonyProcessor& p, const char* id, float value)
{
    auto* param = p.getState().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

static bool waitForRender (HairmonyProcessor& p, double seconds = 120.0)
{
    const auto t0 = Time::getMillisecondCounterHiRes();
    while (p.isRendering() && Time::getMillisecondCounterHiRes() - t0 < seconds * 1000.0) Thread::sleep (20);
    return p.pollRender();
}

static void saveSnapshot (Component& c, const String& name, float scale = 1.0f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    auto f = testDir().getChildFile ("snapshots").getChildFile (name + ".png");
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
    std::cout << "  snapshot: " << f.getFullPathName() << "\n";
}

//==============================================================================
static void testParameters()
{
    section = "parameters";
    HairmonyProcessor p;
    EXPECT (p.getParameters().size() == 16, "16 parameters, got " + String (p.getParameters().size()));
    EXPECT (p.currentSettings().voicing == hm::Voicing::harmony, "Full Harmony is the default voicing");
    EXPECT (p.getName() == "HAIRMONY 3000", "name");
    auto* stereo = p.getState().getParameter (ParamIDs::stereo);
    EXPECT (stereo->getCurrentValueAsText() == "Stereo", "stereo default text");
    EXPECT (stereo->getValueForText ("Mono") < 0.5f, "text -> mono");
    auto* lc = p.getState().getParameter (ParamIDs::loCut);
    EXPECT (lc->getCurrentValueAsText() == "OFF", "lo-cut defaults to OFF, got " + lc->getCurrentValueAsText());
    auto s = p.currentSettings();
    EXPECT (s.choir == hm::ChoirModel::gospel && s.stereo && ! s.thirdAbove, "default settings");
    setParam (p, ParamIDs::choir, 2.0f); setParam (p, ParamIDs::scale, 1.0f); setParam (p, ParamIDs::key, 9.0f); setParam (p, ParamIDs::third, 1.0f);
    s = p.currentSettings();
    EXPECT (s.choir == hm::ChoirModel::gang && s.scale == hm::Scale::naturalMinor && s.key == 9 && s.thirdAbove, "settings follow params");

    AudioProcessor::BusesLayout l;
    l.inputBuses.add (AudioChannelSet::stereo()); l.outputBuses.add (AudioChannelSet::stereo());
    EXPECT (p.isBusesLayoutSupported (l), "stereo");
    l.inputBuses.getReference (0) = AudioChannelSet::mono(); l.outputBuses.getReference (0) = AudioChannelSet::mono();
    EXPECT (p.isBusesLayoutSupported (l), "mono");
    l.outputBuses.getReference (0) = AudioChannelSet::create5point1();
    EXPECT (! p.isBusesLayoutSupported (l), "5.1 rejected");
}

static void testPassthrough()
{
    section = "passthrough";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int bs : { 1, 32, 511, 4096 })
        {
            HairmonyProcessor p;
            p.setPlayConfigDetails (2, 2, sr, bs);
            p.prepareToPlay (sr, bs);
            AudioBuffer<float> b (2, bs); MidiBuffer m;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) b.setSample (c, i, (float) std::sin (i * 0.01 + c));
            AudioBuffer<float> ref (b);
            p.processBlock (b, m);
            float diff = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) diff = std::max (diff, std::abs (b.getSample (c, i) - ref.getSample (c, i)));
            EXPECT (diff <= 0.0f, "input passes through untouched at " + String (sr) + "/" + String (bs));
        }
}

static void testImportAndCraft (HairmonyProcessor& p, File vocal)
{
    section = "import";
    EXPECT (p.getScreen() == HairmonyProcessor::importScreen, "starts on import screen");

    auto txt = testDir().getChildFile ("notes.txt"); txt.replaceWithText ("not audio");
    auto errs = p.importFiles ({ txt, testDir().getChildFile ("missing.wav") });
    EXPECT (errs.size() == 2, "two errors for bad files, got " + String (errs.size()));
    EXPECT (p.getScreen() == HairmonyProcessor::importScreen, "bad files don't advance");
    auto bogus = testDir().getChildFile ("bogus.wav"); bogus.replaceWithText ("RIFF nonsense");
    EXPECT (p.importFiles ({ bogus }).size() == 1, "corrupt wav reported");

    errs = p.importFiles ({ vocal });
    EXPECT (errs.isEmpty(), "valid vocal imports: " + errs.joinIntoString ("; "));
    EXPECT (p.getScreen() == HairmonyProcessor::craftScreen, "moves to craft screen");
    auto infos = p.getTrackInfos();
    EXPECT (infos.size() == 1 && infos[0].name == "lead", "track listed");
    EXPECT (std::abs (infos[0].seconds - 2.4) < 0.01, "duration kept through resampling: " + String (infos[0].seconds));

    section = "craft playback";
    p.play();
    EXPECT (p.isPlaying(), "playing");
    const float lvl = processAndMeasure (p, 40, 512);
    EXPECT (lvl > 0.005f, "imported vocal audible, rms " + String (lvl));
    EXPECT (p.getPlayheadSeconds() > 0.3, "playhead moves");
    p.setTrackMuted (0, true);
    EXPECT (processAndMeasure (p, 10, 512) < 1e-6f, "muted track silent");
    p.setTrackMuted (0, false);
    p.stop();
    EXPECT (processAndMeasure (p, 4, 512) < 1e-6f, "stopped is silent");
    p.play();
    processAndMeasure (p, (int) (2.6 * 48000 / 512), 512);
    EXPECT (! p.isPlaying(), "playback stops at the end");

    section = "render guards";
    p.setTrackMuted (0, true);
    EXPECT (! p.startRender(), "can't render with every vocal muted");
    EXPECT (p.getLastMessage().containsIgnoreCase ("muted"), "explains why");
    p.setTrackMuted (0, false);
}

static void testRenderReviewExport (HairmonyProcessor& p)
{
    section = "render";
    setParam (p, ParamIDs::choir, 2.0f);   // gang
    setParam (p, ParamIDs::third, 1.0f);
    setParam (p, ParamIDs::fifth, 1.0f);
    setParam (p, ParamIDs::below, 1.0f);
    setParam (p, ParamIDs::octUp, 1.0f);
    setParam (p, ParamIDs::key, 0.0f);
    setParam (p, ParamIDs::scale, 0.0f);
    EXPECT (p.startRender(), "render starts");
    EXPECT (p.isRendering(), "is rendering");
    EXPECT (! p.startRender(), "second render refused while busy");
    EXPECT (! p.importFiles ({ testDir().getChildFile ("lead.wav") }).isEmpty(), "import refused while busy");
    float last = -1; bool monotonic = true;
    while (p.isRendering()) { const float pr = p.getRenderProgress(); monotonic &= pr + 1e-6f >= last; last = pr; Thread::sleep (10); }
    EXPECT (monotonic, "progress monotonic");
    EXPECT (p.pollRender(), "finalised");
    EXPECT (p.getScreen() == HairmonyProcessor::reviewScreen, "review screen");
    auto outs = p.getOutputInfos();
    StringArray names; for (auto& o : outs) names.add (o.name);
    EXPECT (names.joinIntoString ("|") == "Choir|Octave Up|3rd Above|3rd Above (Low)|5th Above|5th Above (Low)", "outputs: " + names.joinIntoString ("|"));
    auto peaks = p.getOutputPeaks (0, 100);
    EXPECT (peaks.size() == 100 && *std::max_element (peaks.begin(), peaks.end()) > 0.01f, "waveform peaks");
    EXPECT (p.getOutputPeaks (99, 10).empty(), "bad index -> no peaks");

    section = "review playback";
    p.play();
    bool finite = true;
    const float all = processAndMeasure (p, 30, 512, &finite);
    EXPECT (finite && all > 0.005f, "outputs audible, rms " + String (all));
    p.stop();
    p.play (2);
    EXPECT (p.getSoloIndex() == 2, "solo");
    EXPECT (processAndMeasure (p, 20, 512) > 0.003f, "solo output audible");
    p.stop();
    for (int i = 0; i < (int) outs.size(); ++i) p.setOutputMuted (i, true);
    p.play();
    EXPECT (processAndMeasure (p, 10, 512) < 1e-5f, "all muted -> silent (reverb off)");
    for (int i = 0; i < (int) outs.size(); ++i) p.setOutputMuted (i, false);
    p.stop();

    setParam (p, ParamIDs::reverb, 0.8f);
    setParam (p, ParamIDs::loCut, 300.0f);
    setParam (p, ParamIDs::brightness, 0.7f);
    setParam (p, ParamIDs::gain, -12.0f);
    p.play();
    processAndMeasure (p, 20, 512);
    const float quieter = processAndMeasure (p, 20, 512, &finite);
    EXPECT (finite && quieter > 0.0005f, "FX + gain path finite and audible");
    p.stop();
    setParam (p, ParamIDs::gain, 0.0f);

    // Random automation while playing (what pluginval does)
    Random r (4);
    p.play();
    bool ok = true;
    for (int i = 0; i < 400; ++i)
    {
        for (auto* prm : p.getParameters()) if (r.nextFloat() < 0.2f) prm->setValueNotifyingHost (r.nextFloat());
        processAndMeasure (p, 1, 1 + r.nextInt (1024), &finite);
        ok &= finite;
        if (! p.isPlaying()) p.play (r.nextInt (8) - 1);
    }
    EXPECT (ok, "random automation during playback stays finite");
    p.stop();
    setParam (p, ParamIDs::reverb, 0.3f); setParam (p, ParamIDs::loCut, 20.0f); setParam (p, ParamIDs::brightness, 0.2f); setParam (p, ParamIDs::gain, 0.0f);

    section = "export";
    auto f = p.createExportFile (0);
    EXPECT (f.existsAsFile(), "export file written");
    std::unique_ptr<AudioFormatReader> rd (hm::formatManager().createReaderFor (f));
    EXPECT (rd != nullptr && rd->numChannels == 2 && rd->bitsPerSample == 24, "24-bit stereo wav");
    if (rd) EXPECT (rd->lengthInSamples >= (int64) (2.4 * 48000) && rd->lengthInSamples < (int64) (2.4 * 48000 + 3.6 * 48000), "length includes reverb tail");
    EXPECT (! p.createExportFile (42).existsAsFile(), "bad index -> no file");
    auto all3 = p.createAllExportFiles();
    EXPECT (all3.size() == (int) outs.size(), "drag-all creates every stem");
    auto dest = testDir().getChildFile ("exported");
    dest.deleteRecursively();
    StringArray written;
    EXPECT (p.exportAllTo (dest, written) && written.size() == (int) outs.size(), "save stems to folder");
    EXPECT (dest.getChildFile ("HAIRMONY 3rd Above _Low_.wav").existsAsFile(), "file naming: " + written.joinIntoString (", "));
}

static void testStateRecall (HairmonyProcessor& p)
{
    section = "state";
    p.setOutputMuted (1, true);
    MemoryBlock mb;
    p.getStateInformation (mb);
    EXPECT (mb.getSize() > 100, "state saved");

    HairmonyProcessor q;
    q.prepareToPlay (48000.0, 512);
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    EXPECT (q.getScreen() == HairmonyProcessor::reviewScreen, "recalls review screen");
    EXPECT (q.getOutputInfos().size() == p.getOutputInfos().size(), "outputs recalled");
    EXPECT (q.getOutputInfos()[1].muted && ! q.getOutputInfos()[0].muted, "mute states recalled");
    EXPECT (std::abs (q.getOutputInfos()[0].seconds - p.getOutputInfos()[0].seconds) < 0.001, "lengths match");
    EXPECT (q.getTrackInfos().size() == 1, "imported vocal recalled");
    {
        const auto a = p.currentSettings(), b = q.currentSettings();
        EXPECT (a.choir == b.choir && a.mic == b.mic && a.thirdAbove == b.thirdAbove && a.fifthAbove == b.fifthAbove
                && a.key == b.key && a.scale == b.scale && a.stereo == b.stereo && a.octaveUp == b.octaveUp, "params recalled");
        // Compare what each parameter *means* (an on/off switch fuzzed to 0.37 is still "off").
        for (int i = 0; i < p.getParameters().size(); ++i)
            EXPECT (p.getParameters()[i]->getCurrentValueAsText() == q.getParameters()[i]->getCurrentValueAsText(),
                    "param " + p.getParameters()[i]->getName (32) + " recalled: " + p.getParameters()[i]->getCurrentValueAsText()
                        + " vs " + q.getParameters()[i]->getCurrentValueAsText());
    }
    q.play();
    EXPECT (processAndMeasure (q, 20, 512) > 0.003f, "recalled outputs play");

    // Garbage and foreign state must not crash or change anything.
    HairmonyProcessor z;
    const char junk[] = "definitely not a plugin state";
    z.setStateInformation (junk, (int) sizeof (junk));
    EXPECT (z.getScreen() == HairmonyProcessor::importScreen, "junk state ignored");
    MemoryBlock empty; z.setStateInformation (empty.getData(), 0);
    ValueTree foreign ("SomethingElse"); MemoryBlock fb; if (auto x = foreign.createXml()) AudioProcessor::copyXmlToBinary (*x, fb);
    z.setStateInformation (fb.getData(), (int) fb.getSize());
    EXPECT (z.getScreen() == HairmonyProcessor::importScreen, "foreign state ignored");

    // Missing source file: session degrades gracefully.
    auto gone = writeVocal ("gone.wav", 48000.0, 1.0);
    HairmonyProcessor a; a.prepareToPlay (48000.0, 256);
    a.importFiles ({ gone });
    MemoryBlock ab; a.getStateInformation (ab);
    gone.deleteFile();
    HairmonyProcessor b; b.setStateInformation (ab.getData(), (int) ab.getSize());
    EXPECT (b.getScreen() == HairmonyProcessor::importScreen && b.getLastMessage().contains ("gone.wav"), "missing vocal reported");
}

static void testNavigation (HairmonyProcessor& p)
{
    section = "navigation";
    const auto raw = p.sessionFolderForTests();
    p.backToCraft();
    EXPECT (p.getScreen() == HairmonyProcessor::craftScreen && p.getOutputInfos().empty(), "back clears outputs");
    EXPECT (p.getTrackInfos().size() == 1, "back keeps vocals");

    EXPECT (p.startRender(), "render again");
    Thread::sleep (30);
    p.cancelRender();
    EXPECT (! p.isRendering() && p.getScreen() == HairmonyProcessor::craftScreen, "cancel returns to craft");
    EXPECT (p.getLastMessage().containsIgnoreCase ("cancel"), "cancel message");

    p.startOver();
    EXPECT (p.getScreen() == HairmonyProcessor::importScreen && p.getTrackInfos().empty(), "start over clears everything");
    EXPECT (! raw.exists(), "session temp folder removed");
}

static void testMultiFileAndRates()
{
    section = "multi-file";
    HairmonyProcessor p;
    p.prepareToPlay (44100.0, 512);
    auto a = writeVocal ("alto.wav", 96000.0, 1.2, 2, { 57, 60 });
    auto b = writeVocal ("tenor.wav", 22050.0, 1.0, 1, { 48, 52 });
    EXPECT (p.importFiles ({ a, b }).isEmpty(), "two files at odd rates");
    auto t = p.getTrackInfos();
    EXPECT (t.size() == 2 && std::abs (t[0].seconds - 1.2) < 0.01 && std::abs (t[1].seconds - 1.0) < 0.01, "durations kept");
    setParam (p, ParamIDs::stereo, 0.0f);
    EXPECT (p.startRender(), "render");
    EXPECT (waitForRender (p), "done");
    auto outs = p.getOutputInfos();
    EXPECT (outs.size() == 2 && outs[0].name == "alto - Choir" && outs[1].name == "tenor - Choir", "per-file outputs named");
    auto f = p.createExportFile (1);
    std::unique_ptr<AudioFormatReader> rd (hm::formatManager().createReaderFor (f));
    EXPECT (rd != nullptr && rd->numChannels == 1 && std::abs (rd->sampleRate - 44100.0) < 1.0, "mono export at session rate");

    // Destroying the processor mid-render must not hang or crash.
    p.backToCraft();
    p.startRender();
    Thread::sleep (20);
}

//==============================================================================
static void testEditor()
{
    section = "editor";
    HairmonyProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<AudioProcessorEditor> ed (p.createEditorIfNeeded());
    auto* e = dynamic_cast<HairmonyEditor*> (ed.get());
    EXPECT (e != nullptr, "editor created");
    if (e == nullptr) return;
    EXPECT (e->getWidth() == 1000 && e->getHeight() == 660, "default size");
    e->update();
    EXPECT (e->getShownScreen() == 0 && e->getImportScreen().isVisible(), "import screen shown");
    saveSnapshot (*e, "1-import");

    // Settings overlay
    e->getSettingsButton().triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    EXPECT (e->getSettings().isVisible(), "settings opens");
    EXPECT (e->getSettings().path.getText() == p.getTempFolder().getFullPathName(), "shows temp folder");
    saveSnapshot (*e, "4-settings");
    e->getSettings().close.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    EXPECT (! e->getSettings().isVisible(), "settings closes");

    // Drop a file onto the import zone
    auto& zone = e->getImportScreen().zone;
    EXPECT (zone.isInterestedInFileDrag ({ testDir().getChildFile ("lead.wav").getFullPathName() }), "zone accepts wav");
    EXPECT (! zone.isInterestedInFileDrag ({ "C:/x/readme.txt" }), "zone rejects txt");
    zone.filesDropped ({ writeVocal ("lead.wav", 48000.0, 2.4).getFullPathName() }, 10, 10);
    e->update();
    EXPECT (e->getShownScreen() == 1 && e->getCraftScreen().isVisible(), "drop -> craft screen");
    auto& craft = e->getCraftScreen();

    // Controls drive parameters
    craft.choir.next.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (p.currentSettings().choir == hm::ChoirModel::kids, "choir > arrow");
    craft.choir.prev.triggerClick(); craft.choir.prev.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (p.currentSettings().choir == hm::ChoirModel::cyborg, "choir < arrow wraps");
    craft.mic.next.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (p.currentSettings().mic == hm::MicModel::ribbon, "mic arrow");
    EXPECT (craft.choir.name.getButtonText() == "CYBORG CHOIR", "selector label: " + craft.choir.name.getButtonText());
    craft.choir.next.triggerClick();
    for (auto* b : { &craft.octUp, &craft.third, &craft.below }) b->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    auto s = p.currentSettings();
    EXPECT (s.octaveUp && s.thirdAbove && s.below && ! s.fifthAbove && ! s.octaveDown, "toggles");
    craft.monoBtn.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (! p.currentSettings().stereo && craft.monoBtn.getToggleState() && ! craft.stereoBtn.getToggleState(), "mono");
    craft.stereoBtn.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (p.currentSettings().stereo, "stereo");
    craft.key.setSelectedItemIndex (7); craft.scale.setSelectedItemIndex (1);
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (p.currentSettings().key == 7 && p.currentSettings().scale == hm::Scale::naturalMinor, "key/scale combos");
    craft.key.setSelectedItemIndex (0); craft.scale.setSelectedItemIndex (0);
    setParam (p, ParamIDs::third, 1.0f);
    e->update();
    EXPECT (craft.third.getToggleState(), "UI follows host automation");
    if (auto* row = dynamic_cast<hm::ui::TrackRow*> (craft.list.getRow (0)))
    {
        row->mute.triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        EXPECT (p.getTrackInfos()[0].muted, "row mute");
        row->mute.triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (30);
    }
    else EXPECT (false, "track row present");
    craft.play.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (p.isPlaying(), "craft play");
    e->update();
    craft.play.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (! p.isPlaying(), "craft stop");
    saveSnapshot (*e, "2-craft");

    craft.create.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (p.isRendering(), "create starts render");
    Thread::sleep (120);
    e->update();
    EXPECT (craft.overlay.isVisible() && ! craft.create.isEnabled(), "progress overlay shown");
    saveSnapshot (*e, "2b-rendering");
    while (p.isRendering()) Thread::sleep (20);
    e->update();
    EXPECT (e->getShownScreen() == 2 && e->getReviewScreen().isVisible(), "render -> review");
    auto& review = e->getReviewScreen();
    EXPECT (review.list.getNumRows() == 4, "4 output rows (choir, octave up, 3rd, 3rd low), got " + String (review.list.getNumRows()));

    if (auto* row = dynamic_cast<hm::ui::OutputRow*> (review.list.getRow (1)))
    {
        row->play.triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        EXPECT (p.isPlaying() && p.getSoloIndex() == 1, "row play solos");
        processAndMeasure (p, 30, 512);
        e->update();
        EXPECT (row->play.showsStop(), "row shows stop while playing");
        saveSnapshot (*e, "3-review");
        row->play.triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        EXPECT (! p.isPlaying(), "row play again stops");
        auto files = row->drag.prepareFiles();
        EXPECT (files.size() == 1 && File (files[0]).existsAsFile(), "row drag tile prepares a wav");
    }
    else EXPECT (false, "output row present");

    auto all = review.stems.prepareFiles();
    EXPECT (all.size() == review.list.getNumRows(), "STEMS tile prepares all files");
    review.play.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT (p.isPlaying() && p.getSoloIndex() == -1, "play all");
    review.play.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    review.reverb.slider.setValue (0.5, sendNotificationSync);
    EXPECT (std::abs (p.getState().getRawParameterValue (ParamIDs::reverb)->load() - 0.5f) < 0.01f, "reverb knob");
    review.loCut.slider.setValue (150.0, sendNotificationSync);
    EXPECT (std::abs (p.getState().getRawParameterValue (ParamIDs::loCut)->load() - 150.0f) < 1.0f, "lo-cut knob");
    review.gain.setValue (-6.0, sendNotificationSync);
    EXPECT (std::abs (p.getState().getRawParameterValue (ParamIDs::gain)->load() + 6.0f) < 0.1f, "gain slider");

    // Resizing keeps the aspect ratio and scales the content.
    e->setSize (1500, 990);
    EXPECT (std::abs (e->getContent().getTransform().mat00 - 1.5f) < 0.01f, "scales up");
    saveSnapshot (*e, "3b-review-large", 1.0f);
    e->setSize (700, 462);
    e->update();
    e->setSize (1000, 660);

    review.back.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    e->update();
    EXPECT (e->getShownScreen() == 1, "back -> craft");
    craft.startOver.triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    e->update();
    EXPECT (e->getShownScreen() == 0, "start over -> import");

    // Editor can be closed and reopened mid-render; the render still lands.
    p.importFiles ({ testDir().getChildFile ("lead.wav") });
    p.startRender();
    ed.reset();
    while (p.isRendering()) Thread::sleep (20);
    std::unique_ptr<AudioProcessorEditor> ed2 (p.createEditorIfNeeded());
    if (auto* e2 = dynamic_cast<HairmonyEditor*> (ed2.get())) { e2->update(); EXPECT (e2->getShownScreen() == 2, "reopened editor picks up finished render"); }
    p.editorBeingDeleted (ed2.get());
}


//==============================================================================
// Mirrors pluginval's "Editor Automation": editor open, a background thread hammers every
// parameter with random values while the message thread keeps running.
static void testEditorAutomationStress()
{
    section = "editor automation stress";
    for (int scr = 0; scr < 3; ++scr)
    {
        HairmonyProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        if (scr >= 1) p.importFiles ({ writeVocal ("stress.wav", 48000.0, 1.0) });
        if (scr == 2) { p.startRender(); waitForRender (p); }
        EXPECT (p.getScreen() == scr, "on screen " + String (scr));

        std::unique_ptr<AudioProcessorEditor> ed (p.createEditorIfNeeded());
        if (auto* e = dynamic_cast<HairmonyEditor*> (ed.get())) e->update();

        std::atomic<bool> done { false };
        std::atomic<int> iterations { 0 };
        std::thread worker ([&]
        {
            Random r (scr + 7);
            for (int i = 0; i < 400; ++i)
            {
                for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (r.nextFloat());
                if (i % 3 == 0)
                {
                    AudioBuffer<float> b (2, 256); MidiBuffer m; b.clear();
                    if (i % 30 == 0) p.play (r.nextInt (4) - 1);
                    p.processBlock (b, m);
                }
                ++iterations;
                Thread::sleep (1);
            }
            done = true;
        });
        while (! done) MessageManager::getInstance()->runDispatchLoopUntil (15);
        worker.join();
        MessageManager::getInstance()->runDispatchLoopUntil (100); // let pending async updates land
        EXPECT (iterations == 400, "worker finished");
        ed.reset();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
}

//==============================================================================
// Mirrors pluginval's "Background thread state" and "Parameter thread safety": state save/load
// on a background thread with the editor open, and parameters set from several threads while
// audio is processed.
static void testBackgroundThreadState()
{
    section = "background thread state";
    HairmonyProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    p.importFiles ({ writeVocal ("bg.wav", 48000.0, 1.0) });
    p.startRender(); waitForRender (p);
    MemoryBlock withOutputs; p.getStateInformation (withOutputs);

    std::unique_ptr<AudioProcessorEditor> ed (p.createEditorIfNeeded());
    std::atomic<bool> done { false };
    std::thread worker ([&]
    {
        Random r (3);
        for (int i = 0; i < 40; ++i)
        {
            MemoryBlock mb;
            p.getStateInformation (mb);
            if (i % 2 == 0) p.setStateInformation (withOutputs.getData(), (int) withOutputs.getSize());
            else p.setStateInformation (mb.getData(), (int) mb.getSize());
            for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (r.nextFloat());
            Thread::sleep (2);
        }
        done = true;
    });
    while (! done) MessageManager::getInstance()->runDispatchLoopUntil (10);
    worker.join();
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    EXPECT (p.getScreen() == HairmonyProcessor::reviewScreen && p.getOutputInfos().size() == 1, "state survives background save/load");
    ed.reset();

    section = "parameter thread safety";
    std::atomic<bool> stopAudio { false };
    std::thread audio ([&]
    {
        AudioBuffer<float> b (2, 256); MidiBuffer m;
        p.play();
        while (! stopAudio) { b.clear(); p.processBlock (b, m); if (! p.isPlaying()) p.play(); }
    });
    std::vector<std::thread> setters;
    for (int t2 = 0; t2 < 3; ++t2)
        setters.emplace_back ([&p, t2]
        {
            Random r (t2);
            for (int i = 0; i < 2000; ++i)
                for (auto* prm : p.getParameters()) prm->setValue (r.nextFloat());
        });
    for (auto& s : setters) s.join();
    stopAudio = true;
    audio.join();
    p.stop();
    EXPECT (true, "no crash");
}

//==============================================================================
// A controllable DAW transport for the record-from-track tests.
struct FakePlayHead : AudioPlayHead
{
    bool playingNow = false;
    int64 pos = 0;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playingNow);
        info.setTimeInSamples (pos);
        return info;
    }
};

static std::vector<float> readMono (const File& f)
{
    std::unique_ptr<AudioFormatReader> r (hm::formatManager().createReaderFor (f));
    std::vector<float> out;
    if (! r) return out;
    AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    out.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
    return out;
}

// Feeds `vocal` as the track's input while the fake DAW plays from `startPos`.
static void playThrough (HairmonyProcessor& p, FakePlayHead& ph, const std::vector<float>& vocal, int64 startPos, int block = 512,
                         int silentBlocksBefore = 4)
{
    AudioBuffer<float> b (2, block); MidiBuffer m;
    ph.pos = startPos;
    for (int i = 0; i < silentBlocksBefore; ++i) { b.clear(); ph.playingNow = true; p.processBlock (b, m); ph.pos += block; }
    for (size_t s = 0; s < vocal.size(); s += (size_t) block)
    {
        b.clear();
        for (int i = 0; i < block && s + (size_t) i < vocal.size(); ++i) { b.setSample (0, i, vocal[s + (size_t) i]); b.setSample (1, i, vocal[s + (size_t) i]); }
        p.processBlock (b, m);
        ph.pos += block;
    }
    for (int i = 0; i < 4; ++i) { b.clear(); p.processBlock (b, m); ph.pos += block; }
}

static void testRecordFromTrack()
{
    section = "record from track";
    const double sr = 48000.0;
    std::vector<float> vocal = readMono (writeVocal ("cap.wav", sr, 2.0));

    HairmonyProcessor p;
    p.setPlayConfigDetails (2, 2, sr, 512);
    p.prepareToPlay (sr, 512);
    FakePlayHead ph;
    p.setPlayHead (&ph);

    // Armed but DAW stopped: nothing happens.
    EXPECT (p.armCapture(), "arms");
    EXPECT (p.getCaptureState() == HairmonyProcessor::captureArmed, "armed");
    EXPECT (! p.armCapture(), "can't arm twice");
    { AudioBuffer<float> b (2, 512); MidiBuffer m; b.clear(); ph.playingNow = false; for (int i = 0; i < 10; ++i) p.processBlock (b, m); }
    EXPECT (p.getCaptureState() == HairmonyProcessor::captureArmed && p.getCaptureSeconds() == 0.0, "waits for the DAW");

    // DAW plays from 2 s: records, passes audio through untouched.
    AudioBuffer<float> probe (2, 512); MidiBuffer mm;
    ph.playingNow = true; ph.pos = 96000;
    for (int i = 0; i < 512; ++i) { probe.setSample (0, i, 0.25f); probe.setSample (1, i, 0.25f); }
    p.processBlock (probe, mm);
    EXPECT (p.getCaptureState() == HairmonyProcessor::captureRecording, "recording once the DAW plays");
    EXPECT (std::abs (probe.getSample (0, 100) - 0.25f) < 1e-6f, "input still passes through while recording");
    p.stopCapture(); // discard this probe take
    p.startOver();

    p.armCapture();
    playThrough (p, ph, vocal, 96000);
    ph.playingNow = false;
    { AudioBuffer<float> b (2, 512); MidiBuffer m; b.clear(); p.processBlock (b, m); }
    EXPECT (p.getCaptureState() == HairmonyProcessor::captureIdle, "DAW stop ends the take");
    EXPECT (p.pollCapture(), "take imported");
    EXPECT (p.getScreen() == HairmonyProcessor::craftScreen, "moves to craft screen");
    auto t = p.getTrackInfos();
    EXPECT (t.size() == 1 && t[0].name == "Take 1", "named Take 1");
    EXPECT (t.size() == 1 && std::abs (t[0].seconds - 2.0) < 0.1, "take length ~2 s, got " + String (t.empty() ? 0.0 : t[0].seconds));
    // silence trimmed but the timeline position stays exact: vocal began at 96000 + 4 blocks
    const int64 expectedStart = 96000 + 4 * 512;
    EXPECT (t.size() == 1 && std::llabs (t[0].hostStart - expectedStart) <= (int64) (0.011 * sr) + 1, "timeline position " + String (t.empty() ? -1 : t[0].hostStart));
    EXPECT (p.getLastMessage().contains ("Take 1"), "message names the take");

    // Render, then the choir plays along with the DAW at the recorded spot.
    setParam (p, ParamIDs::voicing, 0.0f);
    EXPECT (p.startRender() && waitForRender (p), "render the take");
    EXPECT (p.getOutputInfos().size() == 1 && p.getOutputInfos()[0].hostStart == t[0].hostStart, "output keeps the timeline position");
    auto dawLevel = [&] (int64 at)
    {
        AudioBuffer<float> b (2, 512); MidiBuffer m;
        ph.playingNow = true; ph.pos = at;
        double e = 0;
        for (int i = 0; i < 20; ++i) { b.clear(); p.processBlock (b, m); ph.pos += 512; for (int s = 0; s < 512; ++s) e += b.getSample (0, s) * b.getSample (0, s); }
        return std::sqrt (e / (20 * 512));
    };
    EXPECT (dawLevel (expectedStart + 24000) > 0.005, "choir plays in sync while the DAW plays over the take");
    EXPECT (p.isSyncedPlaying(), "reports synced playback");
    EXPECT (dawLevel (0) < 1e-6, "silent before the take on the timeline");
    EXPECT (dawLevel (expectedStart + (int64) (sr * 30)) < 1e-6, "silent after it");
    setParam (p, ParamIDs::follow, 0.0f);
    EXPECT (dawLevel (expectedStart + 24000) < 1e-6, "PLAY WITH DAW off -> no sync");
    setParam (p, ParamIDs::follow, 1.0f);
    // alignment: the choir's loudness envelope should line up with the input's on the timeline
    {
        const int64 at = expectedStart - 4800;
        std::vector<float> outEnv, inEnv;
        AudioBuffer<float> b (2, 480); MidiBuffer m;
        ph.playingNow = true; ph.pos = at;
        for (int i = 0; i < 200; ++i)
        {
            b.clear(); p.processBlock (b, m); ph.pos += 480;
            double e = 0; for (int s = 0; s < 480; ++s) e += b.getSample (0, s) * b.getSample (0, s);
            outEnv.push_back ((float) std::sqrt (e / 480));
            const int64 idx = at + i * 480 - 96000 - 4 * 512;
            double ei = 0; for (int s = 0; s < 480; ++s) { const int64 k = idx + s; if (k >= 0 && k < (int64) vocal.size()) ei += vocal[(size_t) k] * vocal[(size_t) k]; }
            inEnv.push_back ((float) std::sqrt (ei / 480));
        }
        int bestLag = 0; double best = -1;
        for (int lag = -8; lag <= 8; ++lag)
        {
            double s = 0; for (int i = 10; i < 190; ++i) s += inEnv[(size_t) i] * outEnv[(size_t) (i + lag)];
            if (s > best) { best = s; bestLag = lag; }
        }
        EXPECT (std::abs (bestLag) <= 1, "choir lines up with the vocal on the timeline (lag " + String (bestLag * 10) + " ms)");
    }
    ph.playingNow = false;

    // State recall keeps the take and its timeline position.
    MemoryBlock mb; p.getStateInformation (mb);
    HairmonyProcessor q; q.prepareToPlay (sr, 512); q.setStateInformation (mb.getData(), (int) mb.getSize());
    EXPECT (q.getTrackInfos().size() == 1 && q.getTrackInfos()[0].hostStart == t[0].hostStart, "take + position recalled");
    EXPECT (q.getOutputInfos().size() == 1 && q.getOutputInfos()[0].hostStart == t[0].hostStart, "synced output recalled");

    // A second take is "Take 2" and lands alongside the first.
    p.backToCraft();
    p.armCapture();
    playThrough (p, ph, vocal, 480000);
    ph.playingNow = false; { AudioBuffer<float> b (2, 512); MidiBuffer m; b.clear(); p.processBlock (b, m); }
    p.pollCapture();
    EXPECT (p.getTrackInfos().size() == 2 && p.getTrackInfos()[1].name == "Take 2", "second take");

    // Loop / jump in the DAW ends the take at the jump.
    p.armCapture();
    {
        AudioBuffer<float> b (2, 512); MidiBuffer m;
        ph.playingNow = true; ph.pos = 1000000;
        for (int i = 0; i < 100; ++i) { b.clear(); for (int s = 0; s < 512; ++s) b.setSample (0, s, 0.2f * std::sin (s * 0.05f)); p.processBlock (b, m); ph.pos += 512; }
        ph.pos = 1000000; // loop back
        b.clear(); p.processBlock (b, m);
    }
    EXPECT (p.getCaptureState() == HairmonyProcessor::captureIdle, "loop jump ends the take");
    p.pollCapture();
    EXPECT (p.getTrackInfos().size() == 3 && std::abs (p.getTrackInfos()[2].seconds - 100 * 512 / sr) < 0.08, "take holds the pre-loop audio");

    // Silence only -> nothing imported, with an explanation.
    p.armCapture();
    { AudioBuffer<float> b (2, 512); MidiBuffer m; ph.playingNow = true; for (int i = 0; i < 50; ++i) { b.clear(); p.processBlock (b, m); ph.pos += 512; } ph.playingNow = false; b.clear(); p.processBlock (b, m); }
    p.pollCapture();
    EXPECT (p.getTrackInfos().size() == 3 && p.getLastMessage().containsIgnoreCase ("nothing was recorded"), "silent take ignored");

    // Disarm before anything is recorded.
    p.armCapture(); p.stopCapture();
    EXPECT (p.getCaptureState() == HairmonyProcessor::captureIdle && p.getTrackInfos().size() == 3, "disarm");

    // Hosts without a transport (standalone): records immediately, STOP imports.
    HairmonyProcessor s2;
    s2.prepareToPlay (sr, 512);
    s2.armCapture();
    { AudioBuffer<float> b (2, 512); MidiBuffer m; for (size_t s = 0; s + 512 <= vocal.size(); s += 512) { for (int i = 0; i < 512; ++i) { b.setSample (0, i, vocal[s + (size_t) i]); b.setSample (1, i, vocal[s + (size_t) i]); } s2.processBlock (b, m); } }
    EXPECT (s2.getCaptureState() == HairmonyProcessor::captureRecording, "records without a DAW transport");
    s2.stopCapture();
    EXPECT (s2.getTrackInfos().size() == 1 && s2.getTrackInfos()[0].hostStart == -1, "standalone take (no timeline position)");

    // Recording while the editor is open, from a background audio thread (like a real host).
    HairmonyProcessor e3;
    e3.setPlayConfigDetails (2, 2, sr, 256);
    e3.prepareToPlay (sr, 256);
    FakePlayHead ph3; e3.setPlayHead (&ph3);
    std::unique_ptr<AudioProcessorEditor> ed (e3.createEditorIfNeeded());
    auto* he = dynamic_cast<HairmonyEditor*> (ed.get());
    he->update();
    he->getImportScreen().record.click();
    EXPECT (e3.getCaptureState() == HairmonyProcessor::captureArmed, "REC button arms");
    saveSnapshot (*he, "1b-armed");
    std::atomic<bool> done { false };
    std::thread audio ([&]
    {
        AudioBuffer<float> b (2, 256); MidiBuffer m;
        ph3.playingNow = true; ph3.pos = 48000;
        for (size_t s = 0; s + 256 <= vocal.size(); s += 256)
        {
            for (int i = 0; i < 256; ++i) { b.setSample (0, i, vocal[s + (size_t) i]); b.setSample (1, i, vocal[s + (size_t) i]); }
            e3.processBlock (b, m); ph3.pos += 256;
            Thread::sleep (1);
        }
        ph3.playingNow = false;
        b.clear(); e3.processBlock (b, m);
        done = true;
    });
    bool sawRecording = false;
    while (! done) { MessageManager::getInstance()->runDispatchLoopUntil (20); he->update(); sawRecording |= e3.getCaptureState() == HairmonyProcessor::captureRecording; if (sawRecording && e3.getCaptureSeconds() > 1.0 && e3.getCaptureSeconds() < 1.05) saveSnapshot (*he, "1c-recording"); }
    audio.join();
    MessageManager::getInstance()->runDispatchLoopUntil (200);
    he->update();
    EXPECT (sawRecording, "UI saw the recording");
    EXPECT (e3.getTrackInfos().size() == 1 && he->getShownScreen() == 1, "take lands on the craft screen (via the processor timer)");
    he->update();
    saveSnapshot (*he, "2c-craft-with-take");
    ed.reset();
}

int main (int argc, char** argv)
{
    std::cout << std::unitbuf;
    ScopedJuceInitialiser_GUI gui;
    testDir().createDirectory();
    const bool quick = argc > 1 && String (argv[1]) == "--quick";
    std::cout << "HAIRMONY 3000 plugin tests\n";

    testParameters();
    testPassthrough();
    {
        HairmonyProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        const auto vocal = writeVocal ("lead.wav", 44100.0, 2.4);
        testImportAndCraft (p, vocal);
        testRenderReviewExport (p);
        testStateRecall (p);
        testNavigation (p);
    }
    if (! quick) testMultiFileAndRates();
    testEditor();
    testEditorAutomationStress();
    testBackgroundThreadState();
    testRecordFromTrack();

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
