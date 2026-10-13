#include "MainComponent.h"
#include "Session.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>
#if HALFTRACK_TESTS_ENABLED
 #include <iostream>
 #include <stdexcept>
#endif

namespace halftrack
{
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
    explicit Impl (Session& s) : session (s), menu (this), canvas (*this), ruler (*this), pianoRoll (*this), deviceChain (*this)
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
                 &playButton, &stopButton, &recordButton, &tempo, &loopButton, &positionLabel,
                 &projectLabel, &audioButton, &addTrackButton, &addMidiButton, &newMidiClipButton, &arrangementTab, &pianoTab, &importButton, &filesButton, &pluginsButton,
                 &folderButton, &upButton, &browserSearch, &browserList, &ruler, &viewport,
                 &horizontalScroll, &zoomIn, &zoomOut, &fitButton, &pianoRoll, &deviceChain })
            addAndMakeVisible (child);

        for (auto* button : std::initializer_list<juce::Button*> { &playButton, &stopButton, &recordButton, &loopButton,
                 &audioButton, &addTrackButton, &addMidiButton, &newMidiClipButton, &arrangementTab, &pianoTab, &importButton, &filesButton, &pluginsButton, &folderButton,
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
        tempo.setTooltip ("Tempo in beats per minute. Double-click to edit.");
        tempo.onTextChange = [this]
        {
            const auto value = tempo.getText().getDoubleValue();
            if (value >= 20.0 && value <= 300.0) session.setTempo (value);
            refresh();
        };
        positionLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
        positionLabel.setJustificationType (juce::Justification::centred);
        positionLabel.setColour (juce::Label::backgroundColourId, background);
        projectLabel.setFont (font (12.0f));
        projectLabel.setJustificationType (juce::Justification::centredRight);
        playButton.setTooltip ("Play / pause  |  Space");
        stopButton.setTooltip ("Stop and return to the beginning  |  Escape");
        recordButton.setTooltip ("Record into armed audio tracks  |  F9");
        playButton.onClick = [this] { togglePlay(); };
        stopButton.onClick = [this] { session.stop(); session.seek (0); refresh(); };
        recordButton.onClick = [this] { if (state.recording) session.stop(); else report (session.record()); refresh(); };
        loopButton.setClickingTogglesState (true);
        loopButton.setTooltip ("Loop the marked time range. Shift-drag the ruler to change it.  |  Cmd/Ctrl+L");
        loopButton.onClick = [this] { session.setLoop (loopButton.getToggleState(), state.loopStart, state.loopEnd); refresh(); };
        audioButton.onClick = [this] { showAudioSettings(); };
        audioButton.setTooltip ("Audio devices, input/output channels, sample rate and buffer size");
        addTrackButton.onClick = [this] { showPiano = false; addTrack(); resized(); };
        addMidiButton.onClick = [this] { addMidiTrack(); };
        newMidiClipButton.onClick = [this] { createMidiClip (state.position); };
        arrangementTab.onClick = [this] { showPiano = false; resized(); };
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
        auto message = status.isNotEmpty() ? status : showPiano ? "Click: draw / select     Drag: move     Drag right edge: resize     Delete: remove     Arrows: move note     Alt: unsnapped" : "Space: play / pause     Click ruler: seek     Shift-drag ruler: loop     Double-click MIDI clip: edit notes";
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
        toolbar.removeFromLeft (17);
        playButton.setBounds (toolbar.removeFromLeft (34));
        stopButton.setBounds (toolbar.removeFromLeft (34));
        recordButton.setBounds (toolbar.removeFromLeft (34));
        toolbar.removeFromLeft (11);
        positionLabel.setBounds (toolbar.removeFromLeft (121));
        toolbar.removeFromLeft (11);
        loopButton.setBounds (toolbar.removeFromLeft (48));
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
        arrangementTab.setBounds (section.removeFromLeft (98));
        pianoTab.setBounds (section.removeFromLeft (90));
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
        pianoRoll.setBounds (area);
        pianoRoll.setVisible (showPiano);
        viewport.setVisible (! showPiano);
        ruler.setVisible (! showPiano);
        horizontalScroll.setVisible (! showPiano);
        arrangementTab.setToggleState (! showPiano, juce::dontSendNotification);
        pianoTab.setToggleState (showPiano, juce::dontSendNotification);
        horizontalScroll.setBounds (area.removeFromBottom (12).withTrimmedRight (trackWidth + 10));
        ruler.setBounds (area.removeFromTop (rulerHeight));
        viewport.setBounds (area);
        canvas.setSize (juce::jmax (1, viewport.getWidth() - 10), juce::jmax (area.getHeight(), (int) state.tracks.size() * trackHeight));
        canvas.resized();
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
            if (code == ',') { showAudioSettings(); return true; }
        }
        if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (code == juce::KeyPress::escapeKey) { session.stop(); session.seek (0); refresh(); return true; }
        if (code == juce::KeyPress::F9Key) { if (state.recording) session.stop(); else report (session.record()); refresh(); return true; }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { if (showPiano) pianoRoll.deleteSelectedNote(); else deleteSelectedClip(); return true; }
        if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=') { if (showPiano) pianoRoll.zoom (1.3); else zoom (1.3); return true; }
        if (key.getTextCharacter() == '-') { if (showPiano) pianoRoll.zoom (1.0 / 1.3); else zoom (1.0 / 1.3); return true; }
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
    void runInteractionChecks();
   #endif

private:
    static constexpr int browserWidth = 214, trackWidth = 280, trackHeight = 112, rulerHeight = 38,
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
        TrackRow (Impl& o, int i) : owner (o), index (i)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &name, &mute, &solo, &arm, &gain, &pan, &input, &monitor }) addAndMakeVisible (c);
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
            input.setTooltip ("Choose an audio input before arming. In this preview, each input is assigned to one track; choosing it here moves its previous assignment.");
            addMouseListener (this, true);
        }
        void sync (const Session::TrackInfo& track)
        {
            colour = track.colour;
            midi = track.isMidi;
            input.setVisible (! midi); monitor.setVisible (! midi); arm.setVisible (! midi);
            peakL = track.peakL; peakR = track.peakR;
            if (! name.isBeingEdited()) name.setText (track.name, juce::dontSendNotification);
            if (! gain.isMouseButtonDown()) gain.setValue (track.gainDb, juce::dontSendNotification);
            if (! pan.isMouseButtonDown()) pan.setValue (track.pan, juce::dontSendNotification);
            mute.setToggleState (track.mute, juce::dontSendNotification);
            solo.setToggleState (track.solo, juce::dontSendNotification);
            arm.setToggleState (track.armed, juce::dontSendNotification);
            auto names = owner.session.inputNames();
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
            g.drawText ("VOL", 12, 38, 25, 17, juce::Justification::centredLeft);
            g.drawText ("PAN", 174, 38, 25, 17, juce::Justification::centredLeft);
            if (midi) g.drawText ("MIDI  /  HALFTRACK SYNTH", 12, 78, getWidth() - 24, 23, juce::Justification::centredLeft);
            else
            {
                g.drawText ("IN", 12, 78, 18, 23, juce::Justification::centredLeft);
                g.drawText ("MON", 174, 78, 26, 23, juce::Justification::centredLeft);
            }
            auto peak = juce::jmax (peakL, peakR);
            auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 60.0f) / 60.0f);
            g.setColour (background); g.fillRect (12, 106, getWidth() - 24, 3);
            g.setColour (peak >= 1.0f ? red : green); g.fillRect (12, 106, (int) ((getWidth() - 24) * fraction), 3);
        }
        void resized() override
        {
            name.setBounds (10, 5, getWidth() - 104, 25);
            mute.setBounds (getWidth() - 88, 7, 26, 23);
            solo.setBounds (getWidth() - 61, 7, 26, 23);
            arm.setBounds (getWidth() - 34, 7, 26, 23);
            gain.setBounds (37, 37, 129, 28);
            pan.setBounds (199, 37, getWidth() - 207, 28);
            input.setBounds (31, 79, 135, 23);
            monitor.setBounds (201, 79, getWidth() - 209, 23);
        }
        void mouseDown (const juce::MouseEvent&) override { owner.selectTrack (index); }
    private:
        Impl& owner;
        int index;
        juce::Colour colour;
        float peakL = 0, peakR = 0;
        bool midi = false;
        juce::Label name;
        juce::TextButton mute { "M" }, solo { "S" }, arm { "R" };
        juce::Slider gain, pan;
        juce::ComboBox input, monitor;
        juce::StringArray inputNames;
    };

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
            if ((int) rows.size() != (int) owner.state.tracks.size())
            {
                rows.clear();
                for (int i = 0; i < (int) owner.state.tracks.size(); ++i)
                {
                    auto row = std::make_unique<TrackRow> (owner, i);
                    addAndMakeVisible (*row);
                    rows.push_back (std::move (row));
                }
                resized();
            }
            for (size_t i = 0; i < rows.size(); ++i) rows[i]->sync (owner.state.tracks[i]);
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
                for (int c = 0; c < (int) track.clips.size(); ++c)
                {
                    const auto& clip = track.clips[(size_t) c];
                    auto bounds = clipBounds (clip, t);
                    if (bounds.getRight() < 0 || bounds.getX() > tw) continue;
                    const bool selected = t == owner.selectedTrack && c == owner.selectedClip;
                    auto colour = track.colour;
                    if (colour.isTransparent()) colour = juce::Colour (0xff76b5bf);
                    g.setColour (colour.withAlpha (track.mute ? 0.24f : 0.72f)); g.fillRect (bounds);
                    g.setColour (selected ? orange : colour.brighter (0.1f));
                    g.fillRect (bounds.withHeight (20));
                    g.setColour (juce::Colour (0xff182025)); g.setFont (font (11.0f, true));
                    g.drawText (clip.name, bounds.withHeight (20).reduced (6, 0), juce::Justification::centredLeft, true);
                    auto wave = bounds.withTrimmedTop (24).reduced (2, 4);
                    g.setColour (juce::Colour (0xff14252b).withAlpha (track.mute ? 0.4f : 0.9f));
                    if (clip.isMidi && clip.lengthBeats > 0)
                    {
                        int low = 60, high = 71;
                        for (const auto& note : clip.notes) { low = juce::jmin (low, note.pitch); high = juce::jmax (high, note.pitch); }
                        const auto noteHeight = wave.getHeight() / (float) (high - low + 1);
                        for (const auto& note : clip.notes)
                            g.fillRect (wave.getX() + (float) (note.startBeat / clip.lengthBeats) * wave.getWidth(),
                                        wave.getY() + (high - note.pitch) * noteHeight,
                                        juce::jmax (2.0f, (float) (note.lengthBeats / clip.lengthBeats) * wave.getWidth()),
                                        juce::jmax (2.0f, noteHeight * 0.65f));
                    }
                    auto* thumbnail = clip.isMidi ? nullptr : thumbnailFor (clip.file);
                    if (thumbnail != nullptr && thumbnail->getTotalLength() > 0)
                    {
                        const auto visible = wave.getIntersection (juce::Rectangle<float> (0, wave.getY(), (float) tw, wave.getHeight()));
                        const auto startSeconds = juce::jmax (0.0, (visible.getX() - wave.getX()) / owner.pixelsPerSecond);
                        const auto endSeconds = juce::jmin (clip.length, startSeconds + visible.getWidth() / owner.pixelsPerSecond);
                        thumbnail->drawChannels (g, visible.toNearestInt(), clip.sourceOffset + startSeconds, clip.sourceOffset + endSeconds, 0.9f);
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
            grabKeyboardFocus();
            auto track = e.y / trackHeight;
            owner.selectedClip = -1;
            if (track < (int) owner.state.tracks.size())
            {
                owner.selectTrack (track);
                const auto& clips = owner.state.tracks[(size_t) track].clips;
                for (int c = (int) clips.size() - 1; c >= 0; --c)
                    if (clipBounds (clips[(size_t) c], track).contains (e.position))
                    {
                        owner.selectedClip = c;
                        draggedClip = c;
                        draggedTrack = track;
                        originalStart = clips[(size_t) c].start;
                        dragStartX = e.x;
                        if (e.mods.isPopupMenu()) showClipMenu();
                        repaint();
                        return;
                    }
            }
            draggedClip = -1;
            owner.session.seek (juce::jmax (0.0, owner.viewStart + e.x / owner.pixelsPerSecond));
            owner.refresh();
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.x >= timelineWidth()) return;
            if (const auto* track = owner.currentTrack(); track != nullptr && track->isMidi)
            {
                if (owner.selectedClip >= 0) owner.openPianoRoll();
                else owner.createMidiClip (juce::jmax (0.0, owner.viewStart + e.x / owner.pixelsPerSecond));
            }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (draggedClip < 0 || e.mods.isPopupMenu() || ! e.mouseWasDraggedSinceMouseDown()) return;
            auto start = juce::jmax (0.0, originalStart + (e.x - dragStartX) / owner.pixelsPerSecond);
            if (! e.mods.isAltDown()) start = std::round (start / owner.gridSeconds()) * owner.gridSeconds();
            if (! clipGesture) { owner.session.beginEditGesture ("Move clip"); clipGesture = true; }
            owner.report (owner.session.moveClip (draggedTrack, draggedClip, start));
            owner.refresh();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (clipGesture) { owner.session.endEditGesture(); clipGesture = false; }
            draggedClip = -1;
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
        int draggedClip = -1, draggedTrack = -1, dragStartX = 0;
        double originalStart = 0;
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
            auto safe = juce::Component::SafePointer<Impl> (&owner);
            popup.showMenuAsync (juce::PopupMenu::Options(), [safe] (int choice) { if (safe != nullptr && choice == 1) safe->deleteSelectedClip(); });
        }
    };

    class Ruler final : public juce::Component
    {
    public:
        explicit Ruler (Impl& o) : owner (o) {}
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff343a3e));
            auto width = owner.canvas.timelineWidth();
            g.saveState(); g.reduceClipRegion (0, 0, width, getHeight());
            auto x1 = (float) ((owner.state.loopStart - owner.viewStart) * owner.pixelsPerSecond);
            auto x2 = (float) ((owner.state.loopEnd - owner.viewStart) * owner.pixelsPerSecond);
            g.setColour (owner.state.looping ? orange : muted.withAlpha (0.55f));
            g.fillRect (x1, 3.0f, juce::jmax (1.0f, x2 - x1), 3.0f);
            g.fillRect (x1, 3.0f, 2.0f, 9.0f); g.fillRect (x2 - 2, 3.0f, 2.0f, 9.0f);
            const auto bar = 240.0 / owner.state.tempo;
            const auto step = bar * juce::jmax (1.0, std::pow (2.0, std::ceil (std::log2 (48.0 / (bar * owner.pixelsPerSecond)))));
            const auto end = owner.viewStart + width / owner.pixelsPerSecond;
            g.setFont (font (10.5f));
            for (double t = std::floor (owner.viewStart / step) * step; t <= end; t += step)
            {
                auto x = (int) std::round ((t - owner.viewStart) * owner.pixelsPerSecond);
                g.setColour (muted); g.drawText (juce::String ((int) std::round (t / bar) + 1), x + 5, 15, 55, 19, juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xff575f65)); g.drawVerticalLine (x, 14.0f, (float) getHeight());
            }
            auto px = (float) ((owner.state.position - owner.viewStart) * owner.pixelsPerSecond);
            juce::Path needle;
            needle.addTriangle (px - 5.0f, 15.0f, px + 5.0f, 15.0f, px, 23.0f);
            g.setColour (owner.state.recording ? red : ink); g.fillPath (needle);
            g.restoreState();
            g.setColour (raised); g.fillRect (width, 0, getWidth() - width, getHeight());
            g.setColour (muted); g.setFont (font (10.0f, true));
            g.drawText ("TRACK / MIXER", width + 12, 0, trackWidth - 24, getHeight(), juce::Justification::centredLeft);
            g.setColour (line); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.x >= owner.canvas.timelineWidth()) return;
            anchor = juce::jmax (0.0, owner.viewStart + event.x / owner.pixelsPerSecond);
            definingLoop = event.mods.isShiftDown();
            if (definingLoop) owner.session.beginEditGesture ("Loop range");
            if (! definingLoop) { owner.session.seek (anchor); owner.refresh(); }
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (! definingLoop) return;
            const auto grid = owner.gridSeconds();
            const auto cursor = juce::jmax (0.0, owner.viewStart + event.x / owner.pixelsPerSecond);
            const auto start = std::floor (juce::jmin (anchor, cursor) / grid) * grid;
            const auto end = juce::jmax (start + grid, std::ceil (juce::jmax (anchor, cursor) / grid) * grid);
            owner.session.setLoop (true, start, end);
            owner.refresh();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (definingLoop) owner.session.endEditGesture();
            definingLoop = false;
        }
    private:
        Impl& owner;
        double anchor = 0;
        bool definingLoop = false;
    };

    class PianoRoll final : public juce::Component, private juce::ScrollBar::Listener
    {
    public:
        explicit PianoRoll (Impl& o) : owner (o)
        {
            setName ("Piano roll editor");
            setWantsKeyboardFocus (true);
            for (auto* c : std::initializer_list<juce::Component*> { &snap, &noteLength, &velocity, &horizontal, &vertical }) addAndMakeVisible (c);
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
            velocity.setTooltip ("Velocity of the selected note, or of the next note you draw");
            velocity.onDragStart = [this] { owner.session.beginEditGesture ("Note velocity"); };
            velocity.onDragEnd = [this] { owner.session.endEditGesture(); };
            velocity.onValueChange = [this]
            {
                drawVelocity = (float) velocity.getValue();
                const auto* clip = owner.currentMidiClip(); const auto* note = selectedNote();
                if (clip != nullptr && note != nullptr)
                {
                    owner.report (owner.session.updateMidiNote (clip->id, note->id, note->pitch, note->startBeat, note->lengthBeats, drawVelocity));
                    owner.refresh();
                }
            };
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
                endGesture(); loadedClip = id; selectedNoteId.clear(); viewBeat = 0;
                if (clip != nullptr && ! clip->notes.empty())
                {
                    int high = 60; for (const auto& note : clip->notes) high = juce::jmax (high, note.pitch);
                    topPitch = juce::jlimit (36, 127, high + 10);
                }
                else topPitch = 77;
                fit();
            }
            if (const auto* note = selectedNote()) { drawVelocity = note->velocity; velocity.setValue (note->velocity, juce::dontSendNotification); }
            else selectedNoteId.clear();
            const bool active = clip != nullptr;
            snap.setEnabled (active); noteLength.setEnabled (active); velocity.setEnabled (active);
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
            if (clip == nullptr)
            {
                g.setColour (muted); g.setFont (font (14.0f));
                g.drawFittedText ("Create a MIDI track to start writing notes.\nDouble-click a MIDI clip in Arrangement to edit it here.", getLocalBounds().withTrimmedTop (headerHeight).reduced (35), juce::Justification::centred, 3);
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
                const bool selected = note.id == selectedNoteId;
                g.setColour ((selected ? orange : juce::Colour (0xff8cc4b5)).withAlpha (0.48f + note.velocity * 0.52f)); g.fillRect (r);
                g.setColour (selected ? ink : juce::Colour (0xff203d36)); g.drawRect (r, 1.0f);
                if (r.getWidth() > 22)
                {
                    g.setColour (juce::Colour (0xff21342f)); g.setFont (font (9.0f));
                    g.drawText (pitchName (note.pitch), r.reduced (4, 0), juce::Justification::centredLeft, true);
                }
                if (selected) { g.setColour (ink); g.fillRect (r.getRight() - 4, r.getY() + 3, 2.0f, r.getHeight() - 6); }
            }
            const auto beatPosition = (owner.state.position - clip->start) * owner.state.tempo / 60.0;
            const auto playhead = (float) (keysWidth + (beatPosition - viewBeat) * pixelsPerBeat);
            g.setColour (ink); g.drawLine (playhead, (float) gridTop, playhead, (float) grid.getBottom(), 1.2f);
            g.restoreState();
            g.setColour (raised); g.fillRect (0, headerHeight, getWidth(), rulerHeight);
            g.setColour (muted); g.setFont (font (10.0f));
            for (double beat = std::floor (viewBeat / 4.0) * 4.0; beat <= viewBeat + grid.getWidth() / pixelsPerBeat; beat += 4.0)
                g.drawText (juce::String ((int) (beat / 4) + 1), (int) (keysWidth + (beat - viewBeat) * pixelsPerBeat) + 4, headerHeight, 45, rulerHeight, juce::Justification::centredLeft);
            g.setColour (line); g.drawHorizontalLine (headerHeight, 0.0f, (float) getWidth());
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr || e.x < keysWidth) return;
            if (e.y >= headerHeight && e.y < gridTop)
            {
                owner.session.seek (clip->start + juce::jmax (0.0, beatAt (e.x)) * 60.0 / owner.state.tempo); owner.refresh(); return;
            }
            if (! gridBounds().contains (e.getPosition())) return;
            grabKeyboardFocus();
            endGesture();
            for (auto it = clip->notes.rbegin(); it != clip->notes.rend(); ++it)
                if (noteBounds (*it).contains (e.position))
                {
                    selectedNoteId = it->id; original = *it; dragOrigin = e.position; dragClipId = clip->id;
                    resizeNote = e.x >= noteBounds (*it).getRight() - 8;
                    drawing = false;
                    if (e.mods.isPopupMenu()) { showNoteMenu(); return; }
                    sync(); return;
                }
            selectedNoteId.clear();
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
                    if (std::none_of (oldNotes.begin(), oldNotes.end(), [&] (const auto& old) { return old.id == note.id; })) { selectedNoteId = note.id; original = note; break; }
            dragClipId = clipId; dragOrigin = e.position; resizeNote = true; drawing = true; sync();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr || selectedNoteId.isEmpty() || e.mods.isPopupMenu() || ! e.mouseWasDraggedSinceMouseDown()) return;
            if (! gesture) { owner.session.beginEditGesture (resizeNote ? "Resize MIDI note" : "Move MIDI note"); gesture = true; }
            auto start = original.startBeat, length = original.lengthBeats;
            auto pitch = original.pitch;
            if (resizeNote)
            {
                length = drawing ? beatAt (e.x) - original.startBeat : original.lengthBeats + (e.x - dragOrigin.x) / pixelsPerBeat;
                if (! e.mods.isAltDown()) length = std::round (length / snapBeats()) * snapBeats();
                length = juce::jlimit (0.03125, juce::jmax (0.03125, clip->lengthBeats - start), length);
            }
            else
            {
                start += (e.x - dragOrigin.x) / pixelsPerBeat;
                if (! e.mods.isAltDown()) start = std::round (start / snapBeats()) * snapBeats();
                start = juce::jlimit (0.0, juce::jmax (0.0, clip->lengthBeats - length), start);
                pitch = juce::jlimit (0, 127, original.pitch - (int) std::round ((e.y - dragOrigin.y) / rowHeight));
            }
            owner.report (owner.session.updateMidiNote (dragClipId, selectedNoteId, pitch, start, length, original.velocity)); owner.refresh();
        }
        void mouseUp (const juce::MouseEvent&) override { endGesture(); drawing = false; }
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
            const auto* clip = owner.currentMidiClip(); const auto* note = selectedNote();
            const auto code = key.getKeyCode();
            if (clip != nullptr && note != nullptr && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
            {
                auto pitch = note->pitch; auto start = note->startBeat;
                if (code == juce::KeyPress::leftKey) start -= snapBeats();
                if (code == juce::KeyPress::rightKey) start += snapBeats();
                if (code == juce::KeyPress::upKey) pitch += key.getModifiers().isShiftDown() ? 12 : 1;
                if (code == juce::KeyPress::downKey) pitch -= key.getModifiers().isShiftDown() ? 12 : 1;
                owner.report (owner.session.updateMidiNote (clip->id, note->id, juce::jlimit (0, 127, pitch), juce::jlimit (0.0, juce::jmax (0.0, clip->lengthBeats - note->lengthBeats), start), note->lengthBeats, note->velocity)); owner.refresh(); return true;
            }
            return owner.handleKey (key);
        }
        bool hasSelectedNote() const { return selectedNote() != nullptr; }
        void deleteSelectedNote()
        {
            const auto* clip = owner.currentMidiClip();
            if (clip == nullptr || selectedNoteId.isEmpty()) return;
            endGesture(); owner.report (owner.session.removeMidiNote (clip->id, selectedNoteId)); selectedNoteId.clear(); owner.refresh();
        }
    private:
        Impl& owner;
        static constexpr int headerHeight = 37, rulerHeight = 23, gridTop = headerHeight + rulerHeight, keysWidth = 55, rowHeight = 15, scrollSize = 11;
        juce::ComboBox snap, noteLength;
        juce::Slider velocity;
        juce::ScrollBar horizontal { false }, vertical { true };
        juce::String loadedClip, selectedNoteId, dragClipId;
        Session::MidiNoteInfo original;
        juce::Point<float> dragOrigin;
        double viewBeat = 0, pixelsPerBeat = 60;
        int topPitch = 77;
        float drawVelocity = 0.8f;
        bool gesture = false, resizeNote = false, drawing = false;
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
        void showNoteMenu()
        {
            juce::PopupMenu popup; popup.addItem (1, "Delete Note");
            auto safe = juce::Component::SafePointer<PianoRoll> (this);
            popup.showMenuAsync (juce::PopupMenu::Options(), [safe] (int result) { if (safe != nullptr && result == 1) safe->deleteSelectedNote(); });
        }
    };

    class SynthPanel final : public juce::Component
    {
    public:
        explicit SynthPanel (Impl& o) : owner (o)
        {
            setName ("Halftrack synthesizer");
            waveform.setName ("Oscillator waveform");
            addAndMakeVisible (waveform); waveform.addItem ("Saw", 1); waveform.addItem ("Square", 2);
            waveform.onChange = [this]
            {
                if (const auto* track = owner.currentTrack(); track != nullptr && track->isMidi)
                { auto settings = track->synth; settings.waveform = waveform.getSelectedId() - 1; owner.report (owner.session.setSynthSettings (track->id, settings)); owner.refresh(); }
            };
            for (int i = 0; i < 7; ++i)
            {
                auto& slider = controls[(size_t) i]; addAndMakeVisible (slider);
                slider.setName (names[i]); slider.setTooltip (names[i]);
                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 67, 17);
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
            for (int i = 0; i < 7; ++i) controls[(size_t) i].onValueChange = [this, i] { changeParameter (i, (float) controls[(size_t) i].getValue()); };
        }
        void sync()
        {
            const auto* track = owner.currentTrack();
            if (track == nullptr || ! track->isMidi) { setVisible (false); return; }
            setVisible (true); waveform.setSelectedId (track->synth.waveform + 1, juce::dontSendNotification);
            const float values[] { track->synth.cutoffHz, track->synth.resonance, track->synth.attackSeconds, track->synth.decaySeconds, track->synth.sustain, track->synth.releaseSeconds, track->synth.level };
            for (int i = 0; i < 7; ++i) controls[(size_t) i].setValue (values[i], juce::dontSendNotification);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (raised); g.setColour (orange); g.setFont (font (10.5f, true));
            g.drawText ("HALFTRACK SYNTH", 11, 2, 185, 24, juce::Justification::centredLeft);
            g.setColour (muted); g.setFont (font (9.5f));
            const auto width = (getWidth() - 16) / 7;
            for (int i = 0; i < 7; ++i) g.drawText (names[i], 8 + i * width, 30, width, 14, juce::Justification::centred);
            g.setColour (line); g.drawRect (getLocalBounds());
        }
        void resized() override
        {
            waveform.setBounds (getWidth() - 118, 5, 107, 22);
            const auto width = (getWidth() - 16) / 7;
            for (int i = 0; i < 7; ++i) controls[(size_t) i].setBounds (8 + i * width, 43, width - 2, getHeight() - 46);
        }
    private:
        Impl& owner;
        juce::ComboBox waveform;
        std::array<juce::Slider, 7> controls;
        static constexpr const char* names[] { "Cutoff", "Resonance", "Attack", "Decay", "Sustain", "Release", "Level" };
        void changeParameter (int index, float value)
        {
            const auto* track = owner.currentTrack(); if (track == nullptr || ! track->isMidi) return;
            auto settings = track->synth;
            switch (index)
            {
                case 0: settings.cutoffHz = value; break; case 1: settings.resonance = value; break;
                case 2: settings.attackSeconds = value; break; case 3: settings.decaySeconds = value; break;
                case 4: settings.sustain = value; break; case 5: settings.releaseSeconds = value; break;
                case 6: settings.level = value; break; default: return;
            }
            owner.report (owner.session.setSynthSettings (track->id, settings));
            owner.refresh();
        }
    };

    class DeviceChain final : public juce::Component
    {
    public:
        explicit DeviceChain (Impl& o) : owner (o), synthPanel (o)
        {
            addAndMakeVisible (addButton); addAndMakeVisible (manageButton); addAndMakeVisible (synthPanel);
            addButton.onClick = [this] { owner.addPluginMenu(); };
            manageButton.onClick = [this] { owner.showPlugins(); };
        }
        void resized() override
        {
            manageButton.setBounds (getWidth() - 212, 8, 94, 24);
            addButton.setBounds (getWidth() - 320, 8, 101, 24);
            synthPanel.setBounds (14 - offset, 44, 536, getHeight() - 59);
        }
        void sync()
        {
            const auto* track = owner.currentTrack();
            const auto total = track != nullptr ? (int) track->plugins.size() * 208 + (track->isMidi ? 554 : 0) : 0;
            offset = juce::jlimit (0, juce::jmax (0, total - getWidth() + 150), offset);
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
            g.saveState(); g.reduceClipRegion (9, 39, getWidth() - 123, getHeight() - 48);
            if (track == nullptr || (! track->isMidi && track->plugins.empty()))
            {
                g.setColour (juce::Colour (0xff343b40));
                g.drawRect (14, 44, getWidth() - 138, getHeight() - 59);
                g.setColour (muted); g.setFont (font (12.0f));
                g.drawText (track == nullptr ? "Select an audio track to view its device chain." : "Add a plug-in, or double-click one in the browser.",
                            25, 59, getWidth() - 160, 32, juce::Justification::centred);
                g.setFont (font (10.5f));
                g.drawText ("Audio flows through devices from left to right.", 25, 91, getWidth() - 160, 25, juce::Justification::centred);
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
            if (track == nullptr || e.x >= getWidth() - 111) return;
            for (int i = 0; i < (int) track->plugins.size(); ++i)
            {
                const auto box = pluginBounds (i);
                if (! box.contains (e.getPosition())) continue;
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
            offset = juce::jlimit (0, juce::jmax (0, total - getWidth() + 150), offset - (int) ((details.deltaX + details.deltaY) * 220));
            resized(); repaint();
        }
    private:
        Impl& owner;
        juce::TextButton addButton { "+ Plug-in" }, manageButton { "Manage..." };
        SynthPanel synthPanel;
        int offset = 0;
        juce::Rectangle<int> pluginBounds (int i) const { const auto* track = owner.currentTrack(); return { 14 + (track != nullptr && track->isMidi ? 554 : 0) + i * 208 - offset, 44, 192, getHeight() - 59 }; }
        void paintMaster (juce::Graphics& g)
        {
            auto box = getLocalBounds().removeFromRight (107).reduced (8, 8);
            g.setColour (background); g.fillRect (box);
            g.setColour (muted); g.setFont (font (9.5f, true));
            g.drawText ("MASTER", box.removeFromTop (23), juce::Justification::centred);
            auto meterArea = box.reduced (13, 12);
            auto meter = [&] (float peak, int x)
            {
                auto rect = juce::Rectangle<int> (x, meterArea.getY(), 11, meterArea.getHeight() - 17);
                g.setColour (line); g.fillRect (rect);
                const auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 60.0f) / 60.0f);
                g.setColour (peak >= 1.0f ? red : green);
                g.fillRect (rect.removeFromBottom ((int) (rect.getHeight() * fraction)));
            };
            meter (owner.state.masterPeakL, meterArea.getX() + 6);
            meter (owner.state.masterPeakR, meterArea.getX() + 23);
            g.setColour (muted); g.setFont (font (9.0f));
            g.drawText ("L   R", meterArea.withTrimmedTop (meterArea.getHeight() - 15), juce::Justification::centredLeft);
        }
    };

    Session& session;
    Theme theme;
    Session::Snapshot state;
    juce::MenuBarComponent menu;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 128 };
    TransportButton playButton { TransportButton::play }, stopButton { TransportButton::stop }, recordButton { TransportButton::record };
    juce::TextButton loopButton { "Loop" }, audioButton { "Audio Setup" }, addTrackButton { "+ Audio" }, addMidiButton { "+ MIDI" }, newMidiClipButton { "+ MIDI Clip" },
                     arrangementTab { "Arrangement" }, pianoTab { "Piano Roll" }, importButton { "Import Audio" },
                     filesButton { "Files" }, pluginsButton { "Plug-ins" }, folderButton { "Choose Folder..." }, upButton { "Up" },
                     zoomIn { "+" }, zoomOut { "-" }, fitButton { "Fit" };
    juce::Label tempo, positionLabel, projectLabel;
    juce::TextEditor browserSearch;
    juce::ListBox browserList;
    Canvas canvas;
    Ruler ruler;
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
    bool pluginBrowser = false, showPiano = false;
    double viewStart = 0, pixelsPerSecond = 35, recordingStart = 0;

    const Session::TrackInfo* currentTrack() const
    {
        return selectedTrack >= 0 && selectedTrack < (int) state.tracks.size() ? &state.tracks[(size_t) selectedTrack] : nullptr;
    }
    const Session::ClipInfo* currentMidiClip() const
    {
        const auto* track = currentTrack();
        if (track == nullptr || selectedClip < 0 || selectedClip >= (int) track->clips.size()) return nullptr;
        const auto& clip = track->clips[(size_t) selectedClip];
        return clip.isMidi ? &clip : nullptr;
    }
    void openPianoRoll()
    {
        const auto* track = currentTrack();
        if (track != nullptr && track->isMidi && currentMidiClip() == nullptr)
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
        if (result.wasOk()) { selectTrack ((int) state.tracks.size() - 1); createMidiClip (state.position); }
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
    void selectTrack (int track)
    {
        if (selectedTrack != track) selectedClip = -1;
        selectedTrack = track;
        canvas.repaint(); deviceChain.sync();
        for (int i = 0; i < canvas.getNumChildComponents(); ++i) canvas.getChildComponent (i)->repaint();
    }
    void refresh()
    {
        const auto previous = state;
        state = session.snapshot();
        if (state.recording && ! previous.recording) recordingStart = state.position;
        if (selectedTrack >= (int) state.tracks.size()) { selectedTrack = (int) state.tracks.size() - 1; selectedClip = -1; }
        if (selectedTrack < 0 && ! state.tracks.empty()) selectedTrack = 0;
        if (const auto* track = currentTrack(); track == nullptr || selectedClip >= (int) track->clips.size()) selectedClip = -1;
        playButton.setToggleState (state.playing, juce::dontSendNotification);
        recordButton.setToggleState (state.recording, juce::dontSendNotification);
        loopButton.setToggleState (state.looping, juce::dontSendNotification);
        if (! tempo.isBeingEdited()) tempo.setText (juce::String (state.tempo, 2), juce::dontSendNotification);
        const auto beatPosition = juce::jmax (0.0, state.position) * state.tempo / 60.0;
        const auto bar = (int) (beatPosition / 4) + 1;
        const auto beat = (int) beatPosition % 4 + 1;
        const auto sixteenth = (int) (beatPosition * 4) % 4 + 1;
        positionLabel.setText (juce::String (bar).paddedLeft ('0', 3) + " . " + juce::String (beat) + " . " + juce::String (sixteenth), juce::dontSendNotification);
        projectLabel.setText (state.name + (state.dirty ? "  *" : ""), juce::dontSendNotification);
        if (state.tracks.size() != previous.tracks.size()) resized();
        canvas.syncRows();
        pianoRoll.sync();
        newMidiClipButton.setEnabled (currentTrack() != nullptr && currentTrack()->isMidi);
        ruler.repaint(); deviceChain.sync(); repaint();
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
    void deleteSelectedClip()
    {
        if (selectedTrack >= 0 && selectedClip >= 0) { report (session.removeClip (selectedTrack, selectedClip)); selectedClip = -1; refresh(); }
    }
    void resetView()
    {
        selectedTrack = -1; selectedClip = -1; viewStart = 0; showPiano = false; resized();
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

    juce::StringArray getMenuBarNames() override { return { "File", "Edit", "Track", "View" }; }
    juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
    {
        juce::PopupMenu popup;
        if (session.isExporting()) { popup.addItem (99, "Export in progress...", false); return popup; }
        if (index == 0)
        {
            popup.addItem (1, "New Project"); popup.addItem (2, "Open Project..."); popup.addSeparator();
            popup.addItem (3, "Save Project"); popup.addItem (4, "Save Project As..."); popup.addSeparator();
            popup.addItem (5, "Import Audio..."); popup.addItem (6, "Export Mix as WAV...", ! state.tracks.empty()); popup.addSeparator();
            popup.addItem (7, "Audio Setup..."); popup.addItem (8, "Manage Plug-ins...");
        }
        else if (index == 1)
        {
            popup.addItem (20, "Undo", session.canUndo()); popup.addItem (21, "Redo", session.canRedo()); popup.addSeparator();
            popup.addItem (22, showPiano ? "Delete Selected Note" : "Delete Selected Clip", showPiano ? pianoRoll.hasSelectedNote() : selectedClip >= 0);
        }
        else if (index == 2)
        {
            popup.addItem (30, "Insert Audio Track"); popup.addItem (33, "Insert MIDI Track"); popup.addItem (34, "Insert MIDI Clip", currentTrack() != nullptr && currentTrack()->isMidi); popup.addItem (31, "Delete Selected Track", currentTrack() != nullptr);
            popup.addSeparator(); popup.addItem (32, "Add Plug-in...", currentTrack() != nullptr);
        }
        else
        {
            popup.addItem (43, "Arrangement"); popup.addItem (44, "Piano Roll"); popup.addSeparator();
            popup.addItem (40, showPiano ? "Fit Piano Roll" : "Fit Arrangement"); popup.addItem (41, "Zoom In"); popup.addItem (42, "Zoom Out");
        }
        return popup;
    }
    void menuItemSelected (int id, int) override
    {
        if (session.isExporting()) return;
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
            case 20: session.undo(); break;
            case 21: session.redo(); break;
            case 22: if (showPiano) pianoRoll.deleteSelectedNote(); else deleteSelectedClip(); break;
            case 30: showPiano = false; addTrack(); resized(); break;
            case 33: addMidiTrack(); break;
            case 34: createMidiClip (state.position); break;
            case 31: if (selectedTrack >= 0) { session.closePluginEditors(); report (session.removeTrack (selectedTrack)); selectedClip = -1; } break;
            case 32: addPluginMenu(); break;
            case 40: if (showPiano) pianoRoll.fit(); else fitTimeline(); break;
            case 41: if (showPiano) pianoRoll.zoom (1.3); else zoom (1.3); break;
            case 42: if (showPiano) pianoRoll.zoom (1.0 / 1.3); else zoom (1.0 / 1.3); break;
            case 43: showPiano = false; resized(); break;
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
    void importDialog()
    {
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
                    if (! before.tracks[(size_t) i].isMidi && before.tracks[(size_t) i].clips.empty()) { target = i; break; }
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
    void exportDialog()
    {
        auto* alert = new juce::AlertWindow ("Export Audio", "Render the full arrangement through its track and master effects to a stereo WAV file.", juce::MessageBoxIconType::NoIcon, this);
        alert->setLookAndFeel (&theme);
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
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, dialog] (int result)
        {
            if (result != 1 || safe == nullptr || dialog == nullptr) return;
            constexpr double rates[] { 44100, 48000, 88200, 96000, 192000 };
            constexpr int depths[] { 16, 24, 32 };
            constexpr double tails[] { 0, 1, 2, 5, 10, 30, 60 };
            auto rate = rates[juce::jlimit (0, 4, dialog->getComboBoxComponent ("sample-rate")->getSelectedItemIndex())];
            auto depth = depths[juce::jlimit (0, 2, dialog->getComboBoxComponent ("bit-depth")->getSelectedItemIndex())];
            auto tail = tails[juce::jlimit (0, 6, dialog->getComboBoxComponent ("effect-tail")->getSelectedItemIndex())];
            juce::MessageManager::callAsync ([safe, rate, depth, tail]
            {
                if (safe != nullptr) safe->chooseExportDestination (rate, depth, tail);
            });
        }), true);
    }
    void chooseExportDestination (double sampleRate, int bitDepth, double tailSeconds)
    {
        chooser = std::make_unique<juce::FileChooser> ("Export Mix as WAV", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (state.name + ".wav"), "*.wav");
        auto safe = juce::Component::SafePointer<Impl> (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, sampleRate, bitDepth, tailSeconds] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                auto result = safe->session.exportWav (fc.getResult().withFileExtension ("wav"), sampleRate, bitDepth, tailSeconds);
                if (safe == nullptr) return;
                if (result.failed() && result.getErrorMessage() == "Export cancelled.")
                {
                    safe->status = "Export cancelled.";
                    safe->statusFrames = 24 * 5;
                }
                else safe->report (result, "WAV exported: " + juce::String (sampleRate / 1000.0, 1) + " kHz / " + juce::String (bitDepth) + "-bit, " + juce::String (tailSeconds, 0) + "s effect tail.");
                safe->refresh();
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
        if (selectedTrack < 0) return;
        auto types = session.knownPlugins().getTypes();
        if (types.isEmpty()) { showPlugins(); return; }
        juce::PopupMenu popup;
        for (int i = 0; i < types.size(); ++i) popup.addItem (i + 1, types[i].name + "  (" + types[i].pluginFormatName + ")");
        popup.addSeparator(); popup.addItem (100000, "Scan / Manage Plug-ins...");
        auto safe = juce::Component::SafePointer<Impl> (this);
        const auto track = selectedTrack;
        popup.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&deviceChain), [safe, types, track] (int choice)
        {
            if (safe == nullptr) return;
            if (choice == 100000) safe->showPlugins();
            else if (choice > 0 && choice <= types.size()) { safe->report (safe->session.addPlugin (track, types[choice - 1])); safe->refresh(); }
        });
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
        const auto p = canvas.getLocalPoint (this, juce::Point<int> (x, y));
        const auto inside = viewport.getBounds().contains (x, y) && p.x < canvas.timelineWidth();
        const auto index = inside && p.y / trackHeight < (int) state.tracks.size() ? p.y / trackHeight : -1;
        const auto time = inside ? juce::jmax (0.0, viewStart + p.x / pixelsPerSecond) : state.position;
        importFiles (files, index, std::round (time / gridSeconds()) * gridSeconds());
    }
};


