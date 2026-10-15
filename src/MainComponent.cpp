#include "MainComponent.h"
#include "AudioClipEditor.h"
#include "NativeEffects.h"
#include "Session.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_set>
#if HALFTRACK_TESTS_ENABLED
 #include <cstdlib>
 #include <iostream>
 #include <stdexcept>
#endif

namespace halftrack
{
#if HALFTRACK_TESTS_ENABLED
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
void withGeneratedMidiController (Session&, const std::function<void(const juce::String&)>&);
#endif
namespace
{
const juce::Colour background { 0xff202326 }, panel { 0xff2b2e31 }, raised { 0xff3b4044 },
                   line { 0xff171a1c }, ink { 0xffe2e5e6 }, muted { 0xff949da4 },
                   orange { 0xffffa65a }, red { 0xffe6656a }, green { 0xff9fd49a };

juce::Font font (float size = 12.0f, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

class Theme final : public juce::LookAndFeel_V4
{
public:
    Theme()
    {
        setColour (juce::ResizableWindow::backgroundColourId, panel);
        setColour (juce::Label::textColourId, ink);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::buttonOnColourId, orange);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, background);
        setColour (juce::ComboBox::backgroundColourId, background);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::arrowColourId, muted);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, raised);
        setColour (juce::PopupMenu::highlightedTextColourId, orange);
        setColour (juce::TextEditor::backgroundColourId, background);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, orange);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ListBox::outlineColourId, line);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff667078));
        setColour (juce::Slider::thumbColourId, ink);
        setColour (juce::Slider::trackColourId, orange);
        setColour (juce::Slider::backgroundColourId, background);
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, background);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, ink);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return font (juce::jlimit (10.0f, 12.0f, height * 0.52f));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (11.0f); }
    juce::Font getPopupMenuFont() override { return font (12.0f); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& colour, bool over, bool down) override
    {
        auto c = colour;
        if (over) c = c.brighter (0.08f);
        if (down) c = c.darker (0.1f);
        if (! button.isEnabled()) c = c.withMultipliedAlpha (0.45f);
        g.setColour (c);
        g.fillRect (button.getLocalBounds().reduced (1));
        g.setColour (line);
        g.drawRect (button.getLocalBounds().reduced (1));
    }
    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRect (0, 0, width, height);
        g.setColour (line);
        g.drawRect (0, 0, width, height);
        juce::Path p;
        auto x = (float) width - 12.0f;
        auto y = (float) height * 0.5f;
        p.startNewSubPath (x - 3.0f, y - 1.5f);
        p.lineTo (x, y + 1.5f);
        p.lineTo (x + 3.0f, y - 1.5f);
        g.setColour (muted);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
};

class TransportButton final : public juce::Button
{
public:
    enum Kind { play, stop, record };
    explicit TransportButton (Kind k) : juce::Button (k == play ? "Play" : k == stop ? "Stop" : "Record"), kind (k) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        g.setColour ((over || down) ? raised.brighter (0.08f) : raised);
        g.fillRect (getLocalBounds().reduced (1));
        g.setColour (line);
        g.drawRect (getLocalBounds().reduced (1));
        auto r = getLocalBounds().toFloat().reduced (10.0f, 9.0f);
        auto c = kind == record ? red : ink;
        if (getToggleState()) c = kind == record ? red.brighter (0.3f) : green;
        g.setColour (c);
        if (kind == play)
        {
            juce::Path p;
            p.addTriangle (r.getX() + 2, r.getY(), r.getX() + 2, r.getBottom(), r.getRight(), r.getCentreY());
            g.fillPath (p);
        }
        else if (kind == stop) g.fillRect (r.reduced (1.0f));
        else g.fillEllipse (r);
    }
private:
    Kind kind;
};

bool isAudio (const juce::File& file)
{
    return file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;wave");
}
}

class MainComponent::Impl final : public juce::Component,
                                  private juce::Timer,
                                  private juce::ChangeListener,
                                  private juce::MenuBarModel,
                                  private juce::ListBoxModel,
                                  private juce::ScrollBar::Listener,
                                  public juce::FileDragAndDropTarget
{
public:
    explicit Impl (Session& s) : session (s), menu (this), canvas (*this), ruler (*this), launcher (*this), pianoRoll (*this), deviceChain (*this)
    {
        setLookAndFeel (&theme);
        formats.registerBasicFormats();
        state = session.snapshot();
        addAndMakeVisible (menu);
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (this);
        menu.setVisible (false);
       #endif
        for (auto* child : std::initializer_list<juce::Component*> {
                 &playButton, &stopButton, &recordButton, &tempo, &tapButton, &loopButton, &clickButton, &clickSettingsButton, &positionLabel,
                 &projectLabel, &audioButton, &addTrackButton, &addMidiButton, &newMidiClipButton, &sessionTab, &arrangementTab, &pianoTab, &importButton, &filesButton, &pluginsButton,
                 &folderButton, &upButton, &browserSearch, &browserList, &ruler, &viewport,
                 &horizontalScroll, &zoomIn, &zoomOut, &fitButton, &launcher, &pianoRoll, &deviceChain })
            addAndMakeVisible (child);

        for (auto* button : std::initializer_list<juce::Button*> { &playButton, &stopButton, &recordButton, &tapButton, &loopButton, &clickButton, &clickSettingsButton,
                 &audioButton, &addTrackButton, &addMidiButton, &newMidiClipButton, &sessionTab, &arrangementTab, &pianoTab, &importButton, &filesButton, &pluginsButton, &folderButton,
                 &upButton, &zoomIn, &zoomOut, &fitButton })
        {
            button->setWantsKeyboardFocus (false);
            button->setMouseClickGrabsKeyboardFocus (false);
        }
        viewport.setViewedComponent (&canvas, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (10);
        horizontalScroll.addListener (this);
        tempo.setFont (font (13.0f, true));
        tempo.setEditable (false, true, false);
        tempo.setJustificationType (juce::Justification::centred);
        tempo.setColour (juce::Label::backgroundColourId, background);
        tempo.setTooltip ("Tempo: 20–999 beats per minute. Double-click to edit.");
        tapButton.setName("Tap tempo");tapButton.setTooltip("Tap a steady beat to set tempo. A pause over three seconds starts a new estimate.");
        tapButton.onClick=[this]{report(session.tapTempo(juce::Time::getMillisecondCounterHiRes()*.001));refresh();};
        tempo.onTextChange = [this]
        {
            const auto text = tempo.getText().trim().toStdString();
            char* end = nullptr; const double value = std::strtod (text.c_str(), &end);
            if (! text.empty() && end == text.c_str()+text.size() && std::isfinite (value) && value >= 20.0 && value <= 999.0)
                session.setTempo (value);
            refresh();
        };
        positionLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
        positionLabel.setJustificationType (juce::Justification::centred);
        positionLabel.setColour (juce::Label::backgroundColourId, background);
        projectLabel.setFont (font (12.0f));
        projectLabel.setJustificationType (juce::Justification::centredRight);
        playButton.setTooltip ("Play / pause  |  Space");
        stopButton.setTooltip ("Stop and return to the beginning  |  Escape");
        recordButton.setTooltip ("Record into armed audio or MIDI tracks  |  F9");
        playButton.onClick = [this] { togglePlay(); };
        stopButton.onClick = [this] { session.stop(); session.seek (0); refresh(); };
        recordButton.onClick = [this] { if (state.recording) session.stop(); else report (session.record()); refresh(); };
        loopButton.setClickingTogglesState (true);
        loopButton.setTooltip ("Loop the marked time range. Shift-drag the ruler to change it.  |  Cmd/Ctrl+L");
        loopButton.onClick = [this] { session.setLoop (loopButton.getToggleState(), state.loopStart, state.loopEnd); refresh(); };
        clickButton.setName("Metronome"); clickSettingsButton.setName("Metronome settings");
        clickButton.setClickingTogglesState(true); clickButton.setTooltip("Enable the metronome");
        clickSettingsButton.setTooltip("Count-in, accent, recording-only click and click level");
        clickButton.onClick=[this] { auto settings=session.snapshot().metronome;settings.enabled=clickButton.getToggleState();report(session.setMetronome(settings));refresh(); };
        clickSettingsButton.onClick=[this] { showMetronomeMenu(); };
        audioButton.onClick = [this] { showAudioSettings(); };
        audioButton.setTooltip ("Audio devices, input/output channels, sample rate and buffer size");
        addTrackButton.onClick = [this] { showPiano = false; addTrack(); resized(); };
        addMidiButton.onClick = [this] { addMidiTrack(); };
        newMidiClipButton.onClick = [this] { if (showSession) createLauncherMidiClip(); else createMidiClip (state.position); };
        sessionTab.onClick = [this] { showSessionView(); };
        arrangementTab.onClick = [this] { showArrangementView(); };
        pianoTab.onClick = [this] { openPianoRoll(); };
        addMidiButton.setTooltip ("Insert a MIDI track with the Halftrack synthesizer | Cmd/Ctrl+Shift+T");
        newMidiClipButton.setTooltip ("Create a four-bar MIDI clip at the playhead");
        addTrackButton.setTooltip ("Insert audio track  |  Cmd/Ctrl+T");
        importButton.onClick = [this] { importDialog(); };
        filesButton.setClickingTogglesState (false);
        pluginsButton.setClickingTogglesState (false);
        filesButton.onClick = [this] { pluginBrowser = false; updateBrowser(); resized(); };
        pluginsButton.onClick = [this] { pluginBrowser = true; updateBrowser(); resized(); };
        folderButton.onClick = [this] { if (pluginBrowser) showPlugins(); else chooseFolder(); };
        upButton.onClick = [this] { if (! pluginBrowser) { browserFolder = browserFolder.getParentDirectory(); updateBrowser(); } };
        upButton.setTooltip ("Parent folder");
        browserSearch.setTextToShowWhenEmpty ("Search current folder", muted);
        browserSearch.setFont (font (12.0f));
        browserSearch.onTextChange = [this] { updateBrowser(); };
        browserList.setModel (this);
        browserList.setRowHeight (27);
        browserList.setOutlineThickness (0);
        browserList.setTooltip ("Double-click an audio file to import, or a plug-in to add it to the selected track.");
        // Do not touch a user or cloud folder before the window is visible.
        // Folder enumeration begins only after an explicit choice or import.
        zoomIn.onClick = [this] { if (showPiano) pianoRoll.zoom (1.3); else zoom (1.3); };
        zoomOut.onClick = [this] { if (showPiano) pianoRoll.zoom (1.0 / 1.3); else zoom (1.0 / 1.3); };
        fitButton.onClick = [this] { if (showPiano) pianoRoll.fit(); else fitTimeline(); };
        zoomIn.setTooltip ("Zoom in  |  +");
        zoomOut.setTooltip ("Zoom out  |  -");
        fitButton.setTooltip ("Fit all clips to the arrangement view");
        session.addChangeListener (this);
        session.knownPlugins().addChangeListener (this);
        updateBrowser();
        refresh();
        startTimerHz (24);
        setWantsKeyboardFocus (true);
    }

    ~Impl() override
    {
        stopTimer();
        if (browserScan) browserScan->cancelled.store (true);
        browserScan.reset();
        session.removeChangeListener (this);
        session.knownPlugins().removeChangeListener (this);
        session.closePluginEditors();
        for (auto& dialog : dialogs) if (dialog != nullptr) delete dialog.getComponent();
        for (auto& alert : alerts) if (alert != nullptr) delete alert.getComponent();
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #endif
        browserList.setModel (nullptr);
        horizontalScroll.removeListener (this);
        viewport.setViewedComponent (nullptr, false);
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);
        g.setColour (panel);
        g.fillRect (browserBounds);
        g.fillRect (toolbarBounds);
        g.fillRect (sectionBounds);
        g.setColour (line);
        g.drawHorizontalLine (toolbarBounds.getBottom(), 0.0f, (float) getWidth());
        g.drawHorizontalLine (sectionBounds.getBottom(), 0.0f, (float) getWidth());
        g.drawVerticalLine (browserBounds.getRight(), (float) browserBounds.getY(), (float) browserBounds.getBottom());
        g.setColour (ink);
        g.setFont (font (13.0f, true));
        g.drawText ("HALFTRACK", toolbarBounds.getX() + 15, toolbarBounds.getY(), 102, toolbarBounds.getHeight(), juce::Justification::centredLeft);
        g.setFont (font (10.0f, true));
        g.setColour (muted);
        g.drawText ("BPM", tempo.getX() - 30, toolbarBounds.getY(), 27, toolbarBounds.getHeight(), juce::Justification::centredRight);
        g.drawText (pluginBrowser ? "PLUG-IN LIBRARY" : browserFolder == juce::File() ? "AUDIO FILES" : browserFolder.getFileName().toUpperCase(),
                    browserBounds.getX() + 12, browserBounds.getY() + 112,
                    browserBounds.getWidth() - 24, 20, juce::Justification::centredLeft, true);
        if (browserItems.empty() && pluginItems.empty())
        {
            g.setFont (font (12.0f));
            juce::String message;
            if (pluginBrowser) message = "No scanned plug-ins.\nUse Manage to scan your\nAudio Units and VST3 library.";
            else if (browserFolder == juce::File()) message = "Choose an audio folder,\nor import audio to begin.\nYou can also drop files\ninto the arrangement.";
            else if (browserError.isNotEmpty()) message = "Cannot read this folder.\n" + browserError;
            else if (browserLoading)
                message = juce::Time::getMillisecondCounterHiRes() - browserScanStarted > 5000.0
                    ? "This folder is taking longer\nto respond. You can choose\nanother folder to continue."
                    : "Loading audio folder...";
            else message = browserSearch.getText().isEmpty() ? "No audio files here.\nChoose another folder or drag\naudio into the arrangement." : "No matching audio files.\nTry another search.";
            g.drawFittedText (message, browserList.getBounds().reduced (14).withHeight (150), juce::Justification::centred, 7);
        }
        g.setColour (line);
        g.drawHorizontalLine (getHeight() - statusHeight, 0.0f, (float) getWidth());
        g.setColour (muted);
        g.setFont (font (10.5f));
        auto message = status.isNotEmpty() ? status : showPiano ? "Click: draw / select    Shift: group select    Ruler: insertion point    Cmd/Ctrl+C/X/V/D: notes clipboard (Halftrack only)" : "Space: play / pause     Click ruler: seek     Shift-drag ruler: loop     Double-click MIDI clip: edit notes";
        if (status.isEmpty() && showSession && ! showPiano) message = "Click triangle: launch     Double-click MIDI slot: edit     Enter: launch slot     Shift+Enter: launch scene     Tab: switch view";
        g.drawText (message, 13, getHeight() - statusHeight, getWidth() - 280, statusHeight, juce::Justification::centredLeft, true);
        auto* device = session.audioDeviceManager().getCurrentAudioDevice();
        const auto deviceText = device != nullptr
            ? juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz  /  " + juce::String (device->getCurrentBufferSizeSamples()) + " smp"
            : "Audio device offline";
        g.drawText (deviceText + "     CPU " + juce::String (state.cpu * 100.0, 1) + "%", getWidth() - 265,
                    getHeight() - statusHeight, 252, statusHeight, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto area = getLocalBounds();
       #if ! JUCE_MAC
        menu.setBounds (area.removeFromTop (23));
       #endif
        toolbarBounds = area.removeFromTop (49);
        auto toolbar = toolbarBounds.reduced (12, 10);
        toolbar.removeFromLeft (139);
        tempo.setBounds (toolbar.removeFromLeft (55));
        toolbar.removeFromLeft (6);tapButton.setBounds(toolbar.removeFromLeft(36));
        toolbar.removeFromLeft (11);
        playButton.setBounds (toolbar.removeFromLeft (34));
        stopButton.setBounds (toolbar.removeFromLeft (34));
        recordButton.setBounds (toolbar.removeFromLeft (34));
        toolbar.removeFromLeft (11);
        positionLabel.setBounds (toolbar.removeFromLeft (121));
        toolbar.removeFromLeft (11);
        loopButton.setBounds (toolbar.removeFromLeft (48));
        toolbar.removeFromLeft(8); clickButton.setBounds(toolbar.removeFromLeft(48));
        clickSettingsButton.setBounds(toolbar.removeFromLeft(25));
        audioButton.setBounds (toolbar.removeFromRight (91));
        toolbar.removeFromRight (12);
        projectLabel.setBounds (toolbar);
        area.removeFromBottom (statusHeight);
        auto deviceArea = area.removeFromBottom (deviceHeight);
        deviceChain.setBounds (deviceArea);
        browserBounds = area.removeFromLeft (browserWidth);
        auto browser = browserBounds.reduced (9, 8);
        auto tabs = browser.removeFromTop (26);
        filesButton.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2));
        pluginsButton.setBounds (tabs);
        browser.removeFromTop (8);
        browserSearch.setBounds (browser.removeFromTop (25));
        browser.removeFromTop (8);
        auto actions = browser.removeFromTop (25);
        if (! pluginBrowser) upButton.setBounds (actions.removeFromLeft (29));
        upButton.setVisible (! pluginBrowser);
        folderButton.setBounds (actions);
        browser.removeFromTop (29);
        browserList.setBounds (browser);
        sectionBounds = area.removeFromTop (35);
        auto section = sectionBounds.reduced (8, 5);
        sessionTab.setBounds (section.removeFromLeft (70));
        arrangementTab.setBounds (section.removeFromLeft (98));
        pianoTab.setBounds (section.removeFromLeft (84));
        addMidiButton.setBounds (section.removeFromRight (71));
        addTrackButton.setBounds (section.removeFromRight (77));
        section.removeFromRight (7);
        newMidiClipButton.setBounds (section.removeFromRight (91));
        section.removeFromRight (7);
        importButton.setBounds (section.removeFromRight (89));
        section.removeFromRight (12);
        zoomIn.setBounds (section.removeFromRight (25));
        zoomOut.setBounds (section.removeFromRight (25));
        fitButton.setBounds (section.removeFromRight (36));
        launcher.setBounds (area);
        launcher.setVisible (showSession && ! showPiano);
        pianoRoll.setBounds (area);
        pianoRoll.setVisible (showPiano);
        viewport.setVisible (! showPiano && ! showSession);
        ruler.setVisible (! showPiano && ! showSession);
        horizontalScroll.setVisible (! showPiano && ! showSession);
        sessionTab.setToggleState (showSession && ! showPiano, juce::dontSendNotification);
        arrangementTab.setToggleState (! showPiano && ! showSession, juce::dontSendNotification);
        const bool timelineTools = ! showSession || showPiano;
        zoomIn.setVisible (timelineTools); zoomOut.setVisible (timelineTools); fitButton.setVisible (timelineTools);
        pianoTab.setToggleState (showPiano, juce::dontSendNotification);
        horizontalScroll.setBounds (area.removeFromBottom (12).withTrimmedRight (trackWidth + 10));
        ruler.setBounds (area.removeFromTop (rulerHeight));
        viewport.setBounds (area);
        canvas.setSize (juce::jmax (1, viewport.getWidth() - 10), juce::jmax (area.getHeight(), (int) state.tracks.size() * trackHeight));
        canvas.resized();
        ruler.resized(); // Place controls using the final timeline width after window resizing.
        updateScroll();
    }

    bool handleKey (const juce::KeyPress& key)
    {
        if (session.isExporting()) return true;
        const auto code = key.getKeyCode();
        const auto cmd = key.getModifiers().isCommandDown();
        const auto shift = key.getModifiers().isShiftDown();
        if (cmd)
        {
            if (code == 'S') { saveProject (shift); return true; }
            if (code == 'O') { openDialog(); return true; }
            if (code == 'N') { ensureSaved ([safe = juce::Component::SafePointer<Impl> (this)] { if (safe != nullptr) { safe->report (safe->session.newProject()); safe->resetView(); } }); return true; }
            if (code == 'T') { if (shift) addMidiTrack(); else { showPiano = false; addTrack(); resized(); } return true; }
            if (code == 'Z') { if (shift) session.redo(); else session.undo(); refresh(); return true; }
            if (code == 'L') { session.setLoop (! state.looping, state.loopStart, state.loopEnd); refresh(); return true; }
            if (code == 'I') { importDialog(); return true; }
            if (code == 'E' && shift) { exportDialog(); return true; }
            if (! shift && ! showSession && ! showPiano && (code == 'E' || code == 'D' || code == 'C' || code == 'X' || code == 'V'))
            {
                for (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr; focused = focused->getParentComponent())
                    if (dynamic_cast<juce::TextEditor*> (focused)) return false;
                if (code == 'V')
                {
                    if (const auto* track = currentTrack(); track && ! track->isReturn)
                    { if(track->isMidi)midiClipCommand(7,{});else audioClipCommand(7,{}); return true; }
                    return false;
                }
                if (const auto* track = currentTrack(); track && juce::isPositiveAndBelow(selectedClip,(int)track->clips.size())
                    && track->clips[(size_t)selectedClip].isMidi && code!='E')
                {midiClipCommand(code=='D'?4:code=='C'?5:6,track->clips[(size_t)selectedClip].id);return true;}
                if (const auto* track = currentTrack(); track && juce::isPositiveAndBelow (selectedClip, (int) track->clips.size())
                    && track->clips[(size_t) selectedClip].isAudio)
                {
                    const auto id = track->clips[(size_t) selectedClip].id;
                    audioClipCommand (code == 'E' ? 3 : code == 'D' ? 4 : code == 'C' ? 5 : 6, id); return true;
                }
            }
            if (code == ',') { showAudioSettings(); return true; }
        }
        if((code=='R'||code=='r')&&cmd&&!shift&&!showPiano)
        {
            for(auto* focused=juce::Component::getCurrentlyFocusedComponent();focused;focused=focused->getParentComponent())
                if(dynamic_cast<juce::TextEditor*>(focused))return false;
            if(showSession){if(const auto* slot=selectedSlot();slot&&slot->hasClip){renameClipDialog(slot->clip.id);return true;}}
            else if(const auto* track=currentTrack();track&&juce::isPositiveAndBelow(selectedClip,(int)track->clips.size()))
            {renameClipDialog(track->clips[(size_t)selectedClip].id);return true;}
            return false;
        }
        if (code=='0' && !cmd && !shift && !key.getModifiers().isAltDown() && !showPiano)
        {
            for(auto* focused=juce::Component::getCurrentlyFocusedComponent();focused;focused=focused->getParentComponent())
                if(dynamic_cast<juce::TextEditor*>(focused))return false;
            if(showSession)
            {
                if(const auto* slot=selectedSlot();slot && slot->hasClip){setClipActiveFromUI(slot->clip.id,!slot->clip.active);return true;}
            }
            else if(const auto* track=currentTrack();track && juce::isPositiveAndBelow(selectedClip,(int)track->clips.size()))
            {const auto& clip=track->clips[(size_t)selectedClip];setClipActiveFromUI(clip.id,!clip.active);return true;}
            return false;
        }
        if (code == juce::KeyPress::tabKey) { if (showSession) showArrangementView(); else showSessionView(); return true; }
        if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (code == juce::KeyPress::escapeKey) { session.stop(); session.seek (0); refresh(); return true; }
        if (code == juce::KeyPress::F9Key) { if (state.recording) session.stop(); else report (session.record()); refresh(); return true; }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { if (showPiano) pianoRoll.deleteSelectedNote(); else if (showSession) clearSelectedSlot(); else deleteSelectedClip(); return true; }
        if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=') { if (showSession && ! showPiano) return false; if (showPiano) pianoRoll.zoom (1.3); else zoom (1.3); return true; }
        if (key.getTextCharacter() == '-') { if (showSession && ! showPiano) return false; if (showPiano) pianoRoll.zoom (1.0 / 1.3); else zoom (1.0 / 1.3); return true; }
        return false;
    }

    void ensureSaved (std::function<void()> action)
    {
        if (session.isExporting())
        {
            status = "Finish or cancel the export before closing or replacing this project.";
            statusFrames = 24 * 7; repaint(); return;
        }
        if (! session.snapshot().dirty) { action(); return; }
        auto safe = juce::Component::SafePointer<Impl> (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle ("Save this Halftrack project?")
            .withMessage ("Save your changes before continuing.")
            .withButton ("Save").withButton ("Discard").withButton ("Cancel"),
            [safe, action = std::move (action)] (int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->saveProject (false, action);
                else if (result == 2) action();
            });
    }

   #if HALFTRACK_TESTS_ENABLED
    void runInteractionChecks (const juce::File& audioFixture);
    bool captureTestErrors = false;
   #endif

private:
    static constexpr int browserWidth = 214, trackWidth = 280, trackHeight = 112, rulerHeight = 60,
                         deviceHeight = 177, statusHeight = 24;
    struct BrowserItem { juce::File file; bool directory = false; };
    struct FolderScan
    {
        struct Entry { std::string name; bool directory = false; };
        std::atomic<bool> cancelled { false }, finished { false };
        std::mutex mutex;
        std::vector<Entry> entries;
        std::string error;
    };

    class TrackRow final : public juce::Component
    {
    public:
        TrackRow (Impl& o, int i, bool verticalLayout = false) : owner (o), index (i), trackId (o.state.tracks[(size_t)i].id), columnLayout (verticalLayout)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &name, &mute, &solo, &arm, &gain, &pan, &input, &monitor, &sendMode }) addAndMakeVisible (c);
            name.setName ("Track name");
            name.setFont (font (12.0f, true));
            name.setEditable (false, true, false);
            name.onTextChange = [this] { owner.report (owner.session.renameTrack (index, name.getText())); };
            name.setTooltip ("Double-click to rename this track");
            for (auto* button : { &mute, &solo, &arm })
            {
                button->setClickingTogglesState (true);
                button->setWantsKeyboardFocus (false);
                button->setMouseClickGrabsKeyboardFocus (false);
            }
            mute.onClick = [this] { owner.selectTrack (index); owner.report (owner.session.setTrackMute (index, mute.getToggleState())); };
            solo.onClick = [this] { owner.selectTrack (index); owner.report (owner.session.setTrackSolo (index, solo.getToggleState())); };
            arm.onClick = [this] { owner.selectTrack (index); owner.report (owner.session.setTrackArmed (index, arm.getToggleState())); owner.refresh(); };
            mute.setTooltip ("Mute track"); solo.setTooltip ("Solo track"); arm.setTooltip ("Arm track for audio recording");
            arm.setName ("Record arm"); input.setName ("Track audio input"); monitor.setName ("Track monitoring");
            sendMode.setName ("Return send position");
            sendMode.addItem ("Pre-fader", 1); sendMode.addItem ("Post-fader", 2);
            sendMode.setTooltip ("All sends into this return are taken before or after their source track volume fader.");
            sendMode.onChange = [this]
            {
                if (! returnTrack || index >= (int) owner.state.tracks.size()) return;
                owner.report (owner.session.setReturnSendMode (owner.state.tracks[(size_t) index].id,
                    sendMode.getSelectedId() == 1 ? Session::SendMode::pre : Session::SendMode::post));
                owner.refresh();
            };
            arm.setColour (juce::TextButton::buttonOnColourId, red);
            gain.setSliderStyle (juce::Slider::LinearHorizontal);
            gain.setRange (-60.0, 12.0, 0.1);
            gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 21);
            gain.setDoubleClickReturnValue (true, 0);
            gain.setTextValueSuffix (" dB");
            gain.setTooltip ("Track volume. Double-click to reset to 0 dB.");
            gain.onDragStart = [this] { owner.session.beginEditGesture ("Track volume"); };
            gain.onDragEnd = [this] { owner.session.endEditGesture(); };
            gain.onValueChange = [this] { owner.report (owner.session.setTrackGain (index, (float) gain.getValue())); };
            pan.setSliderStyle (juce::Slider::LinearHorizontal);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 21);
            pan.setDoubleClickReturnValue (true, 0);
            pan.textFromValueFunction = [] (double v) { return std::abs (v) < 0.01 ? juce::String ("C") : juce::String ((int) std::round (std::abs (v) * 100.0)) + (v < 0 ? "L" : "R"); };
            pan.valueFromTextFunction = [] (const juce::String& value) { if (value.trim().equalsIgnoreCase ("C")) return 0.0; return value.getDoubleValue() * 0.01 * (value.containsIgnoreCase ("L") ? -1.0 : 1.0); };
            pan.setTooltip ("Track pan. Double-click to center.");
            pan.onDragStart = [this] { owner.session.beginEditGesture ("Track pan"); };
            pan.onDragEnd = [this] { owner.session.endEditGesture(); };
            pan.onValueChange = [this] { owner.report (owner.session.setTrackPan (index, (float) pan.getValue())); };
            monitor.addItem ("Off", 1); monitor.addItem ("Auto", 2); monitor.addItem ("In", 3);
            monitor.onChange = [this]
            {
                auto mode = monitor.getSelectedId() == 3 ? Session::MonitorMode::on : monitor.getSelectedId() == 2 ? Session::MonitorMode::automatic : Session::MonitorMode::off;
                owner.report (owner.session.setTrackMonitor (index, mode));
            };
            monitor.setTooltip ("Input monitoring: Off, Auto while armed, or always In");
            input.onChange = [this] { owner.report (owner.session.setTrackInput (index, input.getSelectedId() == 1 ? juce::String() : input.getText())); };
            input.setTooltip ("Choose an input before arming. Each input is assigned to one track; choosing it here moves its previous assignment.");
            addMouseListener (this, true);
        }
        const juce::String& getTrackId() const { return trackId; }
        void sync (const Session::TrackInfo& track, int position)
        {
            jassert (trackId == track.id);
            index = position;
            colour = track.colour;
            midi = track.isMidi; returnTrack = track.isReturn;
            input.setVisible (! returnTrack); monitor.setVisible (! returnTrack); arm.setVisible (! returnTrack);
            input.setName (midi ? "Track MIDI input" : "Track audio input");
            arm.setTooltip (midi ? "Arm track for MIDI recording" : "Arm track for audio recording");
            sendMode.setVisible (returnTrack);
            sendMode.setSelectedId (track.sendMode == Session::SendMode::pre ? 1 : 2, juce::dontSendNotification);
            peakL = track.peakL; peakR = track.peakR;
            if (! name.isBeingEdited()) name.setText (track.name, juce::dontSendNotification);
            if (! gain.isMouseButtonDown()) gain.setValue (track.gainDb, juce::dontSendNotification);
            if (! pan.isMouseButtonDown()) pan.setValue (track.pan, juce::dontSendNotification);
            mute.setToggleState (track.mute, juce::dontSendNotification);
            solo.setToggleState (track.solo, juce::dontSendNotification);
            arm.setToggleState (track.armed, juce::dontSendNotification);
            auto names = owner.session.inputNames (midi);
            if (names != inputNames)
            {
                inputNames = names;
                input.clear (juce::dontSendNotification);
                input.addItem ("No input", 1);
                for (int n = 0; n < inputNames.size(); ++n) input.addItem (inputNames[n], n + 2);
            }
            if (input.getNumItems() == 0) input.addItem ("No input", 1);
            input.setSelectedId (inputNames.indexOf (track.inputName) + 2, juce::dontSendNotification);
            if (track.inputName.isEmpty()) input.setSelectedId (1, juce::dontSendNotification);
            monitor.setSelectedId (track.monitor == Session::MonitorMode::on ? 3 : track.monitor == Session::MonitorMode::automatic ? 2 : 1, juce::dontSendNotification);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (owner.selectedTrack == index ? juce::Colour (0xff41464a) : juce::Colour (0xff35393d));
            g.setColour (colour); g.fillRect (0, 0, 4, getHeight() - 1);
            g.setColour (line); g.drawRect (getLocalBounds());
            g.setColour (muted); g.setFont (font (9.5f, true));
            if (columnLayout)
            {
                g.drawText ("VOL", 9, 67, 29, 20, juce::Justification::centredLeft);
                g.drawText ("PAN", 9, 97, 29, 20, juce::Justification::centredLeft);
                if (returnTrack) { g.setColour (orange); g.drawText ("RETURN / SEND INPUT", 9, 126, getWidth() - 18, 22, juce::Justification::centredLeft); }

                const auto peak = juce::jmax (peakL, peakR);
                const auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 60.0f) / 60.0f);
                g.setColour (background); g.fillRect (9, getHeight() - 7, getWidth() - 18, 3);
                g.setColour (peak >= 1.0f ? red : green); g.fillRect (9, getHeight() - 7, (int) ((getWidth() - 18) * fraction), 3);
                return;
            }
            g.drawText ("VOL", 12, 38, 25, 17, juce::Justification::centredLeft);
            g.drawText ("PAN", 174, 38, 25, 17, juce::Justification::centredLeft);
            if (returnTrack) { g.setColour (orange); g.drawText ("RETURN / SEND INPUT", 12, 78, 129, 23, juce::Justification::centredLeft); }
            else
            {
                g.drawText (midi ? "M" : "IN", 12, 78, 18, 23, juce::Justification::centredLeft);
                g.drawText ("MON", 174, 78, 26, 23, juce::Justification::centredLeft);
            }
            auto peak = juce::jmax (peakL, peakR);
            auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 60.0f) / 60.0f);
            g.setColour (background); g.fillRect (12, 106, getWidth() - 24, 3);
            g.setColour (peak >= 1.0f ? red : green); g.fillRect (12, 106, (int) ((getWidth() - 24) * fraction), 3);
        }
        void resized() override
        {
            if (columnLayout)
            {
                name.setBounds (8, 4, getWidth() - 16, 25);
                mute.setBounds (9, 35, 38, 23); solo.setBounds (49, 35, 38, 23); arm.setBounds (89, 35, 38, 23);
                gain.setBounds (36, 63, getWidth() - 43, 27); pan.setBounds (36, 93, getWidth() - 43, 27);
                input.setBounds (9, 127, getWidth() - 18, 23); monitor.setBounds (9, 153, getWidth() - 18, 23);
                sendMode.setBounds (9, 153, getWidth() - 18, 23);
                return;
            }
            name.setBounds (10, 5, getWidth() - 104, 25);
            mute.setBounds (getWidth() - 88, 7, 26, 23);
            solo.setBounds (getWidth() - 61, 7, 26, 23);
            arm.setBounds (getWidth() - 34, 7, 26, 23);
            gain.setBounds (37, 37, 129, 28);
            pan.setBounds (199, 37, getWidth() - 207, 28);
            input.setBounds (31, 79, 135, 23);
            monitor.setBounds (201, 79, getWidth() - 209, 23);
            sendMode.setBounds (143, 79, getWidth() - 151, 23);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.selectTrack (index);
            if (e.mods.isPopupMenu() && (e.originalComponent == this || e.originalComponent == &name))
                if (const auto* track = owner.currentTrack()) owner.showTrackOrderMenu (track->id, columnLayout);
        }
    private:
        Impl& owner;
        int index;
        juce::String trackId;
        juce::Colour colour;
        float peakL = 0, peakR = 0;
        bool midi = false, columnLayout = false, returnTrack = false;
        juce::Label name;
        juce::TextButton mute { "M" }, solo { "S" }, arm { "R" };
        juce::Slider gain, pan;
        juce::ComboBox input, monitor, sendMode;
        juce::StringArray inputNames;
    };

    bool syncTrackRows (juce::Component& parent, std::vector<std::unique_ptr<TrackRow>>& rows, bool vertical)
    {
        bool reordered = rows.size() != state.tracks.size();
        if (reordered) rows.clear();
        for (size_t i = 0; i < state.tracks.size(); ++i)
        {
            const auto& track = state.tracks[i];
            if (i >= rows.size())
            {
                auto row = std::make_unique<TrackRow> (*this, (int)i, vertical);
                parent.addAndMakeVisible (*row); rows.push_back (std::move (row));
            }
            if (rows[i]->getTrackId() != track.id)
            {
                auto found = std::find_if (rows.begin()+static_cast<std::ptrdiff_t>(i+1), rows.end(),
                    [&] (const auto& row) { return row->getTrackId() == track.id; });
                if (found != rows.end()) std::iter_swap (rows.begin()+static_cast<std::ptrdiff_t>(i), found);
                else { rows[i] = std::make_unique<TrackRow> (*this, (int)i, vertical); parent.addAndMakeVisible (*rows[i]); }
                reordered = true;
            }
            rows[i]->sync (track, (int)i);
        }
        if (reordered) for (auto& row : rows) row->toFront (false);
        return reordered;
    }

    class Canvas final : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit Canvas (Impl& o) : owner (o) { setWantsKeyboardFocus (true); }
        ~Canvas() override
        {
            if (clipGesture) owner.session.endEditGesture();
            for (auto& item : thumbnails) item.second->removeChangeListener (this);
        }
        void syncRows()
        {
            if (owner.syncTrackRows (*this, rows, false)) resized();
            repaint();
        }
        int timelineWidth() const { return getWidth() - trackWidth; }
        void resized() override
        {
            for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds (timelineWidth(), (int) i * trackHeight, trackWidth, trackHeight);
        }
        juce::Rectangle<float> clipBounds (const Session::ClipInfo& clip, int track) const
        {
            return { (float) ((clip.start - owner.viewStart) * owner.pixelsPerSecond), (float) (track * trackHeight + 6),
                     (float) juce::jmax (3.0, clip.length * owner.pixelsPerSecond), (float) trackHeight - 12.0f };
        }
        juce::Point<float> fadeHandle (const Session::ClipInfo& clip, int track, bool out) const
        {
            const auto b = clipBounds (clip, track);
            const auto time = out ? clip.length - clip.audio.fadeOutSeconds : clip.audio.fadeInSeconds;
            return { juce::jlimit (b.getX()+4, b.getRight()-4, b.getX()+(float) (time*owner.pixelsPerSecond)),
                     b.getY()+(out ? 39.0f : 27.0f) };
        }
        int hitFade (const Session::ClipInfo& clip, int track, juce::Point<float> point) const
        {
            if (! clip.isAudio || ! clip.canEditAudioFades || clipBounds (clip, track).getWidth() < 20) return 0;
            if (fadeHandle (clip, track, false).getDistanceFrom (point) <= 6) return 1;
            if (fadeHandle (clip, track, true).getDistanceFrom (point) <= 6) return 2;
            return 0;
        }
        int hitTrim (const Session::ClipInfo& clip, int track, juce::Point<float> point) const
        {
            const auto bounds = clipBounds (clip, track);
            if (! clip.canEditAudioRange || bounds.getWidth() < 20 || ! bounds.contains (point)) return 0;
            if (point.x-bounds.getX() <= 6) return 1;
            if (bounds.getRight()-point.x <= 6) return 2;
            return 0;
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (panel);
            const auto tw = timelineWidth();
            g.saveState();
            g.reduceClipRegion (0, 0, tw, getHeight());
            for (int i = 0; i < (int) owner.state.tracks.size(); ++i)
            {
                g.setColour (i == owner.selectedTrack ? juce::Colour (0xff30363a) : (i % 2 == 0 ? juce::Colour (0xff292d30) : juce::Colour (0xff2d3134)));
                g.fillRect (0, i * trackHeight, tw, trackHeight);
                g.setColour (line); g.drawHorizontalLine ((i + 1) * trackHeight - 1, 0.0f, (float) tw);
            }
            const double beat = 60.0 / owner.state.tempo;
            const auto grid = owner.gridSeconds();
            const auto end = owner.viewStart + tw / owner.pixelsPerSecond;
            for (double t = std::floor (owner.viewStart / grid) * grid; t <= end; t += grid)
            {
                const bool bar = std::abs (t / (beat * 4) - std::round (t / (beat * 4))) < 0.001;
                g.setColour (bar ? juce::Colour (0xff454b50) : juce::Colour (0xff353b3f));
                g.drawVerticalLine ((int) std::round ((t - owner.viewStart) * owner.pixelsPerSecond), 0.0f, (float) getHeight());
            }
            if (owner.state.looping)
            {
                g.setColour (orange.withAlpha (0.035f));
                g.fillRect ((float) ((owner.state.loopStart - owner.viewStart) * owner.pixelsPerSecond), 0.0f,
                            (float) ((owner.state.loopEnd - owner.state.loopStart) * owner.pixelsPerSecond), (float) getHeight());
            }
            for (int t = 0; t < (int) owner.state.tracks.size(); ++t)
            {
                const auto& track = owner.state.tracks[(size_t) t];
                if (track.isReturn)
                {
                    g.setColour (background); g.fillRect (0, t * trackHeight + 1, tw, trackHeight - 2);
                    g.setColour (muted); g.setFont (font (11.0f));
                    g.drawText ("RETURN  /  Audio from track sends", 16, t * trackHeight + 40, tw - 32, 26, juce::Justification::centredLeft);
                    continue;
                }
                for (int c = 0; c < (int) track.clips.size(); ++c)
                {
                    const auto& clip = track.clips[(size_t) c];
                    auto bounds = clipBounds (clip, t);
                    if (bounds.getRight() < 0 || bounds.getX() > tw) continue;
                    const bool selected = t == owner.selectedTrack && c == owner.selectedClip;
                    auto colour = track.colour;
                    if (colour.isTransparent()) colour = juce::Colour (0xff76b5bf);
                    if (!clip.active) colour=muted;
                    g.setColour (colour.withAlpha (track.mute || !clip.active ? 0.24f : 0.72f)); g.fillRect (bounds);
                    g.setColour (selected ? orange : colour.brighter (0.1f));
                    g.fillRect (bounds.withHeight (20));
                    g.setColour (juce::Colour (0xff182025)); g.setFont (font (11.0f, true));
                    g.drawText ((clip.active?juce::String():juce::String("OFF  /  "))+clip.name, bounds.withHeight (20).reduced (6, 0), juce::Justification::centredLeft, true);
                    auto wave = bounds.withTrimmedTop (24).reduced (2, 4);
                    g.setColour (juce::Colour (0xff14252b).withAlpha (track.mute ? 0.4f : 0.9f));
                    if (clip.isMidi && clip.lengthBeats > 0)
                    {
                        int low = 60, high = 71;
                        for (const auto& note : clip.notes) { low = juce::jmin (low, note.pitch); high = juce::jmax (high, note.pitch); }
                        const auto noteHeight = wave.getHeight() / (float) (high - low + 1);
                        for (const auto& note : clip.notes)
                        {
                            g.setColour (juce::Colour (0xff14252b).withAlpha (note.active ? (track.mute ? 0.4f : 0.9f) : 0.25f));
                            g.fillRect (wave.getX() + (float) (note.startBeat / clip.lengthBeats) * wave.getWidth(),
                                        wave.getY() + (high - note.pitch) * noteHeight,
                                        juce::jmax (2.0f, (float) (note.lengthBeats / clip.lengthBeats) * wave.getWidth()),
                                        juce::jmax (2.0f, noteHeight * 0.65f));
                        }
                    }
                    auto* thumbnail = clip.isMidi ? nullptr : thumbnailFor (clip.file);
                    if (thumbnail != nullptr && thumbnail->getTotalLength() > 0)
                    {
                        const auto visible = wave.getIntersection (juce::Rectangle<float> (0, wave.getY(), (float) tw, wave.getHeight()));
                        const auto startSeconds = juce::jmax (0.0, (visible.getX() - wave.getX()) / owner.pixelsPerSecond);
                        const auto endSeconds = juce::jmin (clip.length, startSeconds + visible.getWidth() / owner.pixelsPerSecond);
                        if (clip.reversed)
                        {
                            // Read the same immutable source interval and mirror its display.
                            g.saveState();
                            g.addTransform (juce::AffineTransform (-1,0,visible.getX()+visible.getRight(),0,1,0));
                            thumbnail->drawChannels (g, visible.toNearestInt(), clip.sourceLength-clip.sourceOffset-endSeconds,
                                clip.sourceLength-clip.sourceOffset-startSeconds,0.9f);
                            g.restoreState();
                        }
                        else thumbnail->drawChannels (g, visible.toNearestInt(), clip.sourceOffset + startSeconds, clip.sourceOffset + endSeconds, 0.9f);
                    }
                    if (selected && clip.isAudio && clip.canEditAudioFades && bounds.getWidth() >= 20)
                    {
                        const auto curve = [] (double x, Session::FadeCurve type)
                        {
                            const auto a = x*juce::MathConstants<double>::halfPi;
                            if (type == Session::FadeCurve::convex) return std::sin (a);
                            if (type == Session::FadeCurve::concave) return 1-std::cos (a);
                            if (type == Session::FadeCurve::sCurve) return (1-x)*(1-std::cos (a))+x*std::sin (a);
                            return x;
                        };
                        juce::Path envelope;
                        for (int n=0; n<=128; ++n)
                        {
                            const auto time = clip.length*n/128; double level = 1;
                            if (clip.audio.fadeInSeconds > 0 && time < clip.audio.fadeInSeconds)
                                level *= curve (time/clip.audio.fadeInSeconds, clip.audio.fadeInCurve);
                            if (clip.audio.fadeOutSeconds > 0 && time > clip.length-clip.audio.fadeOutSeconds)
                                level *= curve ((clip.length-time)/clip.audio.fadeOutSeconds, clip.audio.fadeOutCurve);
                            const auto x = bounds.getX()+bounds.getWidth()*n/128;
                            const auto y = wave.getBottom()-(float) level*wave.getHeight();
                            if (n == 0) envelope.startNewSubPath (x,y); else envelope.lineTo (x,y);
                        }
                        g.setColour (orange); g.strokePath (envelope, juce::PathStrokeType (1.5f));
                        for (bool out : { false, true })
                        {
                            const auto handle = fadeHandle (clip,t,out);
                            g.setColour (background); g.fillRect (handle.x-4,handle.y-4,8.0f,8.0f);
                            g.setColour (orange); g.drawRect (handle.x-4,handle.y-4,8.0f,8.0f,1.5f);
                        }
                    }
                    if (selected && clip.canEditAudioRange && bounds.getWidth() >= 20)
                    {
                        g.setColour (ink);
                        for (auto x : { bounds.getX()+3, bounds.getRight()-3 })
                            g.drawLine (x, bounds.getBottom()-16, x, bounds.getBottom()-5, 2.0f);
                    }
                    g.setColour (selected ? orange : colour.darker (0.45f)); g.drawRect (bounds, selected ? 2.0f : 1.0f);
                }
                if (owner.state.recording && track.armed)
                {
                    const auto from = owner.recordingStart;
                    g.setColour (red.withAlpha (0.22f));
                    g.fillRect ((float) ((from - owner.viewStart) * owner.pixelsPerSecond), (float) t * trackHeight + 6,
                                (float) juce::jmax (2.0, (owner.state.position - from) * owner.pixelsPerSecond), (float) trackHeight - 12);
                    g.setColour (red); g.setFont (font (11.0f, true));
                    g.drawText ("RECORDING", (int) ((from - owner.viewStart) * owner.pixelsPerSecond) + 8, t * trackHeight + 12, 96, 18, juce::Justification::centredLeft);
                }
            }
            if (owner.state.tracks.empty())
            {
                auto message = juce::Rectangle<int> (0, 0, tw, juce::jmin (getHeight(), 340)).reduced (35);
                g.setColour (ink); g.setFont (font (18.0f));
                g.drawText ("Drop audio to start your session", message.withHeight (30).withY (message.getCentreY() - 28), juce::Justification::centred);
                g.setColour (muted); g.setFont (font (12.0f));
                g.drawText ("Or insert an audio track, choose an input and arm it to record.", message.withHeight (24).withY (message.getCentreY() + 8), juce::Justification::centred);
            }
            auto x = (float) ((owner.state.position - owner.viewStart) * owner.pixelsPerSecond);
            g.setColour (owner.state.recording ? red : juce::Colour (0xffedf0e9));
            g.drawLine (x, 0, x, (float) getHeight(), 1.3f);
            if (dragHover)
            {
                g.setColour (orange.withAlpha (0.1f)); g.fillRect (0, 0, tw, getHeight());
                g.setColour (orange); g.drawRect (0, 0, tw, getHeight(), 2);
            }
            g.restoreState();
            g.setColour (line); g.drawVerticalLine (tw, 0.0f, (float) getHeight());
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.x >= timelineWidth()) return;
            grabKeyboardFocus(); fadeDrag = 0; rangeDrag = 0; dragClipId.clear();
            const auto previousTrack = owner.selectedTrack, previousClip = owner.selectedClip;
            auto track = e.y / trackHeight;
            if (track < 0) return;
            owner.selectedClip = -1;
            owner.selectedLauncherClipId.clear();
            if (track < (int) owner.state.tracks.size())
            {
                owner.selectTrack (track);
                if (owner.state.tracks[(size_t) track].isReturn) { draggedClip = -1; return; }
                const auto& clips = owner.state.tracks[(size_t) track].clips;
                for (int c = (int) clips.size() - 1; c >= 0; --c)
                    if (clipBounds (clips[(size_t) c], track).contains (e.position))
                    {
                        owner.selectedClip = c;
                        draggedClip = c;
                        originalStart = clips[(size_t) c].start;
                        dragStartX = e.x;
                        if (! e.mods.isPopupMenu())
                        {
                            fadeDrag = track == previousTrack && c == previousClip ? hitFade (clips[(size_t) c],track,e.position) : 0;
                            dragClipId = clips[(size_t) c].id;
                            originalRange = clips[(size_t) c];
                            originalFade = fadeDrag == 2 ? originalRange.audio.fadeOutSeconds : originalRange.audio.fadeInSeconds;
                            if (e.mods.isShiftDown() && e.mods.isAltDown() && originalRange.canEditAudioRange)
                            { rangeDrag = 3; fadeDrag = 0; }
                            else if (fadeDrag == 0 && track == previousTrack && c == previousClip)
                                rangeDrag = hitTrim (originalRange, track, e.position);
                        }
                        if (e.mods.isPopupMenu()) showClipMenu();
                        repaint();
                        return;
                    }
            }
            draggedClip = -1;
            owner.session.seek (juce::jmax (0.0, owner.viewStart + e.x / owner.pixelsPerSecond));
            owner.refresh();
            if (e.mods.isPopupMenu())
                if (const auto* target = owner.currentTrack(); target && ! target->isReturn)
                {
                    const auto trackId = target->id;
                    const auto position = owner.state.position;
                    juce::PopupMenu popup;
                    const bool midi=target->isMidi;
                    popup.addItem (7, midi?"Paste MIDI here":"Paste audio here", midi?owner.session.hasMidiClipClipboard():owner.session.hasAudioClipClipboard());
                    auto safe = juce::Component::SafePointer<Impl> (&owner);
                    popup.showMenuAsync (juce::PopupMenu::Options(), [safe, trackId, position, midi] (int choice)
                    {
                        if (safe != nullptr && choice == 7) {if(midi)safe->pasteMidi(trackId,position);else safe->pasteAudio(trackId,position);}
                    });
                }
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.x >= timelineWidth()) return;
            if (const auto* track = owner.currentTrack(); track != nullptr && track->isMidi)
            {
                if (owner.selectedClip >= 0) owner.openPianoRoll();
                else owner.createMidiClip (juce::jmax (0.0, owner.viewStart + e.x / owner.pixelsPerSecond));
            }
            else if (const auto* track = owner.currentTrack(); track && juce::isPositiveAndBelow (owner.selectedClip, (int)track->clips.size()))
                if (track->clips[(size_t)owner.selectedClip].isAudio) owner.audioClipCommand (2, track->clips[(size_t)owner.selectedClip].id);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (draggedClip < 0 || e.mods.isPopupMenu() || ! e.mouseWasDraggedSinceMouseDown()) return;
            const auto current = owner.session.snapshot();
            if (fadeDrag != 0)
            {
                for (const auto& track : current.tracks) for (const auto& clip : track.clips)
                    if (clip.id == dragClipId && clip.canEditAudioFades)
                    {
                        auto settings = clip.audio;
                        const auto other = fadeDrag == 1 ? settings.fadeOutSeconds : settings.fadeInSeconds;
                        const auto value = juce::jlimit (0.0, juce::jmax (0.0,clip.length-other),
                            originalFade+(e.x-dragStartX)/owner.pixelsPerSecond*(fadeDrag == 1 ? 1 : -1));
                        if (! clipGesture) { owner.session.beginEditGesture ("Adjust clip fade"); clipGesture = true; }
                        if (fadeDrag == 1) settings.fadeInSeconds = value; else settings.fadeOutSeconds = value;
                        owner.report (owner.session.setAudioClipSettings (dragClipId,settings)); owner.refresh(); return;
                    }
                return;
            }
            if (rangeDrag != 0)
            {
                for (const auto& track : current.tracks) for (const auto& clip : track.clips)
                    if (clip.id == dragClipId && clip.canEditAudioRange)
                    {
                        auto start = originalRange.start, length = originalRange.length, offset = originalRange.sourceOffset;
                        const auto minimum = juce::jmax (.001, clip.audio.fadeInSeconds+clip.audio.fadeOutSeconds);
                        const auto delta = (e.x-dragStartX)/owner.pixelsPerSecond;
                        const auto snap = [&] (double value)
                        { return e.mods.isAltDown() ? value : std::round (value/owner.gridSeconds())*owner.gridSeconds(); };
                        if (rangeDrag == 1)
                        {
                            const auto shift = juce::jlimit (-juce::jmin (start,offset), juce::jmax (0.0,length-minimum), snap (start+delta)-start);
                            start += shift; offset += shift; length -= shift;
                        }
                        else if (rangeDrag == 2)
                            length = juce::jlimit (minimum, juce::jmax (minimum,clip.sourceLength-offset), snap (start+length+delta)-start);
                        else
                            offset = juce::jlimit (0.0, juce::jmax (0.0,clip.sourceLength-length), offset-delta);
                        if (! clipGesture) { owner.session.beginEditGesture (rangeDrag == 3 ? "Slip audio source" : "Trim audio edge"); clipGesture = true; }
                        const auto result = owner.session.setAudioClipRange (dragClipId,start,length,offset);
                        owner.report (result); owner.refresh();
                        if (result.wasOk()) owner.selectArrangementClip (dragClipId);
                        return;
                    }
                return;
            }
            // Resolve the stable identity on every event; sorting, deletion or a
            // project switch must never redirect a body drag to the old index.
            for (int t=0; t<(int)current.tracks.size(); ++t)
                for (int c=0; c<(int)current.tracks[(size_t)t].clips.size(); ++c)
                    if (current.tracks[(size_t)t].clips[(size_t)c].id == dragClipId)
                    {
                        auto start = juce::jmax (0.0, originalStart + (e.x - dragStartX) / owner.pixelsPerSecond);
                        if (! e.mods.isAltDown()) start = std::round (start / owner.gridSeconds()) * owner.gridSeconds();
                        const auto& clip = current.tracks[(size_t)t].clips[(size_t)c];
                        const int destination = e.y < 0 ? -1 : e.y/trackHeight;
                        if (! juce::isPositiveAndBelow (destination,(int)current.tracks.size())
                            || current.tracks[(size_t)destination].isMidi != clip.isMidi || current.tracks[(size_t)destination].isReturn) return;
                        if (! clipGesture) { owner.session.beginEditGesture ("Move clip"); clipGesture = true; }
                        const auto result = clip.isAudio ? owner.session.moveAudioClip (dragClipId,current.tracks[(size_t)destination].id,start)
                            : owner.session.moveMidiClip (dragClipId,current.tracks[(size_t)destination].id,start);
                        owner.report (result); owner.refresh();
                        if (result.wasOk()) owner.selectArrangementClip (dragClipId);
                        return;
                    }
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (clipGesture) { owner.session.endEditGesture(); clipGesture = false; }
            draggedClip = -1; fadeDrag = 0; rangeDrag = 0; dragClipId.clear();
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto track = e.y/trackHeight; bool handle = false;
            if (e.x < timelineWidth() && e.y >= 0 && juce::isPositiveAndBelow (track,(int) owner.state.tracks.size()))
            {
                const auto& clips = owner.state.tracks[(size_t) track].clips;
                for (int c=(int)clips.size()-1; c>=0; --c)
                    if (clipBounds (clips[(size_t)c],track).contains (e.position))
                    { handle = track == owner.selectedTrack && c == owner.selectedClip
                        && (hitFade (clips[(size_t)c],track,e.position) != 0 || hitTrim (clips[(size_t)c],track,e.position) != 0); break; }
            }
            setMouseCursor (handle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        }
        void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
        {
            if (event.mods.isCommandDown()) { owner.zoom (wheel.deltaY > 0 ? 1.12 : 1.0 / 1.12); return; }
            if (event.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
            {
                const auto delta = event.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
                owner.viewStart = juce::jmax (0.0, owner.viewStart - delta * 300.0 / owner.pixelsPerSecond);
                owner.updateScroll(); owner.ruler.repaint(); repaint(); return;
            }
            juce::Component::mouseWheelMove (event, wheel);
        }
        bool keyPressed (const juce::KeyPress& key) override { return owner.handleKey (key); }
        bool dragHover = false;
    private:
        Impl& owner;
        std::vector<std::unique_ptr<TrackRow>> rows;
        std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
        int draggedClip = -1, dragStartX = 0;
        double originalStart = 0, originalFade = 0;
        int fadeDrag = 0, rangeDrag = 0;
        Session::ClipInfo originalRange;
        juce::String dragClipId;
        bool clipGesture = false;
        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        juce::AudioThumbnail* thumbnailFor (const juce::File& file)
        {
            if (! file.existsAsFile()) return nullptr;
            const auto key = file.getFullPathName();
            auto found = thumbnails.find (key);
            if (found != thumbnails.end()) return found->second.get();
            auto thumb = std::make_unique<juce::AudioThumbnail> (256, owner.formats, owner.thumbnailCache);
            thumb->addChangeListener (this);
            thumb->setSource (new juce::FileInputSource (file));
            auto* result = thumb.get();
            thumbnails.emplace (key, std::move (thumb));
            return result;
        }
        void showClipMenu()
        {
            juce::PopupMenu popup;
            popup.addItem (1, "Delete clip");
            const auto* track = owner.currentTrack();
            const auto* clip = track && juce::isPositiveAndBelow(owner.selectedClip,(int)track->clips.size()) ? &track->clips[(size_t)owner.selectedClip] : nullptr;
            const auto id = clip ? clip->id : juce::String();
            const bool activate=clip && !clip->active;
            if (clip) {popup.addItem(8,activate?"Activate clip":"Deactivate clip");popup.addItem(9,"Rename clip...");}
            if (clip && clip->isAudio)
            {
                popup.addItem (2, "Gain, fades and range...");
                popup.addItem (3, "Split at playhead", clip->canEditAudioRange);
                popup.addItem (4, "Duplicate after clip");
                popup.addSeparator();
                popup.addItem (5, "Copy audio clip", clip->canEditAudioRange);
                popup.addItem (6, "Cut audio clip", clip->canEditAudioRange);
                popup.addItem (7, "Paste audio at playhead", owner.session.hasAudioClipClipboard());
            }
            if(clip&&clip->isMidi)
            {
                popup.addItem(4,"Duplicate MIDI after clip",clip->canEditMidiClip);
                popup.addItem(5,"Copy MIDI clip",clip->canEditMidiClip);popup.addItem(6,"Cut MIDI clip",clip->canEditMidiClip);
                popup.addItem(7,"Paste MIDI at playhead",owner.session.hasMidiClipClipboard());
            }
            const bool midi=clip&&clip->isMidi;
            auto safe = juce::Component::SafePointer<Impl> (&owner);
            const auto trackId = track ? track->id : juce::String();
            const auto position = owner.state.position;
            popup.showMenuAsync (juce::PopupMenu::Options(), [safe, id, trackId, position, midi, activate] (int choice)
            {
                if (safe == nullptr) return;
                if (choice == 1) safe->deleteClipById(id);
                else if (choice == 8) safe->setClipActiveFromUI(id,activate);
                else if (choice == 9) safe->renameClipDialog(id);
                else if (choice == 7) {if(midi)safe->pasteMidi(trackId,position);else safe->pasteAudio(trackId,position);}
                else if (choice > 1) {if(midi)safe->midiClipCommand(choice,id);else safe->audioClipCommand(choice,id);}
            });
        }
    };

    class Ruler final : public juce::Component
    {
    public:
        explicit Ruler (Impl& o) : owner (o)
        {
            setName ("Arrangement ruler and locators"); setWantsKeyboardFocus (true);
            for (auto* button : { &add, &previous, &next }) { addAndMakeVisible (button); button->setWantsKeyboardFocus (false); }
            add.setName ("Add locator at playhead"); previous.setName ("Previous locator"); next.setName ("Next locator");
            add.setTooltip ("Name a song section at the playhead. Double-click a locator to edit it.");
            previous.setTooltip ("Jump to previous locator"); next.setTooltip ("Jump to next locator");
            add.onClick = [this] { addAt (owner.session.snapshot().position); };
            previous.onClick = [this] { navigate (false); }; next.onClick = [this] { navigate (true); };
        }
        ~Ruler() override { if (definingLoop || locatorGesture) owner.session.endEditGesture(); }
        void resized() override
        {
            const auto left = owner.canvas.timelineWidth()+12;
            add.setBounds (left,32,132,22); previous.setBounds (left+142,32,44,22); next.setBounds (left+192,32,44,22);
        }
        void sync()
        {
            bool exists=false,hasPrevious=false,hasNext=false;
            for (const auto& marker : owner.state.locators)
            {
                exists = exists || marker.id == selected;
                hasPrevious = hasPrevious || marker.position < owner.state.position-.001;
                hasNext = hasNext || marker.position > owner.state.position+.001;
            }
            if (! exists) selected.clear();
            const bool enabled = ! owner.session.isExporting();
            add.setEnabled (enabled); previous.setEnabled (enabled && hasPrevious); next.setEnabled (enabled && hasNext); repaint();
        }
        void addAt (double seconds)
        {
            juce::String id; const auto result = owner.session.addLocator (seconds,{},id);
            owner.report (result); if (result.wasOk()) selected=id; owner.refresh();
        }
        void navigate (bool forward)
        {
            owner.report (owner.session.navigateLocator (forward)); owner.refresh();
            const auto width = owner.canvas.timelineWidth()/owner.pixelsPerSecond;
            if (owner.state.position < owner.viewStart || owner.state.position > owner.viewStart+width)
                owner.viewStart = juce::jmax (0.0,owner.state.position-width*.2);
            owner.updateScroll(); owner.canvas.repaint(); repaint();
        }
        juce::Rectangle<float> locatorBounds (const Session::LocatorInfo& marker) const
        {
            const auto x = (float)((marker.position-owner.viewStart)*owner.pixelsPerSecond);
            float width=130;
            const auto nextMarker=std::upper_bound (owner.state.locators.begin(),owner.state.locators.end(),marker.position,
                [] (double time,const Session::LocatorInfo& item) {return time<item.position;});
            if (nextMarker!=owner.state.locators.end())
                width=juce::jmin (width,(float)((nextMarker->position-marker.position)*owner.pixelsPerSecond)-3);
            return {x,36,juce::jmax (12.0f,width),20};
        }
        juce::String hitLocator (juce::Point<float> point) const
        {
            if (point.x < 0 || point.x >= owner.canvas.timelineWidth()) return {};
            for (auto it=owner.state.locators.rbegin();it!=owner.state.locators.rend();++it)
                if (locatorBounds (*it).contains (point)) return it->id;
            return {};
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff343a3e));
            const auto width=owner.canvas.timelineWidth();
            g.saveState();g.reduceClipRegion (0,0,width,getHeight());
            const auto x1=(float)((owner.state.loopStart-owner.viewStart)*owner.pixelsPerSecond);
            const auto x2=(float)((owner.state.loopEnd-owner.viewStart)*owner.pixelsPerSecond);
            g.setColour (owner.state.looping?orange:muted.withAlpha (.55f));
            g.fillRect (x1,3.0f,juce::jmax (1.0f,x2-x1),3.0f);g.fillRect (x1,3.0f,2.0f,9.0f);g.fillRect (x2-2,3.0f,2.0f,9.0f);
            const auto bar=240.0/owner.state.tempo;
            const auto step=bar*juce::jmax (1.0,std::pow (2.0,std::ceil (std::log2 (48.0/(bar*owner.pixelsPerSecond)))));
            const auto end=owner.viewStart+width/owner.pixelsPerSecond;
            g.setFont (font (10.5f));
            for (double time=std::floor (owner.viewStart/step)*step;time<=end;time+=step)
            {
                const int x=(int)std::round ((time-owner.viewStart)*owner.pixelsPerSecond);
                g.setColour (muted);g.drawText (juce::String ((int)std::round (time/bar)+1),x+5,15,55,19,juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xff575f65));g.drawVerticalLine (x,14.0f,34.0f);
            }
            g.setColour (background);g.fillRect (0,35,width,getHeight()-35);
            for (const auto& marker : owner.state.locators)
            {
                const auto bounds=locatorBounds (marker);
                if (bounds.getRight()<0 || bounds.getX()>=width) continue;
                g.setColour (marker.id==selected?orange:juce::Colour (0xffa9b8a7));
                juce::Path flag;flag.addTriangle (bounds.getX(),37,bounds.getX()+8,37,bounds.getX(),45);g.fillPath (flag);
                g.drawVerticalLine ((int)bounds.getX(),37,56);
                g.setFont (font (10.0f,marker.id==selected));
                g.drawText (marker.name,bounds.withTrimmedLeft (11),juce::Justification::centredLeft,true);
            }
            const auto px=(float)((owner.state.position-owner.viewStart)*owner.pixelsPerSecond);
            juce::Path needle;needle.addTriangle (px-5,15,px+5,15,px,23);
            g.setColour (owner.state.recording?red:ink);g.fillPath (needle);g.restoreState();
            g.setColour (raised);g.fillRect (width,0,getWidth()-width,getHeight());
            g.setColour (muted);g.setFont (font (10.0f,true));
            g.drawText ("TRACK / MIXER",width+12,0,trackWidth-24,29,juce::Justification::centredLeft);
            g.setColour (line);g.drawHorizontalLine (getHeight()-1,0.0f,(float)getWidth());
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.x>=owner.canvas.timelineWidth() || owner.session.isExporting()) return;
            grabKeyboardFocus(); dragId.clear();
            anchor=juce::jmax (0.0,owner.viewStart+event.x/owner.pixelsPerSecond);
            const auto hit=hitLocator (event.position);
            if (event.mods.isPopupMenu()) { showMenu (hit,anchor);return; }
            definingLoop=event.mods.isShiftDown();
            if (definingLoop) {owner.session.beginEditGesture ("Loop range");return;}
            if (hit.isNotEmpty())
            {
                selected=hit;dragId=hit;downX=event.x;
                for (const auto& marker : owner.state.locators) if (marker.id==hit) original=marker.position;
                owner.report (owner.session.seekLocator (hit));
            }
            else {selected.clear();owner.session.seek (anchor);}
            owner.refresh();
        }
        void mouseDoubleClick (const juce::MouseEvent& event) override
        {
            if (event.x>=owner.canvas.timelineWidth() || event.mods.isPopupMenu() || event.mods.isShiftDown()) return;
            const auto hit=hitLocator (event.position);
            if (hit.isNotEmpty()) owner.editLocatorDialog (hit);
            else if (event.y>=35) addAt (juce::jmax (0.0,owner.viewStart+event.x/owner.pixelsPerSecond));
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (owner.session.isExporting()) return;
            if (definingLoop)
            {
                const auto grid=owner.gridSeconds(),cursor=juce::jmax (0.0,owner.viewStart+event.x/owner.pixelsPerSecond);
                const auto start=std::floor (juce::jmin (anchor,cursor)/grid)*grid;
                const auto end=juce::jmax (start+grid,std::ceil (juce::jmax (anchor,cursor)/grid)*grid);
                owner.session.setLoop (true,start,end);owner.refresh();return;
            }
            if (dragId.isEmpty() || ! event.mouseWasDraggedSinceMouseDown()) return;
            for (const auto& marker : owner.session.snapshot().locators) if (marker.id==dragId)
            {
                auto time=original+(event.x-downX)/owner.pixelsPerSecond;
                if (!event.mods.isAltDown())time=std::round (time/owner.gridSeconds())*owner.gridSeconds();
                time=juce::jlimit (0.0,172800.0-.001,time);
                if (!locatorGesture){owner.session.beginEditGesture ("Move locator");locatorGesture=true;}
                owner.report (owner.session.updateLocator (dragId,marker.name,time));owner.refresh();return;
            }
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (definingLoop || locatorGesture)owner.session.endEditGesture();
            definingLoop=false;locatorGesture=false;dragId.clear();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (owner.session.isExporting())return true;
            if ((key.getKeyCode()==juce::KeyPress::deleteKey || key.getKeyCode()==juce::KeyPress::backspaceKey)&&selected.isNotEmpty())
            {owner.report (owner.session.removeLocator (selected));owner.refresh();return true;}
            return owner.handleKey (key);
        }
    private:
        Impl& owner;
        juce::TextButton add {"Add locator"},previous {"<"},next {">"};
        juce::String selected,dragId;
        double anchor=0,original=0;int downX=0;
        bool definingLoop=false,locatorGesture=false;
        void showMenu (const juce::String& id,double position)
        {
            juce::PopupMenu menu;menu.addItem (1,"Add locator here");
            if (id.isNotEmpty()){menu.addItem (2,"Edit locator...");menu.addItem (3,"Delete locator");}
            juce::PopupMenu jumps;juce::StringArray ids;
            for (const auto& marker:owner.state.locators){ids.add(marker.id);jumps.addItem(100+ids.size()-1,marker.name+"  /  "+juce::String(marker.position,3)+" s");}
            if (!ids.isEmpty())menu.addSubMenu ("Jump to locator",jumps);
            auto safe=juce::Component::SafePointer<Ruler> (this);
            menu.showMenuAsync (juce::PopupMenu::Options(),[safe,id,position,ids](int choice)
            {
                if(safe==nullptr)return;
                if(choice==1)safe->addAt(position);
                if(choice==2)safe->owner.editLocatorDialog(id);
                if(choice==3){safe->owner.report(safe->owner.session.removeLocator(id));safe->owner.refresh();}
                if(choice>=100&&choice-100<ids.size()){safe->owner.report(safe->owner.session.seekLocator(ids[choice-100]));safe->owner.refresh();}
            });
        }
    };

    class LauncherGrid final : public juce::Component, private juce::ScrollBar::Listener
    {
    public:
        explicit LauncherGrid (Impl& o) : owner (o)
        {
            setName ("Session clip grid"); setWantsKeyboardFocus (true);
            for (auto* child : std::initializer_list<juce::Component*> { &quantisation, &stopAll, &arrangement, &addScene, &mixers, &horizontal, &vertical }) addAndMakeVisible (child);
            mixers.setInterceptsMouseClicks (false, true);
            quantisation.setName ("Global launch quantization");
            quantisation.addItem ("None", 1); quantisation.addItem ("1 Beat", 2); quantisation.addItem ("1 Bar", 3);
            quantisation.setTooltip ("Clip and scene launch timing. View switches do not change what is playing.");
            quantisation.onChange = [this]
            {
                const auto q = quantisation.getSelectedId() == 1 ? Session::LaunchQuantisation::none
                             : quantisation.getSelectedId() == 2 ? Session::LaunchQuantisation::beat : Session::LaunchQuantisation::bar;
                owner.report (owner.session.setLaunchQuantisation (q)); owner.refresh();
            };
            stopAll.onClick = [this] { owner.report (owner.session.stopAllLauncherClips()); owner.refresh(); };
            arrangement.onClick = [this] { owner.report (owner.session.returnToArrangement()); owner.refresh(); };
            addScene.onClick = [this]
            {
                owner.addSceneFromUI();
            };
            for (auto* button : { &stopAll, &arrangement, &addScene }) { button->setWantsKeyboardFocus (false); button->setMouseClickGrabsKeyboardFocus (false); }
            stopAll.setName ("Stop all Session clips"); arrangement.setName ("Return to Arrangement"); addScene.setName ("Add scene");
            arrangement.setTooltip ("Release Session overrides so tracks play their arrangement again");
            stopAll.setTooltip ("Stop every Session clip at the chosen launch quantization");
            horizontal.addListener (this); vertical.addListener (this);
        }
        ~LauncherGrid() override { horizontal.removeListener (this); vertical.removeListener (this); }
        void sync()
        {
            owner.syncTrackRows (mixers, rows, true);
            quantisation.setSelectedId (owner.state.launchQuantisation == Session::LaunchQuantisation::none ? 1 : owner.state.launchQuantisation == Session::LaunchQuantisation::beat ? 2 : 3, juce::dontSendNotification);
            const bool overridden = std::any_of (owner.state.tracks.begin(), owner.state.tracks.end(), [] (const auto& track) { return track.launcherActive; });
            arrangement.setEnabled (overridden); arrangement.setToggleState (overridden, juce::dontSendNotification);
            stopAll.setEnabled (! owner.state.tracks.empty());
            resized(); repaint();
        }
        void resized() override
        {
            quantisation.setBounds (89, 6, 87, 23); stopAll.setBounds (190, 6, 90, 23);
            arrangement.setBounds (288, 6, 157, 23); addScene.setBounds (getWidth() - 105, 6, 93, 23);
            const auto grid = gridArea();
            mixers.setBounds (0, grid.getBottom() + stopHeight, tracksWidth(), mixerHeight);
            for (int i = 0; i < (int) rows.size(); ++i) rows[(size_t) i]->setBounds (i * columnWidth - (int) offsetX, 0, columnWidth, mixerHeight);
            horizontal.setBounds (0, getHeight() - scrollSize, tracksWidth(), scrollSize);
            vertical.setBounds (getWidth() - scrollSize, gridTop, scrollSize, grid.getHeight());
            const auto totalWidth = juce::jmax ((double) tracksWidth(), (double) owner.state.tracks.size() * columnWidth);
            offsetX = juce::jlimit (0.0, juce::jmax (0.0, totalWidth - tracksWidth()), offsetX);
            horizontal.setRangeLimits (0, totalWidth, juce::dontSendNotification); horizontal.setCurrentRange (offsetX, tracksWidth(), juce::dontSendNotification); horizontal.setSingleStepSize (columnWidth);
            const auto visibleRows = (double) grid.getHeight() / rowHeight;
            const auto totalRows = juce::jmax (visibleRows, (double) owner.state.scenes.size());
            offsetScene = juce::jlimit (0.0, juce::jmax (0.0, totalRows - visibleRows), offsetScene);
            vertical.setRangeLimits (0, totalRows, juce::dontSendNotification); vertical.setCurrentRange (offsetScene, visibleRows, juce::dontSendNotification); vertical.setSingleStepSize (1);
        }
        std::pair<int, int> cellAt (juce::Point<int> point) const
        {
            if (point.y < gridTop || point.y >= gridArea().getBottom() || point.x < 0 || point.x >= tracksWidth()) return { -1, -1 };
            const int track = (int) ((point.x + offsetX) / columnWidth);
            const int scene = (int) ((point.y - gridTop) / (double) rowHeight + offsetScene);
            return track >= 0 && track < (int) owner.state.tracks.size() && ! owner.state.tracks[(size_t) track].isReturn && scene < (int) owner.state.scenes.size() ? std::pair<int, int> { track, scene } : std::pair<int, int> { -1, -1 };
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (background);
            g.setColour (panel); g.fillRect (0, 0, getWidth(), toolbarHeight);
            g.setColour (muted); g.setFont (font (10.0f)); g.drawText ("Quantization", 10, 4, 77, 26, juce::Justification::centredLeft);
            const auto grid = gridArea();
            g.saveState(); g.reduceClipRegion (0, toolbarHeight, tracksWidth(), getHeight() - toolbarHeight);
            for (int t = 0; t < (int) owner.state.tracks.size(); ++t)
            {
                const auto& track = owner.state.tracks[(size_t) t]; const int x = t * columnWidth - (int) offsetX;
                if (x + columnWidth <= 0 || x >= tracksWidth()) continue;
                g.setColour (track.colour.withAlpha (t == owner.selectedTrack ? 0.75f : 0.4f)); g.fillRect (x, toolbarHeight, columnWidth, headerHeight);
                g.setColour (ink); g.setFont (font (11.5f, true)); g.drawText (track.name, x + 9, toolbarHeight, columnWidth - 18, headerHeight, juce::Justification::centredLeft, true);
                g.setColour (line); g.drawRect (x, toolbarHeight, columnWidth, headerHeight);
                if (track.isReturn)
                {
                    g.setColour (panel); g.fillRect (x + 1, gridTop, columnWidth - 2, grid.getHeight() + stopHeight);
                    g.setColour (muted); g.setFont (font (10.5f, true));
                    g.drawText ("RETURN", x + 9, gridTop + 15, columnWidth - 18, 24, juce::Justification::centred);
                    continue;
                }
                g.saveState(); g.reduceClipRegion (grid.withWidth (tracksWidth()));
                for (int scene = 0; scene < (int) owner.state.scenes.size(); ++scene)
                {
                    const auto bounds = slotBounds (t, scene);
                    if (! bounds.intersects (grid)) continue;
                    const auto* slot = slotAt (t, scene);
                    const bool filled = slot != nullptr && slot->hasClip;
                    const bool selected = t == owner.selectedTrack && owner.state.scenes[(size_t) scene].id == owner.selectedSceneId;
                    g.setColour (filled ? (slot->clip.active?track.colour:muted).withAlpha (slot->clip.active?0.30f:0.12f) : scene % 2 == 0 ? panel : juce::Colour (0xff303438)); g.fillRect (bounds.reduced (1));
                    const bool queued = slot != nullptr && (slot->queued || slot->stopQueued);
                    const auto stateColour = queued ? orange.withAlpha (owner.ticks % 16 < 8 ? 1.0f : 0.45f) : slot != nullptr && slot->playing ? green : muted;
                    g.setColour (stateColour);
                    if (filled && ! (slot != nullptr && slot->stopQueued)) drawTriangle (g, bounds.getX() + 10, bounds.getY() + 10);
                    else g.fillRect (bounds.getX() + 10, bounds.getY() + 11, 7, 7);
                    if (filled)
                    {
                        const bool follows = slot->clip.follow.action != Session::FollowAction::off;
                        g.setColour (ink); g.setFont (font (11.0f)); g.drawText ((slot->clip.active?juce::String():juce::String("OFF / "))+slot->clip.name, bounds.withTrimmedLeft (29).withTrimmedRight (follows ? 44 : 7), juce::Justification::centredLeft, true);
                        if (follows)
                        {
                            g.setColour (orange); g.setFont (font (9.0f));
                            g.drawText (juce::String (slot->clip.follow.afterBeats, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "b >", bounds.withLeft (bounds.getRight() - 43).reduced (3, 0), juce::Justification::centredRight, true);
                        }
                        if (slot->playing) { g.setColour (green); g.fillRect (bounds.getX() + 1, bounds.getBottom() - 3, bounds.getWidth() - 2, 2); }
                    }
                    g.setColour (selected ? orange : line); g.drawRect (bounds, selected ? 2 : 1);
                }
                g.restoreState();
                const auto stop = juce::Rectangle<int> (x, grid.getBottom(), columnWidth, stopHeight);
                g.setColour (raised); g.fillRect (stop.reduced (1)); g.setColour (track.launcherActive ? orange : muted); g.fillRect (x + 10, stop.getY() + 9, 7, 7);
                g.setFont (font (9.0f)); g.drawText (track.launcherActive ? "STOP / SESSION" : "STOP / ARRANGEMENT", stop.withTrimmedLeft (26), juce::Justification::centredLeft, true);
            }
            g.restoreState();
            const int sceneX = tracksWidth();
            g.setColour (raised); g.fillRect (sceneX, toolbarHeight, sceneWidth, headerHeight);
            g.setColour (ink); g.setFont (font (10.0f, true)); g.drawText ("SCENES", sceneX + 12, toolbarHeight, sceneWidth - 20, headerHeight, juce::Justification::centredLeft);
            g.saveState(); g.reduceClipRegion (sceneX, gridTop, sceneWidth, grid.getHeight());
            for (int scene = 0; scene < (int) owner.state.scenes.size(); ++scene)
            {
                const auto bounds = juce::Rectangle<int> (sceneX, sceneY (scene), sceneWidth, rowHeight);
                const bool selected = owner.state.scenes[(size_t) scene].id == owner.selectedSceneId;
                g.setColour (selected ? raised : panel); g.fillRect (bounds.reduced (1)); g.setColour (selected ? orange : muted); drawTriangle (g, sceneX + 10, bounds.getY() + 10);
                g.setColour (ink); g.setFont (font (11.0f)); g.drawText (owner.state.scenes[(size_t) scene].name, bounds.withTrimmedLeft (28).withTrimmedRight (24), juce::Justification::centredLeft, true);
                g.setColour (muted); g.drawText ("...", bounds.withTrimmedLeft (sceneWidth - 22), juce::Justification::centred);
            }
            g.restoreState();
            g.setColour (line); g.drawVerticalLine (sceneX, (float) toolbarHeight, (float) getHeight());
            if (owner.state.scenes.empty()) { g.setColour (muted); g.setFont (font (13.0f)); g.drawText ("Add a scene to begin launching clips.", grid.reduced (20), juce::Justification::centred); }
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (owner.session.isExporting()) return;
            grabKeyboardFocus();
            const auto cell = cellAt (event.getPosition());
            if (cell.first >= 0)
            {
                owner.selectLauncherSlot (cell.first, cell.second);
                if (event.mods.isPopupMenu()) { slotMenu (cell.first, cell.second); return; }
                if (event.x - slotBounds (cell.first, cell.second).getX() < 25) { owner.report (owner.session.launchSlot (cell.first, cell.second)); owner.refresh(); }
                repaint(); return;
            }
            const auto scene = sceneAt (event.getPosition());
            if (scene >= 0)
            {
                owner.selectedSceneId = owner.state.scenes[(size_t) scene].id;
                if (event.mods.isPopupMenu() || event.x > tracksWidth() + sceneWidth - 24) sceneMenu (scene);
                else if (event.x - tracksWidth() < 25) { owner.report (owner.session.launchScene (scene)); owner.refresh(); }
                repaint(); return;
            }
            const auto track = (int) ((event.x + offsetX) / columnWidth);
            if (event.x < tracksWidth() && track >= 0 && track < (int) owner.state.tracks.size())
            {
                owner.selectTrack (track);
                if (! owner.state.tracks[(size_t) track].isReturn && event.y >= gridArea().getBottom() && event.y < gridArea().getBottom() + stopHeight) { owner.report (owner.session.stopLauncherTrack (track)); owner.refresh(); }
                repaint();
            }
        }
        void mouseDoubleClick (const juce::MouseEvent& event) override
        {
            const auto cell = cellAt (event.getPosition());
            if (cell.first >= 0)
            {
                owner.selectLauncherSlot (cell.first, cell.second);
                const auto* slot = slotAt (cell.first, cell.second);
                if (owner.state.tracks[(size_t) cell.first].isMidi)
                {
                    if (slot != nullptr && ! slot->hasClip) owner.createLauncherMidiClip();
                    else if (slot != nullptr && slot->hasClip && slot->clip.isMidi) owner.openPianoRoll();
                }
                else if (slot != nullptr && ! slot->hasClip) owner.importSlotDialog (cell.first, cell.second);
                return;
            }
            const int scene = sceneAt (event.getPosition()); if (scene >= 0) owner.renameSceneDialog (scene);
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (owner.session.isExporting()) return true;
            const auto code = key.getKeyCode();
            if (code == juce::KeyPress::returnKey)
            {
                const auto scene = owner.selectedSceneIndex();
                if (scene >= 0 && (key.getModifiers().isShiftDown() || (owner.currentTrack() != nullptr && ! owner.currentTrack()->isReturn)))
                    owner.report (key.getModifiers().isShiftDown() ? owner.session.launchScene (scene) : owner.session.launchSlot (owner.selectedTrack, scene));
                owner.refresh(); return true;
            }
            if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
            {
                int track = owner.selectedTrack, scene = owner.selectedSceneIndex();
                if (code == juce::KeyPress::leftKey) --track; if (code == juce::KeyPress::rightKey) ++track;
                if (code == juce::KeyPress::upKey) --scene; if (code == juce::KeyPress::downKey) ++scene;
                if (! owner.state.tracks.empty() && ! owner.state.scenes.empty()) owner.selectLauncherSlot (juce::jlimit (0, (int) owner.state.tracks.size() - 1, track), juce::jlimit (0, (int) owner.state.scenes.size() - 1, scene));
                scrollSelectionIntoView(); return true;
            }
            return owner.handleKey (key);
        }
        void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
        {
            if (event.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY)) offsetX = juce::jmax (0.0, offsetX - (wheel.deltaX + wheel.deltaY) * 300.0);
            else offsetScene = juce::jmax (0.0, offsetScene - wheel.deltaY * 5.0);
            resized(); repaint();
        }
        void scrollSelectionIntoView()
        {
            const auto scene = owner.selectedSceneIndex();
            if (scene >= 0) { if (scene < offsetScene) offsetScene = scene; if (scene + 1 > offsetScene + gridArea().getHeight() / (double) rowHeight) offsetScene = scene + 1 - gridArea().getHeight() / (double) rowHeight; }
            if (owner.selectedTrack >= 0) { const auto x = owner.selectedTrack * columnWidth; if (x < offsetX) offsetX = x; if (x + columnWidth > offsetX + tracksWidth()) offsetX = x + columnWidth - tracksWidth(); }
            resized(); repaint();
        }
    private:
        Impl& owner;
        static constexpr int toolbarHeight = 35, headerHeight = 29, gridTop = toolbarHeight + headerHeight,
                             columnWidth = 170, rowHeight = 30, sceneWidth = 148, mixerHeight = 190, stopHeight = 26, scrollSize = 11;
        juce::ComboBox quantisation;
        juce::TextButton stopAll { "Stop All Clips" }, arrangement { "Return to Arrangement" }, addScene { "+ Scene" };
        juce::Component mixers;
        std::vector<std::unique_ptr<TrackRow>> rows;
        juce::ScrollBar horizontal { false }, vertical { true };
        double offsetX = 0, offsetScene = 0;
        int tracksWidth() const { return juce::jmax (1, getWidth() - sceneWidth - scrollSize); }
        juce::Rectangle<int> gridArea() const { return { 0, gridTop, tracksWidth(), juce::jmax (rowHeight, getHeight() - gridTop - mixerHeight - stopHeight - scrollSize) }; }
        int sceneY (int scene) const { return gridTop + (int) std::round ((scene - offsetScene) * rowHeight); }
        juce::Rectangle<int> slotBounds (int track, int scene) const { return { track * columnWidth - (int) offsetX, sceneY (scene), columnWidth, rowHeight }; }
        const Session::SlotInfo* slotAt (int track, int scene) const
        {
            if (track < 0 || track >= (int) owner.state.tracks.size()) return nullptr;
            const auto& slots = owner.state.tracks[(size_t) track].slots;
            return scene >= 0 && scene < (int) slots.size() ? &slots[(size_t) scene] : nullptr;
        }
        int sceneAt (juce::Point<int> point) const
        {
            if (point.x < tracksWidth() || point.x >= tracksWidth() + sceneWidth || point.y < gridTop || point.y >= gridArea().getBottom()) return -1;
            const int scene = (int) ((point.y - gridTop) / (double) rowHeight + offsetScene);
            return scene < (int) owner.state.scenes.size() ? scene : -1;
        }
        static void drawTriangle (juce::Graphics& g, int x, int y) { juce::Path path; path.addTriangle ((float) x, (float) y, (float) x, (float) y + 10, (float) x + 8, (float) y + 5); g.fillPath (path); }
        void scrollBarMoved (juce::ScrollBar* bar, double start) override { if (bar == &horizontal) offsetX = start; else offsetScene = start; resized(); repaint(); }
        void slotMenu (int track, int scene)
        {
            const auto* slot = slotAt (track, scene); if (slot == nullptr) return;
            juce::PopupMenu menu;
            if (slot->hasClip) { menu.addItem (1, "Launch Clip"); if (slot->clip.isMidi) menu.addItem (2, "Edit MIDI Notes"); if (slot->clip.isAudio) menu.addItem (7, "Audio clip gain..."); menu.addItem (3, "Clear Clip"); }
            else menu.addItem (owner.state.tracks[(size_t) track].isMidi ? 4 : 5, owner.state.tracks[(size_t) track].isMidi ? "Create MIDI Clip" : "Import Audio Here...");
            menu.addSeparator(); menu.addItem (6, "Stop Track Clips");
            const auto clipId=slot->hasClip?slot->clip.id:juce::String();const bool activate=slot->hasClip && !slot->clip.active;
            if(slot->hasClip){menu.addItem(8,activate?"Activate clip":"Deactivate clip");menu.addItem(9,"Rename clip...");menu.addItem(10,"Follow action...",!owner.state.recording);}
            const auto trackId = owner.state.tracks[(size_t) track].id, sceneId = owner.state.scenes[(size_t) scene].id;
            auto safe = juce::Component::SafePointer<Impl> (&owner);
            menu.showMenuAsync (juce::PopupMenu::Options(), [safe, trackId, sceneId, clipId, activate] (int choice)
            {
                if (safe == nullptr || choice == 0) return;
                if(choice==8){safe->setClipActiveFromUI(clipId,activate);return;}
                if(choice==9){safe->renameClipDialog(clipId);return;}
                if(choice==10){safe->clipFollowDialog(clipId);return;}
                const auto indices = safe->launcherIndices (trackId, sceneId); if (indices.first < 0 || indices.second < 0) return;
                safe->selectLauncherSlot (indices.first, indices.second);
                if (choice == 1) safe->report (safe->session.launchSlot (indices.first, indices.second));
                if (choice == 2) safe->openPianoRoll(); if (choice == 3) safe->clearSelectedSlot(); if (choice == 4) safe->createLauncherMidiClip();
                if (choice == 5) safe->importSlotDialog (indices.first, indices.second); if (choice == 6) safe->report (safe->session.stopLauncherTrack (indices.first));
                if (choice == 7) if (const auto* selected = safe->selectedSlot(); selected && selected->hasClip && selected->clip.isAudio)
                    safe->audioClipCommand (2, selected->clip.id);
                safe->refresh();
            });
        }
        void sceneMenu (int scene)
        {
            juce::PopupMenu menu; menu.addItem (1, "Launch Scene"); menu.addItem (2, "Rename Scene..."); menu.addItem (3, "Delete Scene");menu.addItem(4,"Duplicate Scene");
            menu.addSeparator();menu.addItem(5,"Move Scene Up",scene>0&&!owner.state.recording);menu.addItem(6,"Move Scene Down",scene+1<(int)owner.state.scenes.size()&&!owner.state.recording);
            const auto id = owner.state.scenes[(size_t) scene].id; auto safe = juce::Component::SafePointer<Impl> (&owner);
            menu.showMenuAsync (juce::PopupMenu::Options(), [safe, id] (int choice)
            {
                if (safe == nullptr) return; const auto index = safe->launcherIndices ({}, id).second; if (index < 0) return;
                if(choice==4){safe->duplicateSceneFromUI(id);return;}
                if(choice==5||choice==6){safe->moveSceneFromUI(id,choice==5?-1:1);return;}
                if (choice == 1) safe->report (safe->session.launchScene (index)); if (choice == 2) safe->renameSceneDialog (index); if (choice == 3) safe->report (safe->session.removeScene (index)); safe->refresh();
            });
        }
    };

    class PianoRoll final : public juce::Component, private juce::ScrollBar::Listener
    {
    public:
        explicit PianoRoll (Impl& o) : owner (o)
        {
            setName ("Piano roll editor");
            setWantsKeyboardFocus (true);
            for (auto* c : std::initializer_list<juce::Component*> { &snap, &noteLength, &velocity, &noteTools, &horizontal, &vertical }) addAndMakeVisible (c);
            snap.addItem ("1/4", 1); snap.addItem ("1/8", 2); snap.addItem ("1/16", 3); snap.addItem ("1/32", 4);
            snap.setName ("Note grid");
            snap.setSelectedId (3); snap.onChange = [this] { repaint(); };
            snap.setTooltip ("Note grid resolution. Hold Alt/Option while dragging to bypass snapping.");
            noteLength.addItem ("1/16", 1); noteLength.addItem ("1/8", 2); noteLength.addItem ("1/4", 3); noteLength.addItem ("1/2", 4); noteLength.addItem ("1 bar", 5);
            noteLength.setName ("New note length");
            noteLength.setSelectedId (3); noteLength.setTooltip ("Default length for newly drawn notes");
            velocity.setName ("Note velocity");
            velocity.setSliderStyle (juce::Slider::LinearHorizontal);
            velocity.setRange (0.01, 1.0, 0.01); velocity.setValue (0.8);
            velocity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 23);
            velocity.textFromValueFunction = [] (double value) { return juce::String ((int) std::round (value * 100.0)) + "%"; };
            velocity.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue() / 100.0; };
            velocity.updateText();
            velocity.setTooltip ("Set velocity for all selected notes, or for the next note you draw");
            velocity.onDragStart = [this] { endGesture(); owner.session.beginEditGesture ("Note velocity"); gesture = true; };
            velocity.onDragEnd = [this] { endGesture(); };
            velocity.onValueChange = [this]
            {
                drawVelocity = (float) velocity.getValue();
                if (const auto* clip = owner.currentMidiClip())
                {
                    auto notes = selectedNotes();
                    for (auto& note : notes) note.velocity = drawVelocity;
                    updateNotes (clip->id, notes, "Note velocity");
                }
            };
            noteTools.setName ("Note tools"); noteTools.setTooltip ("Transform selected notes, or all clip notes when none are selected. Cmd/Ctrl+U quantizes starts.");
            noteTools.onClick = [this] { showNoteMenu(); };
            horizontal.addListener (this); vertical.addListener (this);
        }
        ~PianoRoll() override
        {
            endGesture(); horizontal.removeListener (this); vertical.removeListener (this);
        }
        void sync()
        {
            const auto* clip = owner.currentMidiClip();
            auto id = clip == nullptr ? juce::String() : clip->id;
            if (id != loadedClip)
            {
                endGesture(); loadedClip = id; clearSelection(); originalNotes.clear(); marquee = false; viewBeat = 0; insertionBeat = 0;
                if (clip != nullptr && ! clip->notes.empty())
                {
                    int high = 60; for (const auto& note : clip->notes) high = juce::jmax (high, note.pitch);
                    topPitch = juce::jlimit (36, 127, high + 10);
                }
                else topPitch = 77;
                fit();
            }
            std::unordered_set<juce::String> liveIds;
            if (clip != nullptr)
            {
                insertionBeat = juce::jlimit (0.0, clip->lengthBeats, insertionBeat);
                liveIds.reserve (clip->notes.size());
                for (const auto& note : clip->notes) liveIds.insert (note.id);
            }
            std::erase_if (selectedNoteIds, [&] (const auto& noteId) { return ! liveIds.contains (noteId); });
            std::erase_if (originalNotes, [&] (const auto& note) { return ! selectedNoteIds.contains (note.id); });
            if (! selectedNoteIds.contains (selectedNoteId))
            {
                selectedNoteId.clear();
                if (clip != nullptr) for (const auto& note : clip->notes)
                    if (selectedNoteIds.contains (note.id)) { selectedNoteId = note.id; break; }
            }
            if (const auto* note = selectedNote()) { drawVelocity = note->velocity; velocity.setValue (note->velocity, juce::dontSendNotification); }
            const bool active = clip != nullptr;
            snap.setEnabled (active); noteLength.setEnabled (active); velocity.setEnabled (active); noteTools.setEnabled (active);
            updateScroll(); repaint();
        }
        void zoom (double factor)
        {
            pixelsPerBeat = juce::jlimit (12.0, 240.0, pixelsPerBeat * factor);
            updateScroll(); repaint();
        }
        void fit()
        {
            const auto* clip = owner.currentMidiClip();
            viewBeat = 0;
            pixelsPerBeat = juce::jlimit (12.0, 240.0, gridBounds().getWidth() / juce::jmax (4.0, clip != nullptr ? clip->lengthBeats : 16.0));
            updateScroll(); repaint();
        }
        void resized() override
        {
            noteTools.setBounds (698, 7, 100, 23);
            snap.setBounds (278, 7, 66, 23); noteLength.setBounds (400, 7, 76, 23); velocity.setBounds (544, 5, 142, 27);
            horizontal.setBounds (keysWidth, getHeight() - scrollSize, getWidth() - keysWidth - scrollSize, scrollSize);
            vertical.setBounds (getWidth() - scrollSize, gridTop, scrollSize, getHeight() - gridTop - scrollSize);
            updateScroll();
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (background);
            g.setColour (panel); g.fillRect (0, 0, getWidth(), headerHeight);
            g.setColour (ink); g.setFont (font (11.5f, true));
            const auto* clip = owner.currentMidiClip();
            g.drawText (clip == nullptr ? "SELECT A MIDI CLIP" : clip->name, 12, 0, 225, headerHeight, juce::Justification::centredLeft, true);
            g.setColour (muted); g.setFont (font (10.0f));
            g.drawText ("Snap", 244, 0, 32, headerHeight, juce::Justification::centredLeft);
            g.drawText ("Length", 352, 0, 45, headerHeight, juce::Justification::centredLeft);
            g.drawText ("Velocity", 488, 0, 52, headerHeight, juce::Justification::centredLeft);
            g.drawText ((selectedNoteIds.empty() ? juce::String() : juce::String ((int) selectedNoteIds.size()) + " selected  |  ")
                        + "Insert " + juce::String (insertionBeat + 1, 2), 810, 0, juce::jmax (0, getWidth() - 824), headerHeight, juce::Justification::centredRight);
            if (clip == nullptr)
            {
                g.setColour (muted); g.setFont (font (14.0f));
                const auto message = owner.showSession
                    ? "Select a MIDI slot in Session.\nDouble-click an empty MIDI slot or use + MIDI Clip to start writing notes."
                    : "Create a MIDI track to start writing notes.\nDouble-click a MIDI clip in Arrangement to edit it here.";
                g.drawFittedText (message, getLocalBounds().withTrimmedTop (headerHeight).reduced (35), juce::Justification::centred, 3);
                return;
            }
            const auto grid = gridBounds();
            g.saveState(); g.reduceClipRegion (0, gridTop, getWidth() - scrollSize, grid.getHeight());
            for (int row = 0; row <= grid.getHeight() / rowHeight; ++row)
            {
                const int pitch = topPitch - row;
                if (pitch < 0 || pitch > 127) continue;
                const int semitone = pitch % 12;
                const bool black = semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
                const int y = gridTop + row * rowHeight;
                g.setColour (black ? juce::Colour (0xff262b30) : juce::Colour (0xff30363b));
                g.fillRect (keysWidth, y, grid.getWidth(), rowHeight);
                g.setColour (black ? juce::Colour (0xff202429) : juce::Colour (0xffc1c8c8));
                g.fillRect (0, y, black ? keysWidth - 14 : keysWidth, rowHeight);
                g.setColour (line); g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) getWidth());
                if (semitone == 0)
                {
                    g.setColour (juce::Colour (0xff344149)); g.setFont (font (9.5f, true));
                    g.drawText ("C" + juce::String (pitch / 12 - 1), 7, y, 32, rowHeight, juce::Justification::centredLeft);
                }
            }
            for (double beat = std::floor (viewBeat / snapBeats()) * snapBeats(); beat <= viewBeat + grid.getWidth() / pixelsPerBeat; beat += snapBeats())
            {
                const auto x = (int) std::round (keysWidth + (beat - viewBeat) * pixelsPerBeat);
                const bool bar = std::abs (beat / 4.0 - std::round (beat / 4.0)) < 0.001;
                g.setColour (bar ? juce::Colour (0xff667078) : juce::Colour (0xff3c454c));
                g.drawVerticalLine (x, (float) gridTop, (float) grid.getBottom());
            }
            const auto clipEnd = (float) (keysWidth + (clip->lengthBeats - viewBeat) * pixelsPerBeat);
            if (clipEnd < grid.getRight()) { g.setColour (background.withAlpha (0.7f)); g.fillRect (clipEnd, (float) gridTop, grid.getRight() - clipEnd, (float) grid.getHeight()); }
            g.reduceClipRegion (grid);
            for (const auto& note : clip->notes)
            {
                auto r = noteBounds (note);
                const bool selected = selectedNoteIds.contains (note.id);
                const auto noteColour = note.active ? (selected ? orange : juce::Colour (0xff8cc4b5)).withAlpha (0.48f + note.velocity * 0.52f)
                                                    : juce::Colour (0xff526066);
                g.setColour (noteColour); g.fillRect (r);
                g.setColour (selected ? (note.active ? ink : orange) : juce::Colour (0xff203d36)); g.drawRect (r, 1.0f);
                if (r.getWidth() > 22)
                {
                    g.setColour (note.active ? juce::Colour (0xff21342f) : ink); g.setFont (font (9.0f));
                    g.drawText ((note.active ? juce::String() : juce::String("OFF / ")) + pitchName (note.pitch), r.reduced (4, 0), juce::Justification::centredLeft, true);
                }
                if (selected) { g.setColour (ink); g.fillRect (r.getRight() - 4, r.getY() + 3, 2.0f, r.getHeight() - 6); }
            }
            if (marquee)
            {
                g.setColour (orange.withAlpha (0.13f)); g.fillRect (marqueeBounds);
                g.setColour (orange); g.drawRect (marqueeBounds, 1.0f);
            }
            const auto* launched = owner.editedLauncherSlot();
            const auto beatPosition = launched != nullptr ? launched->playPositionBeats : (owner.state.position - clip->start) * owner.state.tempo / 60.0;
            const auto playhead = (float) (keysWidth + (beatPosition - viewBeat) * pixelsPerBeat);
            if (beatPosition >= 0 && (owner.selectedLauncherClipId.isEmpty() || launched != nullptr)) { g.setColour (ink); g.drawLine (playhead, (float) gridTop, playhead, (float) grid.getBottom(), 1.2f); }
            const auto insertX = (float) (keysWidth + (insertionBeat - viewBeat) * pixelsPerBeat);
            if (insertX >= grid.getX() && insertX <= grid.getRight())
            {
                const float dashes[] { 3, 4 };
                g.setColour (orange.withAlpha (0.65f));
                g.drawDashedLine ({ insertX, (float) gridTop, insertX, (float) grid.getBottom() }, dashes, 2);
            }
            g.restoreState();
            g.setColour (raised); g.fillRect (0, headerHeight, getWidth(), rulerHeight);
            g.setColour (muted); g.setFont (font (10.0f));
            for (double beat = std::floor (viewBeat / 4.0) * 4.0; beat <= viewBeat + grid.getWidth() / pixelsPerBeat; beat += 4.0)
                g.drawText (juce::String ((int) (beat / 4) + 1), (int) (keysWidth + (beat - viewBeat) * pixelsPerBeat) + 4, headerHeight, 45, rulerHeight, juce::Justification::centredLeft);
            g.setColour (line); g.drawHorizontalLine (headerHeight, 0.0f, (float) getWidth());
            if (insertX >= grid.getX() && insertX <= grid.getRight())
            {
                juce::Path marker;
                marker.addTriangle (insertX - 5, (float) gridTop - 8, insertX + 5, (float) gridTop - 8, insertX, (float) gridTop);
                g.setColour (orange); g.fillPath (marker);
            }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr || e.x < keysWidth) return;
            if (e.y >= headerHeight && e.y < gridTop)
            {
                grabKeyboardFocus(); endGesture(); clearSelection();
                insertionBeat = juce::jlimit (0.0, clip->lengthBeats, std::round (beatAt (e.x) / snapBeats()) * snapBeats());
                // Launcher playback has a clip-local clock. Its insertion cursor
                // must not seek unrelated arrangement transport state.
                if (owner.selectedLauncherClipId.isEmpty())
                    owner.session.seek (clip->start + juce::jmax (0.0, beatAt (e.x)) * 60.0 / owner.state.tempo);
                owner.refresh(); return;
            }
            if (! gridBounds().contains (e.getPosition())) return;
            grabKeyboardFocus();
            endGesture(); originalNotes.clear(); drawing = false; marquee = false;
            for (auto it = clip->notes.rbegin(); it != clip->notes.rend(); ++it)
                if (noteBounds (*it).contains (e.position))
                {
                    const auto note = *it;
                    if (e.mods.isShiftDown() && ! e.mods.isPopupMenu())
                    {
                        if (selectedNoteIds.contains (note.id))
                        {
                            selectedNoteIds.erase (note.id); sync(); return;
                        }
                        selectedNoteIds.insert (note.id);
                    }
                    else if (! selectedNoteIds.contains (note.id)) selectOnly (note.id);
                    selectedNoteId = note.id; original = note; originalNotes = selectedNotes();
                    dragOrigin = e.position; dragClipId = clip->id;
                    resizeNote = e.x >= noteBounds (note).getRight() - 8;
                    if (e.mods.isPopupMenu()) { showNoteMenu(); return; }
                    sync(); return;
                }
            if (e.mods.isShiftDown() && ! e.mods.isPopupMenu())
            {
                // Keep direct drawing on ordinary clicks. Shift-drag adds a box
                // selection; a Shift-click on empty space clears the selection.
                marquee = true; marqueeBase = selectedNoteIds; dragOrigin = e.position;
                dragClipId = clip->id; marqueeBounds = {}; repaint(); return;
            }
            clearSelection();
            if (e.mods.isPopupMenu()) { repaint(); return; }
            const auto start = std::floor (beatAt (e.x) / snapBeats()) * snapBeats();
            if (start < 0 || start >= clip->lengthBeats) return;
            const auto pitch = juce::jlimit (0, 127, topPitch - (e.y - gridTop) / rowHeight);
            const auto length = juce::jmin (defaultLength(), clip->lengthBeats - start);
            const auto clipId = clip->id;
            const auto oldNotes = clip->notes;
            owner.session.beginEditGesture ("Draw MIDI note"); gesture = true;
            const auto result = owner.session.addMidiNote (clipId, pitch, start, length, drawVelocity);
            owner.report (result); owner.refresh();
            if (result.failed()) { endGesture(); return; }
            if (const auto* updated = owner.currentMidiClip())
                for (const auto& note : updated->notes)
                    if (std::none_of (oldNotes.begin(), oldNotes.end(), [&] (const auto& old) { return old.id == note.id; })) { selectOnly (note.id); original = note; originalNotes = { note }; break; }
            dragClipId = clipId; dragOrigin = e.position; resizeNote = true; drawing = true; sync();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr || clip->id != dragClipId || e.mods.isPopupMenu() || ! e.mouseWasDraggedSinceMouseDown()) return;
            if (marquee)
            {
                marqueeBounds = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (dragOrigin.x, e.position.x), juce::jmin (dragOrigin.y, e.position.y),
                                                                           juce::jmax (dragOrigin.x, e.position.x), juce::jmax (dragOrigin.y, e.position.y)).getIntersection (gridBounds().toFloat());
                selectedNoteIds = marqueeBase;
                for (const auto& note : clip->notes) if (marqueeBounds.intersects (noteBounds (note))) selectedNoteIds.insert (note.id);
                sync(); return;
            }
            if (originalNotes.empty() || selectedNoteIds.empty()) return;
            if (! gesture) { owner.session.beginEditGesture (resizeNote ? "Resize MIDI notes" : "Move MIDI notes"); gesture = true; }
            double beatDelta = (e.x - dragOrigin.x) / pixelsPerBeat;
            if (resizeNote)
            {
                auto length = drawing ? beatAt (e.x) - original.startBeat : original.lengthBeats + beatDelta;
                if (! e.mods.isAltDown()) length = std::round (length / snapBeats()) * snapBeats();
                // Preserve valid model notes shorter than the mouse resize floor.
                // Every selected note must admit the same length delta.
                double minimum = juce::jmin (0.03125, originalNotes.front().lengthBeats) - originalNotes.front().lengthBeats;
                double maximum = clip->lengthBeats - originalNotes.front().startBeat - originalNotes.front().lengthBeats;
                for (const auto& note : originalNotes)
                {
                    minimum = juce::jmax (minimum, juce::jmin (0.03125, note.lengthBeats) - note.lengthBeats);
                    maximum = juce::jmin (maximum, clip->lengthBeats - note.startBeat - note.lengthBeats);
                }
                if (minimum > maximum || ! std::isfinite (minimum) || ! std::isfinite (maximum)) return;
                const auto lengthDelta = juce::jlimit (minimum, maximum, length - original.lengthBeats);
                auto notes = originalNotes;
                for (auto& note : notes) note.lengthBeats += lengthDelta;
                updateNotes (dragClipId, notes, "Resize MIDI notes");
            }
            else
            {
                if (! e.mods.isAltDown()) beatDelta = std::round ((original.startBeat + beatDelta) / snapBeats()) * snapBeats() - original.startBeat;
                const auto pitchDelta = -(int) std::round ((e.y - dragOrigin.y) / rowHeight);
                moveNotes (dragClipId, originalNotes, beatDelta, pitchDelta);
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            endGesture(); drawing = false; originalNotes.clear();
            if (marquee)
            {
                if (! e.mouseWasDraggedSinceMouseDown()) clearSelection();
                marquee = false; sync();
            }
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto* clip = owner.currentMidiClip();
            bool edge = false;
            if (clip != nullptr) for (const auto& note : clip->notes) if (noteBounds (note).contains (e.position) && e.x >= noteBounds (note).getRight() - 8) edge = true;
            setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& details) override
        {
            if (e.mods.isCommandDown()) { zoom (details.deltaY > 0 ? 1.1 : 1.0 / 1.1); return; }
            if (e.mods.isShiftDown() || std::abs (details.deltaX) > std::abs (details.deltaY)) viewBeat = juce::jmax (0.0, viewBeat - (details.deltaX + details.deltaY) * 8.0);
            else topPitch = juce::jlimit (0, 127, topPitch + (int) std::round (details.deltaY * 20.0));
            updateScroll(); repaint();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            // Unhandled text commands (for example Duplicate) can bubble from
            // a slider's editor. A note command must never consume text input.
            for (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr; focused = focused->getParentComponent())
                if (dynamic_cast<juce::TextEditor*> (focused) != nullptr) return false;
            if (owner.session.isExporting()) return true;
            endGesture(); originalNotes.clear(); drawing = false; marquee = false;
            const auto* clip = owner.currentMidiClip();
            const auto code = key.getKeyCode();
            if (code == juce::KeyPress::escapeKey) { clearSelection(); repaint(); return true; }
            if (clip != nullptr && code == '0' && !key.getModifiers().isAnyModifierKeyDown()) { toggleSelectedNotes(); return true; }
            if (clip != nullptr && key.getModifiers().isCommandDown())
            {
                if ((code == 'E' || code == 'e') && !key.getModifiers().isShiftDown()) { splitNotesAtMarker(); return true; }
                if (code == 'U' || code == 'u')
                { transformSelected (key.getModifiers().isShiftDown() ? Session::NoteTransform::quantizeEnds : Session::NoteTransform::quantizeStarts); return true; }
                if (code == 'C' || code == 'c') { copySelectedNotes (false); return true; }
                if (code == 'X' || code == 'x') { copySelectedNotes (true); return true; }
                if (code == 'V' || code == 'v') { insertNotes (noteClipboard, insertionBeat, false); return true; }
                if (code == 'D' || code == 'd')
                {
                    auto notes = selectedNotes();
                    if (! notes.empty())
                    {
                        const auto end = noteExtent (notes).second;
                        insertNotes (normalisedNotes (notes), std::ceil ((end - 1.0e-9) / snapBeats()) * snapBeats(), true);
                    }
                    return true;
                }
            }
            if (clip != nullptr && ! hasSelectedNote() && ! key.getModifiers().isCommandDown()
                && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
            {
                insertionBeat = juce::jlimit (0.0, clip->lengthBeats, insertionBeat + (code == juce::KeyPress::leftKey ? -snapBeats() : snapBeats()));
                revealInsertion(); repaint(); return true;
            }
            if (clip != nullptr && key.getModifiers().isCommandDown() && (code == 'A' || code == 'a'))
            {
                for (const auto& note : clip->notes) selectedNoteIds.insert (note.id);
                sync(); return true;
            }
            if (clip != nullptr && hasSelectedNote() && ! key.getModifiers().isCommandDown()
                && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
            {
                double beatDelta = 0; int pitchDelta = 0;
                if (code == juce::KeyPress::leftKey) beatDelta = -snapBeats();
                if (code == juce::KeyPress::rightKey) beatDelta = snapBeats();
                if (code == juce::KeyPress::upKey) pitchDelta = key.getModifiers().isShiftDown() ? 12 : 1;
                if (code == juce::KeyPress::downKey) pitchDelta = key.getModifiers().isShiftDown() ? -12 : -1;
                moveNotes (clip->id, selectedNotes(), beatDelta, pitchDelta); return true;
            }
            return owner.handleKey (key);
        }
        bool hasSelectedNote() const { return selectedNote() != nullptr; }
        void deleteSelectedNote()
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr) return;
            const auto clipId = clip->id;
            const auto notes = selectedNotes();
            if (notes.empty()) return;
            endGesture();
            juce::StringArray ids; for (const auto& note : notes) ids.add (note.id);
            const auto result = owner.session.removeMidiNotes (clipId, ids);
            owner.report (result);
            if (result.wasOk()) clearSelection();
            owner.refresh();
        }
    private:
        Impl& owner;
        static constexpr int headerHeight = 37, rulerHeight = 23, gridTop = headerHeight + rulerHeight, keysWidth = 55, rowHeight = 15, scrollSize = 11;
        juce::ComboBox snap, noteLength;
        juce::TextButton noteTools { "Note tools" };
        juce::Slider velocity;
        juce::ScrollBar horizontal { false }, vertical { true };
        juce::String loadedClip, selectedNoteId, dragClipId;
        Session::MidiNoteInfo original;
        std::unordered_set<juce::String> selectedNoteIds, marqueeBase;
        std::vector<Session::MidiNoteInfo> originalNotes, noteClipboard;
        juce::Point<float> dragOrigin;
        juce::Rectangle<float> marqueeBounds;
        double viewBeat = 0, pixelsPerBeat = 60, insertionBeat = 0;
        int topPitch = 77;
        float drawVelocity = 0.8f;
        bool gesture = false, resizeNote = false, drawing = false, marquee = false;
        double snapBeats() const { return std::pow (0.5, juce::jmax (0, snap.getSelectedId() - 1)); }
        double defaultLength() const { return std::pow (2.0, noteLength.getSelectedId() - 3); }
        double beatAt (float x) const { return viewBeat + (x - keysWidth) / pixelsPerBeat; }
        juce::Rectangle<int> gridBounds() const { return { keysWidth, gridTop, juce::jmax (1, getWidth() - keysWidth - scrollSize), juce::jmax (1, getHeight() - gridTop - scrollSize) }; }
        juce::Rectangle<float> noteBounds (const Session::MidiNoteInfo& note) const
        {
            return { (float) (keysWidth + (note.startBeat - viewBeat) * pixelsPerBeat), (float) (gridTop + (topPitch - note.pitch) * rowHeight + 1), (float) juce::jmax (3.0, note.lengthBeats * pixelsPerBeat), (float) rowHeight - 2 };
        }
        static juce::String pitchName (int pitch)
        {
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return juce::String (names[pitch % 12]) + juce::String (pitch / 12 - 1);
        }
        const Session::MidiNoteInfo* selectedNote() const
        {
            if (const auto* clip = owner.currentMidiClip()) for (const auto& note : clip->notes) if (note.id == selectedNoteId) return &note;
            return nullptr;
        }
        void clearSelection() { selectedNoteIds.clear(); selectedNoteId.clear(); }
        void selectOnly (const juce::String& id) { clearSelection(); selectedNoteIds.insert (id); selectedNoteId = id; }
        std::vector<Session::MidiNoteInfo> selectedNotes() const
        {
            std::vector<Session::MidiNoteInfo> notes;
            if (const auto* clip = owner.currentMidiClip())
                for (const auto& note : clip->notes) if (selectedNoteIds.contains (note.id)) notes.push_back (note);
            return notes;
        }
        static std::pair<double, double> noteExtent (const std::vector<Session::MidiNoteInfo>& notes)
        {
            double start = notes.front().startBeat, end = start + notes.front().lengthBeats;
            for (const auto& note : notes) { start = juce::jmin (start, note.startBeat); end = juce::jmax (end, note.startBeat + note.lengthBeats); }
            return { start, end };
        }
        static std::vector<Session::MidiNoteInfo> normalisedNotes (std::vector<Session::MidiNoteInfo> notes)
        {
            if (notes.empty()) return notes;
            const auto start = noteExtent (notes).first;
            for (auto& note : notes) { note.id.clear(); note.startBeat -= start; }
            return notes;
        }
        void copySelectedNotes (bool cut)
        {
            const auto* clip = owner.currentMidiClip();
            const auto notes = selectedNotes();
            if (clip == nullptr || notes.empty()) return;
            const auto start = noteExtent (notes).first;
            auto copied = normalisedNotes (notes);
            if (cut)
            {
                juce::StringArray ids; for (const auto& note : notes) ids.add (note.id);
                const auto result = owner.session.removeMidiNotes (clip->id, ids);
                if (result.failed()) { owner.report (result); owner.refresh(); return; }
                insertionBeat = start; clearSelection();
            }
            noteClipboard = std::move (copied);
            owner.report (juce::Result::ok(), juce::String ((int) noteClipboard.size())
                          + (cut ? " notes cut. " : " notes copied. ") + "Notes clipboard stays inside Halftrack; paste at the orange ruler marker.");
            owner.refresh();
        }
        void insertNotes (const std::vector<Session::MidiNoteInfo>& relativeNotes, double atBeat, bool duplicate)
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr || relativeNotes.empty()) return;
            const auto clipId = clip->id;
            auto notes = relativeNotes;
            for (auto& note : notes) note.startBeat += atBeat;
            juce::StringArray insertedIds;
            // The facade validates the entire group before creating any undo
            // action. A rejected paste must preserve selection, cursor and redo.
            const auto result = owner.session.addMidiNotes (clipId, notes, insertedIds);
            owner.report (result, juce::String ((int) insertedIds.size()) + (duplicate ? " notes duplicated." : " notes pasted."));
            if (result.wasOk())
            {
                clearSelection();
                for (const auto& id : insertedIds) selectedNoteIds.insert (id);
                selectedNoteId = insertedIds.isEmpty() ? juce::String() : insertedIds[0];
                insertionBeat = noteExtent (notes).second;
            }
            owner.refresh();
            if (result.wasOk()) { revealInsertion(); repaint(); }
        }
        void revealInsertion()
        {
            const auto visibleBeats = gridBounds().getWidth() / pixelsPerBeat;
            if (insertionBeat < viewBeat) viewBeat = insertionBeat;
            if (insertionBeat > viewBeat + visibleBeats) viewBeat = insertionBeat - visibleBeats;
            updateScroll();
        }
        void updateNotes (juce::String clipId, const std::vector<Session::MidiNoteInfo>& notes, const juce::String& name)
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr || clip->id != clipId || notes.empty()) return;
            // Snapshot values and stable IDs survive the model changes below.
            // Refresh once after the batch, and leave drag-owned transactions open.
            const bool ownGesture = ! gesture;
            if (ownGesture) owner.session.beginEditGesture (name);
            owner.report (owner.session.updateMidiNotes (clipId, notes));
            if (ownGesture) owner.session.endEditGesture();
            owner.refresh();
        }
        void moveNotes (juce::String clipId, std::vector<Session::MidiNoteInfo> notes, double beatDelta, int pitchDelta)
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr || clip->id != clipId || notes.empty()) return;
            double minimumBeatDelta = -notes.front().startBeat;
            double maximumBeatDelta = clip->lengthBeats - notes.front().startBeat - notes.front().lengthBeats;
            int minimumPitchDelta = -notes.front().pitch, maximumPitchDelta = 127 - notes.front().pitch;
            for (const auto& note : notes)
            {
                minimumBeatDelta = juce::jmax (minimumBeatDelta, -note.startBeat);
                maximumBeatDelta = juce::jmin (maximumBeatDelta, clip->lengthBeats - note.startBeat - note.lengthBeats);
                minimumPitchDelta = juce::jmax (minimumPitchDelta, -note.pitch);
                maximumPitchDelta = juce::jmin (maximumPitchDelta, 127 - note.pitch);
            }
            // Loaded clips may retain offscreen notes. An impossible interval
            // must not turn a group edit into per-note clamping or partial repair.
            if (minimumBeatDelta > maximumBeatDelta || ! std::isfinite (minimumBeatDelta) || ! std::isfinite (maximumBeatDelta)) return;
            beatDelta = juce::jlimit (minimumBeatDelta, maximumBeatDelta, beatDelta);
            pitchDelta = juce::jlimit (minimumPitchDelta, maximumPitchDelta, pitchDelta);
            for (auto& note : notes) { note.startBeat += beatDelta; note.pitch += pitchDelta; }
            updateNotes (clipId, notes, "Move MIDI notes");
        }
        void endGesture() { if (gesture) { owner.session.endEditGesture(); gesture = false; } }
        void updateScroll()
        {
            const auto grid = gridBounds(); const auto* clip = owner.currentMidiClip();
            const auto visibleBeats = grid.getWidth() / pixelsPerBeat;
            const auto totalBeats = juce::jmax (visibleBeats, clip != nullptr ? clip->lengthBeats : 16.0);
            viewBeat = juce::jlimit (0.0, juce::jmax (0.0, totalBeats - visibleBeats), viewBeat);
            horizontal.setRangeLimits (0.0, totalBeats, juce::dontSendNotification); horizontal.setCurrentRange (viewBeat, visibleBeats, juce::dontSendNotification);
            const auto rows = juce::jlimit (1, 128, grid.getHeight() / rowHeight);
            topPitch = juce::jlimit (rows - 1, 127, topPitch);
            vertical.setRangeLimits (0.0, 128.0, juce::dontSendNotification); vertical.setCurrentRange (127 - topPitch, rows, juce::dontSendNotification);
        }
        void scrollBarMoved (juce::ScrollBar* bar, double start) override
        {
            if (bar == &horizontal) viewBeat = start; else topPitch = 127 - (int) std::round (start);
            repaint();
        }
        void transformSelected (Session::NoteTransform operation)
        {
            const auto* clip = owner.currentMidiClip(); if (! clip) return;
            const auto clipId = clip->id;
            auto notes = selectedNotes(); if (notes.empty()) notes = clip->notes;
            juce::StringArray ids; for (const auto& note : notes) ids.add (note.id);
            endGesture();
            owner.report (owner.session.transformMidiNotes (clipId, ids, operation,
                operation == Session::NoteTransform::fixedLength ? defaultLength() : snapBeats()));
            owner.refresh();
        }
        void splitNotesAtMarker()
        {
            const auto* clip=owner.currentMidiClip();if(!clip)return;
            const auto id=clip->id;juce::StringArray selected;
            for(const auto& note:clip->notes)if(selectedNoteIds.empty()||selectedNoteIds.contains(note.id))selected.add(note.id);
            endGesture();juce::StringArray inserted;
            const auto result=owner.session.splitMidiNotes(id,selected,insertionBeat,inserted);owner.report(result);
            if(result.wasOk()&&!inserted.isEmpty())
            {clearSelection();for(const auto& note:inserted)selectedNoteIds.insert(note);selectedNoteId=inserted[0];}
            owner.refresh();
        }
        void toggleSelectedNotes()
        {
            const auto* clip = owner.currentMidiClip();
            if (!clip) return;
            const auto clipId = clip->id;
            const auto selected = selectedNotes();
            juce::StringArray ids;
            bool activate = false;
            for (const auto& note : selected) { ids.add (note.id); activate = activate || !note.active; }
            endGesture();
            owner.report (owner.session.setMidiNotesActive (clipId, ids, activate));
            owner.refresh();
        }
        void showNoteMenu()
        {
            juce::PopupMenu popup;
            const auto* clip = owner.currentMidiClip();
            const bool hasNotes = clip && ! clip->notes.empty();
            popup.addSectionHeader (selectedNoteIds.empty() ? "Transform all clip notes" : "Transform selected notes");
            const char* names[] { "Quantize Starts to Grid", "Quantize Ends to Grid", "Reverse Time", "Invert Pitch", "Legato",
                                  "Compress Time by Half", "Stretch Time to Double", "Set Fixed Length" };
            for (int i=0;i<8;++i) popup.addItem (10+i, names[i], hasNotes);
            popup.addSeparator();
            popup.addItem (2, "Copy Notes", hasSelectedNote()); popup.addItem (3, "Cut Notes", hasSelectedNote());
            popup.addItem (4, "Paste Notes at Marker", ! noteClipboard.empty()); popup.addItem (5, "Duplicate Notes");
            popup.addItem (7, "Split Notes at Marker", hasNotes);
            const auto selected = selectedNotes();
            const bool activate = std::any_of (selected.begin(), selected.end(), [] (const auto& note) { return !note.active; });
            popup.addItem (8, activate ? "Activate Selected Notes" : "Deactivate Selected Notes", !selected.empty());
            popup.addSeparator(); popup.addItem (1, "Delete Selected Notes");
            popup.addSeparator(); popup.addItem (6, "Notes clipboard: Halftrack only", false);
            auto safe = juce::Component::SafePointer<PianoRoll> (this);
            const auto clipId = loadedClip; const auto ids = selectedNoteIds; const auto marker=insertionBeat;
            popup.showMenuAsync (juce::PopupMenu::Options(), [safe, clipId, ids, marker] (int result)
            {
                if (safe == nullptr || safe->loadedClip != clipId || safe->selectedNoteIds != ids) return;
                if (result >= 10 && result <= 17) safe->transformSelected (static_cast<Session::NoteTransform> (result-10));
                if (result == 1) safe->deleteSelectedNote();
                if (result == 7 && safe->insertionBeat==marker) safe->splitNotesAtMarker();
                if (result == 8) safe->toggleSelectedNotes();
                const char command = result == 2 ? 'C' : result == 3 ? 'X' : result == 4 ? 'V' : result == 5 ? 'D' : 0;
                if (command != 0) safe->keyPressed (juce::KeyPress (command, juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0));
            });
        }
    };

    class SynthPanel final : public juce::Component
    {
    public:
        explicit SynthPanel (Impl& o) : owner (o)
        {
            setName ("Halftrack synthesizer");
            waveform.setName ("Oscillator waveform");
            addAndMakeVisible (waveform); waveform.addItem ("Saw", 1); waveform.addItem ("Pulse", 2);
            waveform.addItem ("Sine", 3); waveform.addItem ("Noise", 4);
            waveform.onChange = [this]
            {
                if (const auto* track = owner.currentTrack(); track != nullptr && track->isMidi)
                { auto settings = track->synth; settings.waveform = waveform.getSelectedId() - 1; owner.report (owner.session.setSynthSettings (track->id, settings)); owner.refresh(); }
            };
            for (int i = 0; i < 8; ++i)
            {
                auto& slider = controls[(size_t) i]; addAndMakeVisible (slider);
                slider.setName (names[i]); slider.setTooltip (names[i]);
                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 61, 17);
                slider.onDragStart = [this] { owner.session.beginEditGesture ("Synth parameter"); };
                slider.onDragEnd = [this] { owner.session.endEditGesture(); };
            }
            controls[0].setRange (20, 20000, 1); controls[0].setSkewFactorFromMidPoint (1800); controls[0].setTextValueSuffix (" Hz");
            controls[1].setRange (0, 1, 0.01);
            controls[2].setRange (0, 10, 0.001); controls[2].setSkewFactorFromMidPoint (0.12); controls[2].setTextValueSuffix (" s");
            controls[3].setRange (0, 10, 0.001); controls[3].setSkewFactorFromMidPoint (0.25); controls[3].setTextValueSuffix (" s");
            controls[4].setRange (0, 1, 0.01);
            controls[5].setRange (0, 20, 0.001); controls[5].setSkewFactorFromMidPoint (0.4); controls[5].setTextValueSuffix (" s");
            controls[6].setRange (0, 1, 0.01);
            controls[7].setRange (0, 100, 1); controls[7].setTextValueSuffix (" %");
            for (int i = 0; i < 8; ++i) controls[(size_t) i].onValueChange = [this, i] { changeParameter (i, (float) controls[(size_t) i].getValue()); };
        }
        void sync()
        {
            const auto* track = owner.currentTrack();
            if (track == nullptr || ! track->isMidi) { setVisible (false); return; }
            setVisible (true); waveform.setSelectedId (track->synth.waveform + 1, juce::dontSendNotification);
            const float values[] { track->synth.cutoffHz, track->synth.resonance, track->synth.attackSeconds, track->synth.decaySeconds, track->synth.sustain, track->synth.releaseSeconds, track->synth.level, track->synth.pulseWidth * 100 };
            for (int i = 0; i < 8; ++i) controls[(size_t) i].setValue (values[i], juce::dontSendNotification);
            controls[7].setEnabled (track->synth.waveform == 1);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (raised); g.setColour (orange); g.setFont (font (10.5f, true));
            g.drawText ("HALFTRACK SYNTH", 11, 2, 185, 24, juce::Justification::centredLeft);
            g.setColour (muted); g.setFont (font (9.5f));
            const auto width = (getWidth() - 16) / 8;
            for (int i = 0; i < 8; ++i) g.drawText (names[i], 8 + i * width, 30, width, 14, juce::Justification::centred);
            g.setColour (line); g.drawRect (getLocalBounds());
        }
        void resized() override
        {
            waveform.setBounds (getWidth() - 118, 5, 107, 22);
            const auto width = (getWidth() - 16) / 8;
            for (int i = 0; i < 8; ++i) controls[(size_t) i].setBounds (8 + i * width, 43, width - 2, getHeight() - 46);
        }
    private:
        Impl& owner;
        juce::ComboBox waveform;
        std::array<juce::Slider, 8> controls;
        static constexpr const char* names[] { "Cutoff", "Resonance", "Attack", "Decay", "Sustain", "Release", "Level", "Pulse width" };
        void changeParameter (int index, float value)
        {
            const auto* track = owner.currentTrack(); if (track == nullptr || ! track->isMidi) return;
            auto settings = track->synth;
            switch (index)
            {
                case 0: settings.cutoffHz = value; break; case 1: settings.resonance = value; break;
                case 2: settings.attackSeconds = value; break; case 3: settings.decaySeconds = value; break;
                case 4: settings.sustain = value; break; case 5: settings.releaseSeconds = value; break;
                case 6: settings.level = value; break; case 7: settings.pulseWidth = value / 100; break; default: return;
            }
            owner.report (owner.session.setSynthSettings (track->id, settings));
            owner.refresh();
        }
    };

    class SendsPanel final : public juce::Component
    {
        class SendRow final : public juce::Component
        {
        public:
            SendRow (Impl& o, juce::String destination) : destinationId (std::move (destination)), owner (o)
            {
                addAndMakeVisible (label); addAndMakeVisible (level);
                label.setFont (font (10.5f));
                level.setSliderStyle (juce::Slider::LinearHorizontal);
                level.setRange (-100, 0, 0.1); level.setSkewFactorFromMidPoint (-18);
                level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 55, 21);
                level.textFromValueFunction = [] (double v) { return v <= -100 ? juce::String ("Off") : juce::String (v, 1) + " dB"; };
                level.valueFromTextFunction = [] (const juce::String& v) { return v.trim().equalsIgnoreCase ("Off") ? -100.0 : v.getDoubleValue(); };
                level.setDoubleClickReturnValue (true, -100);
                level.onDragStart = [this] { gesture = true; owner.session.beginEditGesture ("Send level"); };
                level.onDragEnd = [this] { gesture = false; owner.session.endEditGesture(); };
                level.onValueChange = [this]
                {
                    const int index = owner.launcherIndices (sourceId, {}).first;
                    if (index >= 0) owner.report (owner.session.setTrackSendGain (index, destinationId, (float) level.getValue()));
                };
            }
            ~SendRow() override { if (gesture) owner.session.endEditGesture(); }
            void sync (const Session::TrackInfo& source, const Session::TrackInfo& destination, float value)
            {
                if (! gesture) sourceId = source.id;
                label.setText (destination.name + (destination.sendMode == Session::SendMode::pre ? "  |  Pre" : "  |  Post"), juce::dontSendNotification);
                level.setName ("Send to " + destination.name);
                level.setTooltip ("Send to " + destination.name + ". Off to 0 dB. Double-click to turn off.");
                if (! level.isMouseButtonDown()) level.setValue (value, juce::dontSendNotification);
            }
            void resized() override { label.setBounds (0, 0, getWidth(), 16); level.setBounds (0, 17, getWidth(), 24); }
            const juce::String destinationId;
        private:
            Impl& owner;
            juce::String sourceId;
            juce::Label label;
            juce::Slider level;
            bool gesture = false;
        };
    public:
        explicit SendsPanel (Impl& o) : owner (o)
        {
            setName ("Track sends"); addAndMakeVisible (addReturn); addAndMakeVisible (viewport);
            addReturn.setName ("Insert return track");
            addReturn.onClick = [this] { owner.addReturnTrack(); };
            viewport.setViewedComponent (&content, false); viewport.setScrollBarsShown (true, false); viewport.setScrollBarThickness (9);
        }
        ~SendsPanel() override { viewport.setViewedComponent (nullptr, false); }
        void sync()
        {
            const auto* source = owner.currentTrack();
            setVisible (source != nullptr && ! source->isReturn);
            if (! isVisible()) return;
            std::vector<const Session::TrackInfo*> returns;
            for (const auto& track : owner.state.tracks) if (track.isReturn) returns.push_back (&track);
            bool rebuild = rows.size() != returns.size();
            if (! rebuild) for (size_t i = 0; i < rows.size(); ++i) if (rows[i]->destinationId != returns[i]->id) rebuild = true;
            if (rebuild)
            {
                rows.clear();
                for (const auto* track : returns)
                {
                    auto row = std::make_unique<SendRow> (owner, track->id);
                    content.addAndMakeVisible (*row); rows.push_back (std::move (row));
                }
            }
            for (size_t i = 0; i < rows.size(); ++i)
            {
                float gainDb = -100;
                for (const auto& send : source->sends) if (send.returnTrackId == returns[i]->id) gainDb = send.gainDb;
                rows[i]->sync (*source, *returns[i], gainDb);
            }
            resized(); repaint();
        }
        void resized() override
        {
            addReturn.setBounds (getWidth() - 89, 4, 81, 23);
            viewport.setBounds (8, 32, getWidth() - 16, getHeight() - 39);
            content.setSize (viewport.getWidth() - 10, juce::jmax (viewport.getHeight(), (int) rows.size() * 44));
            for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, (int) i * 44, content.getWidth(), 42);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (raised); g.setColour (orange); g.setFont (font (10.5f, true));
            g.drawText ("SENDS", 10, 4, 64, 23, juce::Justification::centredLeft);
            if (rows.empty())
            {
                g.setColour (muted); g.setFont (font (11.0f));
                g.drawFittedText ("Add a return track to share an effect across tracks.", 13, 42, getWidth() - 26, getHeight() - 52, juce::Justification::centred, 3);
            }
            g.setColour (line); g.drawRect (getLocalBounds());
        }
    private:
        Impl& owner;
        juce::TextButton addReturn { "+ Return" };
        juce::Viewport viewport;
        juce::Component content;
        std::vector<std::unique_ptr<SendRow>> rows;
    };

    class DeviceChain final : public juce::Component
    {
    public:
        explicit DeviceChain (Impl& o) : owner (o), synthPanel (o), sendsPanel (o)
        {
            addAndMakeVisible (addButton); addAndMakeVisible (manageButton); addAndMakeVisible (synthClip); addAndMakeVisible (sendsPanel);
            synthClip.addAndMakeVisible (synthPanel); synthClip.setInterceptsMouseClicks (false, true);
            addButton.onClick = [this] { owner.addPluginMenu(); };
            manageButton.onClick = [this] { owner.showPlugins(); };
            addAndMakeVisible(masterGain);masterGain.setName("Master volume");
            masterGain.setSliderStyle(juce::Slider::LinearVertical);masterGain.setRange(-100,6,.1);masterGain.setSkewFactorFromMidPoint(-12);
            masterGain.setTextBoxStyle(juce::Slider::TextBoxBelow,false,49,20);masterGain.setDoubleClickReturnValue(true,0);
            masterGain.setTooltip("Master output level. Double-click for 0 dB. The bottom of the fader is silence.");
            masterGain.textFromValueFunction=[](double value){return value<=-100?juce::String("-inf dB"):juce::String(value,1)+" dB";};
            masterGain.valueFromTextFunction=[this](juce::String text)
            {
                text=text.trim();if(text.endsWithIgnoreCase("dB"))text=text.dropLastCharacters(2).trim();
                if(text.equalsIgnoreCase("-inf"))return -100.0;
                const auto value=text.toStdString();char* end=nullptr;const double gain=std::strtod(value.c_str(),&end);
                return !value.empty()&&end==value.c_str()+value.size()&&std::isfinite(gain)?gain:(double)owner.state.masterGainDb;
            };
            masterGain.onDragStart=[this]{owner.session.beginEditGesture("Master volume");};
            masterGain.onDragEnd=[this]{owner.session.endEditGesture();};
            masterGain.onValueChange=[this]{owner.report(owner.session.setMasterGain((float)masterGain.getValue()));owner.refresh();};
        }
        void resized() override
        {
            masterGain.setBounds(getWidth()-98,36,49,getHeight()-46);
            manageButton.setBounds (getWidth() - 212, 8, 94, 24);
            addButton.setBounds (getWidth() - 320, 8, 101, 24);
            sendsPanel.setBounds (9, 39, 232, getHeight() - 48);
            synthClip.setBounds (chainLeft(), 39, getWidth() - chainLeft() - 114, getHeight() - 48);
            synthPanel.setBounds (5 - offset, 5, 536, getHeight() - 59);
        }
        void sync()
        {
            const auto* track = owner.currentTrack();
            const auto total = track != nullptr ? (int) track->plugins.size() * 208 + (track->isMidi ? 554 : 0) : 0;
            sendsPanel.sync();
            if(!masterGain.isMouseButtonDown())masterGain.setValue(owner.state.masterGainDb,juce::dontSendNotification);
            masterGain.setEnabled(!owner.session.isExporting());
            offset = juce::jlimit (0, juce::jmax (0, total - getWidth() + chainLeft() + 141), offset);
            synthPanel.sync(); resized(); repaint();
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff282c2f));
            g.setColour (line); g.drawHorizontalLine (0, 0.0f, (float) getWidth());
            g.setColour (muted); g.setFont (font (10.0f, true));
            const auto* track = owner.currentTrack();
            auto title = track == nullptr ? juce::String ("DEVICE CHAIN") : "DEVICE CHAIN  /  " + track->name.toUpperCase();
            g.drawText (title, 14, 8, juce::jmax (150, getWidth() - 350), 24, juce::Justification::centredLeft);
            addButton.setEnabled (track != nullptr);
            g.saveState(); g.reduceClipRegion (chainLeft(), 39, getWidth() - chainLeft() - 114, getHeight() - 48);
            if (track == nullptr || (! track->isMidi && track->plugins.empty()))
            {
                g.setColour (juce::Colour (0xff343b40));
                g.drawRect (chainLeft() + 5, 44, getWidth() - chainLeft() - 129, getHeight() - 59);
                g.setColour (muted); g.setFont (font (12.0f));
                g.drawText (track == nullptr ? "Select an audio track to view its device chain." : "Add a built-in device or a scanned plug-in.",
                            chainLeft() + 16, 59, getWidth() - chainLeft() - 151, 32, juce::Justification::centred);
                g.setFont (font (10.5f));
                g.drawText ("Audio flows through devices from left to right.", chainLeft() + 16, 91, getWidth() - chainLeft() - 151, 25, juce::Justification::centred);
            }
            else
            {
                for (int i = 0; i < (int) track->plugins.size(); ++i)
                {
                    auto box = pluginBounds (i);
                    const auto& plugin = track->plugins[(size_t) i];
                    g.setColour (raised); g.fillRect (box);
                    g.setColour (plugin.enabled ? orange : muted);
                    g.fillEllipse ((float) box.getX() + 10, (float) box.getY() + 10, 7, 7);
                    g.setColour (ink); g.setFont (font (11.0f, true));
                    g.drawText (plugin.name, box.getX() + 24, box.getY() + 3, box.getWidth() - 30, 22, juce::Justification::centredLeft, true);
                    g.setColour (panel); g.fillRect (box.withTrimmedTop (28).withHeight (47));
                    g.setColour (muted); g.setFont (font (10.0f));
                    g.drawText (plugin.enabled ? "PROCESSING AUDIO" : "BYPASSED", box.withTrimmedTop (30).withHeight (40), juce::Justification::centred);
                    g.setColour (ink); g.drawText ("Open", box.getX() + 5, box.getBottom() - 29, 55, 24, juce::Justification::centred);
                    g.setColour (plugin.enabled ? ink : orange);
                    g.drawText ("Bypass", box.getX() + 62, box.getBottom() - 29, 66, 24, juce::Justification::centred);
                    g.setColour (muted); g.drawText ("Remove", box.getX() + 128, box.getBottom() - 29, 59, 24, juce::Justification::centred);
                    g.setColour (line); g.drawRect (box);
                    g.drawVerticalLine (box.getX() + 61, (float) box.getBottom() - 29, (float) box.getBottom());
                    g.drawVerticalLine (box.getX() + 128, (float) box.getBottom() - 29, (float) box.getBottom());
                    g.setColour (muted); g.drawText (">", box.getRight(), box.getY() + 43, 17, 20, juce::Justification::centred);
                }
            }
            g.restoreState();
            paintMaster (g);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto* track = owner.currentTrack();
            if (track == nullptr || e.x < chainLeft() || e.x >= getWidth() - 111) return;
            for (int i = 0; i < (int) track->plugins.size(); ++i)
            {
                const auto box = pluginBounds (i);
                if (! box.contains (e.getPosition())) continue;
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu menu;
                    menu.addItem (1, "Move left", i > 0);
                    menu.addItem (2, "Move right", i + 1 < (int) track->plugins.size());
                    const auto trackId = track->id, pluginId = track->plugins[(size_t) i].id;
                    juce::Component::SafePointer<Impl> safe (&owner);
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, trackId, pluginId] (int choice)
                    {
                        if (! safe || choice == 0) return;
                        const auto snapshot = safe->session.snapshot();
                        for (int t = 0; t < (int) snapshot.tracks.size(); ++t)
                            if (snapshot.tracks[(size_t) t].id == trackId)
                                for (int p = 0; p < (int) snapshot.tracks[(size_t) t].plugins.size(); ++p)
                                    if (snapshot.tracks[(size_t) t].plugins[(size_t) p].id == pluginId)
                                    { safe->report (safe->session.movePlugin (t, p, p + (choice == 1 ? -1 : 1))); safe->refresh(); return; }
                    });
                    return;
                }
                if (e.y < box.getBottom() - 30) { owner.report (owner.session.showPluginEditor (owner.selectedTrack, i)); return; }
                const auto x = e.x - box.getX();
                if (x < 61) owner.report (owner.session.showPluginEditor (owner.selectedTrack, i));
                else if (x < 128) owner.report (owner.session.setPluginEnabled (owner.selectedTrack, i, ! track->plugins[(size_t) i].enabled));
                else owner.report (owner.session.removePlugin (owner.selectedTrack, i));
                owner.refresh(); return;
            }
        }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& details) override
        {
            const auto* track = owner.currentTrack();
            const auto total = track != nullptr ? (int) track->plugins.size() * 208 + (track->isMidi ? 554 : 0) : 0;
            offset = juce::jlimit (0, juce::jmax (0, total - getWidth() + chainLeft() + 141), offset - (int) ((details.deltaX + details.deltaY) * 220));
            resized(); repaint();
        }
    private:
        Impl& owner;
        juce::TextButton addButton { "+ Device" }, manageButton { "Manage..." };
        SynthPanel synthPanel;
        juce::Slider masterGain;
        juce::Component synthClip;
        SendsPanel sendsPanel;
        int chainLeft() const { return sendsPanel.isVisible() ? 250 : 9; }
        int offset = 0;
        juce::Rectangle<int> pluginBounds (int i) const { const auto* track = owner.currentTrack(); return { chainLeft() + 5 + (track != nullptr && track->isMidi ? 554 : 0) + i * 208 - offset, 44, 192, getHeight() - 59 }; }
        void paintMaster (juce::Graphics& g)
        {
            auto box = getLocalBounds().removeFromRight (107).reduced (8, 8);
            g.setColour (background); g.fillRect (box);
            g.setColour (muted); g.setFont (font (9.5f, true));
            g.drawText ("MASTER", box.removeFromTop (23), juce::Justification::centred);
            auto meterArea = box.withTrimmedLeft(60).reduced(0,12);
            auto meter = [&] (float peak, int x)
            {
                auto rect = juce::Rectangle<int> (x, meterArea.getY(), 8, meterArea.getHeight() - 17);
                g.setColour (line); g.fillRect (rect);
                const auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 60.0f) / 60.0f);
                g.setColour (peak >= 1.0f ? red : green);
                g.fillRect (rect.removeFromBottom ((int) (rect.getHeight() * fraction)));
            };
            meter (owner.state.masterPeakL, meterArea.getX() + 2);
            meter (owner.state.masterPeakR, meterArea.getX() + 16);
            g.setColour (muted); g.setFont (font (9.0f));
            g.drawText ("L  R", meterArea.withTrimmedTop (meterArea.getHeight() - 15), juce::Justification::centredLeft);
        }
    };

    Session& session;
    Theme theme;
    Session::Snapshot state;
    juce::MenuBarComponent menu;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 128 };
    TransportButton playButton { TransportButton::play }, stopButton { TransportButton::stop }, recordButton { TransportButton::record };
    juce::TextButton tapButton { "Tap" }, loopButton { "Loop" }, clickButton { "Click" }, clickSettingsButton { "..." }, audioButton { "Audio Setup" }, addTrackButton { "+ Audio" }, addMidiButton { "+ MIDI" }, newMidiClipButton { "+ MIDI Clip" },
                     sessionTab { "Session" }, arrangementTab { "Arrangement" }, pianoTab { "Piano Roll" }, importButton { "Import Audio" },
                     filesButton { "Files" }, pluginsButton { "Plug-ins" }, folderButton { "Choose Folder..." }, upButton { "Up" },
                     zoomIn { "+" }, zoomOut { "-" }, fitButton { "Fit" };
    juce::Label tempo, positionLabel, projectLabel;
    juce::TextEditor browserSearch;
    juce::ListBox browserList;
    Canvas canvas;
    Ruler ruler;
    LauncherGrid launcher;
    PianoRoll pianoRoll;
    DeviceChain deviceChain;
    juce::Viewport viewport;
    juce::ScrollBar horizontalScroll { false };
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::Component::SafePointer<juce::DialogWindow>> dialogs;
    std::vector<juce::Component::SafePointer<juce::AlertWindow>> alerts;
    juce::Rectangle<int> toolbarBounds, browserBounds, sectionBounds;
    std::vector<BrowserItem> browserItems, allBrowserItems;
    std::shared_ptr<FolderScan> browserScan;
    std::shared_ptr<std::atomic<int>> activeBrowserScans = std::make_shared<std::atomic<int>> (0);
    juce::File scannedBrowserFolder;
    juce::String browserError;
    bool browserLoading = false;
    double browserScanStarted = 0;
    std::vector<juce::PluginDescription> pluginItems;
    juce::File browserFolder;
    juce::String status;
    int selectedTrack = -1, selectedClip = -1, statusFrames = 0, ticks = 0;
    bool pluginBrowser = false, showPiano = false, showSession = false;
    juce::String selectedSceneId, selectedLauncherClipId;
    double viewStart = 0, pixelsPerSecond = 35, recordingStart = 0;

    void applyMetronomeMenu (int item)
    {
        auto settings=session.snapshot().metronome;
        if(item==1) settings.accent=!settings.accent;
        else if(item==2) settings.recordingOnly=!settings.recordingOnly;
        else if(item>=10 && item<=12) settings.countInBars=item-10;
        else if(item>=21 && item<=25) settings.gain=(item-20)*.2f;
        else return;
        report(session.setMetronome(settings));refresh();
    }
    void showMetronomeMenu()
    {
        const auto settings=session.snapshot().metronome;
        juce::PopupMenu menu, countIn, level;
        menu.addItem(1,"Accent first beat",true,settings.accent);
        menu.addItem(2,"Only while recording",true,settings.recordingOnly);
        countIn.addItem(10,"None",!state.recording,settings.countInBars==0);
        countIn.addItem(11,"1 bar",!state.recording,settings.countInBars==1);
        countIn.addItem(12,"2 bars",!state.recording,settings.countInBars==2);
        for(int i=1;i<=5;++i)level.addItem(20+i,juce::String(i*20)+"%",true,std::abs(settings.gain-i*.2f)<.001f);
        menu.addSubMenu("Count-in",countIn);menu.addSubMenu("Click level",level);
        const auto safe=juce::Component::SafePointer<Impl>(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&clickSettingsButton),[safe](int item)
            {if(safe) safe->applyMetronomeMenu(item);});
    }
    void showSessionView()
    {
        showSession = true; showPiano = false;
        if (selectedTrack >= 0 && selectedSceneIndex() >= 0) selectLauncherSlot (selectedTrack, selectedSceneIndex());
        resized(); launcher.grabKeyboardFocus();
    }
    void showArrangementView()
    {
        showSession = false; showPiano = false; selectedLauncherClipId.clear(); resized();
    }
    void addSceneFromUI()
    {
        const auto result = session.addScene(); report (result); refresh();
        if (result.wasOk() && ! state.scenes.empty()) selectedSceneId = state.scenes.back().id;
        launcher.scrollSelectionIntoView();
    }
    std::pair<int, int> launcherIndices (const juce::String& trackId, const juce::String& sceneId) const
    {
        int track = -1, scene = -1;
        for (int i = 0; i < (int) state.tracks.size(); ++i) if (state.tracks[(size_t) i].id == trackId) { track = i; break; }
        for (int i = 0; i < (int) state.scenes.size(); ++i) if (state.scenes[(size_t) i].id == sceneId) { scene = i; break; }
        return { track, scene };
    }
    int selectedSceneIndex() const { return launcherIndices ({}, selectedSceneId).second; }
    const Session::SlotInfo* selectedSlot() const
    {
        const auto* track = currentTrack(); const int scene = selectedSceneIndex();
        return track != nullptr && scene >= 0 && scene < (int) track->slots.size() ? &track->slots[(size_t) scene] : nullptr;
    }
    const Session::SlotInfo* editedLauncherSlot() const
    {
        if (const auto* track = currentTrack()) for (const auto& slot : track->slots) if (slot.hasClip && slot.clip.id == selectedLauncherClipId) return &slot;
        return nullptr;
    }
    void selectLauncherSlot (int track, int scene)
    {
        if (track < 0 || track >= (int) state.tracks.size() || scene < 0 || scene >= (int) state.scenes.size()) return;
        selectTrack (track); selectedClip = -1;
        if (currentTrack()->isReturn) { selectedLauncherClipId.clear(); return; }
        selectedSceneId = state.scenes[(size_t) scene].id;
        const auto* slot = selectedSlot(); selectedLauncherClipId = slot != nullptr && slot->hasClip ? slot->clip.id : juce::String();
        pianoRoll.sync(); launcher.repaint(); deviceChain.sync();
    }
    void clearSelectedSlot()
    {
        const auto scene = selectedSceneIndex(); if (currentTrack() == nullptr || scene < 0 || currentTrack()->isReturn) return;
        report (session.clearSlot (selectedTrack, scene)); selectedLauncherClipId.clear(); refresh();
    }
    void createLauncherMidiClip()
    {
        if (currentTrack() == nullptr || ! currentTrack()->isMidi) return;
        if (state.scenes.empty()) { report (session.addScene()); refresh(); }
        if (state.scenes.empty()) return;
        if (selectedSceneIndex() < 0) selectedSceneId = state.scenes.front().id;
        if (const auto* slot = selectedSlot(); slot != nullptr && slot->hasClip)
        {
            if (slot->clip.isMidi) { selectedLauncherClipId = slot->clip.id; openPianoRoll(); }
            return;
        }
        const auto track = selectedTrack, scene = selectedSceneIndex();
        const auto result = session.addMidiClipToSlot (currentTrack()->id, scene, 4.0); report (result); refresh();
        if (result.wasOk()) { selectLauncherSlot (track, scene); openPianoRoll(); pianoRoll.fit(); }
    }
    void editLocatorDialog (const juce::String& id)
    {
        if (session.isExporting()) return;
        for (const auto& marker : session.snapshot().locators) if (marker.id==id)
        {
            auto* dialog=new juce::AlertWindow ("Edit Locator","Name this song section and set its position in seconds.",juce::MessageBoxIconType::NoIcon);
            dialog->addTextEditor ("name",marker.name,"Name");dialog->addTextEditor ("position",juce::String(marker.position,6),"Position (s)");
            dialog->addButton ("Apply",1,juce::KeyPress(juce::KeyPress::returnKey));dialog->addButton ("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->setLookAndFeel (&theme);alerts.emplace_back (dialog);
            auto safe=juce::Component::SafePointer<Impl>(this);auto safeDialog=juce::Component::SafePointer<juce::AlertWindow>(dialog);
            dialog->enterModalState (true,juce::ModalCallbackFunction::create ([safe,safeDialog,id](int result)
            {
                if(safe==nullptr||safeDialog==nullptr||result!=1)return;
                const auto text=safeDialog->getTextEditorContents("position").trim().toStdString();char* end=nullptr;
                const auto time=std::strtod(text.c_str(),&end);
                if(text.empty()||end!=text.c_str()+text.size()||!std::isfinite(time))
                    safe->report(juce::Result::fail("Enter a complete numeric locator position."));
                else safe->report(safe->session.updateLocator(id,safeDialog->getTextEditorContents("name"),time));
                safe->refresh();
            }),true);
            return;
        }
    }
    void clipFollowDialog (const juce::String& id)
    {
        if (session.isExporting()) return;
        Session::FollowSettings settings; bool found = false;
        for (const auto& track : state.tracks)
            for (const auto& slot : track.slots)
                if (slot.hasClip && slot.clip.id == id) { settings = slot.clip.follow; found = true; }
        if (! found) { report (juce::Result::fail ("Session clip not found.")); return; }
        auto* dialog = new juce::AlertWindow ("Clip Follow Action", "After the chosen beats, change playback on this track. Next and Previous stop at the ends; Cycle wraps. Empty slots are skipped.", juce::MessageBoxIconType::NoIcon, this);
        dialog->addComboBox ("action", { "Off", "Stop", "Play again", "Previous clip", "Next clip", "First clip", "Last clip", "Cycle to next clip" }, "Action");
        dialog->getComboBoxComponent ("action")->setSelectedItemIndex ((int) settings.action, juce::dontSendNotification);
        dialog->addTextEditor ("beats", juce::String (settings.afterBeats > 0 ? settings.afterBeats : 4, 6).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), "After beats");
        dialog->getTextEditor ("beats")->setEnabled (settings.action != Session::FollowAction::off);
        dialog->addButton ("Apply", 1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        dialog->setLookAndFeel (&theme); alerts.emplace_back (dialog);
        auto safe = juce::Component::SafePointer<Impl> (this);
        auto safeDialog = juce::Component::SafePointer<juce::AlertWindow> (dialog);
        dialog->getComboBoxComponent ("action")->onChange = [safeDialog]
        {
            if (safeDialog) safeDialog->getTextEditor ("beats")->setEnabled (safeDialog->getComboBoxComponent ("action")->getSelectedItemIndex() > 0);
        };
        dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, safeDialog, id] (int result)
        {
            if (! safe || ! safeDialog || result != 1) return;
            const auto action = static_cast<Session::FollowAction> (safeDialog->getComboBoxComponent ("action")->getSelectedItemIndex());
            double beats = 0;
            if (action != Session::FollowAction::off)
            {
                const auto text = safeDialog->getTextEditorContents ("beats").trim().toStdString();
                char* end = nullptr; beats = std::strtod (text.c_str(), &end);
                if (text.empty() || end != text.c_str() + text.size() || ! std::isfinite (beats))
                { safe->report (juce::Result::fail ("Enter a complete numeric beat duration.")); return; }
            }
            safe->report (safe->session.setClipFollowAction (id, { action, beats })); safe->refresh();
        }), true);
    }
    void renameClipDialog(const juce::String& id)
    {
        if(session.isExporting())return;
        juce::String name;bool found=false;
        for(const auto& track:state.tracks)
        {
            for(const auto& clip:track.clips)if(clip.id==id){name=clip.name;found=true;}
            for(const auto& slot:track.slots)if(slot.hasClip&&slot.clip.id==id){name=slot.clip.name;found=true;}
        }
        if(!found){report(juce::Result::fail("Clip not found."));return;}
        auto* dialog=new juce::AlertWindow("Rename clip","Enter a clip name.",juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name",name,"Name");
        dialog->addButton("Rename",1,juce::KeyPress(juce::KeyPress::returnKey));dialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
        dialog->setLookAndFeel(&theme);alerts.emplace_back(dialog);
        auto safe=juce::Component::SafePointer<Impl>(this);auto safeDialog=juce::Component::SafePointer<juce::AlertWindow>(dialog);
        dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe,safeDialog,id](int result)
        {
            if(!safe||!safeDialog||result!=1)return;
            safe->report(safe->session.renameClip(id,safeDialog->getTextEditorContents("name")));safe->refresh();
        }),true);
    }
    void moveSceneFromUI (const juce::String& id, int delta)
    {
        const auto stableId = id;
        refresh();
        const auto index = launcherIndices ({}, stableId).second;
        if (index < 0) return;
        report (session.moveScene (stableId, index + delta));
        refresh(); launcher.scrollSelectionIntoView();
    }
    void duplicateSceneFromUI(const juce::String& id)
    {
        juce::String created;const auto result=session.duplicateScene(id,created);report(result);refresh();
        if(result.wasOk())
        {
            const auto index=launcherIndices({},created).second;
            selectedSceneId=created;
            if(selectedTrack>=0)selectLauncherSlot(selectedTrack,index);
            launcher.scrollSelectionIntoView();refresh();
        }
    }
    void renameSceneDialog (int scene)
    {
        if (scene < 0 || scene >= (int) state.scenes.size()) return;
        const auto id = state.scenes[(size_t) scene].id;
        auto* dialog = new juce::AlertWindow ("Rename Scene", "Enter a scene name.", juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor ("name", state.scenes[(size_t) scene].name, "Name");
        dialog->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey)); dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        dialog->setLookAndFeel (&theme); alerts.emplace_back (dialog);
        auto safe = juce::Component::SafePointer<Impl> (this); auto safeDialog = juce::Component::SafePointer<juce::AlertWindow> (dialog);
        dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, safeDialog, id] (int result)
        {
            if (safe == nullptr || safeDialog == nullptr || result != 1) return;
            const int index = safe->launcherIndices ({}, id).second; if (index < 0) return;
            safe->report (safe->session.renameScene (index, safeDialog->getTextEditorContents ("name"))); safe->refresh();
        }), true);
    }
    void importSlotDialog (int track, int scene)
    {
        if (track < 0 || track >= (int) state.tracks.size() || scene < 0 || scene >= (int) state.scenes.size())
        { report (juce::Result::fail ("Select an empty audio slot in Session first.")); return; }
        if (state.tracks[(size_t) track].isMidi || state.tracks[(size_t) track].isReturn) { report (juce::Result::fail ("Choose an audio track to import audio into a Session slot.")); return; }
        if (scene < (int) state.tracks[(size_t) track].slots.size() && state.tracks[(size_t) track].slots[(size_t) scene].hasClip)
        { report (juce::Result::fail ("This slot already contains a clip. Clear it before importing a replacement.")); return; }
        const auto trackId = state.tracks[(size_t) track].id, sceneId = state.scenes[(size_t) scene].id;
        chooser = std::make_unique<juce::FileChooser> ("Import Audio into Session Slot", browserFolder, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        auto safe = juce::Component::SafePointer<Impl> (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, trackId, sceneId] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                const auto indices = safe->launcherIndices (trackId, sceneId);
                if (indices.first >= 0 && indices.second >= 0) safe->importLauncherFiles ({ fc.getResult().getFullPathName() }, indices.first, indices.second);
            });
    }
    void importLauncherFiles (const juce::StringArray& files, int track, int firstScene)
    {
        if (track < 0 || track >= (int) state.tracks.size() || firstScene < 0) return;
        if (state.tracks[(size_t) track].isMidi || state.tracks[(size_t) track].isReturn) { report (juce::Result::fail ("Drop audio into an audio track's Session slot.")); return; }
        std::vector<juce::File> audioFiles;
        for (const auto& file : files) if (isAudio (juce::File (file))) audioFiles.emplace_back (file);
        std::sort (audioFiles.begin(), audioFiles.end(), [] (const auto& a, const auto& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        if (audioFiles.empty()) return;
        for (int i = 0; i < (int) audioFiles.size(); ++i)
        {
            const int scene = firstScene + i;
            if (scene < (int) state.tracks[(size_t) track].slots.size() && state.tracks[(size_t) track].slots[(size_t) scene].hasClip)
            { report (juce::Result::fail ("A destination slot already contains a clip. Clear it before importing a replacement.")); return; }
        }
        session.beginEditGesture ("Import Session clips");
        bool imported = false;
        for (int i = 0; i < (int) audioFiles.size(); ++i)
        {
            const int scene = firstScene + i;
            while (scene >= (int) state.scenes.size())
            {
                const auto result = session.addScene(); report (result); refresh();
                if (result.failed()) { session.endEditGesture(); return; }
            }
            const auto result = session.importAudioToSlot (audioFiles[(size_t) i], track, scene); report (result); refresh();
            if (result.failed()) break;
            imported = true;
        }
        session.endEditGesture();
        if (imported)
        {
            showSession = true; showPiano = false; selectLauncherSlot (track, firstScene);
            browserFolder = audioFiles.front().getParentDirectory(); pluginBrowser = false; updateBrowser(); resized(); launcher.scrollSelectionIntoView();
        }
    }

    const Session::TrackInfo* currentTrack() const
    {
        return selectedTrack >= 0 && selectedTrack < (int) state.tracks.size() ? &state.tracks[(size_t) selectedTrack] : nullptr;
    }
    const Session::ClipInfo* currentMidiClip() const
    {
        const auto* track = currentTrack();
        if (track == nullptr) return nullptr;
        if (selectedLauncherClipId.isNotEmpty())
        {
            for (const auto& slot : track->slots) if (slot.hasClip && slot.clip.id == selectedLauncherClipId) return slot.clip.isMidi ? &slot.clip : nullptr;
            return nullptr;
        }
        if (selectedClip < 0 || selectedClip >= (int) track->clips.size()) return nullptr;
        const auto& clip = track->clips[(size_t) selectedClip];
        return clip.isMidi ? &clip : nullptr;
    }
    void openPianoRoll()
    {
        const auto* track = currentTrack();
        if (showSession)
        {
            const auto* slot = selectedSlot();
            selectedClip = -1;
            selectedLauncherClipId = slot != nullptr && slot->hasClip && slot->clip.isMidi ? slot->clip.id : juce::String();
        }
        if (track != nullptr && track->isMidi && currentMidiClip() == nullptr && ! showSession)
            for (int i = 0; i < (int) track->clips.size(); ++i)
                if (track->clips[(size_t) i].isMidi) { selectedClip = i; break; }
        showPiano = true;
        pianoRoll.sync(); resized(); pianoRoll.grabKeyboardFocus();
    }
    void createMidiClip (double seconds)
    {
        const auto* track = currentTrack();
        if (track == nullptr || ! track->isMidi) return;
        const auto trackId = track->id;
        const auto previousIds = track->clips;
        const auto startBeat = std::floor (seconds * state.tempo / 60.0 / 4.0) * 4.0;
        const auto result = session.addMidiClip (trackId, startBeat, 16.0);
        report (result); refresh();
        if (result.failed()) return;
        const auto* updated = currentTrack();
        if (updated != nullptr)
            for (int i = 0; i < (int) updated->clips.size(); ++i)
                if (std::none_of (previousIds.begin(), previousIds.end(), [&] (const auto& old) { return old.id == updated->clips[(size_t) i].id; })) { selectedClip = i; break; }
        openPianoRoll(); pianoRoll.fit();
    }
    void addMidiTrack()
    {
        session.beginEditGesture ("Create MIDI track");
        const auto result = session.addMidiTrack(); report (result); refresh();
        if (result.wasOk()) { selectTrack ((int) state.tracks.size() - 1); if (showSession) createLauncherMidiClip(); else createMidiClip (state.position); }
        session.endEditGesture();
    }
    double gridSeconds() const
    {
        const auto beat = 60.0 / state.tempo;
        if (beat * pixelsPerSecond >= 30) return beat / 4;
        if (beat * pixelsPerSecond >= 15) return beat;
        if (beat * pixelsPerSecond >= 6) return beat * 2;
        return beat * 4;
    }
    void duplicateTrackFromUI (const juce::String& trackId)
    {
        juce::String created; const auto result = session.duplicateTrack (trackId, created); report (result); refresh();
        if (result.wasOk())
        {
            const auto index = launcherIndices (created, {}).first;
            selectTrack (index); selectedClip = -1; selectedLauncherClipId.clear(); showPiano = false; resized();
        }
    }
    void reorderTrack (const juce::String& trackId, int direction)
    {
        const auto current = session.snapshot();
        for (int i = 0; i < (int)current.tracks.size(); ++i)
            if (current.tracks[(size_t)i].id == trackId)
            { report (session.moveTrack (trackId, i+direction)); refresh(); return; }
    }
    void showTrackOrderMenu (const juce::String& id, bool horizontal)
    {
        const auto indices = launcherIndices (id, {});
        juce::PopupMenu menu;
        menu.addItem (1, horizontal ? "Move track left" : "Move track up", indices.first > 0);
        menu.addItem (2, horizontal ? "Move track right" : "Move track down", indices.first >= 0 && indices.first+1 < (int)state.tracks.size());
        menu.addSeparator(); menu.addItem (3, "Duplicate track", indices.first >= 0 && !state.tracks[(size_t)indices.first].isReturn);
        menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<Impl> (this), id] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1 || result == 2) safe->reorderTrack (id, result == 1 ? -1 : 1);
            else if (result == 3) safe->duplicateTrackFromUI (id);
        });
    }
    void selectTrack (int track)
    {
        if (selectedTrack != track) { selectedClip = -1; selectedLauncherClipId.clear(); }
        selectedTrack = track;
        importButton.setEnabled (currentTrack() == nullptr || ! currentTrack()->isReturn);
        newMidiClipButton.setEnabled (currentTrack() != nullptr && currentTrack()->isMidi && ! currentTrack()->isReturn);
        canvas.repaint(); deviceChain.sync();
        for (int i = 0; i < canvas.getNumChildComponents(); ++i) canvas.getChildComponent (i)->repaint();
    }
    void refresh()
    {
        const auto previous = state;
        const auto selectedId = currentTrack() != nullptr ? currentTrack()->id : juce::String();
        state = session.snapshot();
        if (selectedId.isNotEmpty())
        {
            const int liveIndex = launcherIndices (selectedId, {}).first;
            if (liveIndex >= 0) selectedTrack = liveIndex;
            else { selectedClip = -1; selectedLauncherClipId.clear(); }
        }
        if (state.recording && ! previous.recording) recordingStart = state.position;
        if (selectedTrack >= (int) state.tracks.size()) { selectedTrack = (int) state.tracks.size() - 1; selectedClip = -1; }
        if (selectedTrack < 0 && ! state.tracks.empty()) selectedTrack = 0;
        if (const auto* track = currentTrack(); track == nullptr || selectedClip >= (int) track->clips.size()) selectedClip = -1;
        playButton.setToggleState (state.playing, juce::dontSendNotification);
        recordButton.setToggleState (state.recording, juce::dontSendNotification);
        loopButton.setToggleState (state.looping, juce::dontSendNotification);
        clickButton.setToggleState(state.metronome.enabled,juce::dontSendNotification);
        if (! tempo.isBeingEdited()) tempo.setText (juce::String (state.tempo, 2), juce::dontSendNotification);
        const auto beatPosition = juce::jmax (0.0, state.position) * state.tempo / 60.0;
        const auto bar = (int) (beatPosition / 4) + 1;
        const auto beat = (int) beatPosition % 4 + 1;
        const auto sixteenth = (int) (beatPosition * 4) % 4 + 1;
        positionLabel.setText (state.countInBeatsRemaining>0
            ? "Count in " + juce::String((int)std::ceil(state.countInBeatsRemaining))
            : juce::String (bar).paddedLeft ('0', 3) + " . " + juce::String (beat) + " . " + juce::String (sixteenth), juce::dontSendNotification);
        projectLabel.setText (state.name + (state.dirty ? "  *" : ""), juce::dontSendNotification);
        if (state.tracks.size() != previous.tracks.size()) resized();
        canvas.syncRows();
        if (selectedSceneIndex() < 0) selectedSceneId = state.scenes.empty() ? juce::String() : state.scenes.front().id;
        launcher.sync();
        pianoRoll.sync();
        newMidiClipButton.setEnabled (currentTrack() != nullptr && currentTrack()->isMidi && ! currentTrack()->isReturn);
        importButton.setEnabled (currentTrack() == nullptr || ! currentTrack()->isReturn);
        ruler.sync(); deviceChain.sync(); repaint();
        updateScroll();
    }
    void timerCallback() override
    {
        pollBrowserScan();
        if (statusFrames > 0 && --statusFrames == 0) status.clear();
        ++ticks;
        refresh();
    }
    void changeListenerCallback (juce::ChangeBroadcaster* source) override
    {
        if (source == &session.knownPlugins()) updateBrowser();
        refresh();
    }
    void report (const juce::Result& result, juce::String success = {})
    {
        if (result.failed())
        {
            status = result.getErrorMessage(); statusFrames = 24 * 12;
           #if HALFTRACK_TESTS_ENABLED
            if (! captureTestErrors)
           #endif
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Halftrack", result.getErrorMessage());
        }
        else if (success.isNotEmpty()) { status = std::move (success); statusFrames = 24 * 7; }
        repaint();
    }
    void togglePlay() { if (state.playing) session.stop(); else report (session.play()); refresh(); }
    void addTrack()
    {
        showPiano = false;
        report (session.addAudioTrack()); refresh(); selectTrack ((int) state.tracks.size() - 1);
        viewport.setViewPosition (0, juce::jmax (0, selectedTrack * trackHeight - viewport.getHeight() + trackHeight));
    }
    void addReturnTrack()
    {
        const auto result = session.addReturnTrack(); report (result); refresh();
        if (result.wasOk()) { selectTrack ((int) state.tracks.size() - 1); showPiano = false; resized(); }
    }
    void setClipActiveFromUI(const juce::String& id,bool active)
    {report(session.setClipActive(id,active));refresh();}
    void deleteSelectedClip()
    {
        if (selectedTrack >= 0 && selectedClip >= 0) { report (session.removeClip (selectedTrack, selectedClip)); selectedClip = -1; refresh(); }
    }
    void resetView()
    {
        selectedTrack = -1; selectedClip = -1; selectedLauncherClipId.clear(); selectedSceneId.clear(); viewStart = 0; showPiano = false; resized();
        refresh(); fitTimeline();
    }
    void zoom (double factor)
    {
        const auto center = viewStart + canvas.timelineWidth() / pixelsPerSecond * 0.5;
        pixelsPerSecond = juce::jlimit (3.0, 650.0, pixelsPerSecond * factor);
        viewStart = juce::jmax (0.0, center - canvas.timelineWidth() / pixelsPerSecond * 0.5);
        updateScroll(); canvas.repaint(); ruler.repaint();
    }
    void fitTimeline()
    {
        viewStart = 0;
        pixelsPerSecond = juce::jlimit (3.0, 650.0, canvas.timelineWidth() / juce::jmax (8.0 * 240.0 / state.tempo, state.duration * 1.08));
        updateScroll(); canvas.repaint(); ruler.repaint();
    }
    void updateScroll()
    {
        const auto visible = juce::jmax (1.0, canvas.timelineWidth() / pixelsPerSecond);
        const auto total = juce::jmax (visible, state.duration + 8 * 240.0 / state.tempo, viewStart + visible);
        horizontalScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
        horizontalScroll.setCurrentRange (viewStart, visible, juce::dontSendNotification);
        horizontalScroll.setSingleStepSize (60.0 / state.tempo);
    }
    void scrollBarMoved (juce::ScrollBar*, double start) override { viewStart = start; canvas.repaint(); ruler.repaint(); }

    juce::StringArray getMenuBarNames() override { return { "File", "Edit", "Track", "View", "Session" }; }
    juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
    {
        juce::PopupMenu popup;
        if (session.isExporting()) { popup.addItem (99, "Export in progress...", false); return popup; }
        if (index == 0)
        {
            popup.addItem (1, "New Project"); popup.addItem (2, "Open Project..."); popup.addSeparator();
            popup.addItem (3, "Save Project"); popup.addItem (4, "Save Project As..."); popup.addSeparator();
            popup.addItem (5, "Import Audio...", currentTrack() == nullptr || ! currentTrack()->isReturn); popup.addItem (6, "Export Arrangement Audio...", ! state.tracks.empty()); popup.addSeparator();
            popup.addItem (9, "Import MIDI File..."); popup.addItem (10, "Export Selected MIDI Clip...", currentMidiClip() != nullptr); popup.addSeparator();
            popup.addItem (7, "Audio Setup..."); popup.addItem (8, "Manage Plug-ins...");
        }
        else if (index == 1)
        {
            popup.addItem (20, "Undo", session.canUndo()); popup.addItem (21, "Redo", session.canRedo()); popup.addSeparator();
            juce::PopupMenu recordingGrid;
            const juce::StringArray grids {"Off","1/4 note","1/8 note","1/8 triplet","1/16 note","1/16 triplet","1/32 note"};
            for(int i=0;i<grids.size();++i) recordingGrid.addItem(60+i,grids[i],!state.recording,static_cast<int>(state.recordQuantisation)==i);
            popup.addSubMenu("Recording quantization",recordingGrid);popup.addSeparator();
            popup.addItem (22, showPiano ? "Delete Selected Notes" : showSession ? "Clear Selected Slot" : "Delete Selected Clip", showPiano ? pianoRoll.hasSelectedNote() : showSession ? selectedSlot() != nullptr && selectedSlot()->hasClip : selectedClip >= 0);
        }
        else if (index == 2)
        {
            popup.addItem (30, "Insert Audio Track"); popup.addItem (33, "Insert MIDI Track"); popup.addItem (35, "Insert Return Track"); popup.addItem (34, "Insert MIDI Clip", currentTrack() != nullptr && currentTrack()->isMidi); popup.addItem (31, "Delete Selected Track", currentTrack() != nullptr);
            popup.addSeparator();
            popup.addItem (38, "Duplicate Selected Track", currentTrack() != nullptr && !currentTrack()->isReturn);
            popup.addItem (36, "Move Selected Track Earlier", selectedTrack > 0);
            popup.addItem (37, "Move Selected Track Later", selectedTrack >= 0 && selectedTrack+1 < (int)state.tracks.size());
            popup.addSeparator(); popup.addItem (32, "Add Plug-in...", currentTrack() != nullptr);
        }
        else if (index == 3)
        {
            popup.addItem (45, "Session"); popup.addItem (43, "Arrangement"); popup.addItem (44, "Piano Roll"); popup.addSeparator();
            const bool canZoom = ! showSession || showPiano;
            popup.addItem (40, showPiano ? "Fit Piano Roll" : "Fit Arrangement", canZoom); popup.addItem (41, "Zoom In", canZoom); popup.addItem (42, "Zoom Out", canZoom);
        }
        else
        {
            const bool sceneSelected = selectedSceneIndex() >= 0;
            popup.addItem (50, "Insert Scene"); popup.addItem (51, "Rename Selected Scene...", sceneSelected); popup.addItem (52, "Delete Selected Scene", sceneSelected);
            popup.addItem(56,"Duplicate Selected Scene",sceneSelected&&!state.recording&&state.scenes.size()<256);
            popup.addItem(57,"Move Selected Scene Up",selectedSceneIndex()>0&&!state.recording);
            popup.addItem(58,"Move Selected Scene Down",sceneSelected&&selectedSceneIndex()+1<(int)state.scenes.size()&&!state.recording);
            popup.addItem (59, "Selected Clip Follow Action...", selectedSlot() && selectedSlot()->hasClip && !state.recording);
            popup.addSeparator(); popup.addItem (53, "Launch Selected Scene", sceneSelected); popup.addItem (54, "Stop All Session Clips"); popup.addItem (55, "Return to Arrangement");
        }
        return popup;
    }
    void menuItemSelected (int id, int) override
    {
        if (session.isExporting()) return;
        if(id>=60 && id<=66) { report(session.setRecordQuantisation(static_cast<Session::RecordQuantisation>(id-60)));refresh();return; }
        switch (id)
        {
            case 1: ensureSaved ([safe = juce::Component::SafePointer<Impl> (this)] { if (safe != nullptr) { safe->report (safe->session.newProject()); safe->resetView(); } }); break;
            case 2: openDialog(); break;
            case 3: saveProject (false); break;
            case 4: saveProject (true); break;
            case 5: importDialog(); break;
            case 6: exportDialog(); break;
            case 7: showAudioSettings(); break;
            case 8: showPlugins(); break;
            case 9: importMidiDialog(); break;
            case 10: exportMidiDialog(); break;
            case 20: session.undo(); break;
            case 21: session.redo(); break;
            case 22: if (showPiano) pianoRoll.deleteSelectedNote(); else if (showSession) clearSelectedSlot(); else deleteSelectedClip(); break;
            case 30: showPiano = false; addTrack(); resized(); break;
            case 33: addMidiTrack(); break;
            case 35: addReturnTrack(); break;
            case 34: if (showSession) createLauncherMidiClip(); else createMidiClip (state.position); break;
            case 31: if (selectedTrack >= 0) { session.closePluginEditors(); report (session.removeTrack (selectedTrack)); selectedClip = -1; } break;
            case 32: addPluginMenu(); break;
            case 36: case 37: if (const auto* track = currentTrack()) reorderTrack (track->id, id == 36 ? -1 : 1); break;
            case 38: if (const auto* track = currentTrack()) duplicateTrackFromUI (track->id); break;
            case 40: if (showPiano) pianoRoll.fit(); else fitTimeline(); break;
            case 41: if (showPiano) pianoRoll.zoom (1.3); else zoom (1.3); break;
            case 42: if (showPiano) pianoRoll.zoom (1.0 / 1.3); else zoom (1.0 / 1.3); break;
            case 43: showArrangementView(); break;
            case 45: showSessionView(); break;
            case 59: if (selectedSlot() && selectedSlot()->hasClip) clipFollowDialog (selectedSlot()->clip.id); break;
            case 50: addSceneFromUI(); break;
            case 51: renameSceneDialog (selectedSceneIndex()); break;
            case 56: if(selectedSceneIndex()>=0)duplicateSceneFromUI(selectedSceneId); break;
            case 57: case 58: moveSceneFromUI(selectedSceneId,id==57?-1:1); break;
            case 52: report (session.removeScene (selectedSceneIndex())); break;
            case 53: report (session.launchScene (selectedSceneIndex())); break;
            case 54: report (session.stopAllLauncherClips()); break;
            case 55: report (session.returnToArrangement()); break;
            case 44: openPianoRoll(); break;
            default: break;
        }
        refresh();
    }

    void saveProject (bool saveAs, std::function<void()> onSaved = {})
    {
        if (! saveAs && state.projectFile != juce::File())
        {
            auto result = session.saveProject (state.projectFile);
            report (result, "Project and media saved."); refresh();
            if (result.wasOk() && onSaved) onSaved();
            return;
        }
        auto file = state.projectFile != juce::File() ? state.projectFile : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (state.name + ".halftrack");
        chooser = std::make_unique<juce::FileChooser> ("Save Halftrack Project", file, "*.halftrack");
        auto safe = juce::Component::SafePointer<Impl> (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, onSaved] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                auto result = safe->session.saveProject (fc.getResult().withFileExtension ("halftrack"));
                safe->report (result, "Project and media saved."); safe->refresh();
                if (result.wasOk() && onSaved) onSaved();
            });
    }
    void openDialog()
    {
        auto safe = juce::Component::SafePointer<Impl> (this);
        ensureSaved ([safe]
        {
            if (safe == nullptr) return;
            safe->chooser = std::make_unique<juce::FileChooser> ("Open Halftrack Project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.halftrack");
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe] (const juce::FileChooser& fc)
                {
                    if (safe == nullptr || fc.getResult() == juce::File()) return;
                    safe->session.closePluginEditors();
                    safe->report (safe->session.loadProject (fc.getResult())); safe->resetView();
                });
        });
    }
    void importMidiSelection (const juce::File& file,double startBeat)
    {
        std::vector<juce::String> ids;const auto result=session.importMidiFile(file,startBeat,ids);
        report(result,"Imported "+juce::String((int)ids.size())+" MIDI tracks at the project tempo.");refresh();
        if(result.failed()||ids.empty())return;
        for(int t=0;t<(int)state.tracks.size();++t)for(int c=0;c<(int)state.tracks[(size_t)t].clips.size();++c)
            if(state.tracks[(size_t)t].clips[(size_t)c].id==ids.front())
            {
                selectTrack(t);selectedClip=c;selectedLauncherClipId.clear();showSession=false;showPiano=true;
                resized();pianoRoll.fit();refresh();return;
            }
    }
    void importMidiDialog()
    {
        const auto current=session.snapshot();const auto startBeat=current.position*current.tempo/60;
        chooser=std::make_unique<juce::FileChooser>("Import MIDI File",browserFolder,"*.mid;*.midi");
        auto safe=juce::Component::SafePointer<Impl>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
            [safe,startBeat](const juce::FileChooser& fc)
            {if(safe!=nullptr&&fc.getResult()!=juce::File())safe->importMidiSelection(fc.getResult(),startBeat);});
    }
    void exportMidiSelection (const juce::String& id,const juce::File& file)
    {report(session.exportMidiClip(id,file),"MIDI clip exported.");refresh();}
    void exportMidiDialog()
    {
        const auto* clip=currentMidiClip();if(!clip)return;
        const auto id=clip->id;
        const auto destination=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(juce::File::createLegalFileName(clip->name)+".mid");
        chooser=std::make_unique<juce::FileChooser>("Export Selected MIDI Clip",destination,"*.mid");
        auto safe=juce::Component::SafePointer<Impl>(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,
            [safe,id](const juce::FileChooser& fc)
            {
                // Honor the exact destination whose overwrite choice the chooser presented.
                if(safe!=nullptr&&fc.getResult()!=juce::File())safe->exportMidiSelection(id,fc.getResult());
            });
    }
    void importDialog()
    {
        if (currentTrack() != nullptr && currentTrack()->isReturn) return;
        if (showSession) { importSlotDialog (selectedTrack, selectedSceneIndex()); return; }
        chooser = std::make_unique<juce::FileChooser> ("Import Audio", browserFolder, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        auto safe = juce::Component::SafePointer<Impl> (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
            [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                juce::StringArray files;
                for (const auto& file : fc.getResults()) files.add (file.getFullPathName());
                safe->importFiles (files, safe->selectedTrack, safe->state.position);
            });
    }
    void importFiles (const juce::StringArray& files, int track, double at)
    {
        if (track >= 0 && track < (int) state.tracks.size() && state.tracks[(size_t) track].isReturn)
        { report (juce::Result::fail ("Return tracks receive audio from sends. Import clips into an audio track.")); return; }
        std::vector<juce::File> audioFiles;
        for (const auto& path : files)
        {
            juce::File file (path);
            if (isAudio (file)) audioFiles.push_back (file);
        }
        std::sort (audioFiles.begin(), audioFiles.end(), [] (const auto& a, const auto& b)
        {
            const auto order = a.getFileName().compareNatural (b.getFileName());
            return order != 0 ? order < 0 : a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
        });
        if (audioFiles.empty()) return;
        auto before = session.snapshot();
        const bool validSelection = track >= 0 && track < (int) before.tracks.size() && ! before.tracks[(size_t) track].isMidi;
        int nextTrack = validSelection ? track : 0;
        int imported = 0, firstImportedTrack = -1;
        session.beginEditGesture ("Import audio");
        for (const auto& file : audioFiles)
        {
            before = session.snapshot();
            int target = -1;
            if (imported == 0 && validSelection) target = track;
            else
                for (int i = nextTrack; i < (int) before.tracks.size(); ++i)
                    if (! before.tracks[(size_t) i].isMidi && ! before.tracks[(size_t) i].isReturn && before.tracks[(size_t) i].clips.empty()) { target = i; break; }
            const bool reusedEmptyTrack = target >= 0 && before.tracks[(size_t) target].clips.empty();
            const auto result = session.importAudio (file, target, at);
            report (result);
            if (result.failed()) continue;
            if (target < 0) target = (int) session.snapshot().tracks.size() - 1;
            if (reusedEmptyTrack) report (session.renameTrack (target, file.getFileNameWithoutExtension()));
            if (firstImportedTrack < 0)
            {
                firstImportedTrack = target;
                browserFolder = file.getParentDirectory();
                scannedBrowserFolder = juce::File();
            }
            nextTrack = target + 1;
            ++imported;
        }
        session.endEditGesture();
        refresh();
        if (imported > 0)
        {
            selectTrack (firstImportedTrack);
            showPiano = false; resized();
            updateBrowser();
            status = juce::String (imported) + (imported == 1 ? " audio file imported." : " audio files imported.");
            statusFrames = 24 * 6;
        }
    }
    static juce::String exportExtension (Session::ExportFormat format)
    {
        switch (format)
        {
            case Session::ExportFormat::wav: return "wav";
            case Session::ExportFormat::aiff: return "aiff";
            case Session::ExportFormat::flac: return "flac";
        }
        return {};
    }
    static Session::AudioExportSettings readAudioExportSettings (juce::AlertWindow& dialog)
    {
        constexpr double rates[] { 44100, 48000, 88200, 96000, 192000 };
        constexpr int depths[] { 16, 24, 32 };
        constexpr double tails[] { 0, 1, 2, 5, 10, 30, 60 };
        const auto format = static_cast<Session::ExportFormat> (juce::jlimit (0, 2, dialog.getComboBoxComponent ("format")->getSelectedItemIndex()));
        const auto rate = rates[juce::jlimit (0, 4, dialog.getComboBoxComponent ("sample-rate")->getSelectedItemIndex())];
        const auto depth = depths[juce::jlimit (0, 2, dialog.getComboBoxComponent ("bit-depth")->getSelectedItemIndex())];
        const auto tail = tails[juce::jlimit (0, 6, dialog.getComboBoxComponent ("effect-tail")->getSelectedItemIndex())];
        Session::AudioExportSettings settings { format, rate, depth, tail };
        settings.mono = dialog.getComboBoxComponent ("channels")->getSelectedItemIndex() == 1;
        const auto normalization = dialog.getComboBoxComponent ("normalization")->getSelectedItemIndex();
        settings.normalizePeak = normalization > 0;
        settings.peakCeilingDb = normalization == 2 ? -3.0f : normalization == 3 ? 0.0f : -1.0f;
        return settings;
    }
    void exportDialog()
    {
        auto* alert = new juce::AlertWindow ("Export Audio", "Render the full arrangement through its track and master effects to an audio file. Export options leave the project mix unchanged.", juce::MessageBoxIconType::NoIcon, this);
        alert->setLookAndFeel (&theme);
        alert->addComboBox ("format", { "WAV", "AIFF", "FLAC (lossless)" }, "File format");
        alert->addComboBox ("channels", { "Stereo", "Mono (combine both channels)" }, "Output channels");
        alert->addComboBox ("normalization", { "Off", "Peak ceiling: -1 dB", "Peak ceiling: -3 dB", "Peak ceiling: 0 dB" }, "Normalize exported peak");
        alert->addComboBox ("sample-rate", { "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz", "192 kHz" }, "Sample rate");
        alert->getComboBoxComponent ("sample-rate")->setSelectedItemIndex (1);
        alert->addComboBox ("bit-depth", { "16-bit", "24-bit", "32-bit" }, "Bit depth");
        alert->getComboBoxComponent ("bit-depth")->setSelectedItemIndex (1);
        alert->addComboBox ("effect-tail", { "0 seconds (none)", "1 second", "2 seconds", "5 seconds", "10 seconds", "30 seconds", "60 seconds" }, "Extra time for reverb / delay tails");
        alert->getComboBoxComponent ("effect-tail")->setSelectedItemIndex (2);
        alert->addButton ("Choose Destination...", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        alerts.emplace_back (alert);
        auto safe = juce::Component::SafePointer<Impl> (this);
        auto dialog = juce::Component::SafePointer<juce::AlertWindow> (alert);
        alert->getComboBoxComponent ("format")->onChange = [dialog]
        {
            if (dialog == nullptr) return;
            const bool canUse32 = dialog->getComboBoxComponent ("format")->getSelectedItemIndex() == 0;
            auto* depth = dialog->getComboBoxComponent ("bit-depth");
            depth->setItemEnabled (3, canUse32);
            if (! canUse32 && depth->getSelectedId() == 3) depth->setSelectedId (2);
        };
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, dialog] (int result)
        {
            if (result != 1 || safe == nullptr || dialog == nullptr) return;
            const auto settings = readAudioExportSettings (*dialog);
            juce::MessageManager::callAsync ([safe, settings]
            {
                if (safe != nullptr) safe->chooseExportDestination (settings);
            });
        }), true);
    }
    void performAudioExport (const juce::File& destination, const Session::AudioExportSettings& settings)
    {
        auto safe = juce::Component::SafePointer<Impl> (this);
        const auto result = session.exportAudio (destination, settings);
        if (safe == nullptr) return;
        if (result.failed() && result.getErrorMessage() == "Export cancelled.")
        {
            safe->status = "Export cancelled.";
            safe->statusFrames = 24 * 5;
        }
        else safe->report (result, exportExtension (settings.format).toUpperCase() + (settings.mono ? " mono" : " stereo") + " exported: " + juce::String (settings.sampleRate / 1000.0, 1) + " kHz / " + juce::String (settings.bitDepth) + "-bit" + (settings.normalizePeak ? ", peak normalized." : "."));
        safe->refresh();
    }
    void exportAudioSelection (const juce::File& selected, Session::AudioExportSettings settings)
    {
        if (selected == juce::File()) return;
        const auto destination = selected.withFileExtension (exportExtension (settings.format));
        // The native chooser only confirmed the spelling it returned. Adding the
        // chosen extension can name another existing file, which needs its own choice.
        if (destination != selected && destination.existsAsFile())
        {
            auto* alert = new juce::AlertWindow ("Replace Existing Audio?", "The selected format uses " + destination.getFileName() + ". Replace the existing file?", juce::MessageBoxIconType::WarningIcon, this);
            alert->setLookAndFeel (&theme);
            alert->addButton ("Replace", 1);
            alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            alerts.emplace_back (alert);
            auto safe = juce::Component::SafePointer<Impl> (this);
            alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, destination, settings] (int result)
            {
                if (result == 1 && safe != nullptr) safe->performAudioExport (destination, settings);
            }), true);
            return;
        }
        performAudioExport (destination, settings);
    }
    void chooseExportDestination (Session::AudioExportSettings settings)
    {
        const auto extension = exportExtension (settings.format);
        chooser = std::make_unique<juce::FileChooser> ("Export Arrangement as " + extension.toUpperCase(), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (state.name + "." + extension), "*." + extension);
        auto safe = juce::Component::SafePointer<Impl> (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, settings] (const juce::FileChooser& fc)
            {
                if (safe != nullptr) safe->exportAudioSelection (fc.getResult(), settings);
            });
    }
    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an Audio Folder", browserFolder);
        auto safe = juce::Component::SafePointer<Impl> (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe] (const juce::FileChooser& fc) { if (safe != nullptr && fc.getResult() != juce::File()) { safe->browserFolder = fc.getResult(); safe->scannedBrowserFolder = juce::File(); safe->updateBrowser(); } });
    }

    void showDialog (juce::String title, std::unique_ptr<juce::Component> content, int width, int height)
    {
        content->setSize (width, height);
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = std::move (title);
        options.dialogBackgroundColour = panel;
        options.content.setOwned (content.release());
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.escapeKeyTriggersCloseButton = true;
        auto* dialog = options.launchAsync();
        dialog->setLookAndFeel (&theme);
        dialogs.emplace_back (dialog);
    }
    void deleteClipById (const juce::String& id)
    {
        const auto current=session.snapshot();
        for(int t=0;t<(int)current.tracks.size();++t)for(int c=0;c<(int)current.tracks[(size_t)t].clips.size();++c)
            if(current.tracks[(size_t)t].clips[(size_t)c].id==id)
            {report(session.removeClip(t,c));selectedClip=-1;refresh();return;}
    }
    void pasteMidi (const juce::String& trackId,double position)
    {
        juce::String id;const auto result=session.pasteMidiClip(trackId,position,id);report(result);refresh();
        if(result.wasOk())selectArrangementClip(id);
    }
    void midiClipCommand (int choice,const juce::String& id)
    {
        if(choice==7){if(const auto* track=currentTrack())pasteMidi(track->id,session.snapshot().position);return;}
        if(choice==5||choice==6)
        {
            const auto result=session.copyMidiClip(id,choice==6);if(result.wasOk()&&choice==6)selectedClip=-1;
            report(result);refresh();return;
        }
        if(choice==4)
        {
            juce::String created;const auto result=session.duplicateMidiClip(id,created);report(result);refresh();
            if(result.wasOk())selectArrangementClip(created);
        }
    }
    void audioClipCommand (int choice, const juce::String& id)
    {
        if (choice == 2) { showDialog ("Halftrack - Audio Clip", makeAudioClipEditor (session, id), 620, 480); return; }
        if (choice == 5 || choice == 6)
        {
            const auto result = session.copyAudioClip (id, choice == 6);
            if (result.wasOk() && choice == 6) selectedClip = -1;
            report (result); refresh(); return;
        }
        if (choice == 7)
        {
            if (const auto* track = currentTrack()) pasteAudio (track->id, session.snapshot().position);
            return;
        }
        if (choice != 3 && choice != 4) return;
        juce::String created;
        const auto result = choice == 3 ? session.splitAudioClip (id, session.snapshot().position, created)
            : session.duplicateAudioClip (id, created);
        report (result); refresh();
        if (result.wasOk()) selectArrangementClip (created);
    }
    void pasteAudio (const juce::String& trackId, double position)
    {
        juce::String created;
        const auto result = session.pasteAudioClip (trackId, position, created);
        report (result); refresh();
        if (result.wasOk()) selectArrangementClip (created);
    }
    void selectArrangementClip (const juce::String& id)
    {
        for (int t=0;t<(int)state.tracks.size();++t)
            for (int c=0;c<(int)state.tracks[(size_t)t].clips.size();++c)
                if (state.tracks[(size_t)t].clips[(size_t)c].id == id)
                { selectTrack(t);selectedClip=c;selectedLauncherClipId.clear();canvas.repaint();return; }
    }
    void showAudioSettings()
    {
        auto settings = std::make_unique<juce::AudioDeviceSelectorComponent> (session.audioDeviceManager(), 0, 128, 0, 128, true, true, true, false);
        showDialog ("Halftrack - Audio & MIDI Setup", std::move (settings), 660, 600);
    }
    void showPlugins()
    {
        auto list = std::make_unique<juce::PluginListComponent> (session.pluginFormats(), session.knownPlugins(), session.pluginScanDeadMansFile(), nullptr, false);
        list->setOptionsButtonText ("Scan / Manage Plug-ins");
        list->setNumberOfThreadsForScanning (1);
        showDialog ("Halftrack - Plug-in Library", std::move (list), 780, 520);
    }
    void addPluginMenu()
    {
        if (selectedTrack < 0 || selectedTrack >= static_cast<int> (state.tracks.size())) return;
        auto types = session.knownPlugins().getTypes();
        juce::PopupMenu popup;
        popup.addItem (100001, "Utility  (Built-in)");
        for (int i = 0; i <= static_cast<int> (Session::EffectKind::limiter); ++i) popup.addItem (100002+i, juce::String (effectName (static_cast<Session::EffectKind> (i))) + "  (Built-in)");
        popup.addSeparator();
        for (int i = 0; i < types.size(); ++i) popup.addItem (i + 1, types[i].name + "  (" + types[i].pluginFormatName + ")");
        popup.addSeparator(); popup.addItem (100000, "Scan / Manage Plug-ins...");
        auto safe = juce::Component::SafePointer<Impl> (this);
        const auto track = state.tracks[static_cast<size_t> (selectedTrack)].id;
        const auto generation = state.projectGeneration;
        popup.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&deviceChain), [safe, types, track, generation] (int choice)
        {
            if (safe == nullptr) return;
            safe->insertDeviceSelection (generation, track, choice, types);
        });
    }
    void insertDeviceSelection (uint64_t generation, const juce::String& trackId, int choice, const juce::Array<juce::PluginDescription>& types)
    {
        if (choice == 100000) { showPlugins(); return; }
        const bool native = choice >= 100001 && choice <= 100002 + static_cast<int> (Session::EffectKind::limiter);
        if (! native && (choice <= 0 || choice > types.size())) return;
        const auto current = session.snapshot();
        if (current.projectGeneration != generation)
        { report (juce::Result::fail ("That project's device menu is no longer current.")); refresh(); return; }
        const auto found = std::find_if (current.tracks.begin(), current.tracks.end(),
            [&] (const auto& item) { return item.id == trackId; });
        if (found == current.tracks.end())
        { report (juce::Result::fail ("The selected track is no longer available.")); refresh(); return; }
        const int track = static_cast<int> (found - current.tracks.begin());
        if (choice == 100001) { report (session.addUtility (track)); refresh(); }
        else if (choice >= 100002 && choice <= 100002 + static_cast<int> (Session::EffectKind::limiter))
        { report (session.addNativeEffect (track, static_cast<Session::EffectKind> (choice-100002))); refresh(); }
        else if (choice > 0 && choice <= types.size()) { report (session.addPlugin (track, types[choice-1])); refresh(); }
    }
    void startBrowserScan()
    {
        if (browserScan) browserScan->cancelled.store (true);
        browserScan.reset();
        scannedBrowserFolder = browserFolder;
        allBrowserItems.clear();
        browserError.clear();
        browserLoading = false;
        if (browserFolder == juce::File()) return;
        // A filesystem call may remain blocked inside the OS. Keep at most four
        // outstanding workers, including cancelled scans still waiting on I/O.
        if (activeBrowserScans->fetch_add (1) >= 4)
        {
            activeBrowserScans->fetch_sub (1);
            browserError = "Previous folders are still waiting on the filesystem. Try again shortly.";
            return;
        }
        browserLoading = true;
        browserScanStarted = juce::Time::getMillisecondCounterHiRes();
        auto scan = std::make_shared<FolderScan>();
        browserScan = scan;
        auto active = activeBrowserScans;
        const auto path = browserFolder.getFullPathName().toStdString();
        // Workers own only standard-library data. They never capture a Component,
        // call JUCE, or post callbacks. A blocked OS operation can finish after the
        // window closes without touching its memory or delaying destruction.
        try
        {
            std::thread ([scan, active, path]
            {
                std::vector<FolderScan::Entry> entries;
                std::string error;
                try
                {
                    std::error_code ec;
                    const std::u8string utf8Path (reinterpret_cast<const char8_t*> (path.data()), path.size());
                    std::filesystem::directory_iterator iterator (std::filesystem::path (utf8Path), ec), end;
                    if (ec) error = ec.message();
                    while (! ec && iterator != end && ! scan->cancelled.load())
                    {
                        const auto nameBytes = iterator->path().filename().u8string();
                        std::string name (reinterpret_cast<const char*> (nameBytes.data()), nameBytes.size());
                        if (! name.empty() && name.front() != '.')
                        {
                            std::error_code typeError;
                            const auto directory = iterator->is_directory (typeError);
                            auto extensionBytes = iterator->path().extension().u8string();
                            std::string extension (reinterpret_cast<const char*> (extensionBytes.data()), extensionBytes.size());
                            std::transform (extension.begin(), extension.end(), extension.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
                            const auto audio = extension == ".wav" || extension == ".wave" || extension == ".aif" || extension == ".aiff"
                                || extension == ".flac" || extension == ".ogg" || extension == ".mp3";
                            if (! typeError && (directory || audio)) entries.push_back ({ std::move (name), directory });
                        }
                        iterator.increment (ec);
                    }
                    if (ec) error = ec.message();
                }
                catch (const std::exception& e) { error = e.what(); }
                {
                    const std::lock_guard<std::mutex> lock (scan->mutex);
                    scan->entries = std::move (entries);
                    scan->error = std::move (error);
                }
                scan->finished.store (true);
                active->fetch_sub (1);
            }).detach();
        }
        catch (const std::exception& e)
        {
            activeBrowserScans->fetch_sub (1);
            browserLoading = false;
            browserError = juce::String::fromUTF8 (e.what());
            browserScan.reset();
        }
    }
    void pollBrowserScan()
    {
        if (! browserScan || ! browserScan->finished.load()) return;
        std::vector<FolderScan::Entry> entries;
        std::string error;
        {
            const std::lock_guard<std::mutex> lock (browserScan->mutex);
            entries = std::move (browserScan->entries);
            error = std::move (browserScan->error);
        }
        browserScan.reset();
        browserLoading = false;
        browserError = juce::String::fromUTF8 (error.data(), (int) error.size());
        allBrowserItems.clear();
        for (const auto& entry : entries)
            allBrowserItems.push_back ({ browserFolder.getChildFile (juce::String::fromUTF8 (entry.name.data(), (int) entry.name.size())), entry.directory });
        std::sort (allBrowserItems.begin(), allBrowserItems.end(), [] (const auto& a, const auto& b)
        { return a.directory != b.directory ? a.directory : a.file.getFileName().compareNatural (b.file.getFileName()) < 0; });
        updateBrowser();
    }
    void updateBrowser()
    {
        browserItems.clear(); pluginItems.clear();
        const auto query = browserSearch.getText().trim();
        filesButton.setToggleState (! pluginBrowser, juce::dontSendNotification);
        pluginsButton.setToggleState (pluginBrowser, juce::dontSendNotification);
        folderButton.setButtonText (pluginBrowser ? "Manage / Scan..." : "Choose Folder...");
        upButton.setEnabled (browserFolder != juce::File() && browserFolder.getParentDirectory() != browserFolder);
        browserSearch.setTextToShowWhenEmpty (pluginBrowser ? "Search plug-ins" : "Search current folder", muted);
        if (browserFolder != scannedBrowserFolder) startBrowserScan();
        if (pluginBrowser)
        {
            for (const auto& type : session.knownPlugins().getTypes()) if (query.isEmpty() || type.name.containsIgnoreCase (query)) pluginItems.push_back (type);
            std::sort (pluginItems.begin(), pluginItems.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        }
        else
        {
            for (const auto& item : allBrowserItems)
                if (query.isEmpty() || item.file.getFileName().containsIgnoreCase (query)) browserItems.push_back (item);
        }
        browserList.updateContent(); browserList.repaint(); repaint();
    }
    int getNumRows() override { return pluginBrowser ? (int) pluginItems.size() : (int) browserItems.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        if (selected) { g.setColour (raised); g.fillRect (0, 0, width, height); }
        g.setColour (selected ? orange : muted);
        g.setFont (font (12.0f));
        auto label = pluginBrowser ? pluginItems[(size_t) row].name : browserItems[(size_t) row].file.getFileName();
        const bool folder = ! pluginBrowser && browserItems[(size_t) row].directory;
        if (folder)
        {
            g.drawRect (6, 9, 11, 9); g.fillRect (6, 7, 5, 2);
        }
        else if (pluginBrowser)
        {
            g.drawRect (7, 8, 9, 11); g.drawHorizontalLine (11, 5.0f, 18.0f); g.drawHorizontalLine (16, 5.0f, 18.0f);
        }
        else
        {
            for (int i = 0; i < 5; ++i) { const int h = i % 2 == 0 ? 6 : 12; g.drawVerticalLine (6 + i * 2, (float) (height - h) / 2, (float) (height + h) / 2); }
        }
        g.setColour (selected ? ink : juce::Colour (0xffc1c7cb));
        g.drawText (label, 25, 0, width - 29, height, juce::Justification::centredLeft, true);
    }
    void activateBrowserRow (int row)
    {
        if (row < 0 || row >= getNumRows()) return;
        if (pluginBrowser)
        {
            if (selectedTrack < 0) addTrack();
            report (session.addPlugin (selectedTrack, pluginItems[(size_t) row])); refresh();
        }
        else
        {
            auto item = browserItems[(size_t) row];
            if (item.directory) { browserFolder = item.file; browserSearch.clear(); updateBrowser(); }
            else if (showSession) importLauncherFiles ({ item.file.getFullPathName() }, selectedTrack, selectedSceneIndex());
            else importFiles ({ item.file.getFullPathName() }, selectedTrack, state.position);
        }
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { activateBrowserRow (row); }
    void returnKeyPressed (int row) override { activateBrowserRow (row); }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& path : files) if (isAudio (juce::File (path)) || juce::File (path).hasFileExtension ("halftrack")) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { canvas.dragHover = true; canvas.repaint(); }
    void fileDragExit (const juce::StringArray&) override { canvas.dragHover = false; canvas.repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        canvas.dragHover = false;
        if (files.size() == 1 && juce::File (files[0]).hasFileExtension ("halftrack"))
        {
            auto safe = juce::Component::SafePointer<Impl> (this); auto file = juce::File (files[0]);
            ensureSaved ([safe, file] { if (safe != nullptr) { safe->report (safe->session.loadProject (file)); safe->resetView(); } }); return;
        }
        if (showSession && showPiano)
        {
            report (juce::Result::fail ("Audio cannot be dropped into the MIDI editor. Choose an empty audio slot in Session."));
            return;
        }
        if (showSession)
        {
            const auto cell = launcher.cellAt (launcher.getLocalPoint (this, juce::Point<int> (x, y)));
            if (cell.first >= 0 && cell.second >= 0) importLauncherFiles (files, cell.first, cell.second);
            return;
        }
        const auto p = canvas.getLocalPoint (this, juce::Point<int> (x, y));
        const auto inside = viewport.getBounds().contains (x, y) && p.x < canvas.timelineWidth();
        const auto index = inside && p.y / trackHeight < (int) state.tracks.size() ? p.y / trackHeight : -1;
        const auto time = inside ? juce::jmax (0.0, viewStart + p.x / pixelsPerSecond) : state.position;
        importFiles (files, index, std::round (time / gridSeconds()) * gridSeconds());
    }
};


#if HALFTRACK_TESTS_ENABLED
void MainComponent::Impl::runInteractionChecks (const juce::File& audioFixture)
{
    auto check = [] (bool condition, const juce::String& message)
    {
        if (! condition) throw std::runtime_error (message.toStdString());
        std::cout << "PASS UI " << message << std::endl;
    };
    // Drive the same entry points used by the native menu and mouse dispatch.
    // User-facing note/synth edits use production handlers. The bounded fixture
    // at the end also establishes a facade-valid length below the mouse minimum.
    menuItemSelected (33, 2);
    check (currentMidiClip() != nullptr && currentMidiClip()->lengthBeats == 16.0 && showPiano,
           "MIDI track action creates and opens a four-bar editor");
    pianoRoll.fit();
    const auto beatWidth = (pianoRoll.getWidth() - 55.0 - 11.0) / 16.0;
    auto point = [&] (double beat, int pitch)
    {
        return juce::Point<float> ((float) (55.0 + beat * beatWidth), (float) (60 + (77 - pitch) * 15 + 7));
    };
    auto event = [&] (juce::Point<float> at, juce::Point<float> down, bool dragged = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                &pianoRoll, &pianoRoll, now, down, now, 1, dragged);
    };
    const auto arrangementRuler = juce::Point<float> ((float) (55 + 2 * beatWidth), 48.0f);
    pianoRoll.mouseDown (event (arrangementRuler, arrangementRuler));
    check (std::abs (state.position - 1.0) < 0.001, "arrangement MIDI ruler seeks the global transport");
    pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    auto click = [&] (juce::Point<float> at)
    {
        const auto parentPoint = getLocalPoint (&pianoRoll, at.toInt());
        check (getComponentAt (parentPoint) == &pianoRoll, "piano grid receives pointer events without an overlay");
        pianoRoll.mouseDown (event (at, at)); pianoRoll.mouseUp (event (at, at));
    };
    click (point (0.35, 60));
    check (currentMidiClip()->notes.size() == 1, "grid mouse-down creates an actual MIDI note");
    auto note = currentMidiClip()->notes.front();
    const auto noteId = note.id;
    check (note.pitch == 60 && note.startBeat == 0.25 && note.lengthBeats == 1,
           "draw uses the selected pitch, grid and default note length");
    auto down = point (0.3, 60), to = down + juce::Point<float> ((float) beatWidth, -60.0f);
    pianoRoll.mouseDown (event (down, down)); pianoRoll.mouseDrag (event (to, down, true)); pianoRoll.mouseUp (event (to, down, true));
    note = currentMidiClip()->notes.front();
    check (note.id == noteId && note.pitch == 64 && note.startBeat == 1.25,
           "note body drag moves time and pitch while retaining identity");
    down = point (2.25, 64) - juce::Point<float> (2.0f, 0.0f);
    to = down + juce::Point<float> ((float) beatWidth, 0.0f);
    pianoRoll.mouseDown (event (down, down)); pianoRoll.mouseDrag (event (to, down, true)); pianoRoll.mouseUp (event (to, down, true));
    check (currentMidiClip()->notes.front().lengthBeats == 2, "right-edge drag resizes a real note");
    const auto undo = juce::KeyPress ('Z', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0);
    const auto redo = juce::KeyPress ('Z', juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0);
    pianoRoll.keyPressed (undo);
    check (currentMidiClip()->notes.front().lengthBeats == 1, "one undo reverses one resize gesture");
    pianoRoll.keyPressed (redo);
    check (currentMidiClip()->notes.front().lengthBeats == 2, "redo restores the resize gesture");
    pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    check (currentMidiClip()->notes.empty(), "Delete key removes the selected note");
    pianoRoll.keyPressed (undo);
    check (currentMidiClip()->notes.size() == 1 && currentMidiClip()->notes.front().id == noteId,
           "undo deletion restores the original note identity");
    std::function<juce::Component* (juce::Component&, const juce::String&)> findNamed;
    findNamed = [&] (juce::Component& parent, const juce::String& name) -> juce::Component*
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            auto* child = parent.getChildComponent (i);
            if (child->getName() == name) return child;
            if (auto* found = findNamed (*child, name)) return found;
        }
        return nullptr;
    };
    withGeneratedMidiController(session,[&](const juce::String& controller)
    {
        refresh(); auto* row = canvas.getChildComponent(selectedTrack);
        auto* input = dynamic_cast<juce::ComboBox*>(findNamed(*row,"Track MIDI input"));
        auto* monitor = dynamic_cast<juce::ComboBox*>(findNamed(*row,"Track monitoring"));
        auto* arm = dynamic_cast<juce::Button*>(findNamed(*row,"Record arm"));
        check(input && monitor && arm && input->isVisible() && monitor->isVisible() && arm->isVisible(),
            "MIDI row exposes controller, monitoring and recording controls");
        int inputId=0; for(int i=0;i<input->getNumItems();++i)if(input->getItemText(i)==controller)inputId=input->getItemId(i);
        check(inputId>1,"generated controller appears in the MIDI input chooser");
        input->setSelectedId(inputId,juce::sendNotificationSync); refresh();
        check(currentTrack()->inputName==controller && !currentTrack()->armed,"MIDI chooser assigns the real native input without arming");
        monitor->setSelectedId(2,juce::sendNotificationSync);
        arm->setToggleState(true,juce::dontSendNotification); arm->onClick(); refresh();
        check(currentTrack()->armed && currentTrack()->monitor==Session::MonitorMode::automatic,
            "native MIDI arm and Auto controls update engine capture state");
        arm->setToggleState(false,juce::dontSendNotification); arm->onClick();
        input->setSelectedId(1,juce::sendNotificationSync); refresh();
        check(!currentTrack()->armed && currentTrack()->inputName.isEmpty(),"MIDI input chooser disconnects the controller");
    });
    auto* cutoff = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "Cutoff"));
    auto* resonance = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "Resonance"));
    auto* release = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "Release"));
    auto* width = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "Pulse width"));
    auto* waveform = dynamic_cast<juce::ComboBox*> (findNamed (deviceChain, "Oscillator waveform"));
    check (cutoff != nullptr && resonance != nullptr && release != nullptr && waveform != nullptr && width != nullptr,
           "synth controls expose accessible names");
    check (! width->isEnabled() && waveform->getNumItems() == 4, "width is disabled outside pulse mode and all four waveforms are available");
    cutoff->setValue (8100, juce::sendNotificationSync);
    resonance->setValue (0.72, juce::sendNotificationSync);
    waveform->setSelectedId (2, juce::sendNotificationSync);
    check (width->isEnabled() && width->getValue() == 100, "pulse mode enables the legacy square width");
    width->setValue (37, juce::sendNotificationSync);
    release->setValue (1.75, juce::sendNotificationSync);
    const auto settings = session.snapshot().tracks[(size_t) selectedTrack].synth;
    check (settings.cutoffHz == 8100 && std::abs (settings.resonance - 0.72f) < 0.0001f
               && settings.waveform == 1 && settings.releaseSeconds == 1.75f && settings.pulseWidth == 0.37f,
           "immediate consecutive synth changes preserve every previous control value");
    for (const int mode : { 3, 4 })
    {
        waveform->setSelectedId (mode, juce::sendNotificationSync);
        check (! width->isEnabled() && session.snapshot().tracks[(size_t) selectedTrack].synth.waveform == mode - 1,
               "native waveform control selects the new sound and disables width");
    }
    waveform->setSelectedId (2, juce::sendNotificationSync);
    check (width->isEnabled() && width->getValue() == 37, "returning to pulse retains its previous width");
    width->setValue (64, juce::sendNotificationSync);
    session.undo(); refresh();
    check (width->getValue() == 37 && session.snapshot().tracks[(size_t) selectedTrack].synth.pulseWidth == 0.37f,
           "native width edit undo restores both UI and model");
    session.redo(); refresh();
    check (width->getValue() == 64 && session.snapshot().tracks[(size_t) selectedTrack].synth.pulseWidth == 0.64f,
           "native width edit redo restores both UI and model");
    menuItemSelected (31, 2);
    menuItemSelected (20, 1);
    menuItemSelected (21, 1);
    menuItemSelected (33, 2);
    check (currentMidiClip() != nullptr && currentMidiClip()->notes.empty(),
           "delete, undo, redo and new track leave the piano editor attached to live state");
    menuItemSelected (45, 3);
    check (showSession && ! showPiano && state.scenes.size() == 8, "Session view opens the eight real scene rows");
    const int midiTrack = selectedTrack;
    auto launcherEvent = [&] (juce::Point<float> at)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                &launcher, &launcher, now, at, now, 1, false);
    };
    const auto slotPoint = juce::Point<float> ((float) (midiTrack * 170 + 65), 79.0f);
    check (getComponentAt (getLocalPoint (&launcher, slotPoint.toInt())) == &launcher,
           "Session slot hit testing reaches the production clip-grid handler");
    launcher.mouseDown (launcherEvent (slotPoint)); launcher.mouseDoubleClick (launcherEvent (slotPoint));
    check (showPiano && currentMidiClip() != nullptr && currentMidiClip()->lengthBeats == 4 && selectedLauncherClipId.isNotEmpty(),
           "empty MIDI slot double-click creates and opens its own one-bar clip");
    const auto launcherClipId = currentMidiClip()->id;
    const auto launcherNotePoint = juce::Point<float> (139.0f, 322.0f);
    pianoRoll.mouseDown (event (launcherNotePoint, launcherNotePoint)); pianoRoll.mouseUp (event (launcherNotePoint, launcherNotePoint));
    check (currentMidiClip()->notes.size() == 1 && currentMidiClip()->notes.front().pitch == 60,
           "existing piano mouse handler edits the selected Session clip");
    auto clipIdentities = [this]
    {
        std::vector<juce::String> ids;
        for (const auto& track : state.tracks)
        {
            ids.push_back (track.id);
            for (const auto& clip : track.clips) ids.push_back (clip.id);
            for (const auto& slot : track.slots) { ids.push_back (slot.id); if (slot.hasClip) ids.push_back (slot.clip.id); }
        }
        return ids;
    };
    const auto beforeEditorDrop = clipIdentities();
    const auto editorDropPoint = getLocalPoint (&pianoRoll, launcherNotePoint.toInt());
    filesDropped ({ audioFixture.getFullPathName() }, editorDropPoint.x, editorDropPoint.y);
    check (clipIdentities() == beforeEditorDrop && currentMidiClip()->id == launcherClipId
               && status.contains ("Choose an empty audio slot in Session"),
           "audio drop onto a Session MIDI editor preserves arrangement and slot content");
    menuItemSelected (45, 3);
    launcher.mouseDown (launcherEvent (slotPoint.withY (109.0f)));
    menuItemSelected (44, 3);
    check (showPiano && currentMidiClip() == nullptr && selectedLauncherClipId.isEmpty() && selectedSceneIndex() == 1,
           "Piano Roll on an empty selected Session slot never opens another row's clip");
    menuItemSelected (45, 3);
    launcher.mouseDown (launcherEvent (slotPoint)); launcher.mouseDoubleClick (launcherEvent (slotPoint));
    check (currentMidiClip()->id == launcherClipId && currentMidiClip()->notes.size() == 1,
           "double-click on an occupied MIDI slot opens it without replacing notes");
    menuItemSelected (45, 3);
    launcher.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    check (! state.tracks[(size_t) midiTrack].slots.front().hasClip, "Session Delete key clears the selected slot");
    launcher.keyPressed (undo);
    check (state.tracks[(size_t) midiTrack].slots.front().hasClip && state.tracks[(size_t) midiTrack].slots.front().clip.id == launcherClipId,
           "slot-clear undo restores clip identity and notes");
    auto* launchGrid = dynamic_cast<juce::ComboBox*> (findNamed (launcher, "Global launch quantization"));
    check (launchGrid != nullptr, "global launch quantization has a named native control");
    launchGrid->setSelectedId (1, juce::sendNotificationSync);
    check (state.launchQuantisation == Session::LaunchQuantisation::none, "quantization control updates the actual launcher setting");
    auto launchPoint = slotPoint.withX ((float) (midiTrack * 170 + 12));
    launcher.mouseDown (launcherEvent (launchPoint));
    pumpSyntheticTestDevice (session, 48); refresh();
    check (state.tracks[(size_t) midiTrack].slots.front().playing,
           "Session launch triangle starts the selected clip through the synthetic audio device");
    menuItemSelected (44, 3);
    const auto beforeRuler = state;
    const auto sessionRuler = juce::Point<float> (415.0f, 48.0f);
    pianoRoll.mouseDown (event (sessionRuler, sessionRuler)); refresh();
    const auto& beforeSlot = beforeRuler.tracks[(size_t) midiTrack].slots.front();
    const auto& afterSlot = state.tracks[(size_t) midiTrack].slots.front();
    check (state.position == beforeRuler.position && afterSlot.playing == beforeSlot.playing
               && afterSlot.queued == beforeSlot.queued && afterSlot.stopQueued == beforeSlot.stopQueued,
           "Session MIDI ruler leaves global time and launch state unchanged");
    menuItemSelected (45, 3);
    const auto trackStopPoint = juce::Point<float> (launchPoint.x, (float) (launcher.getHeight() - 227 + 12));
    launcher.mouseDown (launcherEvent (trackStopPoint));
    pumpSyntheticTestDevice (session, 8); refresh();
    check (! state.tracks[(size_t) midiTrack].slots.front().playing,
           "per-track stop control stops the active Session clip");
    selectLauncherSlot (midiTrack, 0);
    launcher.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    pumpSyntheticTestDevice (session, 8); refresh();
    check (state.tracks[(size_t) midiTrack].slots.front().playing,
           "Enter key launches the selected Session slot through the production handler");
    menuItemSelected (43, 3);
    check (! showSession && state.tracks[(size_t) midiTrack].slots.front().playing,
           "switching to Arrangement does not stop a launched clip");
    menuItemSelected (45, 3);
    const auto sceneCount = state.scenes.size();
    menuItemSelected (50, 4);
    const auto addedScene = selectedSceneId;
    check (state.scenes.size() == sceneCount + 1 && selectedSceneIndex() == (int) state.scenes.size() - 1,
           "Insert Scene command adds and selects a model scene");
    menuItemSelected (52, 4);
    check (state.scenes.size() == sceneCount && selectedSceneId != addedScene && selectedSceneIndex() >= 0,
           "deleting the selected scene refreshes selection to live scene state");
    menuItemSelected (20, 1);
    check (state.scenes.size() == sceneCount + 1 && state.scenes.back().id == addedScene,
           "scene deletion undo preserves the scene identity");
    const auto dropPoint = getLocalPoint (&launcher, juce::Point<int> (65, 109));
    filesDropped ({ audioFixture.getFullPathName() }, dropPoint.x, dropPoint.y);
    check (state.tracks.front().slots[1].hasClip && ! state.tracks.front().slots[1].clip.isMidi,
           "production file-drop handler imports decoded audio into the addressed Session slot");
    const auto audioClipId = state.tracks.front().slots[1].clip.id;
    if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
    {
        const juce::File directory (juce::String::fromUTF8 (configured));
        if (directory.createDirectory().wasOk())
        {
            auto* host = getParentComponent();
            auto capture = [&] (const juce::String& view, int width, int height)
            {
                if (host == nullptr) return;
                const auto oldSize = host->getBounds();
                host->setSize (width, height); refresh();
                const auto image = host->createComponentSnapshot (host->getLocalBounds(), true, 1.0f);
                const auto output = directory.getChildFile (view + "-" + juce::String (width) + "x" + juce::String (height) + ".png");
                auto stream = output.createOutputStream(); juce::PNGImageFormat png;
                if (stream && stream->setPosition (0) && stream->truncate().wasOk() && png.writeImageToStream (image, *stream)) std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
                else std::cerr << "UI snapshot export failed: " << output.getFullPathName() << std::endl;
                host->setSize (oldSize.getWidth(), oldSize.getHeight()); refresh();
            };
            capture ("session", 1440, 900); capture ("session", 1100, 720);
            selectLauncherSlot (midiTrack, 0); openPianoRoll();
            capture ("session-piano", 1440, 900); capture ("session-piano", 1100, 720);
            showSessionView(); selectLauncherSlot (0, 1);
        }
        else std::cerr << "UI snapshot directory could not be created" << std::endl;
    }
    status.clear();
    filesDropped ({ audioFixture.getFullPathName() }, dropPoint.x, dropPoint.y);
    check (state.tracks.front().slots[1].clip.id == audioClipId && status.contains ("already contains"),
           "occupied audio-slot drop is rejected without replacing the existing clip");
    launcher.keyPressed (juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys (juce::ModifierKeys::shiftModifier), 0));
    pumpSyntheticTestDevice (session, 8); refresh();
    check (state.tracks.front().slots[1].playing && ! state.tracks[(size_t) midiTrack].slots.front().playing,
           "Shift+Enter launches the selected scene and its empty MIDI slot stops the previous clip");
    menuItemSelected (54, 4);
    pumpSyntheticTestDevice (session, 8); refresh();
    check (std::all_of (state.tracks.begin(), state.tracks.end(), [] (const auto& track)
           { return std::none_of (track.slots.begin(), track.slots.end(), [] (const auto& slot) { return slot.playing || slot.queued; }); }),
           "Stop All command stops active Session clips through the audio callback");
    std::vector<bool> overrides; for (const auto& track : state.tracks) overrides.push_back (track.launcherActive);
    menuItemSelected (43, 3);
    bool unchanged = ! showSession;
    for (size_t i = 0; i < overrides.size(); ++i) unchanged = unchanged && state.tracks[i].launcherActive == overrides[i];
    check (unchanged, "switching to Arrangement leaves launcher playback ownership unchanged");
    menuItemSelected (55, 4);
    check (std::none_of (state.tracks.begin(), state.tracks.end(), [] (const auto& track) { return track.launcherActive; }),
           "Return to Arrangement explicitly releases every launcher override");
    menuItemSelected (45, 3);
    selectLauncherSlot (midiTrack, 0);
    launcher.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    check (selectedSceneIndex() == 1, "Session arrow key moves the actual selected slot");
    menuItemSelected (31, 2); menuItemSelected (20, 1); menuItemSelected (21, 1); menuItemSelected (50, 4);
    check (currentTrack() != nullptr && selectedSceneIndex() >= 0,
           "track delete, undo, redo and new scene leave Session selection attached to live state");

    menuItemSelected (35, 2);
    check (currentTrack() != nullptr && currentTrack()->isReturn && currentTrack()->sends.empty(),
           "Insert Return Track command creates and selects a real return without outgoing sends");
    const auto firstReturnId = currentTrack()->id;
    auto* returnRow = canvas.getChildComponent (selectedTrack);
    auto* returnMode = dynamic_cast<juce::ComboBox*> (findNamed (*returnRow, "Return send position"));
    auto* returnName = dynamic_cast<juce::Label*> (findNamed (*returnRow, "Track name"));
    check (returnMode != nullptr && returnMode->isVisible() && returnName != nullptr
               && ! findNamed (*returnRow, "Record arm")->isVisible()
               && ! findNamed (*returnRow, "Track audio input")->isVisible()
               && ! findNamed (*returnRow, "Track monitoring")->isVisible(),
           "return mixer exposes send position and hides recording and input controls");
    returnMode->setSelectedId (1, juce::sendNotificationSync);
    check (currentTrack()->sendMode == Session::SendMode::pre,
           "return Pre-fader control changes the actual destination routing mode");
    returnName->setText ("Room Return", juce::sendNotificationSync); refresh();
    check (currentTrack()->id == firstReturnId && currentTrack()->name == "Room Return",
           "existing rename control preserves the return identity");
    menuItemSelected (35, 2);
    const auto secondReturnId = currentTrack()->id;
    returnName = dynamic_cast<juce::Label*> (findNamed (*canvas.getChildComponent (selectedTrack), "Track name"));
    returnName->setText ("Echo Return", juce::sendNotificationSync); refresh();
    const int sourceTrack = 0;
    selectTrack (sourceTrack);
    auto sendSlider = [&] (const juce::String& name)
    {
        auto* slider = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "Send to " + name));
        bool visible = slider != nullptr;
        for (auto* child = static_cast<juce::Component*> (slider); child != this; child = child != nullptr ? child->getParentComponent() : nullptr)
        {
            if (child == nullptr || ! child->isVisible()) { visible = false; break; }
        }
        check (visible, "selected source exposes a visible named send control for " + name);
        auto* hit = getComponentAt (getLocalPoint (slider, slider->getLocalBounds().getCentre()));
        check (hit == slider || slider->isParentOf (hit), "send control receives pointer hits without a covering component");
        return slider;
    };
    auto sendGain = [&] (int track, const juce::String& id)
    {
        const auto snapshot = session.snapshot();
        for (const auto& send : snapshot.tracks[(size_t) track].sends) if (send.returnTrackId == id) return send.gainDb;
        throw std::runtime_error ("Expected send route missing from snapshot");
    };
    // Native sends store a float fader position via exp, then recover dB via
    // log. Allow 0.00001 dB round-trip error, far below the 0.1 dB UI step;
    // Off remains an exact sentinel and is never covered by this tolerance.
    auto sendIs = [&] (int track, const juce::String& id, float expected)
    {
        const auto actual = sendGain (track, id);
        return expected == -100 ? actual == -100 : std::isfinite (actual) && std::abs (actual - expected) < 0.00001f;
    };
    auto* roomSend = sendSlider ("Room Return");
    auto* echoSend = sendSlider ("Echo Return");
    check (roomSend->getValue() == -100 && echoSend->getValue() == -100 && roomSend->getTextFromValue (-100) == "Off",
           "new send controls start Off with exact silence semantics");
    roomSend->setValue (-12, juce::sendNotificationSync);
    echoSend->setValue (0, juce::sendNotificationSync);
    check (sendIs (sourceTrack, firstReturnId, -12) && sendIs (sourceTrack, secondReturnId, 0) && echoSend->getValue() == 0,
           "independent send controls target stable destinations and allow 0 dB");
    roomSend->setValue (-100, juce::sendNotificationSync);
    check (sendIs (sourceTrack, firstReturnId, -100) && sendIs (sourceTrack, secondReturnId, 0),
           "turning one send Off leaves the other route unchanged");
    roomSend->setValue (-9, juce::sendNotificationSync);
    menuItemSelected (33, 2);
    const auto midiSourceId = currentTrack()->id;
    check (sendSlider ("Room Return")->getValue() == -100 && sendSlider ("Echo Return")->getValue() == -100,
           "new MIDI source exposes its own independent send levels");
    sendSlider ("Room Return")->setValue (-6, juce::sendNotificationSync);
    refresh();
    check (sendIs (selectedTrack, firstReturnId, -6) && sendIs (sourceTrack, firstReturnId, -9),
           "MIDI send change cannot overwrite the audio source route");
    selectTrack (launcherIndices (firstReturnId, {}).first);
    menuItemSelected (31, 2);
    check (launcherIndices (firstReturnId, {}).first < 0, "existing delete-track command removes a return");
    selectTrack (launcherIndices (midiSourceId, {}).first);
    check (currentTrack()->sends.size() == 1 && currentTrack()->sends.front().returnTrackId == secondReturnId,
           "return deletion removes its send row without retargeting the surviving destination");
    menuItemSelected (20, 1);
    check (currentTrack()->id == midiSourceId && currentTrack()->sends.size() == 2
               && sendIs (selectedTrack, firstReturnId, -6),
           "undo restores the return and source routing while preserving source selection across index changes");
    auto* restoredRoomSend = sendSlider ("Room Return");
    check (std::isfinite (restoredRoomSend->getValue()) && std::abs (restoredRoomSend->getValue() + 6.0) < 0.00001
               && restoredRoomSend->getTextFromValue (restoredRoomSend->getValue()) == "-6.0 dB"
               && sendSlider ("Echo Return")->getValue() == -100,
           "restored send controls follow stable IDs and original values");
    const int returnIndex = launcherIndices (firstReturnId, {}).first;
    selectTrack (returnIndex); menuItemSelected (43, 3);
    check (! importButton.isEnabled() && ! newMidiClipButton.isEnabled() && ! findNamed (deviceChain, "Track sends")->isVisible(),
           "selected return disables clip actions and has no outgoing-send panel");
    viewport.setViewPosition (0, returnIndex * trackHeight);
    const auto returnDrop = getLocalPoint (&canvas, juce::Point<int> (40, returnIndex * trackHeight + 42));
    const auto beforeReturnDrop = clipIdentities();
    filesDropped ({ audioFixture.getFullPathName() }, returnDrop.x, returnDrop.y);
    check (clipIdentities() == beforeReturnDrop && currentTrack()->clips.empty(),
           "actual Arrangement drop handler cannot create or redirect clips dropped on a return");
    menuItemSelected (45, 3); launcher.scrollSelectionIntoView();
    // The return is selected from its mixer/header; keyboard launch and deletion
    // must never treat its empty column as a clip slot.
    const auto beforeReturnKeys = clipIdentities();
    launcher.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    launcher.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    check (clipIdentities() == beforeReturnKeys && ! currentTrack()->launcherActive
               && std::none_of (currentTrack()->slots.begin(), currentTrack()->slots.end(), [] (const auto& slot)
                   { return slot.hasClip || slot.playing || slot.queued || slot.stopQueued; }),
           "Session Enter and Delete leave a selected return outside clip launching");
    // Locate the return's visible column via its shared mixer row, then use the
    // same pointer entry point as native Session clicks.
    std::function<juce::Component* (juce::Component&)> findReturnLabel;
    findReturnLabel = [&] (juce::Component& parent) -> juce::Component*
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            auto* child = parent.getChildComponent (i);
            if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->getName() == "Track name" && label->getText() == "Room Return") return label;
            if (auto* found = findReturnLabel (*child)) return found;
        }
        return nullptr;
    };
    auto* sessionReturnLabel = findReturnLabel (launcher);
    check (sessionReturnLabel != nullptr, "Session return mixer exposes its real track label");
    auto returnPoint = launcher.getLocalPoint (sessionReturnLabel, juce::Point<int> (0, 0)).toFloat();
    returnPoint.y = 79;
    check (launcher.cellAt (returnPoint.toInt()).first == -1, "return column has no clickable clip or stop slot");
    launcher.mouseDown (launcherEvent (returnPoint)); launcher.mouseDoubleClick (launcherEvent (returnPoint));
    check (currentTrack()->id == firstReturnId && ! showPiano && clipIdentities() == beforeReturnKeys,
           "return column pointer handlers select the return without creating or launching clips");
    returnMode = dynamic_cast<juce::ComboBox*> (findNamed (*sessionReturnLabel->getParentComponent(), "Return send position"));
    returnMode->setSelectedId (2, juce::sendNotificationSync);
    check (currentTrack()->sendMode == Session::SendMode::post, "Session return mixer changes the same destination to Post-fader");
    if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
    {
        const juce::File directory (juce::String::fromUTF8 (configured));
        if (directory.createDirectory().wasOk())
        {
            auto* host = getParentComponent();
            auto capture = [&] (const juce::String& view, bool sessionView, bool returnSelected, int width, int height)
            {
                if (host == nullptr) return;
                const auto oldBounds = host->getBounds(); host->setSize (width, height);
                selectTrack (returnSelected ? launcherIndices (firstReturnId, {}).first : sourceTrack);
                if (sessionView) { showSessionView(); launcher.scrollSelectionIntoView(); }
                else { showArrangementView(); viewport.setViewPosition (0, returnSelected ? selectedTrack * trackHeight : 0); }
                refresh();
                const auto image = host->createComponentSnapshot (host->getLocalBounds(), true, 1.0f);
                const auto output = directory.getChildFile (view + "-" + juce::String (width) + "x" + juce::String (height) + ".png");
                auto stream = output.createOutputStream(); juce::PNGImageFormat png;
                if (stream && stream->setPosition (0) && stream->truncate().wasOk() && png.writeImageToStream (image, *stream)) std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
                else std::cerr << "UI snapshot export failed: " << output.getFullPathName() << std::endl;
                host->setSize (oldBounds.getWidth(), oldBounds.getHeight()); refresh();
            };
            for (bool sessionView : { false, true }) for (bool returnSelected : { false, true })
            {
                const auto view = juce::String (sessionView ? "session-" : "arrangement-") + (returnSelected ? "return" : "sends");
                capture (view, sessionView, returnSelected, 1440, 900); capture (view, sessionView, returnSelected, 1100, 720);
            }
        }
    }
    // Multi-note regression: all edits enter through the production piano mouse,
    // key, menu and slider callbacks. Fixture notes are drawn by the mouse handler.
    menuItemSelected (43, 3); menuItemSelected (33, 2); pianoRoll.fit();
    const auto groupClipId = currentMidiClip()->id;
    const auto groupTrack = selectedTrack;
    const auto groupBeatWidth = (pianoRoll.getWidth() - 66.0) / 16.0;
    auto groupPoint = [&] (double beat, int pitch)
    {
        return juce::Point<float> ((float) (55 + beat * groupBeatWidth), (float) (60 + (77 - pitch) * 15 + 7));
    };
    auto groupEvent = [&] (juce::Point<float> at, juce::Point<float> down, bool dragged, int modifiers)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | modifiers), 1.0f, 0, 0, 0, 0,
                                &pianoRoll, &pianoRoll, now, down, now, 1, dragged);
    };
    auto groupClick = [&] (juce::Point<float> at, int modifiers = 0)
    {
        check (getComponentAt (getLocalPoint (&pianoRoll, at.toInt())) == &pianoRoll, "multi-note pointer target is the live piano editor");
        pianoRoll.mouseDown (groupEvent (at, at, false, modifiers)); pianoRoll.mouseUp (groupEvent (at, at, false, modifiers));
    };
    auto groupDrag = [&] (juce::Point<float> from, juce::Point<float> to, int modifiers = 0)
    {
        pianoRoll.mouseDown (groupEvent (from, from, false, modifiers));
        pianoRoll.mouseDrag (groupEvent ((from + to) * 0.5f, from, true, modifiers));
        pianoRoll.mouseDrag (groupEvent (to, from, true, modifiers));
        pianoRoll.mouseUp (groupEvent (to, from, true, modifiers));
    };
    auto noteById = [&] (const juce::String& id)
    {
        const auto* clip = currentMidiClip();
        if (clip != nullptr) for (const auto& note : clip->notes) if (note.id == id) return note;
        throw std::runtime_error ("Expected fixture note is missing");
    };
    const auto shiftModifier = juce::ModifierKeys::shiftModifier;
    const auto allNotes = juce::KeyPress ('A', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0);
    const auto escape = juce::KeyPress (juce::KeyPress::escapeKey);
    const auto deleteNotes = juce::KeyPress (juce::KeyPress::deleteKey);
    groupClick (groupPoint (1.1, 60)); const auto aId = currentMidiClip()->notes.back().id;
    groupClick (groupPoint (4.1, 64)); const auto bId = currentMidiClip()->notes.back().id;
    groupClick (groupPoint (8.1, 67)); const auto cId = currentMidiClip()->notes.back().id;
    groupClick (groupPoint (1.1, 60), shiftModifier); // C + A
    groupClick (groupPoint (8.1, 67), shiftModifier); // A (toggle C off)
    groupClick (groupPoint (4.1, 64), shiftModifier); // A + B
    pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    check (noteById (aId).startBeat == 1.25 && noteById (bId).startBeat == 4.25 && noteById (cId).startBeat == 8,
           "Shift-click adds and toggles notes; an arrow moves exactly the remaining selection");
    pianoRoll.keyPressed (undo);
    check (noteById (aId).startBeat == 1 && noteById (bId).startBeat == 4, "one undo reverses the entire multi-note arrow edit");
    pianoRoll.keyPressed (redo);
    check (noteById (aId).startBeat == 1.25 && noteById (bId).startBeat == 4.25, "redo preserves selected note identities and relative timing");
    pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys (shiftModifier), 0));
    check (noteById (aId).pitch == 72 && noteById (bId).pitch == 76 && noteById (cId).pitch == 67,
           "Shift-arrow transposes every selected note by an octave");
    pianoRoll.keyPressed (undo);
    groupClick (groupPoint (11, 70), shiftModifier);
    check (! pianoRoll.hasSelectedNote() && currentMidiClip()->notes.size() == 3, "Shift-click empty space clears selection without drawing");
    groupDrag (groupPoint (0.5, 66), groupPoint (5.75, 58), shiftModifier);
    check (currentMidiClip()->notes.size() == 3, "Shift-marquee selects existing notes without drawing a fourth note");
    auto body = groupPoint (1.35, 60);
    groupDrag (body, body + juce::Point<float> ((float) (2 * groupBeatWidth), -45));
    check (noteById (aId).startBeat == 3.25 && noteById (bId).startBeat == 6.25 && noteById (aId).pitch == 63
               && noteById (bId).pitch == 67 && noteById (cId).startBeat == 8,
           "marquee-selected notes drag as a group while unselected notes stay fixed");
    pianoRoll.keyPressed (undo);
    check (noteById (aId).startBeat == 1.25 && noteById (bId).startBeat == 4.25 && noteById (aId).pitch == 60,
           "one undo reverses every event and note in a group drag");
    pianoRoll.keyPressed (redo);
    auto edge = groupPoint (4.25, 63) - juce::Point<float> (2, 0);
    groupDrag (edge, edge + juce::Point<float> ((float) (0.5 * groupBeatWidth), 0));
    check (noteById (aId).lengthBeats == 1.5 && noteById (bId).lengthBeats == 1.5 && noteById (cId).lengthBeats == 1,
           "a selected right edge resizes every selected note by one common length delta");
    pianoRoll.keyPressed (undo);
    check (noteById (aId).lengthBeats == 1 && noteById (bId).lengthBeats == 1, "one undo reverses the complete group resize");
    pianoRoll.keyPressed (redo);
    check (noteById (aId).lengthBeats == 1.5 && noteById (bId).lengthBeats == 1.5, "redo restores every grouped note length");
    pianoRoll.keyPressed (undo);
    body = groupPoint (3.35, 63);
    groupDrag (body, body - juce::Point<float> ((float) (100 * groupBeatWidth), 0));
    check (noteById (aId).startBeat == 0 && noteById (bId).startBeat == 3, "group drag clamps at the earliest note without collapsing timing offsets");
    body = groupPoint (0.1, 63);
    groupDrag (body, body + juce::Point<float> ((float) (100 * groupBeatWidth), 0));
    check (noteById (aId).startBeat == 12 && noteById (bId).startBeat == 15, "group drag clamps at the latest note's clip end");
    pianoRoll.keyPressed (undo); pianoRoll.keyPressed (undo);
    auto* groupVelocity = dynamic_cast<juce::Slider*> (findNamed (pianoRoll, "Note velocity"));
    check (groupVelocity != nullptr, "group velocity uses the existing accessible native slider");
    const auto beforeVelocityA = noteById (aId).velocity, beforeVelocityB = noteById (bId).velocity, beforeVelocityC = noteById (cId).velocity;
    groupVelocity->onDragStart(); groupVelocity->setValue (0.33, juce::sendNotificationSync);
    groupVelocity->setValue (0.66, juce::sendNotificationSync); groupVelocity->onDragEnd();
    const auto nativeVelocity = 84.0f / 127.0f; // Session stores MIDI 1.0 integer velocity.
    check (noteById (aId).velocity == nativeVelocity && noteById (bId).velocity == nativeVelocity && noteById (cId).velocity == beforeVelocityC,
           "velocity changes affect the whole selected group with native MIDI quantization");
    pianoRoll.keyPressed (undo);
    check (noteById (aId).velocity == beforeVelocityA && noteById (bId).velocity == beforeVelocityB,
           "one undo reverses every event of grouped velocity editing");
    pianoRoll.keyPressed (redo);
    check (noteById (aId).velocity == nativeVelocity && noteById (bId).velocity == nativeVelocity, "redo restores the entire velocity group");
    if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
    {
        const juce::File directory (juce::String::fromUTF8 (configured));
        if (directory.createDirectory().wasOk()) if (auto* host = getParentComponent())
        {
            const auto oldBounds = host->getBounds();
            for (const auto size : { juce::Point<int> (1440, 900), juce::Point<int> (1100, 720) })
            {
                host->setSize (size.x, size.y); pianoRoll.fit(); refresh();
                const auto scale = (pianoRoll.getWidth() - 66.0) / 16.0;
                const auto from = juce::Point<float> ((float) (55 + 2.75 * scale), 60 + (77 - 69) * 15.0f);
                const auto to = juce::Point<float> ((float) (55 + 7.5 * scale), 60 + (77 - 62) * 15.0f);
                pianoRoll.mouseDown (groupEvent (from, from, false, shiftModifier));
                pianoRoll.mouseDrag (groupEvent (to, from, true, shiftModifier));
                const auto image = host->createComponentSnapshot (host->getLocalBounds(), true, 1.0f);
                const auto output = directory.getChildFile ("piano-multiselect-" + juce::String (size.x) + "x" + juce::String (size.y) + ".png");
                auto stream = output.createOutputStream(); juce::PNGImageFormat png;
                if (stream && stream->setPosition (0) && stream->truncate().wasOk() && png.writeImageToStream (image, *stream)) std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
                else std::cerr << "UI snapshot export failed: " << output.getFullPathName() << std::endl;
                pianoRoll.mouseUp (groupEvent (to, from, true, shiftModifier));
            }
            host->setSize (oldBounds.getWidth(), oldBounds.getHeight()); pianoRoll.fit(); refresh();
        }
    }
    pianoRoll.keyPressed (deleteNotes);
    check (currentMidiClip()->notes.size() == 1 && currentMidiClip()->notes.front().id == cId, "Delete removes every selected note and preserves unselected notes");
    pianoRoll.keyPressed (undo);
    check (currentMidiClip()->notes.size() == 3 && noteById (aId).id == aId && noteById (bId).id == bId && ! pianoRoll.hasSelectedNote(),
           "bulk delete undo restores stable identities without reviving pruned selection");
    pianoRoll.keyPressed (redo);
    check (currentMidiClip()->notes.size() == 1, "one redo removes the entire restored group");
    pianoRoll.keyPressed (undo);
    pianoRoll.keyPressed (escape);
    groupClick (groupPoint (8.1, 67));
    groupDrag (groupPoint (2.75, 69), groupPoint (7.5, 62), shiftModifier);
    pianoRoll.keyPressed (deleteNotes);
    check (currentMidiClip()->notes.empty(), "Shift-marquee adds intersecting notes to the prior selection");
    pianoRoll.keyPressed (undo);
    pianoRoll.keyPressed (allNotes); pianoRoll.keyPressed (escape); pianoRoll.keyPressed (deleteNotes);
    check (currentMidiClip()->notes.size() == 3 && ! pianoRoll.hasSelectedNote(), "Escape clears select-all and makes Delete harmless");
    pianoRoll.keyPressed (allNotes); pianoRoll.keyPressed (deleteNotes);
    check (currentMidiClip()->notes.empty(), "Cmd/Ctrl+A selects all notes for bulk deletion");
    pianoRoll.keyPressed (undo); pianoRoll.keyPressed (allNotes);
    menuItemSelected (34, 2);
    check (currentMidiClip() != nullptr && currentMidiClip()->id != groupClipId && ! pianoRoll.hasSelectedNote(), "switching to a new clip clears the prior note selection");
    pianoRoll.keyPressed (deleteNotes);
    check (state.tracks[(size_t) groupTrack].clips.front().notes.size() == 3, "a new clip cannot delete selected IDs from the previous clip");
    pianoRoll.keyPressed (undo); menuItemSelected (44, 3);
    check (currentMidiClip() != nullptr && currentMidiClip()->id == groupClipId && ! pianoRoll.hasSelectedNote(), "clip creation undo resolves live clip identity without stale selection");

    menuItemSelected (33, 2); pianoRoll.fit();
    groupClick (groupPoint (0.1, 60)); const auto lowId = currentMidiClip()->notes.back().id;
    edge = groupPoint (1, 60) - juce::Point<float> (2, 0);
    groupDrag (edge, edge + juce::Point<float> ((float) groupBeatWidth, 0));
    groupClick (groupPoint (15.1, 64)); const auto highId = currentMidiClip()->notes.back().id;
    pianoRoll.keyPressed (allNotes);
    pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)); pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    check (noteById (lowId).startBeat == 0 && noteById (highId).startBeat == 15,
           "mixed clip-boundary notes share one arrow clamp in both directions");
    edge = groupPoint (2, 60) - juce::Point<float> (2, 0);
    groupDrag (edge, edge - juce::Point<float> ((float) (4 * groupBeatWidth), 0));
    check (noteById (lowId).lengthBeats == 1.03125 && noteById (highId).lengthBeats == 0.03125,
           "group resize stops at the shortest note and preserves relative lengths");
    pianoRoll.keyPressed (undo);
    check (noteById (lowId).lengthBeats == 2 && noteById (highId).lengthBeats == 1, "boundary resize is one reversible transaction");
    groupDrag (edge, edge + juce::Point<float> ((float) groupBeatWidth, 0));
    check (noteById (lowId).lengthBeats == 2 && noteById (highId).lengthBeats == 1, "group resize cannot extend another selected note beyond the clip");
    for (int i = 0; i < 7; ++i) pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys (shiftModifier), 0));
    check (noteById (lowId).pitch == 123 && noteById (highId).pitch == 127, "octave movement clamps the whole group at MIDI pitch 127");
    for (int i = 0; i < 12; ++i) pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys (shiftModifier), 0));
    check (noteById (lowId).pitch == 0 && noteById (highId).pitch == 4, "octave movement preserves intervals at MIDI pitch zero");
    pianoRoll.keyPressed (undo);
    check (noteById (lowId).pitch == 3 && noteById (highId).pitch == 7, "bounded no-op arrows add no undo step");
    pianoRoll.keyPressed (redo);
    check (noteById (lowId).pitch == 0 && noteById (highId).pitch == 4, "redo restores a partially bounded common pitch delta");

    // Reopen a generated native project with offscreen note content. Loading
    // currently preserves such content, so UI edits must not partially repair it.
    struct NoteLoadFixture
    {
        juce::File directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-note-ui-" + juce::Uuid().toString());
        ~NoteLoadFixture() { directory.deleteRecursively(); }
    } noteLoad;
    check (noteLoad.directory.createDirectory().wasOk(), "create isolated note-load fixture directory");
    const auto noteProject = noteLoad.directory.getChildFile ("Offscreen notes.halftrack");
    check (session.saveProject (noteProject).wasOk(), "save generated note-load fixture");
    auto noteXml = juce::XmlDocument::parse (noteProject);
    std::function<juce::XmlElement* (juce::XmlElement*, const juce::String&)> findNoteXml;
    findNoteXml = [&] (juce::XmlElement* xml, const juce::String& id) -> juce::XmlElement*
    {
        if (xml == nullptr) return nullptr;
        if (xml->getStringAttribute ("halftrackNoteId") == id) return xml;
        for (auto* child : xml->getChildIterator()) if (auto* found = findNoteXml (child, id)) return found;
        return nullptr;
    };
    auto* lowXml = findNoteXml (noteXml.get(), lowId); auto* highXml = findNoteXml (noteXml.get(), highId);
    check (lowXml != nullptr && highXml != nullptr, "locate original fixture notes by persisted identity");
    lowXml->setAttribute ("p", 60); lowXml->setAttribute ("l", 1.0);
    highXml->setAttribute ("p", 64); highXml->setAttribute ("l", 2.0);
    check (noteXml->writeTo (noteProject), "write original offscreen-note project fixture");
    groupClick (groupPoint (8.1, 60));
    check (pianoRoll.hasSelectedNote() && currentMidiClip()->notes.size() == 3, "a newly drawn note is selected before reopening earlier state");
    check (session.loadProject (noteProject).wasOk(), "loader accepts original MIDI content extending past the clip");
    refresh();
    check (noteById (lowId).startBeat == 0 && noteById (lowId).lengthBeats == 1 && noteById (highId).startBeat == 15 && noteById (highId).lengthBeats == 2
               && ! pianoRoll.hasSelectedNote(), "reopen preserves offscreen content and prunes selected IDs absent from the loaded clip");
    pianoRoll.keyPressed (deleteNotes);
    check (currentMidiClip()->notes.size() == 2, "Delete after load cannot target a stale note ID");
    pianoRoll.keyPressed (allNotes);
    pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    groupDrag (groupPoint (0.1, 60), groupPoint (1.1, 60));
    edge = groupPoint (1, 60) - juce::Point<float> (2, 0);
    groupDrag (edge, edge - juce::Point<float> ((float) groupBeatWidth, 0));
    check (noteById (lowId).pitch == 60 && noteById (highId).pitch == 64 && noteById (lowId).startBeat == 0
               && noteById (highId).startBeat == 15 && noteById (lowId).lengthBeats == 1 && noteById (highId).lengthBeats == 2 && ! session.canUndo(),
           "infeasible loaded note groups reject movement and resize before any mutation or undo action");
    const auto lowVelocity = noteById (lowId).velocity, highVelocity = noteById (highId).velocity;
    groupVelocity->onDragStart(); groupVelocity->setValue (0.23, juce::sendNotificationSync); groupVelocity->onDragEnd();
    check (noteById (lowId).velocity == lowVelocity && noteById (highId).velocity == highVelocity && ! session.canUndo()
               && status.contains ("duration inside the MIDI clip"),
           "grouped velocity rejects loaded overhang atomically and reports the validation error");

    // The facade also accepts notes shorter than the mouse editor's 1/32-beat
    // floor. Establish that valid fixture directly, then use real UI handlers.
    menuItemSelected (33, 2); pianoRoll.fit();
    groupClick (groupPoint (1.1, 60)); const auto shortId = currentMidiClip()->notes.back().id;
    groupClick (groupPoint (15.1, 64)); const auto endId = currentMidiClip()->notes.back().id;
    check (session.updateMidiNote (currentMidiClip()->id, shortId, 60, 1, 1.0 / 64, 0.8f).wasOk(),
           "facade-valid short note fixture is accepted before UI resizing");
    refresh(); pianoRoll.keyPressed (allNotes);
    edge = groupPoint (1, 60) + juce::Point<float> (1.5f, 0);
    groupDrag (edge, edge - juce::Point<float> ((float) groupBeatWidth, 0));
    check (noteById (shortId).lengthBeats == 1.0 / 64 && noteById (endId).lengthBeats == 1,
           "group resize preserves valid sub-grid notes without an inverted interval or partial edit");
    // A bounded, preexisting dense clip exercises Select All across notes outside
    // the current viewport. This checks correctness without a timing threshold.
    menuItemSelected (33, 2); pianoRoll.fit();
    const auto denseTrackId = currentTrack()->id, denseClipId = currentMidiClip()->id;
    const auto denseProject = noteLoad.directory.getChildFile ("Dense notes.halftrack");
    check (session.saveProject (denseProject).wasOk(), "save generated dense-note project fixture");
    auto denseXml = juce::XmlDocument::parse (denseProject);
    auto* denseEdit = denseXml != nullptr ? denseXml->getChildByName ("EDIT") : nullptr;
    auto* denseTrackXml = denseEdit != nullptr ? denseEdit->getChildByAttribute ("id", denseTrackId) : nullptr;
    auto* denseClipXml = denseTrackXml != nullptr ? denseTrackXml->getChildByAttribute ("id", denseClipId) : nullptr;
    check (denseClipXml != nullptr, "locate original dense clip by stable persisted IDs");
    auto* sequence = denseClipXml->getChildByName ("SEQUENCE");
    if (sequence == nullptr) sequence = denseClipXml->createNewChildElement ("SEQUENCE");
    sequence->deleteAllChildElements();
    constexpr int denseCount = 512;
    for (int i = 0; i < denseCount; ++i)
    {
        auto* note = sequence->createNewChildElement ("NOTE");
        note->setAttribute ("halftrackNoteId", juce::Uuid().toString());
        note->setAttribute ("p", 48 + i / 64); note->setAttribute ("b", (i % 64) * 0.25);
        note->setAttribute ("l", 0.125); note->setAttribute ("v", 80); note->setAttribute ("c", 0);
    }
    check (denseXml->writeTo (denseProject) && session.loadProject (denseProject).wasOk(), "load original 512-note fixture");
    refresh();
    const auto denseBefore = currentMidiClip()->notes;
    check (denseBefore.size() == denseCount, "dense fixture retains all on-screen and offscreen notes");
    pianoRoll.keyPressed (allNotes); pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    auto denseMatches = [&] (int pitchDelta, bool changedVelocity)
    {
        const auto* clip = currentMidiClip();
        if (clip == nullptr || clip->notes.size() != denseBefore.size()) return false;
        std::map<juce::String, Session::MidiNoteInfo> byId;
        for (const auto& note : clip->notes) byId.emplace (note.id, note);
        for (const auto& old : denseBefore)
        {
            const auto found = byId.find (old.id);
            if (found == byId.end()) return false;
            const auto& note = found->second;
            if (note.pitch != old.pitch + pitchDelta || note.startBeat != old.startBeat || note.lengthBeats != old.lengthBeats
                || note.velocity != (changedVelocity ? 53.0f / 127.0f : old.velocity)) return false;
        }
        return true;
    };
    check (denseMatches (1, false), "Select All transposes every note in a larger clip and preserves IDs and timing");
    pianoRoll.keyPressed (undo);
    check (denseMatches (0, false), "one undo restores every note in the larger selection");
    pianoRoll.keyPressed (redo);
    groupVelocity->onDragStart(); groupVelocity->setValue (0.42, juce::sendNotificationSync); groupVelocity->onDragEnd();
    check (denseMatches (1, true), "larger selected-note set receives one consistent velocity change");
    pianoRoll.keyPressed (deleteNotes);
    check (currentMidiClip()->notes.empty(), "bulk deletion removes all 512 selected notes");
    pianoRoll.keyPressed (undo);
    check (denseMatches (1, true) && ! pianoRoll.hasSelectedNote(), "one bulk-delete undo restores all dense note identities while selection stays pruned");

    // Clipboard commands use copied values and exact IDs returned by the facade.
    // All note edits below enter through the production mouse/key handlers.
    menuItemSelected (33, 2); pianoRoll.fit();
    const auto clipboardTrackId = currentTrack()->id, clipboardClipId = currentMidiClip()->id;
    const auto commandKey = [] (char key) { return juce::KeyPress (key, juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0); };
    const auto copyNotes = commandKey ('C'), cutNotes = commandKey ('X'), pasteNotes = commandKey ('V'), duplicateNotes = commandKey ('D');
    auto noteIds = [] (const std::vector<Session::MidiNoteInfo>& notes)
    {
        std::vector<juce::String> ids; for (const auto& note : notes) ids.push_back (note.id);
        std::sort (ids.begin(), ids.end()); return ids;
    };
    auto addedSince = [&] (const std::vector<Session::MidiNoteInfo>& before)
    {
        std::vector<Session::MidiNoteInfo> added;
        for (const auto& note : currentMidiClip()->notes)
            if (std::none_of (before.begin(), before.end(), [&] (const auto& old) { return old.id == note.id; })) added.push_back (note);
        return added;
    };
    auto markerClick = [&] (double beat)
    {
        const auto at = juce::Point<float> ((float) (55 + beat * groupBeatWidth), 48);
        pianoRoll.mouseDown (groupEvent (at, at, false, 0)); pianoRoll.mouseUp (groupEvent (at, at, false, 0));
    };
    groupClick (groupPoint (1.1, 60));
    groupVelocity->setValue (0.31, juce::sendNotificationSync);
    groupClick (groupPoint (3.35, 64));
    groupVelocity->setValue (0.73, juce::sendNotificationSync);
    pianoRoll.keyPressed (allNotes);
    const auto sourceNotes = currentMidiClip()->notes;
    auto matchesCopiedGroup = [&] (std::vector<Session::MidiNoteInfo> actual, double start)
    {
        auto expected = sourceNotes;
        const auto byPitch = [] (const auto& a, const auto& b) { return a.pitch < b.pitch; };
        std::sort (actual.begin(), actual.end(), byPitch); std::sort (expected.begin(), expected.end(), byPitch);
        if (actual.size() != expected.size()) return false;
        for (size_t i = 0; i < actual.size(); ++i)
            if (actual[i].pitch != expected[i].pitch || actual[i].startBeat != start + expected[i].startBeat - 1
                || actual[i].lengthBeats != expected[i].lengthBeats || actual[i].velocity != expected[i].velocity
                || actual[i].id == expected[i].id) return false;
        return true;
    };
    pianoRoll.keyPressed (copyNotes);
    check (noteIds (currentMidiClip()->notes) == noteIds (sourceNotes) && status.contains ("inside Halftrack"),
           "Copy preserves notes and explicitly describes the internal clipboard");
    markerClick (6); pianoRoll.keyPressed (pasteNotes);
    const auto firstPaste = addedSince (sourceNotes);
    check (matchesCopiedGroup (firstPaste, 6) && pianoRoll.hasSelectedNote(),
           "Paste uses the visible ruler marker with fresh IDs and exact relative pitch, timing and velocity");
    pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    bool exactSelection = true;
    for (const auto& old : sourceNotes) exactSelection = exactSelection && noteById (old.id).pitch == old.pitch;
    for (const auto& pasted : firstPaste) exactSelection = exactSelection && noteById (pasted.id).pitch == pasted.pitch + 1;
    check (exactSelection, "Paste selects exactly returned IDs, so later edits leave source notes untouched");
    pianoRoll.keyPressed (undo);
    const auto beforeSecondPaste = currentMidiClip()->notes;
    pianoRoll.keyPressed (pasteNotes);
    check (matchesCopiedGroup (addedSince (beforeSecondPaste), 9.25), "repeated Paste advances by the complete group extent");
    const auto beforeDuplicate = currentMidiClip()->notes;
    pianoRoll.keyPressed (duplicateNotes);
    const auto duplicated = addedSince (beforeDuplicate);
    check (matchesCopiedGroup (duplicated, 12.5), "Duplicate inserts after the selected extent on the note grid");
    pianoRoll.keyPressed (undo);
    check (noteIds (currentMidiClip()->notes) == noteIds (beforeDuplicate) && session.canRedo(), "one undo removes the whole duplicate group");
    const auto clipboardProject = noteLoad.directory.getChildFile ("Clipboard notes.halftrack");
    const auto clipboardSave = session.saveProject (clipboardProject);
    check (clipboardSave.wasOk(), "save clipboard project: " + clipboardSave.getErrorMessage());
    check (! session.snapshot().dirty, "saved clipboard project has no pending edits");
    check (session.canRedo(), "saving preserves duplicate redo before rejected Paste");
    pianoRoll.keyPressed (pasteNotes);
    check (noteIds (currentMidiClip()->notes) == noteIds (beforeDuplicate) && ! session.snapshot().dirty && session.canRedo()
               && status.contains ("duration inside the MIDI clip"),
           "out-of-bounds Paste rejects the entire group without dirtying the project or discarding redo");
    pianoRoll.keyPressed (redo);
    for (const auto& copied : duplicated) check (noteById (copied.id).startBeat == copied.startBeat, "redo restores each duplicate identity and position");
    groupClick (groupPoint (12.6, 60)); groupClick (groupPoint (14.85, 64), shiftModifier);
    check (session.saveProject (clipboardProject).wasOk(), "save before impossible duplicate");
    const auto beforeRejectedDuplicate = currentMidiClip()->notes;
    pianoRoll.keyPressed (duplicateNotes);
    check (noteIds (currentMidiClip()->notes) == noteIds (beforeRejectedDuplicate) && ! session.snapshot().dirty && pianoRoll.hasSelectedNote(),
           "out-of-bounds Duplicate neither trims notes nor clears the original selection");
    pianoRoll.keyPressed (cutNotes);
    check (noteIds (currentMidiClip()->notes) == noteIds (beforeDuplicate) && ! pianoRoll.hasSelectedNote(), "Cut atomically removes the selected group");
    pianoRoll.keyPressed (undo);
    check (noteIds (currentMidiClip()->notes) == noteIds (beforeRejectedDuplicate), "one Cut undo restores all original IDs");
    pianoRoll.keyPressed (redo);
    check (noteIds (currentMidiClip()->notes) == noteIds (beforeDuplicate), "one Cut redo removes the complete group again");
    pianoRoll.keyPressed (pasteNotes);
    const auto afterCutPaste = addedSince (beforeDuplicate);
    check (matchesCopiedGroup (afterCutPaste, 12.5) && noteIds (afterCutPaste) != noteIds (duplicated),
           "Paste after Cut uses the cut start marker and allocates distinct identities");
    const auto oldClipNotes = currentMidiClip()->notes;
    menuItemSelected (34, 2); pianoRoll.fit();
    check (currentMidiClip()->id != clipboardClipId && currentMidiClip()->notes.empty() && ! pianoRoll.hasSelectedNote(),
           "changing clips resets the insertion point and prunes selection");
    pianoRoll.keyPressed (pasteNotes);
    check (matchesCopiedGroup (currentMidiClip()->notes, 0), "typed note clipboard survives switching clips and pastes at the new clip start");
    bool oldClipUnchanged = false;
    for (const auto& track : state.tracks) if (track.id == clipboardTrackId)
        for (const auto& clip : track.clips) if (clip.id == clipboardClipId) oldClipUnchanged = noteIds (clip.notes) == noteIds (oldClipNotes);
    check (oldClipUnchanged, "cross-clip Paste cannot edit stale source IDs");
    markerClick (5); pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    const auto beforeCursorPaste = currentMidiClip()->notes;
    pianoRoll.keyPressed (pasteNotes);
    check (matchesCopiedGroup (addedSince (beforeCursorPaste), 5.25), "arrow key moves the insertion marker by one grid step when no notes are selected");

    if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
    {
        const juce::File directory (juce::String::fromUTF8 (configured));
        if (directory.createDirectory().wasOk()) if (auto* host = getParentComponent())
        {
            const auto oldBounds = host->getBounds();
            for (const auto size : { juce::Point<int> (1440, 900), juce::Point<int> (1100, 720) })
            {
                host->setSize (size.x, size.y); pianoRoll.fit(); refresh();
                const auto image = host->createComponentSnapshot (host->getLocalBounds(), true, 1.0f);
                const auto output = directory.getChildFile ("piano-clipboard-" + juce::String (size.x) + "x" + juce::String (size.y) + ".png");
                auto stream = output.createOutputStream(); juce::PNGImageFormat png;
                if (stream && stream->setPosition (0) && stream->truncate().wasOk() && png.writeImageToStream (image, *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
                else throw std::runtime_error ("Could not write clipboard UI snapshot");
            }
            host->setSize (oldBounds.getWidth(), oldBounds.getHeight()); pianoRoll.fit(); refresh();
        }
    }

    // Exercise real focus rather than sending commands to an unfocused text
    // editor. These calls intentionally do not invoke the OS clipboard at all.
    {
        const auto beforeText = noteIds (currentMidiClip()->notes);
        juce::TextEditor editor;
        editor.setText ("Text entry keeps its own commands");
        juce::DocumentWindow focusWindow ("Halftrack shortcut test", panel, juce::DocumentWindow::closeButton);
        focusWindow.setUsingNativeTitleBar (true); focusWindow.setContentNonOwned (&editor, false);
        focusWindow.centreWithSize (300, 70); focusWindow.setVisible (true);
        // A CTest-launched macOS app is initially inactive. Showing a temporary
        // peer alone does not give it key-window focus; activate the real host.
        juce::Process::makeForegroundProcess(); focusWindow.toFront (true);
        for (int attempt = 0; attempt < 20 && ! editor.hasKeyboardFocus (true); ++attempt)
        {
            editor.grabKeyboardFocus();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        check (editor.hasKeyboardFocus (true), "native text entry receives real keyboard focus for shortcut isolation");
        check (! pianoRoll.keyPressed (copyNotes) && ! pianoRoll.keyPressed (cutNotes)
                   && ! pianoRoll.keyPressed (pasteNotes) && ! pianoRoll.keyPressed (duplicateNotes)
                   && noteIds (currentMidiClip()->notes) == beforeText,
               "Copy/Cut/Paste/Duplicate bubbling from text entry never become note commands");
        editor.giveAwayKeyboardFocus(); focusWindow.clearContentComponent();
    }

    // A project can contain ambiguous note IDs. Neither Cut nor Delete may
    // remove a valid first note before rejecting a later ambiguous identity.
    menuItemSelected (33, 2); pianoRoll.fit();
    groupClick (groupPoint (1.1, 60)); const auto uniqueCutId = currentMidiClip()->notes.back().id;
    groupClick (groupPoint (3.1, 64)); const auto ambiguousCutId = currentMidiClip()->notes.back().id;
    groupClick (groupPoint (5.1, 67)); const auto repeatedCutId = currentMidiClip()->notes.back().id;
    const auto ambiguousProject = noteLoad.directory.getChildFile ("Ambiguous clipboard notes.halftrack");
    check (session.saveProject (ambiguousProject).wasOk(), "save original ambiguous-ID fixture before mutation");
    auto ambiguousXml = juce::XmlDocument::parse (ambiguousProject);
    auto* repeatedNote = findNoteXml (ambiguousXml.get(), repeatedCutId);
    check (repeatedNote != nullptr, "find generated note for repeated-ID fixture");
    repeatedNote->setAttribute ("halftrackNoteId", ambiguousCutId);
    check (ambiguousXml->writeTo (ambiguousProject) && session.loadProject (ambiguousProject).wasOk(), "reopen generated project containing a repeated note identity");
    refresh();
    groupClick (groupPoint (1.1, 60)); pianoRoll.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)); pianoRoll.keyPressed (undo);
    check (noteById (uniqueCutId).startBeat == 1 && session.saveProject (ambiguousProject).wasOk() && session.canRedo(),
           "unique-note edit establishes redo before ambiguous group commands");
    pianoRoll.keyPressed (allNotes);
    const auto ambiguousBefore = currentMidiClip()->notes;
    pianoRoll.keyPressed (cutNotes);
    check (noteIds (currentMidiClip()->notes) == noteIds (ambiguousBefore) && ! session.snapshot().dirty && session.canRedo() && pianoRoll.hasSelectedNote(),
           "ambiguous Cut preserves every note, selection, clean state and redo");
    pianoRoll.keyPressed (deleteNotes);
    check (noteIds (currentMidiClip()->notes) == noteIds (ambiguousBefore) && ! session.snapshot().dirty && session.canRedo() && pianoRoll.hasSelectedNote(),
           "ambiguous Delete is one prevalidated group operation and never partially deletes");
    menuItemSelected (34, 2); pianoRoll.fit(); pianoRoll.keyPressed (pasteNotes);
    check (matchesCopiedGroup (currentMidiClip()->notes, 0), "rejected Cut preserves the prior typed clipboard across another clip switch");

    // Exercise the actual quantization shortcut and selection preservation.
    pianoRoll.keyPressed (allNotes);
    auto unquantized = currentMidiClip()->notes;
    for (auto& note : unquantized) note.startBeat += .06;
    const auto toolsClip = currentMidiClip()->id;
    check (session.updateMidiNotes (toolsClip, unquantized).wasOk(), "prepare off-grid notes for production quantize shortcut"); refresh();
    pianoRoll.keyPressed (allNotes);
    const auto toolIds = noteIds (currentMidiClip()->notes);
    const juce::KeyPress quantize ('U', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0);
    check (pianoRoll.keyPressed (quantize), "production Cmd/Ctrl+U is handled");
    bool onGrid = true;
    for (const auto& note : currentMidiClip()->notes) onGrid = onGrid && std::abs (note.startBeat/.25-std::round(note.startBeat/.25)) < 1e-8;
    check (onGrid && noteIds (currentMidiClip()->notes) == toolIds && pianoRoll.hasSelectedNote(), "quantization preserves selection and stable IDs");
    pianoRoll.keyPressed (undo);
    check (noteIds (currentMidiClip()->notes) == toolIds
        && std::abs (currentMidiClip()->notes.front().startBeat-unquantized.front().startBeat) < 1e-8,
        "one production undo restores quantized group");

    check (session.newProject().wasOk() && session.importAudio(audioFixture,0).wasOk(), "prepare arrangement audio command fixture"); refresh();
    const auto originalAudio = state.tracks[0].clips[0].id;
    session.seek(.05); audioClipCommand(3,originalAudio);
    check(state.tracks[0].clips.size()==2 && selectedClip>=0 && state.tracks[0].clips[(size_t)selectedClip].id!=originalAudio,
        "production split command selects the newly created audio clip");
    const auto splitAudio = state.tracks[0].clips[(size_t)selectedClip].id;audioClipCommand(4,splitAudio);
    check(state.tracks[0].clips.size()==3 && state.tracks[0].clips[(size_t)selectedClip].id!=splitAudio,
        "production duplicate command selects a fresh clip identity");
    session.undo();session.undo();refresh();check(state.tracks[0].clips.size()==1,"two undo steps restore split and duplicate");
    const auto beforeDialogs=dialogs.size();audioClipCommand(2,originalAudio);
    check(dialogs.size()==beforeDialogs+1 && dialogs.back()!=nullptr,"production audio editor opens a native dialog");
    session.newProject();session.dispatchPendingMessages();
    if(dialogs.back()!=nullptr)delete dialogs.back().getComponent();

    check(session.importAudio(audioFixture,0).wasOk(),"import direct fade gesture fixture");
    showArrangementView();pixelsPerSecond=4000;viewStart=0;selectedTrack=0;selectedClip=0;refresh();
    const auto fadeEvent = [&] (juce::Point<float> at,juce::Point<float> down,bool dragged=false)
    {
        const auto now=juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1.0f,0,0,0,0,&canvas,&canvas,now,down,now,1,dragged);
    };
    auto fadePoint=canvas.fadeHandle(state.tracks[0].clips[0],0,false);
    canvas.mouseDown(fadeEvent(fadePoint,fadePoint));
    auto fadeTo=fadePoint+juce::Point<float>(100,0);canvas.mouseDrag(fadeEvent(fadeTo,fadePoint,true));
    fadeTo=fadePoint+juce::Point<float>(140,0);canvas.mouseDrag(fadeEvent(fadeTo,fadePoint,true));canvas.mouseUp(fadeEvent(fadeTo,fadePoint,true));
    check(std::abs(state.tracks[0].clips[0].audio.fadeInSeconds-.035)<1e-9&&state.tracks[0].clips[0].start==0,
        "timeline fade-in drag changes duration without moving the clip");
    session.undo();refresh();check(state.tracks[0].clips[0].audio.fadeInSeconds==0,"one undo restores a complete fade drag");session.redo();refresh();
    fadePoint=canvas.fadeHandle(state.tracks[0].clips[0],0,true);
    canvas.mouseDown(fadeEvent(fadePoint,fadePoint));fadeTo=fadePoint-juce::Point<float>(2000,0);
    canvas.mouseDrag(fadeEvent(fadeTo,fadePoint,true));canvas.mouseUp(fadeEvent(fadeTo,fadePoint,true));
    check(std::abs(state.tracks[0].clips[0].audio.fadeOutSeconds-.065)<1e-9,"fade-out handle clamps against the retained fade-in");
    // Both handles remain separately selectable when the fades meet.
    const auto inHandle=canvas.fadeHandle(state.tracks[0].clips[0],0,false),outHandle=canvas.fadeHandle(state.tracks[0].clips[0],0,true);
    check(canvas.hitFade(state.tracks[0].clips[0],0,inHandle)==1&&canvas.hitFade(state.tracks[0].clips[0],0,outHandle)==2,
        "meeting fade handles have distinct pointer targets");
    session.undo();refresh();
    fadePoint=canvas.fadeHandle(state.tracks[0].clips[0],0,false);
    canvas.mouseDown(fadeEvent(fadePoint,fadePoint));fadeTo=fadePoint-juce::Point<float>(1000,0);
    canvas.mouseDrag(fadeEvent(fadeTo,fadePoint,true));canvas.mouseUp(fadeEvent(fadeTo,fadePoint,true));
    check(state.tracks[0].clips[0].audio.fadeInSeconds==0,"fade-in clamps to zero");session.undo();refresh();
    if(const auto path=juce::SystemStats::getEnvironmentVariable("HALFTRACK_UI_SNAPSHOT",{});path.isNotEmpty())
    {
        const juce::File requested(path);const auto output=requested.getSiblingFile(requested.getFileNameWithoutExtension()+"-audio.png");
        auto stream=output.createOutputStream();juce::PNGImageFormat png;
        check(stream&&stream->setPosition(0)&&stream->truncate().wasOk()&&png.writeImageToStream(canvas.createComponentSnapshot(canvas.getLocalBounds()),*stream),
            "render actual timeline fade controls");
    }
    fadePoint=canvas.fadeHandle(state.tracks[0].clips[0],0,false);canvas.mouseDown(fadeEvent(fadePoint,fadePoint));
    check(session.removeClip(0,0).wasOk()&&session.importAudio(audioFixture,0).wasOk(),"replace clip during pending pointer drag");refresh();
    fadeTo=fadePoint+juce::Point<float>(60,0);canvas.mouseDrag(fadeEvent(fadeTo,fadePoint,true));canvas.mouseUp(fadeEvent(fadeTo,fadePoint,true));
    check(state.tracks[0].clips[0].audio.fadeInSeconds==0,"stale fade drag cannot edit the replacement at the same index");
    selectedClip=0;session.seek(.05);refresh();
    check(canvas.keyPressed(commandKey('E'))&&state.tracks[0].clips.size()==2,"Cmd/Ctrl+E splits selected arrangement audio");
    check(canvas.keyPressed(commandKey('D'))&&state.tracks[0].clips.size()==3,"Cmd/Ctrl+D duplicates selected arrangement audio");
    session.undo();session.undo();refresh();check(state.tracks[0].clips.size()==1,"keyboard split and duplicate undo separately");selectedClip=0;
    check(canvas.keyPressed(commandKey('C'))&&session.hasAudioClipClipboard(),"Cmd/Ctrl+C copies arrangement audio");
    session.seek(.25);refresh();check(canvas.keyPressed(commandKey('V'))&&state.tracks[0].clips.size()==2,
        "Cmd/Ctrl+V pastes arrangement audio at playhead");
    const auto pastedAudio=state.tracks[0].clips[(size_t)selectedClip];
    check(pastedAudio.start==.25&&pastedAudio.id!=state.tracks[0].clips[0].id,"paste selects new clip identity and requested position");
    check(canvas.keyPressed(commandKey('X'))&&state.tracks[0].clips.size()==1&&selectedClip==-1,"Cmd/Ctrl+X cuts audio and clears stale selection");
    session.undo();refresh();check(state.tracks[0].clips.size()==2,"one undo restores Cut");
    session.undo();refresh();check(state.tracks[0].clips.size()==1,"second undo removes Paste while Copy added no edit");selectedClip=0;
    {
        juce::TextEditor editor;juce::DocumentWindow focusWindow("Halftrack audio shortcut test",panel,juce::DocumentWindow::closeButton);
        focusWindow.setUsingNativeTitleBar(true);focusWindow.setContentNonOwned(&editor,false);focusWindow.centreWithSize(300,70);focusWindow.setVisible(true);
        juce::Process::makeForegroundProcess();focusWindow.toFront(true);
        for(int attempt=0;attempt<20&&!editor.hasKeyboardFocus(true);++attempt)
        {editor.grabKeyboardFocus();juce::MessageManager::getInstance()->runDispatchLoopUntil(10);}
        check(editor.hasKeyboardFocus(true)&&!canvas.keyPressed(commandKey('E'))&&!canvas.keyPressed(commandKey('D'))&&!canvas.keyPressed(commandKey('C'))&&!canvas.keyPressed(commandKey('X'))
            &&!canvas.keyPressed(commandKey('V'))&&state.tracks[0].clips.size()==1,
            "focused text entry keeps its own clipboard and does not trigger audio edits");
        editor.giveAwayKeyboardFocus();focusWindow.clearContentComponent();
    }
    const auto rangeId=state.tracks[0].clips[0].id;
    Session::AudioClipSettings rangeFades;rangeFades.fadeInSeconds=.005;rangeFades.fadeOutSeconds=.005;
    check(session.setAudioClipSettings(rangeId,rangeFades).wasOk()&&session.setAudioClipRange(rangeId,.02,.06,.01).wasOk(),
        "prepare nonzero source offset and fades for direct range gestures");refresh();selectedTrack=0;selectedClip=0;
    const auto rangeEvent=[&](juce::Point<float> at,juce::Point<float> down,bool dragged,int modifiers)
    {
        const auto now=juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|modifiers),1.0f,0,0,0,0,&canvas,&canvas,now,down,now,1,dragged);
    };
    const auto rangeDrag=[&](juce::Point<float> from,float dx,int modifiers)
    {
        auto to=from+juce::Point<float>(dx,0);
        canvas.mouseDown(rangeEvent(from,from,false,modifiers));
        canvas.mouseDrag(rangeEvent((from+to)*.5f,from,true,modifiers));
        canvas.mouseDrag(rangeEvent(to,from,true,modifiers));
        canvas.mouseUp(rangeEvent(to,from,true,modifiers));
    };
    const auto rangePoint=[&](bool right)
    {
        const auto bounds=canvas.clipBounds(state.tracks[0].clips[0],0);
        return juce::Point<float>(right?bounds.getRight()-2:bounds.getX()+2,bounds.getBottom()-8);
    };
    const auto rangeIs=[&](double start,double length,double offset)
    {
        const auto& clip=state.tracks[0].clips[0];
        // Native fade getters normalize when subtraction rounds the length a
        // fraction below the sum; tolerate only floating-point-scale error.
        return std::abs(clip.start-start)<1e-8&&std::abs(clip.length-length)<1e-8&&std::abs(clip.sourceOffset-offset)<1e-8
            &&std::abs(clip.audio.fadeInSeconds-rangeFades.fadeInSeconds)<1e-12
            &&std::abs(clip.audio.fadeOutSeconds-rangeFades.fadeOutSeconds)<1e-12
            &&clip.audio.gainDb==rangeFades.gainDb&&clip.audio.fadeInCurve==rangeFades.fadeInCurve&&clip.audio.fadeOutCurve==rangeFades.fadeOutCurve;
    };
    const int freeRange=juce::ModifierKeys::altModifier,slipRange=freeRange|juce::ModifierKeys::shiftModifier;
    rangeDrag(rangePoint(false),40,freeRange);
    check(rangeIs(.03,.05,.02),"left trim moves start and source offset together, retaining end and fades");
    session.undo();refresh();check(rangeIs(.02,.06,.01),"one undo restores the whole multi-event left trim");
    session.redo();refresh();check(rangeIs(.03,.05,.02),"redo restores trimmed range");session.undo();refresh();
    rangeDrag(rangePoint(false),-2000,freeRange);check(rangeIs(.01,.07,0),"left trim extends only to source start");session.undo();refresh();
    rangeDrag(rangePoint(false),2000,freeRange);check(rangeIs(.07,.01,.06),"left trim cannot remove retained fade durations");session.undo();refresh();
    rangeDrag(rangePoint(true),-40,freeRange);check(rangeIs(.02,.05,.01),"right trim changes length while retaining start and source offset");
    session.undo();refresh();check(rangeIs(.02,.06,.01),"right trim undoes as one gesture");
    rangeDrag(rangePoint(true),2000,freeRange);check(rangeIs(.02,.09,.01),"right trim clamps at source end");session.undo();refresh();
    rangeDrag(rangePoint(true),-2000,freeRange);check(rangeIs(.02,.01,.01),"right trim preserves both fades at its minimum length");session.undo();refresh();
    auto audioBodyPoint=canvas.clipBounds(state.tracks[0].clips[0],0).getCentre();
    rangeDrag(audioBodyPoint,-80,slipRange);check(rangeIs(.02,.06,.03),"Shift+Alt body drag slips source without moving timeline range");
    session.undo();refresh();check(rangeIs(.02,.06,.01),"source slip undoes in one step");
    rangeDrag(audioBodyPoint,-2000,slipRange);check(rangeIs(.02,.06,.04),"source slip clamps at last readable region");session.undo();refresh();
    rangeDrag(audioBodyPoint,2000,slipRange);check(rangeIs(.02,.06,0),"source slip clamps at source start");session.undo();refresh();
    // Set a grid small enough to expose snapping inside the bounded 100 ms source.
    session.setTempo(300);
    check(session.setAudioClipRange(rangeId,.02,.06,.01).wasOk(),"restore absolute range after tempo change");refresh();
    rangeDrag(rangePoint(false),40,0);
    check(rangeIs(.05,.03,.04),"edge trim snaps to project grid unless Alt is held");session.undo();refresh();
    session.setTempo(120);refresh();
    // Drag the same identity through multiple owners within one gesture.
    selectedClip=0;refresh();
    auto movingClip=state.tracks[0].clips[0];const auto moveFrom=canvas.clipBounds(movingClip,0).getCentre();
    const auto moveTo=moveFrom+juce::Point<float>(40,(float)trackHeight);
    canvas.mouseDown(rangeEvent(moveFrom,moveFrom,false,freeRange));
    canvas.mouseDrag(rangeEvent(moveTo,moveFrom,true,freeRange));
    const auto moveFurther=moveTo+juce::Point<float>(40,0);
    canvas.mouseDrag(rangeEvent(moveFurther,moveFrom,true,freeRange));canvas.mouseUp(rangeEvent(moveFurther,moveFrom,true,freeRange));
    check(state.tracks[0].clips.empty()&&state.tracks[1].clips[0].id==movingClip.id
        &&std::abs(state.tracks[1].clips[0].start-movingClip.start-.02)<1e-8&&selectedTrack==1&&selectedClip==0,
        "body drag moves audio across tracks and subsequent events keep the same identity selected");
    session.undo();refresh();check(state.tracks[1].clips.empty()&&state.tracks[0].clips[0].id==movingClip.id
        &&std::abs(state.tracks[0].clips[0].start-movingClip.start)<1e-8,"one undo restores cross-track body gesture");
    selectedTrack=0;selectedClip=0;
    for(int mode=0;mode<3;++mode)
    {
        selectedClip=0;const auto currentId=state.tracks[0].clips[0].id;
        check(session.setAudioClipSettings(currentId,rangeFades).wasOk()&&session.setAudioClipRange(currentId,.02,.06,.01).wasOk(),"reset stale gesture fixture");refresh();
        const auto from=mode==0?rangePoint(false):canvas.clipBounds(state.tracks[0].clips[0],0).getCentre();
        const int modifiers=mode==1?slipRange:freeRange;
        canvas.mouseDown(rangeEvent(from,from,false,modifiers));
        check(session.removeClip(0,0).wasOk()&&session.importAudio(audioFixture,0).wasOk(),"replace clip during trim, slip or body drag");refresh();
        const auto to=from+juce::Point<float>(20,0);
        canvas.mouseDrag(rangeEvent(to,from,true,modifiers));canvas.mouseUp(rangeEvent(to,from,true,modifiers));
        check(state.tracks[0].clips[0].start==0&&state.tracks[0].clips[0].length==.1&&state.tracks[0].clips[0].sourceOffset==0,
            "stale range and body gestures cannot edit a replacement clip");
    }
    check(session.newProject().wasOk()&&session.importAudio(audioFixture,0).wasOk()
        &&session.importAudio(audioFixture,0,.03).wasOk(),"prepare overlapping clips for trim sorting");refresh();
    const auto sortId=state.tracks[0].clips[0].id;
    check(session.setAudioClipRange(sortId,.02,.06,.01).wasOk(),"set sortable trim range");refresh();selectedTrack=0;selectedClip=0;
    rangeDrag(rangePoint(false),80,freeRange);
    check(selectedClip>=0&&state.tracks[0].clips[(size_t)selectedClip].id==sortId
        &&std::abs(state.tracks[0].clips[(size_t)selectedClip].start-.04)<1e-8,
        "trimming past another clip preserves selected identity after native sorting");
    session.undo();refresh();
    bool restoredSorted=false;for(const auto& clip:state.tracks[0].clips)if(clip.id==sortId)restoredSorted=std::abs(clip.start-.02)<1e-8;
    check(restoredSorted,"sorted trim still restores its whole gesture in one undo");

    check(session.newProject().wasOk(),"prepare locator UI project");showArrangementView();pixelsPerSecond=100;viewStart=0;refresh();
    auto* addLocatorButton=dynamic_cast<juce::TextButton*>(findNamed(ruler,"Add locator at playhead"));
    auto* previousLocatorButton=dynamic_cast<juce::TextButton*>(findNamed(ruler,"Previous locator"));
    auto* nextLocatorButton=dynamic_cast<juce::TextButton*>(findNamed(ruler,"Next locator"));
    check(addLocatorButton&&previousLocatorButton&&nextLocatorButton,"locator controls have accessible names");
    const auto originalSize=getBounds();
    for(const int width:{1100,1440})
    {
        setSize(width,720);
        check(addLocatorButton->getX()>=canvas.timelineWidth()&&nextLocatorButton->getRight()<=ruler.getWidth(),
            "resizing keeps locator controls inside the track header and clear of the timeline");
    }
    setBounds(originalSize);
    session.seek(1);refresh();addLocatorButton->onClick();
    check(state.locators.size()==1&&state.locators[0].position==1,"Add locator button uses transport position");
    const auto firstLocator=state.locators[0].id;ruler.addAt(3);const auto secondLocator=state.locators.back().id;
    session.seek(0);refresh();nextLocatorButton->onClick();check(state.position==1,"Next locator button seeks first section");
    nextLocatorButton->onClick();check(state.position==3&&!nextLocatorButton->isEnabled(),"Next reaches last locator and disables at end");
    previousLocatorButton->onClick();check(state.position==1,"Previous locator button seeks preceding section");
    const auto locatorEvent=[&](juce::Point<float> at,juce::Point<float> down,bool dragged=false,int modifiers=0)
    {
        const auto now=juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|modifiers),1.0f,0,0,0,0,&ruler,&ruler,now,down,now,1,dragged);
    };
    const auto markerById=[&](const juce::String& id)
    {
        for(const auto& marker:state.locators)if(marker.id==id)return marker;
        throw std::runtime_error("Expected locator is missing");
    };
    auto locatorPoint=ruler.locatorBounds(markerById(firstLocator)).getCentre();
    check(ruler.hitLocator(locatorPoint)==firstLocator,"locator hit testing resolves stable identity");
    ruler.mouseDown(locatorEvent(locatorPoint,locatorPoint));
    ruler.mouseDrag(locatorEvent(locatorPoint+juce::Point<float>(20,0),locatorPoint,true));
    ruler.mouseDrag(locatorEvent(locatorPoint+juce::Point<float>(40,0),locatorPoint,true));
    ruler.mouseUp(locatorEvent(locatorPoint+juce::Point<float>(40,0),locatorPoint,true));
    check(markerById(firstLocator).position==1.375,"locator drag snaps to the existing grid");
    session.undo();refresh();check(markerById(firstLocator).position==1,"one undo restores entire locator drag");
    ruler.mouseDown(locatorEvent(locatorPoint,locatorPoint,false,freeRange));
    ruler.mouseDrag(locatorEvent(locatorPoint+juce::Point<float>(37,0),locatorPoint,true,freeRange));
    ruler.mouseUp(locatorEvent(locatorPoint+juce::Point<float>(37,0),locatorPoint,true,freeRange));
    check(std::abs(markerById(firstLocator).position-1.37)<1e-9,"Alt locator drag bypasses snapping");session.undo();refresh();
    ruler.mouseDoubleClick(locatorEvent(locatorPoint,locatorPoint));
    check(!alerts.empty()&&alerts.back()!=nullptr,"locator double-click opens the production editor");
    auto* locatorDialog=alerts.back().getComponent();locatorDialog->getTextEditor("name")->setText("Bridge");
    locatorDialog->getTextEditor("position")->setText("2.25");locatorDialog->exitModalState(1);
    for(int attempt=0;attempt<50;++attempt)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);refresh();
        if(markerById(firstLocator).name=="Bridge")break;
    }
    check(markerById(firstLocator).name=="Bridge"&&markerById(firstLocator).position==2.25,"locator editor applies name and time");
    session.undo();refresh();check(markerById(firstLocator).position==1&&markerById(firstLocator).name!="Bridge","editor mutation undoes in one step");
    locatorPoint=ruler.locatorBounds(markerById(firstLocator)).getCentre();ruler.mouseDown(locatorEvent(locatorPoint,locatorPoint));ruler.mouseUp(locatorEvent(locatorPoint,locatorPoint));
    check(ruler.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey))&&state.locators.size()==1&&state.locators[0].id==secondLocator,
        "Delete in focused locator ruler removes selected locator only");session.undo();refresh();
    locatorPoint=ruler.locatorBounds(markerById(firstLocator)).getCentre();ruler.mouseDown(locatorEvent(locatorPoint,locatorPoint));
    check(session.removeLocator(firstLocator).wasOk(),"delete locator during pending gesture");
    juce::String replacementLocator;check(session.addLocator(1,"Replacement",replacementLocator).wasOk(),"replace locator at same position");refresh();
    ruler.mouseDrag(locatorEvent(locatorPoint+juce::Point<float>(50,0),locatorPoint,true));ruler.mouseUp(locatorEvent(locatorPoint+juce::Point<float>(50,0),locatorPoint,true));
    check(markerById(replacementLocator).position==1,"stale locator drag cannot move replacement");
    const auto loopFrom=juce::Point<float>(50,20),loopTo=juce::Point<float>(240,20);
    ruler.mouseDown(locatorEvent(loopFrom,loopFrom,false,shiftModifier));ruler.mouseDrag(locatorEvent(loopTo,loopFrom,true,shiftModifier));ruler.mouseUp(locatorEvent(loopTo,loopFrom,true,shiftModifier));
    check(state.looping&&state.loopStart==.5&&state.loopEnd==2.5,"Shift-drag loop editing remains available above locators");
    if(const auto path=juce::SystemStats::getEnvironmentVariable("HALFTRACK_UI_SNAPSHOT",{});path.isNotEmpty())
    {
        const juce::File requested(path);const auto output=requested.getSiblingFile(requested.getFileNameWithoutExtension()+"-locators.png");
        auto stream=output.createOutputStream();juce::PNGImageFormat png;
        check(stream&&stream->setPosition(0)&&stream->truncate().wasOk()&&png.writeImageToStream(createComponentSnapshot(getLocalBounds()),*stream),"render actual locator ruler and controls");
    }
    check(session.newProject().wasOk(),"prepare MIDI interchange UI fixture");refresh();
    const auto midiMenuEnabled=[&](int id)
    {
        const auto fileMenu=getMenuForIndex(0,{});juce::PopupMenu::MenuItemIterator items(fileMenu);
        while(items.next())if(items.getItem().itemID==id)return items.getItem().isEnabled;
        throw std::runtime_error("Expected MIDI menu item is missing");
    };
    check(midiMenuEnabled(9)&&!midiMenuEnabled(10),"MIDI import is enabled and export requires a selected MIDI clip");
    juce::TemporaryFile midiInput(".mid"),midiOutput(".mid");
    {juce::MidiMessageSequence notes;auto on=juce::MidiMessage::noteOn(3,62,(juce::uint8)90);on.setTimeStamp(240);notes.addEvent(on);
    auto off=juce::MidiMessage::noteOff(3,62);off.setTimeStamp(720);notes.addEvent(off);juce::MidiFile file;file.setTicksPerQuarterNote(960);file.addTrack(notes);
    auto stream=midiInput.getFile().createOutputStream();check(stream&&file.writeTo(*stream,0),"write MIDI UI fixture");}
    importMidiSelection(midiInput.getFile(),4);
    check(showPiano&&currentMidiClip()!=nullptr&&currentMidiClip()->startBeat==4&&currentMidiClip()->notes.size()==1
        &&currentMidiClip()->notes[0].pitch==62,"production import completion selects new notes in piano roll at captured beat");
    check(midiMenuEnabled(10),"MIDI export becomes enabled for the imported selection");
    const auto importedMidiId=currentMidiClip()->id;exportMidiSelection(importedMidiId,midiOutput.getFile());
    {juce::FileInputStream input(midiOutput.getFile());juce::MidiFile file;
    check(input.openedOk()&&file.readFrom(input)&&file.getTimeFormat()==9600,"production export completion writes a real MIDI file");}
    session.undo();refresh();check(state.tracks.size()==2,"native import completion groups all musical edits in one undo");
    check(midiOutput.getFile().replaceWithText("preserve existing file"),"prepare stale MIDI export destination");
    exportMidiSelection(importedMidiId,midiOutput.getFile());
    check(midiOutput.getFile().loadFileAsString()=="preserve existing file"&&session.canRedo(),"stale MIDI chooser identity cannot export a replacement clip or overwrite destination");

    session.redo();refresh();showArrangementView();selectArrangementClip(importedMidiId);
    const auto midiSourceNote=currentMidiClip()->notes[0].id;
    check(canvas.keyPressed(commandKey('C'))&&session.hasMidiClipClipboard(),"Arrangement Cmd/Ctrl+C copies the complete MIDI clip");
    session.seek(3);refresh();check(canvas.keyPressed(commandKey('V'))&&currentTrack()->clips.size()==2,"Arrangement Cmd/Ctrl+V pastes MIDI at playhead");
    const auto pastedMidi=currentMidiClip()->id;
    check(pastedMidi!=importedMidiId&&currentMidiClip()->start==3&&currentMidiClip()->notes[0].id!=midiSourceNote,"whole MIDI paste selects the new clip with independent note identities");
    session.undo();refresh();check(currentTrack()->clips.size()==1,"one UI undo removes the MIDI paste");session.redo();refresh();selectArrangementClip(pastedMidi);
    check(canvas.keyPressed(commandKey('D'))&&currentTrack()->clips.size()==3,"Arrangement Cmd/Ctrl+D duplicates the complete MIDI clip");
    const auto duplicateMidi=currentMidiClip()->id;
    check(duplicateMidi!=pastedMidi&&std::abs(currentMidiClip()->start-3.375)<1e-9,"MIDI duplicate is selected immediately after its source");
    check(canvas.keyPressed(commandKey('X'))&&currentTrack()->clips.size()==2&&selectedClip==-1,"Arrangement Cmd/Ctrl+X cuts only the selected MIDI clip");
    session.undo();refresh();selectArrangementClip(pastedMidi);
    deleteClipById(importedMidiId);check(currentTrack()->clips.size()==2,"clip menu delete resolves its captured identity after selection changes");
    bool retainedPasted=false;for(const auto& clip:currentTrack()->clips)retainedPasted=retainedPasted||clip.id==pastedMidi;
    check(retainedPasted,"context deletion does not redirect to the newly selected clip");
    deleteClipById(importedMidiId);check(currentTrack()->clips.size()==2,"stale clip menu deletion leaves replacement indices untouched");session.undo();refresh();selectArrangementClip(pastedMidi);
    {
        juce::TextEditor editor;juce::DocumentWindow focusWindow("Halftrack MIDI shortcut test",panel,juce::DocumentWindow::closeButton);
        focusWindow.setUsingNativeTitleBar(true);focusWindow.setContentNonOwned(&editor,false);focusWindow.centreWithSize(300,70);focusWindow.setVisible(true);
        juce::Process::makeForegroundProcess();focusWindow.toFront(true);
        for(int attempt=0;attempt<20&&!editor.hasKeyboardFocus(true);++attempt)
        {editor.grabKeyboardFocus();juce::MessageManager::getInstance()->runDispatchLoopUntil(10);}
        check(editor.hasKeyboardFocus(true)&&!canvas.keyPressed(commandKey('C'))&&!canvas.keyPressed(commandKey('X'))
            &&!canvas.keyPressed(commandKey('V'))&&!canvas.keyPressed(commandKey('D'))&&currentTrack()->clips.size()==3,
            "focused text keeps its own shortcuts while Arrangement MIDI clips are selected");
        editor.giveAwayKeyboardFocus();focusWindow.clearContentComponent();
    }

    check(session.newProject().wasOk()&&session.addMidiTrack("Move source").wasOk()
        &&session.addMidiTrack("Move target").wasOk()&&session.addMidiTrack("Further target").wasOk(),"prepare MIDI body-move UI tracks");
    refresh();showArrangementView();viewStart=0;pixelsPerSecond=100;resized();
    check(session.addMidiClip(state.tracks[2].id,2,4).wasOk(),"prepare MIDI body-move UI clip");refresh();
    const auto movingMidi=state.tracks[2].clips[0].id;
    check(session.addMidiNote(movingMidi,60,0,1,.7f).wasOk(),"prepare movable MIDI note");refresh();selectArrangementClip(movingMidi);
    const auto movingNote=currentMidiClip()->notes[0].id;
    const auto midiFrom=canvas.clipBounds(*currentMidiClip(),2).getCentre();
    const auto midiTo=midiFrom+juce::Point<float>(25,(float)trackHeight);
    const auto midiFurther=midiFrom+juce::Point<float>(50,2.0f*trackHeight);
    canvas.mouseDown(rangeEvent(midiFrom,midiFrom,false,freeRange));canvas.mouseDrag(rangeEvent(midiTo,midiFrom,true,freeRange));
    canvas.mouseDrag(rangeEvent(midiFurther,midiFrom,true,freeRange));canvas.mouseUp(rangeEvent(midiFurther,midiFrom,true,freeRange));
    check(state.tracks[2].clips.empty()&&state.tracks[3].clips.empty()&&selectedTrack==4&&currentMidiClip()->id==movingMidi
        &&currentMidiClip()->notes[0].id==movingNote&&currentMidiClip()->start==1.5,"MIDI drag follows stable identity through successive destination tracks");
    session.undo();refresh();selectArrangementClip(movingMidi);
    check(selectedTrack==2&&currentMidiClip()->start==1&&state.tracks[4].clips.empty(),"one undo restores MIDI drag owner and position");
    const auto forbiddenMidi=midiFrom+juce::Point<float>(25,-2.0f*trackHeight);
    canvas.mouseDown(rangeEvent(midiFrom,midiFrom,false,freeRange));canvas.mouseDrag(rangeEvent(forbiddenMidi,midiFrom,true,freeRange));
    canvas.mouseUp(rangeEvent(forbiddenMidi,midiFrom,true,freeRange));
    check(selectedTrack==2&&currentMidiClip()->start==1&&session.canRedo(),"dragging MIDI over an audio row leaves placement and redo intact");
    canvas.mouseDown(rangeEvent(midiFrom,midiFrom,false,freeRange));
    check(session.removeClip(2,0).wasOk()&&session.addMidiClip(state.tracks[2].id,2,4).wasOk(),"replace MIDI during a pending body drag");refresh();
    const auto replacementMidi=state.tracks[2].clips[0].id;
    canvas.mouseDrag(rangeEvent(midiTo,midiFrom,true,freeRange));canvas.mouseUp(rangeEvent(midiTo,midiFrom,true,freeRange));
    check(state.tracks[2].clips[0].id==replacementMidi&&state.tracks[2].clips[0].start==1&&state.tracks[3].clips.empty(),"stale MIDI body drag cannot move its replacement");

    check(session.newProject().wasOk()&&session.addMidiTrack("Ordered MIDI").wasOk()&&session.addReturnTrack("Ordered return").wasOk(),"prepare track-order UI fixture");
    refresh();showArrangementView();const auto orderMidiId=state.tracks[2].id,orderReturnId=state.tracks[3].id;
    check(session.addMidiClip(orderMidiId,0,4).wasOk()&&session.addMidiClipToSlot(orderMidiId,0,4).wasOk(),"prepare Arrangement and Session selection identities");
    refresh();selectArrangementClip(state.tracks[2].clips[0].id);const auto orderClipId=currentMidiClip()->id;
    const auto findRow=[&](juce::Component& parent,const juce::String& id)
    {
        std::function<TrackRow*(juce::Component&)> search=[&](juce::Component& node)->TrackRow*
        {if(auto* row=dynamic_cast<TrackRow*>(&node);row&&row->getTrackId()==id)return row;for(auto* child:node.getChildren())if(auto* row=search(*child))return row;return nullptr;};
        return search(parent);
    };
    auto* arrangementOrderRow=findRow(canvas,orderMidiId);auto* sessionOrderRow=findRow(launcher,orderMidiId);
    check(arrangementOrderRow&&sessionOrderRow,"both views own the MIDI track's controls");
    auto* orderingName=dynamic_cast<juce::Label*>(findNamed(*arrangementOrderRow,"Track name"));
    check(orderingName!=nullptr,"find actual rename control before reordering");orderingName->showEditor();
    auto* orderingEditor=orderingName->getCurrentTextEditor();check(orderingEditor!=nullptr,"open actual track rename editor");orderingEditor->setText("Renamed after move");
    menuItemSelected(36,2);
    check(selectedTrack==1&&currentTrack()->id==orderMidiId&&currentMidiClip()->id==orderClipId
        &&findRow(canvas,orderMidiId)==arrangementOrderRow&&findRow(launcher,orderMidiId)==sessionOrderRow
        &&orderingName->getCurrentTextEditor()==orderingEditor,"track menu moves both views while preserving active editor and selected clip");
    orderingName->hideEditor(false);refresh();
    check(currentTrack()->name=="Renamed after move"&&state.tracks[2].name!="Renamed after move","rename completion still edits the moved identity");
    session.undo();refresh();session.undo();refresh();
    check(selectedTrack==2&&currentTrack()->id==orderMidiId&&currentMidiClip()->id==orderClipId,"undo order preserves selection identity");
    session.redo();refresh();check(selectedTrack==1&&currentTrack()->id==orderMidiId,"redo order follows selected identity");
    showSessionView();selectLauncherSlot(1,0);const auto orderedSlot=selectedSlot()->id;
    menuItemSelected(37,2);check(selectedTrack==2&&currentTrack()->id==orderMidiId&&selectedSlot()->id==orderedSlot,"Session ordering preserves selected slot identity");
    const auto orderSelected=currentTrack()->id;reorderTrack(orderReturnId,-1);
    check(currentTrack()->id==orderSelected&&state.tracks[2].id==orderReturnId,"captured header command reorders its original track after selection changes");
    const auto beforeStale=state.tracks;check(session.removeTrack(2).wasOk(),"remove context-menu target before completion");refresh();
    reorderTrack(orderReturnId,-1);check(state.tracks.size()==beforeStale.size()-1&&currentTrack()->id==orderSelected,"stale header command cannot reorder replacement index");

    const auto trackCountBeforeCopy=state.tracks.size();const auto selectedBeforeCopy=currentTrack()->id;
    menuItemSelected(38,2);
    check(state.tracks.size()==trackCountBeforeCopy+1&&currentTrack()->id!=selectedBeforeCopy&&currentTrack()->isMidi
        &&currentTrack()->clips.size()==1&&selectedSlot()->hasClip,"Track-menu duplication selects a complete independent MIDI track in Session");
    const auto selectedCopy=currentTrack()->id;session.undo();refresh();
    check(state.tracks.size()==trackCountBeforeCopy,"one UI undo removes the whole copied track");session.redo();refresh();
    const auto copiedIndex=launcherIndices(selectedCopy,{}).first;check(copiedIndex>=0,"UI duplicate redo retains the same track identity");
    selectTrack(0);duplicateTrackFromUI(selectedBeforeCopy);
    check(currentTrack()->isMidi&&currentTrack()->id!=selectedBeforeCopy&&currentTrack()->id!=selectedCopy,"captured header duplicate acts on its original identity after selection changes");
    const auto beforeStaleDuplicate=state.tracks.size();duplicateTrackFromUI("deleted-track");
    check(state.tracks.size()==beforeStaleDuplicate,"stale duplicate action cannot copy a replacement track");

    check(session.newProject().wasOk(),"prepare clean tempo editor fixture");refresh();
    auto* tap=dynamic_cast<juce::Button*>(findNamed(*this,"Tap tempo"));
    check(tap && tap->isVisible(),"transport exposes tap tempo");
    const double beforeTap=juce::Time::getMillisecondCounterHiRes()*.001;tap->onClick();
    const double afterTap=juce::Time::getMillisecondCounterHiRes()*.001;
    check(state.tempo==120 && !session.canUndo(),"first real tap button press anchors without editing");
    check(session.tapTempo(afterTap+1).wasOk(),"complete native button interval with controlled second tap");refresh();
    check(state.tempo>=60.0/(afterTap+1-beforeTap)-1e-7 && state.tempo<=60.0+1e-7,"button publishes its monotonic timestamp to the tempo model");
    session.undo();refresh();check(state.tempo==120,"tap-button tempo change undoes once");
    tempo.setText("500.5",juce::sendNotificationSync);
    check(state.tempo==500.5&&tempo.getText()=="500.50","native tempo editor accepts and displays values above 300 BPM");
    session.undo();refresh();check(state.tempo==120&&session.canRedo(),"tempo editor change undoes once");
    for(const auto& invalid:juce::StringArray{"999.01","19.99","300 trailing","nan","inf",""})
    {
        tempo.setText(invalid,juce::sendNotificationSync);
        check(state.tempo==120&&tempo.getText()=="120.00"&&session.canRedo(),"invalid tempo text restores display without discarding redo");
    }
    auto* master=dynamic_cast<juce::Slider*>(findNamed(*this,"Master volume"));
    check(master&&master->isVisible(),"master strip exposes a real output fader");
    master->setValue(-9,juce::sendNotificationSync);refresh();check(std::abs(state.masterGainDb+9)<1e-4,"master fader updates the native output level");
    master->onDragStart();master->setValue(-3,juce::sendNotificationSync);master->setValue(-6,juce::sendNotificationSync);master->onDragEnd();
    session.undo();refresh();check(std::abs(state.masterGainDb+9)<1e-4,"a complete master-fader drag undoes once");
    check(std::abs(master->valueFromTextFunction("invalid")+9)<1e-4&&master->valueFromTextFunction("-inf dB")==-100,"master text rejects invalid values and accepts silence");
    master->setValue(0,juce::sendNotificationSync);refresh();check(std::abs(state.masterGainDb)<1e-4,"master fader resets to unity");
    menuItemSelected(64,1);check(state.recordQuantisation==Session::RecordQuantisation::sixteenth,"Edit menu sets recording quantization");
    menuItemSelected(20,1);check(state.recordQuantisation==Session::RecordQuantisation::none,"recording quantization menu edit undoes once");
    menuItemSelected(21,1);check(state.recordQuantisation==Session::RecordQuantisation::sixteenth,"recording quantization menu redo restores grid");
    auto* metronome=dynamic_cast<juce::Button*>(findNamed(*this,"Metronome"));
    check(metronome && metronome->isVisible(),"transport exposes the real metronome toggle");
    metronome->setToggleState(true,juce::dontSendNotification);metronome->onClick();refresh();
    check(state.metronome.enabled,"metronome button enables the native click");
    applyMetronomeMenu(11);check(state.metronome.countInBars==1,"native count-in menu chooses one bar");
    session.undo();refresh();check(state.metronome.countInBars==0&&state.metronome.enabled,"count-in menu edit undoes independently");
    applyMetronomeMenu(22);applyMetronomeMenu(1);applyMetronomeMenu(2);
    check(state.metronome.gain==.4f&&!state.metronome.accent&&state.metronome.recordingOnly,"native click menu controls update model");
    tempo.setText("999",juce::sendNotificationSync);check(state.tempo==999&&tempo.getText()=="999.00","native tempo editor reaches exact supported maximum");
    check(session.newProject().wasOk()&&session.importAudio(audioFixture,0).wasOk()&&session.addMidiTrack("Activation").wasOk(),"prepare clip activation controls");refresh();
    const auto activationAudio=state.tracks[0].clips[0].id,activationTrack=state.tracks[2].id;
    check(session.addMidiClip(activationTrack,0,4).wasOk(),"prepare activation MIDI clip");refresh();const auto activationMidi=state.tracks[2].clips[0].id;
    showArrangementView();selectArrangementClip(activationAudio);
    check(canvas.keyPressed(juce::KeyPress('0'))&&!state.tracks[0].clips[0].active,"Arrangement 0 deactivates the selected audio clip");
    session.undo();refresh();check(state.tracks[0].clips[0].active,"clip activation shortcut undoes once");session.redo();refresh();
    selectArrangementClip(activationMidi);check(canvas.keyPressed(juce::KeyPress('0'))&&!currentMidiClip()->active,"Arrangement 0 also deactivates MIDI");
    setClipActiveFromUI(activationAudio,true);check(state.tracks[0].clips[0].active&&!currentMidiClip()->active,"context action uses captured identity rather than the current selection");
    {
        juce::TextEditor editor;juce::DocumentWindow focusWindow("Halftrack activation shortcut test",panel,juce::DocumentWindow::closeButton);
        focusWindow.setUsingNativeTitleBar(true);focusWindow.setContentNonOwned(&editor,false);focusWindow.centreWithSize(300,70);focusWindow.setVisible(true);
        juce::Process::makeForegroundProcess();focusWindow.toFront(true);
        for(int attempt=0;attempt<20&&!editor.hasKeyboardFocus(true);++attempt)
        {editor.grabKeyboardFocus();juce::MessageManager::getInstance()->runDispatchLoopUntil(10);}
        check(editor.hasKeyboardFocus(true)&&!canvas.keyPressed(juce::KeyPress('0'))&&!currentMidiClip()->active,"typing zero in a text editor does not toggle a clip");
        editor.giveAwayKeyboardFocus();focusWindow.clearContentComponent();
    }
    check(session.importAudioToSlot(audioFixture,0,0).wasOk()&&session.addMidiClipToSlot(activationTrack,0,4).wasOk(),"prepare Session activation controls");
    refresh();showSessionView();selectLauncherSlot(0,0);
    check(launcher.keyPressed(juce::KeyPress('0'))&&!selectedSlot()->clip.active,"Session 0 deactivates an audio slot");
    selectLauncherSlot(2,0);check(launcher.keyPressed(juce::KeyPress('0'))&&!selectedSlot()->clip.active,"Session 0 deactivates a MIDI slot");
    session.undo();refresh();check(selectedSlot()->clip.active,"Session activation undoes once");session.redo();refresh();
    if(const auto* configured=std::getenv("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
    {
        const juce::File directory(juce::String::fromUTF8(configured));
        if(directory.createDirectory().wasOk())if(auto* host=getParentComponent())
        {
            const auto old=host->getBounds();host->setSize(1100,720);refresh();
            juce::PNGImageFormat png;auto image=host->createComponentSnapshot(host->getLocalBounds(),true,1.0f);
            {auto output=directory.getChildFile("inactive-session-1100x720.png").createOutputStream();check(output&&png.writeImageToStream(image,*output),"capture inactive Session presentation");}
            showArrangementView();setClipActiveFromUI(activationAudio,false);image=host->createComponentSnapshot(host->getLocalBounds(),true,1.0f);
            {auto output=directory.getChildFile("inactive-arrangement-1100x720.png").createOutputStream();check(output&&png.writeImageToStream(image,*output),"capture inactive Arrangement presentation");}
            host->setBounds(old);
        }
    }
    check(session.removeClip(0,0).wasOk()&&session.importAudio(audioFixture,0).wasOk(),"replace the clip after its context action was captured");refresh();
    setClipActiveFromUI(activationAudio,false);check(state.tracks[0].clips[0].active&&state.tracks[0].clips[0].id!=activationAudio,"stale context identity cannot deactivate a replacement clip");
    check(session.newProject().wasOk()&&session.addMidiTrack("Split notes").wasOk(),"prepare note split UI");refresh();
    const auto splitTrack=state.tracks.back().id;check(session.addMidiClip(splitTrack,0,16).wasOk(),"prepare marker split clip");refresh();
    const auto splitClip=state.tracks.back().clips[0].id;juce::StringArray splitSource;
    check(session.addMidiNotes(splitClip,{{"",60,0,4,.8f},{"",64,1,4,.5f},{"",67,5,1,.7f}},splitSource).wasOk(),"prepare note split phrase");
    refresh();showArrangementView();selectArrangementClip(splitClip);openPianoRoll();pianoRoll.fit();markerClick(2);
    const auto splitKey=commandKey('E');check(pianoRoll.keyPressed(splitKey)&&currentMidiClip()->notes.size()==5,"Cmd/Ctrl E splits all crossing notes when none are selected");
    check(noteById(splitSource[0]).lengthBeats==2&&noteById(splitSource[1]).lengthBeats==1&&noteById(splitSource[2]).lengthBeats==1,"marker split uses clip-relative beats and leaves other notes unchanged");
    const auto splitBeforePitch=currentMidiClip()->notes;const auto splitIds=noteIds(splitBeforePitch);pianoRoll.keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    bool rightHalvesMoved=true;int rightCount=0;
    for(const auto& note:splitBeforePitch)if(!splitSource.contains(note.id)){++rightCount;rightHalvesMoved=rightHalvesMoved&&noteById(note.id).pitch==note.pitch+1;}
    check(rightHalvesMoved&&rightCount==2&&noteById(splitSource[0]).pitch==60&&noteById(splitSource[1]).pitch==64&&noteById(splitSource[2]).pitch==67,"split selects exactly the new right halves without selecting originals");
    pianoRoll.keyPressed(undo);pianoRoll.keyPressed(undo);check(currentMidiClip()->notes.size()==3,"one undo rejoins all split notes");
    pianoRoll.keyPressed(redo);check(noteIds(currentMidiClip()->notes)==splitIds,"split redo retains all note identities");pianoRoll.keyPressed(undo);
    markerClick(2);groupClick(groupPoint(.5,60));pianoRoll.keyPressed(splitKey);
    check(currentMidiClip()->notes.size()==4&&noteById(splitSource[1]).lengthBeats==4,"selected-note split leaves the other intersecting note whole");
    {
        juce::TextEditor editor;juce::DocumentWindow focusWindow("Halftrack split shortcut test",panel,juce::DocumentWindow::closeButton);
        focusWindow.setContentNonOwned(&editor,false);focusWindow.setBounds(120,120,260,80);focusWindow.setVisible(true);editor.grabKeyboardFocus();
        for(int i=0;i<4&&!editor.hasKeyboardFocus(true);++i){juce::MessageManager::getInstance()->runDispatchLoopUntil(10);editor.grabKeyboardFocus();}
        check(editor.hasKeyboardFocus(true)&&!pianoRoll.keyPressed(splitKey)&&currentMidiClip()->notes.size()==4,"note split shortcut does not consume text editing");
        editor.giveAwayKeyboardFocus();focusWindow.clearContentComponent();
    }
    check(session.addMidiClipToSlot(splitTrack,0,16).wasOk(),"prepare Session note split");refresh();const auto slotSplit=state.tracks.back().slots[0].clip.id;
    check(session.addMidiNote(slotSplit,60,0,4,.8f).wasOk(),"draw Session note to split");refresh();showSessionView();selectLauncherSlot(2,0);openPianoRoll();pianoRoll.fit();markerClick(2);pianoRoll.keyPressed(splitKey);
    check(currentMidiClip()->id==slotSplit&&currentMidiClip()->notes.size()==2&&state.tracks[2].clips[0].notes.size()==4,"Session marker splits only the selected slot's notes");
    showArrangementView();selectArrangementClip(splitClip);
    check(canvas.keyPressed(commandKey('R'))&&!alerts.empty()&&alerts.back()!=nullptr,"Cmd/Ctrl R opens the selected clip rename dialog");
    auto finishRename=[&](const juce::String& name,int result)
    {
        auto dialog=alerts.back();check(dialog!=nullptr&&dialog->getTextEditor("name")!=nullptr,"rename dialog has a real text editor");
        dialog->getTextEditor("name")->setText(name);dialog->exitModalState(result);
        for(int i=0;i<10&&dialog!=nullptr;++i)juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        refresh();
    };
    showSessionView();selectLauncherSlot(2,0);finishRename("Arrangement phrase",1);
    check(state.tracks[2].clips[0].name=="Arrangement phrase"&&state.tracks[2].slots[0].clip.name!="Arrangement phrase","rename callback retains the original clip when selection changes");
    session.undo();refresh();check(state.tracks[2].clips[0].name!="Arrangement phrase","dialog rename undoes in one step");session.redo();refresh();
    check(launcher.keyPressed(commandKey('R')),"Session rename shortcut opens slot dialog");finishRename("Session phrase",1);
    check(state.tracks[2].slots[0].clip.name=="Session phrase"&&state.tracks[2].clips[0].name=="Arrangement phrase","Session rename changes only its own slot");
    const auto beforeCancel=state.tracks[2].slots[0].clip.name;renameClipDialog(slotSplit);finishRename("Cancelled",0);
    check(state.tracks[2].slots[0].clip.name==beforeCancel,"cancelling the rename dialog does not change the project");
    renameClipDialog(slotSplit);check(session.clearSlot(2,0).wasOk()&&session.addMidiClipToSlot(splitTrack,0,16).wasOk(),"replace slot while its rename dialog is open");refresh();finishRename("Stale name",1);
    check(state.tracks[2].slots[0].clip.id!=slotSplit&&state.tracks[2].slots[0].clip.name!="Stale name","stale rename dialog cannot retarget a new clip");
    check(session.importAudioToSlot(audioFixture,0,0).wasOk(),"add audio beside scene MIDI");refresh();showSessionView();selectLauncherSlot(2,0);
    const auto copiedSceneSource=state.scenes[0].id;const auto originalSceneCount=state.scenes.size();menuItemSelected(56,4);
    const auto newSceneId=selectedSceneId;check(state.scenes.size()==originalSceneCount+1&&selectedSceneIndex()==1&&newSceneId!=copiedSceneSource&&state.tracks[0].slots[1].hasClip&&state.tracks[2].slots[1].hasClip,"Session menu duplicates and selects the complete next scene");
    session.undo();refresh();check(state.scenes.size()==originalSceneCount,"scene-copy menu action undoes once");session.redo();refresh();check(state.scenes[1].id==newSceneId,"scene-copy menu redo retains identity");
    check(session.removeScene(0).wasOk(),"remove captured scene before delayed copy");refresh();const auto countAfterRemoval=state.scenes.size();duplicateSceneFromUI(copiedSceneSource);
    check(state.scenes.size()==countAfterRemoval&&state.scenes[0].id==newSceneId,"stale scene-copy target does not duplicate the replacement row");
    check(session.addReturnTrack("Scene selection return").wasOk(),"add return for scene selection");refresh();selectTrack((int)state.tracks.size()-1);
    selectLauncherSlot(2,0);const auto movingScene=selectedSceneId;const auto movingSceneClip=selectedLauncherClipId;
    menuItemSelected(58,4);check(selectedSceneIndex()==1&&selectedSceneId==movingScene&&selectedLauncherClipId==movingSceneClip&&state.tracks[2].slots[1].clip.id==movingSceneClip,"moving a scene keeps its selected MIDI clip");
    session.undo();refresh();check(selectedSceneIndex()==0&&selectedLauncherClipId==movingSceneClip,"undo follows the same selected scene back to its row");
    session.redo();refresh();menuItemSelected(57,4);check(selectedSceneIndex()==0&&state.tracks[2].slots[0].clip.id==movingSceneClip,"Move Up restores the selected row and its clips");
    selectTrack((int)state.tracks.size()-1);
    const auto selectedReturn=currentTrack()->id;selectedSceneId=newSceneId;menuItemSelected(56,4);
    check(selectedSceneIndex()==1&&selectedSceneId!=newSceneId&&currentTrack()->id==selectedReturn,"scene duplication selects its new row even with a return track selected");
    session.undo();refresh();
    selectLauncherSlot(2,0);openPianoRoll();pianoRoll.fit();const auto activationClip=selectedLauncherClipId;
    check(session.addMidiNote(activationClip,60,0,1,.8f).wasOk()&&session.addMidiNote(activationClip,67,2,1,.6f).wasOk(),"draw notes for activation controls");refresh();
    pianoRoll.keyPressed(juce::KeyPress('A',juce::ModifierKeys(juce::ModifierKeys::commandModifier),0));
    pianoRoll.keyPressed(juce::KeyPress('0'));refresh();
    check(std::all_of(currentMidiClip()->notes.begin(),currentMidiClip()->notes.end(),[](const auto& n){return !n.active;})&&currentMidiClip()->active,"piano-roll zero deactivates selected notes without disabling the clip");
    if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
        if (auto* host = getParentComponent())
        {
            const juce::File directory (juce::String::fromUTF8 (configured));check(directory.createDirectory().wasOk(),"create note activation snapshot directory");
            const auto bounds=host->getBounds();host->setSize(1100,720);pianoRoll.fit();refresh();
            const auto image=host->createComponentSnapshot(host->getLocalBounds(),true,1.0f);
            auto stream=directory.getChildFile("inactive-notes-1100x720.png").createOutputStream();juce::PNGImageFormat png;
            check(stream&&stream->setPosition(0)&&stream->truncate().wasOk()&&png.writeImageToStream(image,*stream),"write actual inactive note view");
            host->setSize(bounds.getWidth(),bounds.getHeight());pianoRoll.fit();refresh();
        }
    const auto inactiveIds=currentMidiClip()->notes;
    pianoRoll.keyPressed(juce::KeyPress('D',juce::ModifierKeys(juce::ModifierKeys::commandModifier),0));refresh();
    check(currentMidiClip()->notes.size()==inactiveIds.size()*2&&std::all_of(currentMidiClip()->notes.begin(),currentMidiClip()->notes.end(),[](const auto& n){return !n.active;}),"note duplicate preserves inactive state");
    session.undo();refresh();session.undo();refresh();check(std::all_of(currentMidiClip()->notes.begin(),currentMidiClip()->notes.end(),[](const auto& n){return n.active;}),"one activation undo restores the whole note selection");
    const auto activationNote=currentMidiClip()->notes.front().id;check(session.setMidiNotesActive(activationClip,{activationNote},false).wasOk(),"prepare mixed note activation");refresh();
    pianoRoll.keyPressed(juce::KeyPress('A',juce::ModifierKeys(juce::ModifierKeys::commandModifier),0));pianoRoll.keyPressed(juce::KeyPress('0'));refresh();
    check(std::all_of(currentMidiClip()->notes.begin(),currentMidiClip()->notes.end(),[](const auto& n){return n.active;}),"mixed selection zero activates all selected notes");
    pianoRoll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));pianoRoll.keyPressed(juce::KeyPress('0'));refresh();
    check(std::all_of(currentMidiClip()->notes.begin(),currentMidiClip()->notes.end(),[](const auto& n){return n.active;}),"zero without selected notes makes no note edit");
    exportDialog();
    auto exportSettings = alerts.back();
    check (exportSettings != nullptr, "production audio export settings open");
    auto* formatChoice = exportSettings->getComboBoxComponent ("format");
    auto* depthChoice = exportSettings->getComboBoxComponent ("bit-depth");
    check (formatChoice && depthChoice && formatChoice->getNumItems() == 3, "WAV, AIFF and FLAC are real export choices");
    check (exportSettings->getComboBoxComponent ("channels")->getNumItems() == 2
        && exportSettings->getComboBoxComponent ("normalization")->getNumItems() == 4,
        "production dialog exposes channel and peak options with unchanged stereo defaults");
    depthChoice->setSelectedId (3, juce::dontSendNotification);
    formatChoice->setSelectedId (2, juce::sendNotificationSync);
    check (! depthChoice->isItemEnabled (3) && depthChoice->getSelectedId() == 2, "AIFF selects supported depth when switching from 32-bit WAV");
    formatChoice->setSelectedId (3, juce::sendNotificationSync);
    check (! depthChoice->isItemEnabled (3), "FLAC disables unsupported 32-bit output");
    formatChoice->setSelectedId (1, juce::sendNotificationSync);
    check (depthChoice->isItemEnabled (3), "WAV restores its 32-bit choice");
    formatChoice->setSelectedId (3, juce::sendNotificationSync);
    exportSettings->getComboBoxComponent ("channels")->setSelectedItemIndex (1, juce::sendNotificationSync);
    exportSettings->getComboBoxComponent ("normalization")->setSelectedItemIndex (2, juce::sendNotificationSync);
    exportSettings->getComboBoxComponent ("effect-tail")->setSelectedItemIndex (0, juce::sendNotificationSync);
    const auto selectedExportSettings = readAudioExportSettings (*exportSettings);
    check (selectedExportSettings.format == Session::ExportFormat::flac && selectedExportSettings.mono
        && selectedExportSettings.normalizePeak && selectedExportSettings.peakCeilingDb == -3 && selectedExportSettings.tailSeconds == 0,
        "production dialog transfers format, channels, normalization and tail to the actual renderer settings");
    if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
    {
        const juce::File directory (juce::String::fromUTF8 (configured)); check (directory.createDirectory().wasOk(), "create export dialog snapshot directory");
        auto stream = directory.getChildFile ("export-options-dialog.png").createOutputStream(); juce::PNGImageFormat png;
        check (stream && stream->setPosition (0) && stream->truncate().wasOk()
            && png.writeImageToStream (exportSettings->createComponentSnapshot (exportSettings->getLocalBounds()), *stream), "capture actual export settings dialog");
    }
    exportSettings->exitModalState (0); juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (session.newProject().wasOk() && session.importAudio (audioFixture, 0).wasOk(), "prepare real audio export UI fixture"); refresh();
    juce::TemporaryFile destination (".flac");
    check (destination.getFile().replaceWithText ("keep this existing audio"), "prepare normalized export destination collision");
    const auto selectedPath = destination.getFile().withFileExtension ("unexpected");
    exportAudioSelection (selectedPath, selectedExportSettings);
    check (alerts.back() != nullptr && destination.getFile().loadFileAsString() == "keep this existing audio", "extension correction requests permission before replacing a different existing file");
    alerts.back()->exitModalState (0); juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (destination.getFile().loadFileAsString() == "keep this existing audio", "cancelled overwrite preserves destination");
    exportAudioSelection (selectedPath, selectedExportSettings);
    alerts.back()->exitModalState (1); juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    juce::AudioFormatManager exportedFormats; exportedFormats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> exportedReader (exportedFormats.createReaderFor (destination.getFile()));
    check (exportedReader && exportedReader->sampleRate == 48000 && exportedReader->bitsPerSample == 24
        && exportedReader->lengthInSamples == 4800 && exportedReader->numChannels == 1 && ! selectedPath.exists(), "confirmed production export writes the normalized FLAC with the requested audio extent");
    juce::AudioBuffer<float> exportedAudio (1, 4800); check (exportedReader->read (&exportedAudio, 0, 4800, 0, true, false), "decode production mono export");
    check (exportedAudio.getMagnitude (0, 4800) > .69f && exportedAudio.getMagnitude (0, 4800) <= std::pow (10.0f, -3.0f / 20), "dialog-selected peak normalization changes the actual exported audio");
    check (session.importAudioToSlot (audioFixture, 0, 0).wasOk(), "prepare follow-action UI clip"); refresh(); showSessionView(); selectLauncherSlot (0, 0);
    const auto followClipId = selectedSlot()->clip.id;
    menuItemSelected (59, 4); check (alerts.back() != nullptr, "Session menu opens follow-action controls");
    const auto finishFollowDialog = [&] (int action, const juce::String& beats, int result)
    {
        auto dialog = alerts.back(); check (dialog != nullptr, "follow-action dialog remains valid");
        dialog->getComboBoxComponent ("action")->setSelectedItemIndex (action, juce::sendNotificationSync);
        check (dialog->getTextEditor ("beats")->isEnabled() == (action != 0), "Off disables timed follow input");
        dialog->getTextEditor ("beats")->setText (beats); dialog->exitModalState (result);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20); refresh();
    };
    finishFollowDialog ((int) Session::FollowAction::cycle, "2.5", 1);
    check (selectedSlot()->clip.follow == Session::FollowSettings { Session::FollowAction::cycle, 2.5 }, "production follow dialog sets actual native beat timing");
    session.undo(); refresh(); check (selectedSlot()->clip.follow.action == Session::FollowAction::off, "follow settings undo once"); session.redo(); refresh();
    clipFollowDialog (followClipId); finishFollowDialog ((int) Session::FollowAction::next, "2oops", 1);
    check (selectedSlot()->clip.follow.action == Session::FollowAction::cycle, "invalid follow timing preserves existing state");
    clipFollowDialog (followClipId); finishFollowDialog ((int) Session::FollowAction::stop, "1", 0);
    check (selectedSlot()->clip.follow.action == Session::FollowAction::cycle, "cancelling follow dialog retains settings");
    clipFollowDialog (followClipId);
    check (session.clearSlot (0, 0).wasOk() && session.importAudioToSlot (audioFixture, 0, 0).wasOk(), "replace follow target while its dialog is open"); refresh();
    finishFollowDialog ((int) Session::FollowAction::next, "1", 1);
    check (selectedSlot()->clip.id != followClipId && selectedSlot()->clip.follow.action == Session::FollowAction::off, "stale follow dialog cannot change replacement clip");
    check (session.newProject().wasOk(), "prepare pending device menu"); refresh(); selectTrack (0);
    const auto pendingDeviceTrackId = state.tracks[0].id;
    const auto pendingDeviceGeneration = state.projectGeneration;
    const auto pendingDeviceTrack = state.tracks[static_cast<size_t> (selectedTrack)].id;
    check (session.moveTrack (pendingDeviceTrackId, 1).wasOk(), "reorder target while device menu is pending"); refresh();
    insertDeviceSelection (pendingDeviceGeneration, pendingDeviceTrack, 100002 + static_cast<int> (Session::EffectKind::bitcrusher), {});
    refresh();
    check (state.tracks[1].id == pendingDeviceTrackId && state.tracks[1].plugins.size() == 1
        && state.tracks[1].plugins[0].effect.kind == Session::EffectKind::bitcrusher && state.tracks[0].plugins.empty(),
        "pending device menu inserts on the original track after reordering");
    session.undo(); refresh(); check (state.tracks[1].id == pendingDeviceTrackId && state.tracks[1].plugins.empty(), "one undo removes the delayed insertion without undoing track order");
    session.redo(); refresh();
    insertDeviceSelection (pendingDeviceGeneration, pendingDeviceTrack, 100001, {}); refresh();
    check (state.tracks[1].plugins.size() == 2 && state.tracks[1].plugins.back().isUtility && state.tracks[0].plugins.empty(),
        "pending Utility selection uses the same stable track resolution");
    check (session.removeTrack (1).wasOk() && session.addAudioTrack ("Replacement").wasOk(), "replace the pending target's former row"); refresh();
    insertDeviceSelection (pendingDeviceGeneration, pendingDeviceTrack, 100002 + static_cast<int> (Session::EffectKind::limiter), {}); refresh();
    check (state.tracks[1].id != pendingDeviceTrackId && state.tracks[1].plugins.empty(), "stale device menu cannot insert into a replacement track");
    check (session.newProject().wasOk(), "replace the project before resolving an old menu"); refresh();
    insertDeviceSelection (pendingDeviceGeneration, pendingDeviceTrack, 100002 + static_cast<int> (Session::EffectKind::gate), {}); refresh();
    check (!state.dirty && !session.canUndo() && !session.canRedo() && state.tracks[0].plugins.empty() && state.tracks[1].plugins.empty(),
        "missing device-menu target preserves a clean replacement project and its history");
    check (state.projectGeneration != pendingDeviceGeneration, "New changes transient project identity even if track IDs repeat");
    const auto savedGeneration = state.projectGeneration; const auto savedTrackId = state.tracks[0].id;
    juce::TemporaryFile menuProject (".halftrack");
    check (session.saveProject (menuProject.getFile()).wasOk() && session.snapshot().projectGeneration == savedGeneration,
        "Save As preserves the active project instance");
    check (session.loadProject (audioFixture).failed() && session.snapshot().projectGeneration == savedGeneration,
        "rejected load preserves the active project instance");
    check (session.loadProject (menuProject.getFile()).wasOk(), "reopen identical track IDs in a replacement edit"); refresh();
    check (state.tracks[0].id == savedTrackId && state.projectGeneration != savedGeneration,
        "successful load changes project instance while preserving saved track identity");
    insertDeviceSelection (savedGeneration, savedTrackId, 100001, {}); refresh();
    check (!state.dirty && !session.canUndo() && state.tracks[0].plugins.empty(), "old menu cannot modify a reopened project with identical track IDs");
    insertDeviceSelection (state.projectGeneration, savedTrackId, 100002 + static_cast<int> (Session::EffectKind::gate), {}); refresh();
    check (state.tracks[0].plugins.size() == 1 && state.tracks[0].plugins[0].effect.kind == Session::EffectKind::gate,
        "fresh menu still inserts after a project replacement");







}

int MainComponent::runInteractionTests()
{
    try
    {
        juce::TemporaryFile audioFixture (".wav");
        {
            std::unique_ptr<juce::OutputStream> stream = audioFixture.getFile().createOutputStream();
            juce::WavAudioFormat wav;
            auto writer = stream ? wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (16)) : nullptr;
            if (! writer) throw std::runtime_error ("Could not write UI audio-drop fixture");
            juce::AudioBuffer<float> audio (1, 4800);
            for (int i = 0; i < audio.getNumSamples(); ++i) audio.setSample (0, i, 0.1f * std::sin ((float) i * 0.031f));
            if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) throw std::runtime_error ("Could not finish UI audio-drop fixture");
        }
        Session session (false, makeSyntheticTestDeviceType());
        MainComponent component (session);
        component.impl->captureTestErrors = true;
        component.impl->runInteractionChecks (audioFixture.getFile());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL UI " << error.what() << std::endl;
        return 1;
    }
}
#endif

MainComponent::MainComponent (Session& session) : impl (std::make_unique<Impl> (session))
{
    addAndMakeVisible (*impl);
    setWantsKeyboardFocus (true);
    setSize (1440, 900);
}
MainComponent::~MainComponent() = default;
void MainComponent::resized() { impl->setBounds (getLocalBounds()); }
bool MainComponent::keyPressed (const juce::KeyPress& key) { return impl->handleKey (key); }
void MainComponent::requestClose (std::function<void()> onClose) { impl->ensureSaved (std::move (onClose)); }
}
