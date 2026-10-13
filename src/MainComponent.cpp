#include "MainComponent.h"
#include "Session.h"

#include <algorithm>
#include <cmath>
#include <map>

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
    explicit Impl (Session& s) : session (s), menu (this), canvas (*this), ruler (*this), deviceChain (*this)
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
                 &projectLabel, &audioButton, &addTrackButton, &importButton, &filesButton, &pluginsButton,
                 &folderButton, &upButton, &browserSearch, &browserList, &ruler, &viewport,
                 &horizontalScroll, &zoomIn, &zoomOut, &fitButton, &deviceChain })
            addAndMakeVisible (child);

        for (auto* button : std::initializer_list<juce::Button*> { &playButton, &stopButton, &recordButton, &loopButton,
                 &audioButton, &addTrackButton, &importButton, &filesButton, &pluginsButton, &folderButton,
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
        addTrackButton.onClick = [this] { addTrack(); };
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
        browserFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! browserFolder.isDirectory()) browserFolder = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        zoomIn.onClick = [this] { zoom (1.3); };
        zoomOut.onClick = [this] { zoom (1.0 / 1.3); };
        fitButton.onClick = [this] { fitTimeline(); };
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
        g.drawText ("ARRANGEMENT", sectionBounds.getX() + 12, sectionBounds.getY(), 120, sectionBounds.getHeight(), juce::Justification::centredLeft);
        g.drawText (pluginBrowser ? "PLUG-IN LIBRARY" : browserFolder.getFileName().toUpperCase(),
                    browserBounds.getX() + 12, browserBounds.getY() + 112,
                    browserBounds.getWidth() - 24, 20, juce::Justification::centredLeft, true);
        if (browserItems.empty() && pluginItems.empty())
        {
            g.setFont (font (12.0f));
            g.drawFittedText (pluginBrowser ? "No scanned plug-ins.\nUse Manage to scan your\nAudio Units and VST3 library."
                                           : "No audio files here.\nChoose a folder or drag\naudio into the arrangement.",
                              browserList.getBounds().reduced (14).withHeight (120), juce::Justification::centred, 5);
        }
        g.setColour (line);
        g.drawHorizontalLine (getHeight() - statusHeight, 0.0f, (float) getWidth());
        g.setColour (muted);
        g.setFont (font (10.5f));
        auto message = status.isNotEmpty() ? status : "Space: play / pause     Click ruler: seek     Shift-drag ruler: loop     Drop audio to import";
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
        addTrackButton.setBounds (section.removeFromRight (94));
        section.removeFromRight (7);
        importButton.setBounds (section.removeFromRight (89));
        section.removeFromRight (12);
        zoomIn.setBounds (section.removeFromRight (25));
        zoomOut.setBounds (section.removeFromRight (25));
        fitButton.setBounds (section.removeFromRight (36));
        horizontalScroll.setBounds (area.removeFromBottom (12).withTrimmedRight (trackWidth + 10));
        ruler.setBounds (area.removeFromTop (rulerHeight));
        viewport.setBounds (area);
        canvas.setSize (juce::jmax (1, viewport.getWidth() - 10), juce::jmax (area.getHeight(), (int) state.tracks.size() * trackHeight));
        canvas.resized();
        updateScroll();
    }

    bool handleKey (const juce::KeyPress& key)
    {
        const auto code = key.getKeyCode();
        const auto cmd = key.getModifiers().isCommandDown();
        const auto shift = key.getModifiers().isShiftDown();
        if (cmd)
        {
            if (code == 'S') { saveProject (shift); return true; }
            if (code == 'O') { openDialog(); return true; }
            if (code == 'N') { ensureSaved ([safe = juce::Component::SafePointer<Impl> (this)] { if (safe != nullptr) { safe->report (safe->session.newProject()); safe->resetView(); } }); return true; }
            if (code == 'T') { addTrack(); return true; }
            if (code == 'Z') { if (shift) session.redo(); else session.undo(); refresh(); return true; }
            if (code == 'L') { session.setLoop (! state.looping, state.loopStart, state.loopEnd); refresh(); return true; }
            if (code == 'I') { importDialog(); return true; }
            if (code == 'E' && shift) { exportDialog(); return true; }
            if (code == ',') { showAudioSettings(); return true; }
        }
        if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (code == juce::KeyPress::escapeKey) { session.stop(); session.seek (0); refresh(); return true; }
        if (code == juce::KeyPress::F9Key) { if (state.recording) session.stop(); else report (session.record()); refresh(); return true; }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelectedClip(); return true; }
        if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=') { zoom (1.3); return true; }
        if (key.getTextCharacter() == '-') { zoom (1.0 / 1.3); return true; }
        return false;
    }

    void ensureSaved (std::function<void()> action)
    {
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

private:
    static constexpr int browserWidth = 214, trackWidth = 280, trackHeight = 112, rulerHeight = 38,
                         deviceHeight = 177, statusHeight = 24;
    struct BrowserItem { juce::File file; bool directory = false; };

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
            g.drawText ("IN", 12, 78, 18, 23, juce::Justification::centredLeft);
            g.drawText ("MON", 174, 78, 26, 23, juce::Justification::centredLeft);
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
                    auto* thumbnail = thumbnailFor (clip.file);
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

    class DeviceChain final : public juce::Component
    {
    public:
        explicit DeviceChain (Impl& o) : owner (o)
        {
            addAndMakeVisible (addButton); addAndMakeVisible (manageButton);
            addButton.onClick = [this] { owner.addPluginMenu(); };
            manageButton.onClick = [this] { owner.showPlugins(); };
        }
        void resized() override
        {
            manageButton.setBounds (getWidth() - 212, 8, 94, 24);
            addButton.setBounds (getWidth() - 320, 8, 101, 24);
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
            if (track == nullptr || track->plugins.empty())
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
            const auto total = track != nullptr ? (int) track->plugins.size() * 208 : 0;
            offset = juce::jlimit (0, juce::jmax (0, total - getWidth() + 150), offset - (int) ((details.deltaX + details.deltaY) * 220));
            repaint();
        }
    private:
        Impl& owner;
        juce::TextButton addButton { "+ Plug-in" }, manageButton { "Manage..." };
        int offset = 0;
        juce::Rectangle<int> pluginBounds (int i) const { return { 14 + i * 208 - offset, 44, 192, getHeight() - 59 }; }
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
    juce::TextButton loopButton { "Loop" }, audioButton { "Audio Setup" }, addTrackButton { "+ Audio Track" }, importButton { "Import Audio" },
                     filesButton { "Files" }, pluginsButton { "Plug-ins" }, folderButton { "Choose Folder..." }, upButton { "Up" },
                     zoomIn { "+" }, zoomOut { "-" }, fitButton { "Fit" };
    juce::Label tempo, positionLabel, projectLabel;
    juce::TextEditor browserSearch;
    juce::ListBox browserList;
    Canvas canvas;
    Ruler ruler;
    DeviceChain deviceChain;
    juce::Viewport viewport;
    juce::ScrollBar horizontalScroll { false };
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::Component::SafePointer<juce::DialogWindow>> dialogs;
    std::vector<juce::Component::SafePointer<juce::AlertWindow>> alerts;
    juce::Rectangle<int> toolbarBounds, browserBounds, sectionBounds;
    std::vector<BrowserItem> browserItems;
    std::vector<juce::PluginDescription> pluginItems;
    juce::File browserFolder;
    juce::String status;
    int selectedTrack = -1, selectedClip = -1, statusFrames = 0, ticks = 0;
    bool pluginBrowser = false;
    double viewStart = 0, pixelsPerSecond = 35, recordingStart = 0;

    const Session::TrackInfo* currentTrack() const
    {
        return selectedTrack >= 0 && selectedTrack < (int) state.tracks.size() ? &state.tracks[(size_t) selectedTrack] : nullptr;
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
        canvas.repaint(); deviceChain.repaint();
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
        ruler.repaint(); deviceChain.repaint(); repaint();
        updateScroll();
    }
    void timerCallback() override
    {
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
        report (session.addAudioTrack()); refresh(); selectTrack ((int) state.tracks.size() - 1);
        viewport.setViewPosition (0, juce::jmax (0, selectedTrack * trackHeight - viewport.getHeight() + trackHeight));
    }
    void deleteSelectedClip()
    {
        if (selectedTrack >= 0 && selectedClip >= 0) { report (session.removeClip (selectedTrack, selectedClip)); selectedClip = -1; refresh(); }
    }
    void resetView()
    {
        selectedTrack = -1; selectedClip = -1; viewStart = 0;
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
            popup.addItem (22, "Delete Selected Clip", selectedClip >= 0);
        }
        else if (index == 2)
        {
            popup.addItem (30, "Insert Audio Track"); popup.addItem (31, "Delete Selected Track", currentTrack() != nullptr);
            popup.addSeparator(); popup.addItem (32, "Add Plug-in...", currentTrack() != nullptr);
        }
        else
        {
            popup.addItem (40, "Fit Arrangement"); popup.addItem (41, "Zoom In"); popup.addItem (42, "Zoom Out");
        }
        return popup;
    }
    void menuItemSelected (int id, int) override
    {
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
            case 22: deleteSelectedClip(); break;
            case 30: addTrack(); break;
            case 31: if (selectedTrack >= 0) { session.closePluginEditors(); report (session.removeTrack (selectedTrack)); selectedClip = -1; } break;
            case 32: addPluginMenu(); break;
            case 40: fitTimeline(); break;
            case 41: zoom (1.3); break;
            case 42: zoom (1.0 / 1.3); break;
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
        const bool validSelection = track >= 0 && track < (int) before.tracks.size();
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
                    if (before.tracks[(size_t) i].clips.empty()) { target = i; break; }
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
            }
            nextTrack = target + 1;
            ++imported;
        }
        session.endEditGesture();
        refresh();
        if (imported > 0)
        {
            selectTrack (firstImportedTrack);
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
            [safe] (const juce::FileChooser& fc) { if (safe != nullptr && fc.getResult().isDirectory()) { safe->browserFolder = fc.getResult(); safe->updateBrowser(); } });
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
    void updateBrowser()
    {
        browserItems.clear(); pluginItems.clear();
        const auto query = browserSearch.getText().trim();
        filesButton.setToggleState (! pluginBrowser, juce::dontSendNotification);
        pluginsButton.setToggleState (pluginBrowser, juce::dontSendNotification);
        folderButton.setButtonText (pluginBrowser ? "Manage / Scan..." : "Choose Folder...");
        browserSearch.setTextToShowWhenEmpty (pluginBrowser ? "Search plug-ins" : "Search current folder", muted);
        if (pluginBrowser)
        {
            for (const auto& type : session.knownPlugins().getTypes()) if (query.isEmpty() || type.name.containsIgnoreCase (query)) pluginItems.push_back (type);
            std::sort (pluginItems.begin(), pluginItems.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        }
        else
        {
            for (const auto& file : browserFolder.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false))
                if ((file.isDirectory() || isAudio (file)) && (query.isEmpty() || file.getFileName().containsIgnoreCase (query))) browserItems.push_back ({ file, file.isDirectory() });
            std::sort (browserItems.begin(), browserItems.end(), [] (const auto& a, const auto& b)
            { return a.directory != b.directory ? a.directory : a.file.getFileName().compareIgnoreCase (b.file.getFileName()) < 0; });
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