#if HALFTRACK_TESTS_ENABLED
void MainComponent::Impl::runInteractionChecks()
{
    auto check = [] (bool condition, const juce::String& message)
    {
        if (! condition) throw std::runtime_error (message.toStdString());
        std::cout << "PASS UI " << message << std::endl;
    };
    // Drive the same entry points used by the native menu and mouse dispatch.
    // No direct note or synth facade mutations are used by this regression.
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
    auto* cutoff = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "Cutoff"));
    auto* resonance = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "Resonance"));
    auto* release = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "Release"));
    auto* waveform = dynamic_cast<juce::ComboBox*> (findNamed (deviceChain, "Oscillator waveform"));
    check (cutoff != nullptr && resonance != nullptr && release != nullptr && waveform != nullptr,
           "synth controls expose accessible names");
    cutoff->setValue (8100, juce::sendNotificationSync);
    resonance->setValue (0.72, juce::sendNotificationSync);
    waveform->setSelectedId (2, juce::sendNotificationSync);
    release->setValue (1.75, juce::sendNotificationSync);
    const auto settings = session.snapshot().tracks[(size_t) selectedTrack].synth;
    check (settings.cutoffHz == 8100 && std::abs (settings.resonance - 0.72f) < 0.0001f
               && settings.waveform == 1 && settings.releaseSeconds == 1.75f,
           "immediate consecutive synth changes preserve every previous control value");
    menuItemSelected (31, 2);
    menuItemSelected (20, 1);
    menuItemSelected (21, 1);
    menuItemSelected (33, 2);
    check (currentMidiClip() != nullptr && currentMidiClip()->notes.empty(),
           "delete, undo, redo and new track leave the piano editor attached to live state");
}

int MainComponent::runInteractionTests()
{
    try
    {
        Session session (false);
        MainComponent component (session);
        component.impl->runInteractionChecks();
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
