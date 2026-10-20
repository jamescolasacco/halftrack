#include "MainComponent.h"
#include "AudioClipEditor.h"
#include "AutomationEditor.h"
#include "NativeEffects.h"
#include "Session.h"

#include <algorithm>
#include <string_view>
#include <atomic>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
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
// Neutral studio grays with one warm selection accent.
const juce::Colour background { 0xff1d1d1d }, panel { 0xff2d2d2d }, raised { 0xff3e3e3e },
                   line { 0xff141414 }, ink { 0xffdadada }, muted { 0xff8e8e8e },
                   orange { 0xffffab3d }, red { 0xffed5249 }, green { 0xff48d17e };

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
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff6e6e6e));
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
    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (font (slider.getTextBoxHeight() <= 16 ? 10.0f : 12.0f));
        return label;
    }
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
    // Knob: dark ring, value arc from the centre detent for bipolar ranges, pointer line.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + position * (endAngle - startAngle);
        const bool bipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;
        const auto origin = bipolar ? startAngle + (float) ((0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum())) * (endAngle - startAngle) : startAngle;
        juce::Path ring; ring.addCentredArc (centre.x, centre.y, radius - 2, radius - 2, 0, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff141414)); g.strokePath (ring, juce::PathStrokeType (3.0f));
        juce::Path value; value.addCentredArc (centre.x, centre.y, radius - 2, radius - 2, 0, juce::jmin (origin, angle), juce::jmax (origin, angle), true);
        // A colour set on the slider itself (macro knobs and mapped controls) replaces the accent.
        const auto accent = slider.isColourSpecified (juce::Slider::rotarySliderFillColourId) ? slider.findColour (juce::Slider::rotarySliderFillColourId) : orange;
        g.setColour (slider.isEnabled() ? accent : muted); g.strokePath (value, juce::PathStrokeType (3.0f));
        g.setColour (juce::Colour (0xff3a3a3a)); g.fillEllipse (bounds.withSizeKeepingCentre ((radius - 5) * 2, (radius - 5) * 2));
        g.setColour (ink); g.drawLine (centre.x, centre.y, centre.x + (radius - 5) * std::sin (angle), centre.y - (radius - 5) * std::cos (angle), 2.0f);
    }
    // Faders: thin dark slot, lighter fill below the cap, flat rectangular cap.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
        { LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider); return; }
        const bool vertical = style == juce::Slider::LinearVertical;
        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const auto track = vertical ? area.withSizeKeepingCentre (4.0f, area.getHeight()) : area.withSizeKeepingCentre (area.getWidth(), 4.0f);
        g.setColour (juce::Colour (0xff141414)); g.fillRect (track);
        const auto filled = vertical ? track.withTop (sliderPos) : track.withRight (sliderPos);
        g.setColour (slider.isEnabled() ? orange : muted); g.fillRect (filled);
        const auto cap = vertical ? juce::Rectangle<float> (area.getCentreX() - 9.0f, sliderPos - 4.0f, 18.0f, 8.0f)
                                  : juce::Rectangle<float> (sliderPos - 4.0f, area.getCentreY() - 8.0f, 8.0f, 16.0f);
        g.setColour (slider.isEnabled() ? juce::Colour (0xffcfcfcf) : muted); g.fillRect (cap);
        g.setColour (juce::Colour (0xff141414)); g.drawRect (cap, 1.0f);
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
        const auto side = juce::jmin (getWidth(), getHeight()) * .42f;
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (side, side);
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
                 &playButton, &stopButton, &recordButton, &tempo, &meterLabel, &tapButton, &loopButton, &clickButton, &clickSettingsButton, &positionLabel, &loopStartLabel, &loopLengthLabel, &quantisation, &backToArrangement,
                 &projectLabel, &audioButton, &addTrackButton, &addMidiButton, &newMidiClipButton, &sessionTab, &arrangementTab, &pianoTab, &importButton, &filesButton, &pluginsButton,
                 &folderButton, &upButton, &browserSearch, &browserList, &browserCategories, &ruler, &viewport,
                 &horizontalScroll, &zoomIn, &zoomOut, &fitButton, &launcher, &pianoRoll, &audioClipView, &midiLaunch, &deviceChain })
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
        meterLabel.setName ("Time signature"); meterLabel.setFont (font (11.0f));
        meterLabel.setEditable (false, true, false); meterLabel.setJustificationType (juce::Justification::centred);
        meterLabel.setColour (juce::Label::backgroundColourId, background); meterLabel.setColour (juce::Label::outlineColourId, line);
        meterLabel.setTooltip ("Time signature, such as 3 / 4 or 6 / 8. Double-click to edit.");
        meterLabel.onTextChange = [this]
        {
            const auto fields = juce::StringArray::fromTokens (meterLabel.getText(), "/", "");
            if (fields.size() == 2 && fields[0].trim().containsOnly ("0123456789") && fields[1].trim().containsOnly ("0123456789")
                && fields[0].trim().isNotEmpty() && fields[1].trim().isNotEmpty())
                report (session.setTimeSignature (fields[0].getIntValue(), fields[1].getIntValue()));
            refresh();
        };
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
        for (auto* display : { &positionLabel, &loopStartLabel, &loopLengthLabel })
        {
            display->setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain)));
            display->setJustificationType (juce::Justification::centred);
            display->setColour (juce::Label::backgroundColourId, background);
            display->setColour (juce::Label::outlineColourId, line);
        }
        positionLabel.setTooltip ("Song position in bars, beats and sixteenths");
        loopStartLabel.setName ("Loop start"); loopLengthLabel.setName ("Loop length");
        quantisation.setName ("Global launch quantization");
        quantisation.addItem ("None", 1); quantisation.addItem ("1/4", 2); quantisation.addItem ("1 Bar", 3);
        quantisation.setTooltip ("Global launch quantization for clips and scenes. View switches do not change what is playing.");
        quantisation.onChange = [this]
        {
            const auto q = quantisation.getSelectedId() == 1 ? Session::LaunchQuantisation::none
                         : quantisation.getSelectedId() == 2 ? Session::LaunchQuantisation::beat : Session::LaunchQuantisation::bar;
            report (session.setLaunchQuantisation (q)); refresh();
        };
        backToArrangement.setName ("Return to Arrangement");
        backToArrangement.setTooltip ("Release Session overrides so tracks play their arrangement again");
        backToArrangement.setColour (juce::TextButton::buttonOnColourId, orange);
        backToArrangement.onClick = [this] { report (session.returnToArrangement()); refresh(); };
        backToArrangement.setWantsKeyboardFocus (false); backToArrangement.setMouseClickGrabsKeyboardFocus (false);
        loopStartLabel.setTooltip ("Loop start in bars.beats.sixteenths. Double-click to type a new start.");
        loopLengthLabel.setTooltip ("Loop length in bars.beats.sixteenths. Double-click to type a new length.");
        loopStartLabel.setEditable (false, true, false); loopLengthLabel.setEditable (false, true, false);
        loopStartLabel.onTextChange = [this]
        {
            if (const auto beats = parseMusicalTime (loopStartLabel.getText(), true, state.timeSignature))
            {
                const auto start = *beats * 60.0 / state.tempo;
                session.setLoop (state.looping, start, start + (state.loopEnd - state.loopStart));
            }
            refresh();
        };
        loopLengthLabel.onTextChange = [this]
        {
            if (const auto beats = parseMusicalTime (loopLengthLabel.getText(), false, state.timeSignature); beats && *beats > 0)
                session.setLoop (state.looping, state.loopStart, state.loopStart + *beats * 60.0 / state.tempo);
            refresh();
        };
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
        pianoTab.onClick = [this] { if (showPiano) closeClipView(); else openPianoRoll(); };
        pianoTab.setTooltip ("Show the selected clip's editor in the detail view  |  Shift+Tab");
        addMidiButton.setTooltip ("Insert a MIDI track with the Halftrack synthesizer | Cmd/Ctrl+Shift+T");
        newMidiClipButton.setTooltip ("Create a four-bar MIDI clip at the playhead");
        addTrackButton.setTooltip ("Insert audio track  |  Cmd/Ctrl+T");
        importButton.onClick = [this] { importDialog(); };
        filesButton.setClickingTogglesState (false);
        pluginsButton.setClickingTogglesState (false);
        filesButton.onClick = [this] { setBrowserCategory (BrowserCategory::samples); };
        pluginsButton.onClick = [this] { setBrowserCategory (BrowserCategory::plugins); };
        folderButton.onClick = [this] { if (pluginBrowser()) showPlugins(); else chooseFolder(); };
        upButton.onClick = [this] { if (sampleBrowser()) { browserFolder = browserFolder.getParentDirectory(); updateBrowser(); } };
        filesButton.setVisible (false); pluginsButton.setVisible (false); // The category list replaces these tabs.
        upButton.setTooltip ("Parent folder");
        browserSearch.setTextToShowWhenEmpty ("Search current folder", muted);
        browserSearch.setFont (font (12.0f));
        browserSearch.onTextChange = [this] { updateBrowser(); };
        browserList.setModel (this);
        browserList.setRowHeight (21);
        browserList.setOutlineThickness (0);
        browserList.setTooltip ("Double-click or press Return to insert: devices go on the selected track, samples import at the playhead.");
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
        for (auto& window : automationWindows) if (window != nullptr) delete window.getComponent();
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
        g.setColour (line);
        g.drawHorizontalLine (toolbarBounds.getBottom(), 0.0f, (float) getWidth());
        g.drawVerticalLine (browserBounds.getRight(), (float) browserBounds.getY(), (float) browserBounds.getBottom());
        g.setColour (juce::Colour (0xff121212)); g.fillRect (dividerBounds);
        // Control bar: tempo/meter on the left, transport and loop in the centre, status on the right.
        g.setFont (font (11.0f));
        g.setColour (muted); g.setFont (font (10.0f));
        g.drawText ("CPU " + juce::String (juce::roundToInt (state.cpu * 100.0)) + "%", cpuBounds, juce::Justification::centredRight);
        g.setFont (font (10.0f, true)); g.setColour (muted);
        g.drawText (browserCategory == BrowserCategory::instruments ? "INSTRUMENTS" : browserCategory == BrowserCategory::audioEffects ? "AUDIO EFFECTS"
                        : browserCategory == BrowserCategory::midiEffects ? "MIDI EFFECTS" : pluginBrowser() ? "PLUG-INS"
                        : browserFolder == juce::File() ? "SAMPLES" : browserFolder.getFileName().toUpperCase(),
                    browserList.getX() + 4, browserList.getY() - 22, browserList.getWidth() - 8, 20, juce::Justification::centredLeft, true);
        if ((pluginBrowser() || sampleBrowser()) && getNumRows() == 0)
        {
            g.setFont (font (12.0f));
            juce::String message;
            if (pluginBrowser()) message = "No scanned plug-ins.\nUse Manage to scan your\nAudio Units and VST3 library.";
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
        g.drawText (deviceText, getWidth() - 265,
                    getHeight() - statusHeight, 252, statusHeight, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto area = getLocalBounds();
       #if ! JUCE_MAC
        menu.setBounds (area.removeFromTop (23));
       #endif
        toolbarBounds = area.removeFromTop (38);
        auto bar = toolbarBounds.reduced (8, 7);
        tapButton.setBounds (bar.removeFromLeft (36)); bar.removeFromLeft (3);
        tempo.setBounds (bar.removeFromLeft (58)); bar.removeFromLeft (3);
        meterBounds = bar.removeFromLeft (40); bar.removeFromLeft (8); meterLabel.setBounds (meterBounds);
        clickButton.setBounds (bar.removeFromLeft (46)); clickSettingsButton.setBounds (bar.removeFromLeft (20));
        bar.removeFromLeft (8); quantisation.setBounds (bar.removeFromLeft (64));
        audioButton.setBounds (bar.removeFromRight (52)); bar.removeFromRight (8);
        arrangementTab.setBounds (bar.removeFromRight (62)); sessionTab.setBounds (bar.removeFromRight (62));
        pianoTab.setBounds (bar.removeFromRight (40)); bar.removeFromRight (10);
        cpuBounds = bar.removeFromRight (56); bar.removeFromRight (8);
        // Transport is centred on the window, independent of the side groups.
        constexpr int transportWidth = 92 + 6 + 3 * 28 + 14 + 78 + 3 + 48 + 3 + 78;
        auto centre = juce::Rectangle<int> (juce::jmax (bar.getX(), getWidth() / 2 - transportWidth / 2), bar.getY(), transportWidth, bar.getHeight());
        positionLabel.setBounds (centre.removeFromLeft (92)); centre.removeFromLeft (6);
        playButton.setBounds (centre.removeFromLeft (28));
        stopButton.setBounds (centre.removeFromLeft (28));
        recordButton.setBounds (centre.removeFromLeft (28)); centre.removeFromLeft (14);
        loopStartLabel.setBounds (centre.removeFromLeft (78)); centre.removeFromLeft (3);
        loopButton.setBounds (centre.removeFromLeft (48)); centre.removeFromLeft (3);
        loopLengthLabel.setBounds (centre.removeFromLeft (78));
        auto status = bar.withLeft (centre.getRight() + 12);
        backToArrangement.setBounds (status.removeFromLeft (124)); status.removeFromLeft (8);
        projectLabel.setBounds (status);
        // Creation and zoom live in menus, keys and context menus, as in the reference workflow.
        for (auto* hidden : std::initializer_list<juce::Component*> { &importButton, &newMidiClipButton, &addTrackButton, &addMidiButton, &zoomIn, &zoomOut, &fitButton })
            hidden->setVisible (false);
        area.removeFromBottom (statusHeight);
        browserBounds = area.removeFromLeft (browserWidth);
        // The detail view shows either the device chain or, for an open clip, its editor.
        const int detailHeight = showPiano ? juce::jlimit (minimumClipViewHeight, juce::jmax (minimumClipViewHeight, area.getHeight() - 140),
                                                           juce::roundToInt (area.getHeight() * clipViewFraction))
                                           : deviceHeight;
        auto detail = area.removeFromBottom (detailHeight);
        dividerBounds = detail.removeFromTop (dividerHeight);
        deviceChain.setBounds (detail); deviceChain.setVisible (! showPiano);
        auto browser = browserBounds.reduced (6, 6);
        browserSearch.setBounds (browser.removeFromTop (24));
        browser.removeFromTop (6);
        browserCategories.setBounds (browser.removeFromLeft (categoryWidth));
        browser.removeFromLeft (4);
        auto actions = browser.removeFromBottom (24);
        upButton.setVisible (sampleBrowser()); folderButton.setVisible (sampleBrowser() || pluginBrowser());
        if (sampleBrowser()) upButton.setBounds (actions.removeFromLeft (29));
        folderButton.setBounds (actions);
        browser.removeFromBottom (4); browser.removeFromTop (22);
        browserList.setBounds (browser);
        launcher.setBounds (area);
        launcher.setVisible (showSession);
        const bool audioClip = showPiano && selectedAudioClip() != nullptr;
        // A Session MIDI clip shows its Launch box to the left of the note editor.
        const auto* launchClip = showPiano && ! audioClip ? sessionMidiClip() : nullptr;
        auto editor = deviceChain.getBounds();
        midiLaunch.setVisible (launchClip != nullptr);
        if (launchClip != nullptr) { midiLaunch.setBounds (editor.removeFromLeft (LaunchBox::width).reduced (0, 6).withTrimmedLeft (6)); midiLaunch.setClip (*launchClip); }
        pianoRoll.setBounds (editor);
        pianoRoll.setVisible (showPiano && ! audioClip);
        audioClipView.setBounds (deviceChain.getBounds());
        audioClipView.setVisible (audioClip);
        if (audioClip) audioClipView.sync();
        viewport.setVisible (! showSession);
        ruler.setVisible (! showSession);
        horizontalScroll.setVisible (! showSession);
        sessionTab.setToggleState (showSession, juce::dontSendNotification);
        arrangementTab.setToggleState (! showSession, juce::dontSendNotification);
        pianoTab.setToggleState (showPiano, juce::dontSendNotification);
        horizontalScroll.setBounds (area.removeFromBottom (12).withTrimmedRight (trackWidth + 10));
        ruler.setBounds (area.removeFromTop (rulerHeight));
        viewport.setBounds (area);
        canvas.setSize (juce::jmax (1, viewport.getWidth() - 10), juce::jmax (area.getHeight(), lanesHeight));
        canvas.resized();
        ruler.resized(); // Place controls using the final timeline width after window resizing.
        updateScroll();
    }

    void mouseDown (const juce::MouseEvent& e) override { draggingDivider = showPiano && dividerBounds.expanded (0, 3).contains (e.getPosition()); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! draggingDivider) return;
        const auto main = getLocalBounds().withTrimmedTop (toolbarBounds.getBottom()).withTrimmedBottom (statusHeight);
        clipViewFraction = juce::jlimit (.25, .85, (main.getBottom() - e.y) / (double) juce::jmax (1, main.getHeight()));
        resized();
    }
    void mouseUp (const juce::MouseEvent&) override { draggingDivider = false; }
    juce::MouseCursor getMouseCursor() override { return showPiano ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor; }
    bool draggingDivider = false;
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
            if (code == 'L' && timeSelection.active() && ! showSession && ! showPiano) { selectionCommand (79); return true; }
            if (code == 'J' && ! shift && timeSelection.active() && ! showSession && ! showPiano) { selectionCommand (80); return true; }
            if (code == 'L') { session.setLoop (! state.looping, state.loopStart, state.loopEnd); refresh(); return true; }
            if (! shift && ! showSession && ! showPiano && timeSelection.active() && (code == 'E' || code == 'D')) { selectionCommand (code == 'E' ? 76 : 77); return true; }
            if (code == 'I' && ! showSession) { menuItemSelected (74, 1); return true; }
            if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && shift && ! showSession) { menuItemSelected (75, 1); return true; }
            if (code == 'G') { if (shift) ungroupSelectedTrack(); else groupSelectedTracks(); return true; }
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
                    && track->clips[(size_t)selectedClip].isMidi && code=='E')
                { juce::String right; report (session.splitMidiClip (track->clips[(size_t) selectedClip].id, state.position, right)); refresh(); return true; }
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
        if (code == juce::KeyPress::tabKey && shift) { if (showPiano) closeClipView(); else openPianoRoll(); return true; }
        if (code == juce::KeyPress::tabKey) { if (showSession) showArrangementView(); else showSessionView(); return true; }
        if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (code == juce::KeyPress::escapeKey) { session.stop(); session.seek (0); refresh(); return true; }
        if (code == juce::KeyPress::F9Key) { if (state.recording) session.stop(); else report (session.record()); refresh(); return true; }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            if (showPiano) pianoRoll.deleteSelectedNote(); else if (showSession) clearSelectedSlot();
            else if (timeSelection.active()) selectionCommand (78); else deleteSelectedClip();
            return true;
        }
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
    void runExportInteractionChecks (const juce::File& audioFixture);
    bool captureTestErrors = false;
    std::function<void (const Session::AudioExportSettings&, uint64_t)> testExportDestination;
   #endif

private:
    // Arrangement lanes in visual order: group headers above their members; folded members have no height.
    static constexpr int groupHeight = 40;
    struct Lane { bool group = false; int index = -1, y = 0, height = 0; };
    std::vector<Lane> lanes;
    int lanesHeight = 0;
    std::set<juce::String> multiSelectedTracks;
    // The Arrangement time selection: a range over a run of tracks; empty when end <= start.
    struct TimeSelection
    {
        double start = 0, end = 0;
        juce::StringArray trackIds;
        bool active() const { return end - start > .001 && ! trackIds.isEmpty(); }
    };
    TimeSelection timeSelection;
    int trackIndex (const juce::String& id) const
    { for (int t = 0; t < (int) state.tracks.size(); ++t) if (state.tracks[(size_t) t].id == id) return t; return -1; }
    double snapTime (double seconds, juce::ModifierKeys mods) const { return juce::jmax (0.0, mods.isAltDown() ? seconds : std::round (seconds / gridSeconds()) * gridSeconds()); }
    // Time edits use the selection, or the loop range when nothing is selected.
    std::pair<double, double> editRange() const
    { return timeSelection.active() ? std::pair { timeSelection.start, timeSelection.end } : std::pair { state.loopStart, state.loopEnd }; }
    void selectionCommand (int item)
    {
        if (! timeSelection.active()) return;
        const auto [start, end] = editRange();
        if (item == 76) report (session.splitRange (timeSelection.trackIds, start, end));
        else if (item == 77)
        {
            const auto result = session.duplicateRange (timeSelection.trackIds, start, end);
            report (result);
            // As in the reference, the selection moves onto the copy so repeating it continues the pattern.
            if (result.wasOk()) { timeSelection.start = end; timeSelection.end = end + (end - start); }
        }
        else if (item == 78) report (session.deleteRange (timeSelection.trackIds, start, end));
        else if (item == 79) session.setLoop (true, start, end);
        else if (item == 80) { juce::StringArray created; report (session.consolidateRange (timeSelection.trackIds, start, end, created)); }
        refresh();
    }
    const Session::GroupInfo* groupInfo (const juce::String& id) const
    { for (const auto& g : state.groups) if (g.id == id) return &g; return nullptr; }
    void buildLanes()
    {
        lanes.clear(); int y = 0; juce::String open;
        for (int t = 0; t < (int) state.tracks.size(); ++t)
        {
            const auto& track = state.tracks[(size_t) t];
            if (track.groupId != open && track.groupId.isNotEmpty())
                for (int g = 0; g < (int) state.groups.size(); ++g)
                    if (state.groups[(size_t) g].id == track.groupId) { lanes.push_back ({ true, g, y, groupHeight }); y += groupHeight; }
            open = track.groupId;
            const auto* group = groupInfo (track.groupId);
            const int height = group != nullptr && group->folded ? 0 : trackHeight;
            lanes.push_back ({ false, t, y, height }); y += height;
        }
        lanesHeight = y;
    }
    int laneY (int track) const
    { for (const auto& lane : lanes) if (! lane.group && lane.index == track) return lane.y; return track * trackHeight; }
    int laneHeight (int track) const
    { for (const auto& lane : lanes) if (! lane.group && lane.index == track) return lane.height; return trackHeight; }
    // Track under a canvas y, -1 for none (including group header lanes).
    int trackAtY (int y) const
    {
        if (y < 0) return -1;
        for (const auto& lane : lanes) if (y >= lane.y && y < lane.y + lane.height) return lane.group ? -1 : lane.index;
        return lanes.empty() && y / trackHeight < (int) state.tracks.size() ? y / trackHeight : -1;
    }
    void groupSelectedTracks()
    {
        juce::StringArray ids;
        for (const auto& track : state.tracks) if (multiSelectedTracks.contains (track.id) || (currentTrack() != nullptr && track.id == currentTrack()->id)) ids.add (track.id);
        juce::String group; report (session.groupTracks (ids, group)); multiSelectedTracks.clear(); refresh();
    }
    void ungroupSelectedTrack()
    {
        if (const auto* track = currentTrack(); track != nullptr && track->groupId.isNotEmpty()) report (session.ungroupTracks (track->groupId));
        refresh();
    }
    static constexpr int minimumClipViewHeight = 340, dividerHeight = 5;
    double clipViewFraction = .58;
    juce::Rectangle<int> dividerBounds;
    static constexpr int categoryWidth = 98, browserWidth = 300, trackWidth = 252, trackHeight = 72, rulerHeight = 60,
                         deviceHeight = 268, statusHeight = 24;
    struct BrowserItem { juce::File file; bool directory = false; };
    struct FolderScan
    {
        struct Entry { std::string name; bool directory = false; };
        std::atomic<bool> cancelled { false }, finished { false };
        std::mutex mutex;
        std::vector<Entry> entries;
        std::string error;
    };

    enum class BrowserCategory { instruments, audioEffects, midiEffects, plugins, samples };
    // Left column of the browser: one row per category, as in the reference browser sidebar.
    class BrowserCategories final : public juce::Component
    {
    public:
        explicit BrowserCategories (Impl& o) : owner (o) { setName ("Browser categories"); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff272727));
            g.setColour (muted); g.setFont (font (9.5f, true)); g.drawText ("CATEGORIES", 6, 0, getWidth() - 8, headerHeight, juce::Justification::centredLeft);
            for (int i = 0; i < (int) entries.size(); ++i)
            {
                const auto row = rowBounds (i); const bool selected = owner.browserCategory == entries[(size_t) i].first;
                if (selected) { g.setColour (raised); g.fillRect (row); }
                g.setColour (selected ? orange : ink.withAlpha (0.85f)); g.setFont (font (11.0f, selected));
                g.drawText (entries[(size_t) i].second, row.withTrimmedLeft (8), juce::Justification::centredLeft, true);
            }
            g.setColour (line); g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < (int) entries.size(); ++i) if (rowBounds (i).contains (e.getPosition())) { owner.setBrowserCategory (entries[(size_t) i].first); return; }
        }
        juce::Point<int> categoryPoint (BrowserCategory category) const
        {
            for (int i = 0; i < (int) entries.size(); ++i) if (entries[(size_t) i].first == category) return rowBounds (i).getCentre();
            return {};
        }
    private:
        static constexpr int headerHeight = 20, rowHeight = 22;
        juce::Rectangle<int> rowBounds (int i) const { return { 0, headerHeight + i * rowHeight, getWidth() - 1, rowHeight }; }
        Impl& owner;
        const std::array<std::pair<BrowserCategory, const char*>, 5> entries {{ { BrowserCategory::instruments, "Instruments" },
            { BrowserCategory::audioEffects, "Audio Effects" }, { BrowserCategory::midiEffects, "MIDI Effects" },
            { BrowserCategory::plugins, "Plug-ins" }, { BrowserCategory::samples, "Samples" } }};
    };

    class TrackRow final : public juce::Component
    {
    public:
        TrackRow (Impl& o, int i, bool verticalLayout = false) : owner (o), index (i), trackId (o.state.tracks[(size_t)i].id), columnLayout (verticalLayout)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &name, &mute, &solo, &arm, &gain, &pan, &input, &monitor, &sendMode }) addAndMakeVisible (c);
            name.setName ("Track name");
            name.setFont (font (11.5f, true));
            if (! columnLayout) name.setColour (juce::Label::textColourId, juce::Colour (0xff141414));
            name.setEditable (false, true, false);
            name.onTextChange = [this] { owner.report (owner.session.renameTrack (index, name.getText())); };
            name.setTooltip ("Double-click to rename this track");
            for (auto* button : { &mute, &solo, &arm })
            {
                button->setClickingTogglesState (true);
                button->setWantsKeyboardFocus (false);
                button->setMouseClickGrabsKeyboardFocus (false);
            }
            // The numbered track activator is lit while the track is audible.
            mute.onClick = [this] { owner.selectTrack (index); owner.report (owner.session.setTrackMute (index, ! mute.getToggleState())); };
            solo.onClick = [this] { owner.selectTrack (index); owner.report (owner.session.setTrackSolo (index, solo.getToggleState())); };
            arm.onClick = [this] { owner.selectTrack (index); owner.report (owner.session.setTrackArmed (index, arm.getToggleState())); owner.refresh(); };
            mute.setTooltip ("Track activator: click to mute or unmute"); solo.setTooltip ("Solo track"); arm.setTooltip ("Arm track for audio recording");
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a)); mute.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff57a7ff)); solo.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
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
            gain.setName ("Track volume"); gain.setRange (-100.0, 6.0, 0.1);
            gain.setSkewFactorFromMidPoint (-12);
            gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
            gain.setDoubleClickReturnValue (true, 0);
            gain.textFromValueFunction = [] (double value) { return value <= -100 ? juce::String ("-inf") : juce::String (value, 1) + " dB"; };
            gain.valueFromTextFunction = [this] (juce::String text)
            {
                text = text.trim(); if (text.endsWithIgnoreCase ("dB")) text = text.dropLastCharacters (2).trim();
                if (text.equalsIgnoreCase ("-inf")) return -100.0;
                const auto input = text.toStdString(); char* end = nullptr; const auto value = std::strtod (input.c_str(), &end);
                return !input.empty() && end == input.c_str() + input.size() && std::isfinite (value) ? value : gain.getValue();
            };
            gain.setTooltip ("Track volume. Double-click to reset to 0 dB.");
            gain.onDragStart = [this] { owner.session.beginEditGesture ("Track volume"); };
            gain.onDragEnd = [this] { owner.session.endEditGesture(); };
            gain.onValueChange = [this] { owner.report (owner.session.setTrackGain (index, (float) gain.getValue())); };
            pan.setSliderStyle (juce::Slider::LinearHorizontal);
            pan.setName ("Track pan"); pan.setRange (-1.0, 1.0, 0.01);
            pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 32, 18);
            pan.setDoubleClickReturnValue (true, 0);
            pan.textFromValueFunction = [] (double v) { return std::abs (v) < 0.01 ? juce::String ("C") : juce::String ((int) std::round (std::abs (v) * 100.0)) + (v < 0 ? "L" : "R"); };
            pan.valueFromTextFunction = [] (const juce::String& value) { if (value.trim().equalsIgnoreCase ("C")) return 0.0; return value.getDoubleValue() * 0.01 * (value.containsIgnoreCase ("L") ? -1.0 : 1.0); };
            pan.setTooltip ("Track pan. Double-click to center.");
            pan.onDragStart = [this] { owner.session.beginEditGesture ("Track pan"); };
            pan.onDragEnd = [this] { owner.session.endEditGesture(); };
            pan.onValueChange = [this] { owner.report (owner.session.setTrackPan (index, (float) pan.getValue())); };
            if (columnLayout)
            {
                // Track Delay, in milliseconds; negative values play the track early.
                addAndMakeVisible (delay); delay.setName ("Track delay");
                delay.setSliderStyle (juce::Slider::LinearBar); delay.setRange (-Session::maximumTrackDelayMs, Session::maximumTrackDelayMs, .1);
                delay.setDoubleClickReturnValue (true, 0); delay.setWantsKeyboardFocus (false);
                delay.setColour (juce::Slider::trackColourId, juce::Colour (0xff4a4a4a)); delay.setColour (juce::Slider::textBoxTextColourId, ink);
                delay.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " ms"; };
                delay.valueFromTextFunction = [] (const juce::String& text) { return text.retainCharacters ("-.0123456789").getDoubleValue(); };
                delay.setTooltip ("Track delay: positive values play this track later, negative values earlier. Double-click for 0 ms.");
                delay.onDragStart = [this] { owner.session.beginEditGesture ("Track delay"); };
                delay.onDragEnd = [this] { owner.session.endEditGesture(); };
                delay.onValueChange = [this] { owner.report (owner.session.setTrackDelay (trackId, delay.getValue())); };
                delay.updateText();
                gain.setSliderStyle (juce::Slider::LinearVertical); gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);
                pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 42, 15);
                pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
                name.setVisible (false); // The column title bar names the track.
            }
            gain.updateText(); pan.updateText(); // Text boxes predate their formatters.
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
            gain.setEnabled (track.volumeAutomation.points.empty() && !owner.session.isExporting());
            pan.setEnabled (track.panAutomation.points.empty() && !owner.session.isExporting());
            gain.setTooltip (track.volumeAutomation.points.empty() ? "Track volume. Double-click to reset to 0 dB."
                : "Volume has automation. Edit or clear its lane in Automation.");
            pan.setTooltip (track.panAutomation.points.empty() ? "Track pan. Double-click to center."
                : "Pan has automation. Edit or clear its lane in Automation.");
            mute.setToggleState (! track.mute, juce::dontSendNotification);
            mute.setButtonText (juce::String (position + 1));
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
            if (columnLayout) syncSends (track);
            if (columnLayout)
            {
                if (! delay.isMouseButtonDown()) delay.setValue (track.delayMs, juce::dontSendNotification);
                delay.setEnabled (! track.frozen && ! owner.session.isExporting());
                // Crossfader sides: A and B are exclusive toggles; neither leaves the track unassigned.
                if (sideA == nullptr)
                {
                    for (auto* side : { &sideA, &sideB })
                    {
                        *side = std::make_unique<juce::TextButton> (side == &sideA ? "A" : "B");
                        (*side)->setName (side == &sideA ? "Crossfade A" : "Crossfade B");
                        (*side)->setClickingTogglesState (true); (*side)->setWantsKeyboardFocus (false);
                        (*side)->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a)); (*side)->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
                        addAndMakeVisible (**side);
                    }
                    sideA->setTooltip ("Assign this track to the crossfader's A side"); sideB->setTooltip ("Assign this track to the crossfader's B side");
                    sideA->onClick = [this] { owner.report (owner.session.setTrackCrossfade (trackId, sideA->getToggleState() ? Session::CrossfadeAssign::a : Session::CrossfadeAssign::none)); owner.refresh(); };
                    sideB->onClick = [this] { owner.report (owner.session.setTrackCrossfade (trackId, sideB->getToggleState() ? Session::CrossfadeAssign::b : Session::CrossfadeAssign::none)); owner.refresh(); };
                    resized();
                }
                sideA->setToggleState (track.crossfade == Session::CrossfadeAssign::a, juce::dontSendNotification);
                sideB->setToggleState (track.crossfade == Session::CrossfadeAssign::b, juce::dontSendNotification);
            }
            repaint();
        }
        // Session mixer sends: one knob per return, labelled A, B, C as the returns are.
        void syncSends (const Session::TrackInfo& track)
        {
            juce::StringArray ids;
            if (! track.isReturn) for (const auto& send : track.sends) ids.add (send.returnTrackId);
            if (ids != sendReturnIds)
            {
                sendReturnIds = ids; sends.clear();
                for (int i = 0; i < ids.size(); ++i)
                {
                    auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
                    knob->setName ("Send " + juce::String::charToString ((juce::juce_wchar) ('A' + i)));
                    knob->setRange (-100, 6, .1); knob->setSkewFactorFromMidPoint (-12); knob->setDoubleClickReturnValue (true, -100);
                    knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
                    knob->textFromValueFunction = [] (double v) { return v <= -99.95 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
                    knob->onDragStart = [this] { owner.session.beginEditGesture ("Send level"); };
                    knob->onDragEnd = [this] { owner.session.endEditGesture(); };
                    knob->onValueChange = [this, i]
                    { owner.report (owner.session.setTrackSendGain (index, sendReturnIds[i], (float) sends[(size_t) i]->getValue())); };
                    addAndMakeVisible (*knob); sends.push_back (std::move (knob));
                }
                resized();
            }
            for (size_t i = 0; i < sends.size(); ++i)
            {
                if (! sends[i]->isMouseButtonDown()) sends[i]->setValue (track.sends[i].gainDb, juce::dontSendNotification);
                for (const auto& other : owner.state.tracks) if (other.id == sendReturnIds[(int) i]) sends[i]->setTooltip ("Send to " + other.name + ": " + sends[i]->getTextFromValue (sends[i]->getValue()));
                sends[i]->setEnabled (! owner.session.isExporting());
            }
        }
        // Rows of send knobs, three to a row, above the pan control.
        int sendOffset() const noexcept { return sends.empty() ? 0 : (int) ((sends.size() + 2) / 3) * 40; }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (owner.selectedTrack == index ? juce::Colour (0xff454545) : juce::Colour (0xff383838));
            g.setColour (colour); g.fillRect (0, 0, 4, getHeight() - 1);
            g.setColour (line); g.drawRect (getLocalBounds());
            g.setColour (muted); g.setFont (font (9.5f, true));
            if (columnLayout)
            {
                // Two-channel meter beside the fader, 0 dB to -60 dB.
                const auto meter = meterBounds();
                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto bar = meter.withWidth (4).translated (channel * 6, 0);
                    const auto level = channel == 0 ? peakL : peakR;
                    const auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 60.0f);
                    g.setColour (juce::Colour (0xff181818)); g.fillRect (bar);
                    const auto lit = bar.withTop (bar.getBottom() - juce::roundToInt (bar.getHeight() * fraction));
                    g.setColour (level >= 1.0f ? red : green); g.fillRect (lit);
                }
                if (returnTrack) { g.setColour (muted); g.setFont (font (9.0f)); g.drawText ("SEND INPUT", 6, 27, getWidth() - 12, 16, juce::Justification::centred); }
                g.setColour (muted); g.setFont (font (9.0f, true));
                for (size_t i = 0; i < sends.size(); ++i)
                    g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + (int) i)), sends[i]->getBounds().withY (sends[i]->getBottom()).withHeight (11), juce::Justification::centred);
                return;
            }
            // Header strip in the track colour, then routing and mixer lines.
            const bool selected = owner.selectedTrack == index || owner.multiSelectedTracks.contains (trackId);
            g.setColour (selected ? colour.brighter (0.2f) : colour); g.fillRect (0, 0, getWidth() - 8, 24);
            if (owner.multiSelectedTracks.contains (trackId)) { g.setColour (orange); g.drawRect (0, 0, getWidth() - 8, getHeight(), 2); }
            g.setColour (muted); g.setFont (font (9.0f, true));
            if (returnTrack) g.drawText ("SEND INPUT", 10, 26, 64, 19, juce::Justification::centredLeft);
            else g.drawText (midi ? "MIDI" : "IN", 10, 26, 24, 19, juce::Justification::centredLeft);
            g.drawText ("VOL", 10, 49, 24, 20, juce::Justification::centredLeft);
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto level = channel == 0 ? peakL : peakR;
                const auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 60.0f);
                const auto bar = juce::Rectangle<int> (getWidth() - 7 + channel * 3, 1, 2, getHeight() - 2);
                g.setColour (juce::Colour (0xff181818)); g.fillRect (bar);
                g.setColour (level >= 1.0f ? red : green); g.fillRect (bar.withTop (bar.getBottom() - juce::roundToInt (bar.getHeight() * fraction)));
            }
        }
        void resized() override
        {
            if (columnLayout)
            {
                // Top to bottom: input routing, pan, volume with meter, activator / solo / arm.
                name.setBounds (6, 2, getWidth() - 12, 18);
                input.setBounds (5, 4, getWidth() - 10, 19); monitor.setBounds (5, 25, getWidth() - 10, 19);
                sendMode.setBounds (5, 4, getWidth() - 10, 19);
                for (size_t i = 0; i < sends.size(); ++i)
                    sends[i]->setBounds (8 + (int) (i % 3) * 36, 48 + (int) (i / 3) * 40, 34, 28);
                pan.setBounds ((getWidth() - 46) / 2, 50 + sendOffset(), 46, 50);
                gain.setBounds (6, 106 + sendOffset(), 54, getHeight() - 184 - sendOffset());
                delay.setBounds (5, getHeight() - 74, getWidth() - 10, 19);
                if (sideA != nullptr)
                {
                    const int half = (getWidth() - 12) / 2;
                    sideA->setBounds (5, getHeight() - 52, half, 20); sideB->setBounds (7 + half, getHeight() - 52, half, 20);
                }
                const int buttons = getHeight() - 28, w = (getWidth() - 14) / 3;
                mute.setBounds (5, buttons, w, 22); solo.setBounds (7 + w, buttons, w, 22); arm.setBounds (9 + 2 * w, buttons, w, 22);
                return;
            }
            const int w = getWidth() - 8, half = (w - 40) / 2;
            name.setBounds (6, 2, w - 92, 20);
            mute.setBounds (w - 84, 3, 26, 18); solo.setBounds (w - 56, 3, 26, 18); arm.setBounds (w - 28, 3, 26, 18);
            input.setBounds (36, 27, half, 18); monitor.setBounds (40 + half, 27, w - 44 - half, 18);
            sendMode.setBounds (76, 27, w - 80, 18);
            gain.setBounds (32, 49, half + 4, 20); pan.setBounds (40 + half, 49, w - 44 - half, 20);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! columnLayout && e.mods.isShiftDown() && ! e.mods.isPopupMenu())
            {
                if (const auto* current = owner.currentTrack(); current != nullptr && current->id != trackId) owner.multiSelectedTracks.insert (current->id);
                if (! owner.multiSelectedTracks.erase (trackId)) owner.multiSelectedTracks.insert (trackId);
            }
            else if (! e.mods.isPopupMenu()) owner.multiSelectedTracks.clear();
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
        juce::Slider gain, pan, delay;
        juce::ComboBox input, monitor, sendMode;
        juce::StringArray inputNames, sendReturnIds;
        std::vector<std::unique_ptr<juce::Slider>> sends;
        std::unique_ptr<juce::TextButton> sideA, sideB;
        juce::Rectangle<int> meterBounds() const { return { 66, 110 + sendOffset(), 10, juce::jmax (10, getHeight() - 204 - sendOffset()) }; }
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

    // Disclosure triangle: points right when folded, down when open.
    class FoldButton final : public juce::Button
    {
    public:
        FoldButton() : juce::Button ("Fold") {}
        bool folded = false;
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            juce::Path triangle; const auto b = getLocalBounds().toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            if (folded) triangle.addTriangle (b.getX() + 1, b.getY(), b.getX() + 1, b.getBottom(), b.getRight(), b.getCentreY());
            else triangle.addTriangle (b.getX(), b.getY() + 1, b.getRight(), b.getY() + 1, b.getCentreX(), b.getBottom());
            g.setColour (juce::Colour (over ? 0xff000000 : 0xff141414)); g.fillPath (triangle);
        }
    };
    // Arrangement header for a group: fold switch, name, activator, solo and the group mixer.
    class GroupHeader final : public juce::Component
    {
    public:
        GroupHeader (Impl& o, juce::String id, bool column = false) : owner (o), groupId (std::move (id)), columnLayout (column)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &fold, &name, &activator, &solo, &gain, &pan }) addAndMakeVisible (c);
            fold.setName ("Group fold"); fold.setTooltip ("Fold or unfold the group's tracks");
            fold.onClick = [this] { if (const auto* g = info()) owner.report (owner.session.setGroupFolded (groupId, ! g->folded)); owner.refresh(); };
            name.setName ("Group name"); name.setFont (font (11.5f, true)); name.setEditable (false, true, false);
            name.setColour (juce::Label::textColourId, juce::Colour (0xff141414));
            name.onTextChange = [this] { owner.report (owner.session.renameGroup (groupId, name.getText())); owner.refresh(); };
            for (auto* b : { &activator, &solo }) { b->setClickingTogglesState (true); b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
            activator.setName ("Group activator"); activator.setTooltip ("Group activator: click to mute or unmute the whole group");
            activator.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a)); activator.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff57a7ff)); solo.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
            activator.onClick = [this] { owner.report (owner.session.setGroupMute (groupId, ! activator.getToggleState())); owner.refresh(); };
            solo.onClick = [this] { owner.report (owner.session.setGroupSolo (groupId, solo.getToggleState())); owner.refresh(); };
            gain.setName ("Group volume"); gain.setSliderStyle (juce::Slider::LinearHorizontal); gain.setRange (-100, 6, .1); gain.setSkewFactorFromMidPoint (-12);
            gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 16); gain.setDoubleClickReturnValue (true, 0);
            gain.textFromValueFunction = [] (double v) { return v <= -100 ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
            gain.onDragStart = [this] { owner.session.beginEditGesture ("Group volume"); };
            gain.onDragEnd = [this] { owner.session.endEditGesture(); };
            gain.onValueChange = [this] { owner.report (owner.session.setGroupGain (groupId, (float) gain.getValue())); };
            pan.setName ("Group pan"); pan.setSliderStyle (juce::Slider::LinearHorizontal); pan.setRange (-1, 1, .01);
            pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 32, 16); pan.setDoubleClickReturnValue (true, 0);
            pan.textFromValueFunction = [] (double v) { return std::abs (v) < 0.01 ? juce::String ("C") : juce::String ((int) std::round (std::abs (v) * 100.0)) + (v < 0 ? "L" : "R"); };
            pan.onDragStart = [this] { owner.session.beginEditGesture ("Group pan"); };
            pan.onDragEnd = [this] { owner.session.endEditGesture(); };
            pan.onValueChange = [this] { owner.report (owner.session.setGroupPan (groupId, (float) pan.getValue())); };
            if (columnLayout)
            {
                // Session mixer strip: the grid column header names and folds the group.
                gain.setSliderStyle (juce::Slider::LinearVertical); gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);
                pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 42, 15);
                pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
                fold.setVisible (false); name.setVisible (false);
            }
            gain.updateText(); pan.updateText();
        }
        const juce::String& getId() const noexcept { return groupId; }
        void sync()
        {
            const auto* g = info(); if (g == nullptr) return;
            colour = g->colour; folded = g->folded; peakL = g->peakL; peakR = g->peakR; fold.folded = folded; fold.repaint();
            if (! name.isBeingEdited()) name.setText (g->name, juce::dontSendNotification);
            activator.setToggleState (! g->mute, juce::dontSendNotification); solo.setToggleState (g->solo, juce::dontSendNotification);
            activator.setButtonText ("G");
            if (! gain.isMouseButtonDown()) gain.setValue (g->gainDb, juce::dontSendNotification);
            if (! pan.isMouseButtonDown()) pan.setValue (g->pan, juce::dontSendNotification);
            repaint();
        }
        void resized() override
        {
            if (columnLayout)
            {
                pan.setBounds ((getWidth() - 46) / 2, 50, 46, 50);
                gain.setBounds (6, 106, 54, getHeight() - 140);
                const int buttons = getHeight() - 28, half = (getWidth() - 12) / 2;
                activator.setBounds (5, buttons, half, 22); solo.setBounds (7 + half, buttons, half, 22);
                return;
            }
            const int w = getWidth() - 8;
            fold.setBounds (2, 2, 18, 18); name.setBounds (20, 1, w - 80, 20);
            activator.setBounds (w - 56, 2, 26, 18); solo.setBounds (w - 28, 2, 26, 18);
            gain.setBounds (24, 21, (w - 30) / 2 + 10, 17); pan.setBounds (24 + (w - 30) / 2 + 14, 21, w - 38 - (w - 30) / 2 - 10, 17);
        }
        void paint (juce::Graphics& g) override
        {
            if (columnLayout)
            {
                g.fillAll (juce::Colour (0xff383838));
                g.setColour (colour); g.fillRect (0, 0, 4, getHeight() - 1);
                g.setColour (muted); g.setFont (font (9.0f, true)); g.drawText ("GROUP", 6, 8, getWidth() - 12, 16, juce::Justification::centred);
                const juce::Rectangle<int> meter { 66, 110, 10, juce::jmax (10, getHeight() - 160) };
                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto bar = meter.withWidth (4).translated (channel * 6, 0);
                    const auto level = channel == 0 ? peakL : peakR;
                    const auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 60.0f);
                    g.setColour (juce::Colour (0xff181818)); g.fillRect (bar);
                    g.setColour (level >= 1.0f ? red : green); g.fillRect (bar.withTop (bar.getBottom() - juce::roundToInt (bar.getHeight() * fraction)));
                }
                g.setColour (line); g.drawRect (getLocalBounds()); return;
            }
            g.fillAll (juce::Colour (0xff2a2a2a));
            g.setColour (colour); g.fillRect (0, 0, getWidth() - 8, 21);
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto level = channel == 0 ? peakL : peakR;
                const auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 60.0f);
                const auto bar = juce::Rectangle<int> (getWidth() - 7 + channel * 3, 1, 2, getHeight() - 2);
                g.setColour (juce::Colour (0xff181818)); g.fillRect (bar);
                g.setColour (level >= 1.0f ? red : green); g.fillRect (bar.withTop (bar.getBottom() - juce::roundToInt (bar.getHeight() * fraction)));
            }
            g.setColour (line); g.drawRect (getLocalBounds());
        }
    private:
        const Session::GroupInfo* info() const { return owner.groupInfo (groupId); }
        Impl& owner;
        juce::String groupId;
        juce::Colour colour;
        bool folded = false, columnLayout = false;
        float peakL = 0, peakR = 0;
        FoldButton fold;
        juce::TextButton activator { "G" }, solo { "S" };
        juce::Label name;
        juce::Slider gain, pan;
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
            bool layout = owner.syncTrackRows (*this, rows, false);
            bool same = groupHeaders.size() == owner.state.groups.size();
            for (size_t i = 0; same && i < groupHeaders.size(); ++i) same = groupHeaders[i]->getId() == owner.state.groups[i].id;
            if (! same)
            {
                groupHeaders.clear();
                for (const auto& group : owner.state.groups) { groupHeaders.push_back (std::make_unique<GroupHeader> (owner, group.id)); addAndMakeVisible (*groupHeaders.back()); }
                layout = true;
            }
            for (auto& header : groupHeaders) header->sync();
            if (layout || owner.lanesHeight != lastLanesHeight) { lastLanesHeight = owner.lanesHeight; resized(); }
            repaint();
        }
        std::vector<std::unique_ptr<GroupHeader>> groupHeaders;
        int lastLanesHeight = -1;
        int timelineWidth() const { return getWidth() - trackWidth; }
        void resized() override
        {
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const int height = owner.laneHeight ((int) i);
                rows[i]->setVisible (height > 0); rows[i]->setBounds (timelineWidth(), owner.laneY ((int) i), trackWidth, juce::jmax (1, height));
            }
            for (const auto& lane : owner.lanes)
                if (lane.group && juce::isPositiveAndBelow (lane.index, (int) groupHeaders.size()))
                    groupHeaders[(size_t) lane.index]->setBounds (timelineWidth(), lane.y, trackWidth, lane.height);
        }
        juce::Rectangle<float> clipBounds (const Session::ClipInfo& clip, int track) const
        {
            return { (float) ((clip.start - owner.viewStart) * owner.pixelsPerSecond), (float) (owner.laneY (track) + 6),
                     (float) juce::jmax (3.0, clip.length * owner.pixelsPerSecond), (float) juce::jmax (0, owner.laneHeight (track) - 12) };
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
            for (const auto& lane : owner.lanes)
            {
                if (lane.height == 0) continue;
                if (lane.group)
                {
                    const auto& group = owner.state.groups[(size_t) lane.index];
                    g.setColour (juce::Colour (0xff262626)); g.fillRect (0, lane.y, tw, lane.height);
                    // Member clips summarized as thin bars, as on a folded group lane.
                    for (int t = 0; t < (int) owner.state.tracks.size(); ++t)
                        if (owner.state.tracks[(size_t) t].groupId == group.id)
                            for (const auto& clip : owner.state.tracks[(size_t) t].clips)
                            {
                                const auto x = (float) ((clip.start - owner.viewStart) * owner.pixelsPerSecond);
                                g.setColour (owner.state.tracks[(size_t) t].colour.withAlpha (clip.active ? 0.8f : 0.3f));
                                g.fillRect (x, (float) lane.y + 12.0f, (float) juce::jmax (2.0, clip.length * owner.pixelsPerSecond), (float) lane.height - 24.0f);
                            }
                }
                else
                {
                    const int i = lane.index;
                    g.setColour (i == owner.selectedTrack ? juce::Colour (0xff353535) : (i % 2 == 0 ? juce::Colour (0xff2c2c2c) : juce::Colour (0xff303030)));
                    g.fillRect (0, lane.y, tw, lane.height);
                }
                g.setColour (line); g.drawHorizontalLine (lane.y + lane.height - 1, 0.0f, (float) tw);
            }
            const double beat = 60.0 / owner.state.tempo;
            const auto grid = owner.gridSeconds();
            const auto end = owner.viewStart + tw / owner.pixelsPerSecond;
            for (double t = std::floor (owner.viewStart / grid) * grid; t <= end; t += grid)
            {
                const auto barSeconds = beat * owner.state.timeSignature.beatsPerBar();
                const bool bar = std::abs (t / barSeconds - std::round (t / barSeconds)) < 0.001;
                g.setColour (bar ? juce::Colour (0xff4a4a4a) : juce::Colour (0xff3a3a3a));
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
                    g.setColour (background); g.fillRect (0, owner.laneY (t) + 1, tw, owner.laneHeight (t) - 2);
                    g.setColour (muted); g.setFont (font (11.0f));
                    g.drawText ("RETURN  /  Audio from track sends", 16, owner.laneY (t) + owner.laneHeight (t) / 2 - 13, tw - 32, 26, juce::Justification::centredLeft);
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
                    if (track.frozen)
                    {
                        // Frozen clips play their render: an icy hatch marks them as locked.
                        juce::Graphics::ScopedSaveState frost (g); g.reduceClipRegion (bounds.toNearestInt());
                        g.setColour (juce::Colour (0x6094c8ff));
                        for (auto x = bounds.getX() - bounds.getHeight(); x < bounds.getRight(); x += 9.0f)
                            g.drawLine (x, bounds.getBottom(), x + bounds.getHeight(), bounds.getY(), 1.4f);
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
                    g.fillRect ((float) ((from - owner.viewStart) * owner.pixelsPerSecond), (float) owner.laneY (t) + 6,
                                (float) juce::jmax (2.0, (owner.state.position - from) * owner.pixelsPerSecond), (float) owner.laneHeight (t) - 12);
                    g.setColour (red); g.setFont (font (11.0f, true));
                    g.drawText ("RECORDING", (int) ((from - owner.viewStart) * owner.pixelsPerSecond) + 8, owner.laneY (t) + 12, 96, 18, juce::Justification::centredLeft);
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
            // The time selection lights its lanes between its edges.
            if (owner.timeSelection.active())
            {
                const auto x0 = (float) ((owner.timeSelection.start - owner.viewStart) * owner.pixelsPerSecond);
                const auto x1 = (float) ((owner.timeSelection.end - owner.viewStart) * owner.pixelsPerSecond);
                for (int t = 0; t < (int) owner.state.tracks.size(); ++t)
                    if (owner.timeSelection.trackIds.contains (owner.state.tracks[(size_t) t].id) && owner.laneHeight (t) > 0)
                    {
                        const juce::Rectangle<float> area (x0, (float) owner.laneY (t), x1 - x0, (float) owner.laneHeight (t));
                        g.setColour (juce::Colour (0xffb9d3ff).withAlpha (.3f)); g.fillRect (area);
                        g.setColour (juce::Colour (0xffb9d3ff).withAlpha (.7f)); g.drawVerticalLine ((int) x0, area.getY(), area.getBottom()); g.drawVerticalLine ((int) x1, area.getY(), area.getBottom());
                    }
            }
            auto x = (float) ((owner.state.position - owner.viewStart) * owner.pixelsPerSecond);
            g.setColour (owner.state.recording ? red : juce::Colour (0xffeeeeee));
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
            auto track = owner.trackAtY (e.y);
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
                        owner.timeSelection = {};
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
            // Dragging over empty lane space selects time; Shift-click extends the selection.
            if (! e.mods.isPopupMenu())
            {
                const auto at = owner.snapTime (owner.viewStart + e.x / owner.pixelsPerSecond, e.mods);
                if (e.mods.isShiftDown() && owner.timeSelection.active())
                {
                    selectionAnchorTime = std::abs (at - owner.timeSelection.start) > std::abs (at - owner.timeSelection.end) ? owner.timeSelection.start : owner.timeSelection.end;
                    selectionAnchorTrack = owner.trackIndex (owner.timeSelection.trackIds[0]);
                    extendSelection (at, track);
                }
                else { owner.timeSelection = {}; selectionAnchorTime = at; selectionAnchorTrack = track; }
                selecting = true;
            }
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
                if (track->clips[(size_t)owner.selectedClip].isAudio) owner.openPianoRoll();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (selecting && e.mouseWasDraggedSinceMouseDown())
            {
                extendSelection (owner.snapTime (juce::jmax (0.0, owner.viewStart + e.x / owner.pixelsPerSecond), e.mods), owner.trackAtY (juce::jmax (0, e.y)));
                return;
            }
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
                        const int destination = owner.trackAtY (e.y);
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
            draggedClip = -1; fadeDrag = 0; rangeDrag = 0; dragClipId.clear(); selecting = false;
        }
        // The selection runs from the anchor to the pointer in time and across the tracks between them.
        void extendSelection (double at, int track)
        {
            const int count = (int) owner.state.tracks.size();
            if (count == 0 || selectionAnchorTrack < 0) return;
            const int first = juce::jlimit (0, count - 1, juce::jmin (selectionAnchorTrack, track < 0 ? count - 1 : track));
            const int last = juce::jlimit (0, count - 1, juce::jmax (selectionAnchorTrack, track < 0 ? count - 1 : track));
            Impl::TimeSelection next { juce::jmin (selectionAnchorTime, at), juce::jmax (selectionAnchorTime, at), {} };
            for (int t = first; t <= last; ++t) next.trackIds.add (owner.state.tracks[(size_t) t].id);
            owner.timeSelection = next; repaint(); owner.ruler.repaint();
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto track = owner.trackAtY (e.y); bool handle = false;
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
        int draggedClip = -1, dragStartX = 0, selectionAnchorTrack = -1;
        double originalStart = 0, originalFade = 0, selectionAnchorTime = 0;
        bool selecting = false;
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
            g.fillAll (juce::Colour (0xff393939));
            const auto width=owner.canvas.timelineWidth();
            g.saveState();g.reduceClipRegion (0,0,width,getHeight());
            const auto x1=(float)((owner.state.loopStart-owner.viewStart)*owner.pixelsPerSecond);
            const auto x2=(float)((owner.state.loopEnd-owner.viewStart)*owner.pixelsPerSecond);
            g.setColour (owner.state.looping?orange:muted.withAlpha (.55f));
            g.fillRect (x1,3.0f,juce::jmax (1.0f,x2-x1),3.0f);g.fillRect (x1,3.0f,2.0f,9.0f);g.fillRect (x2-2,3.0f,2.0f,9.0f);
            const auto bar=owner.state.timeSignature.beatsPerBar()*60.0/owner.state.tempo;
            const auto step=bar*juce::jmax (1.0,std::pow (2.0,std::ceil (std::log2 (48.0/(bar*owner.pixelsPerSecond)))));
            const auto end=owner.viewStart+width/owner.pixelsPerSecond;
            g.setFont (font (10.5f));
            for (double time=std::floor (owner.viewStart/step)*step;time<=end;time+=step)
            {
                const int x=(int)std::round ((time-owner.viewStart)*owner.pixelsPerSecond);
                g.setColour (muted);g.drawText (juce::String ((int)std::round (time/bar)+1),x+5,15,55,19,juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xff5d5d5d));g.drawVerticalLine (x,14.0f,34.0f);
            }
            g.setColour (background);g.fillRect (0,35,width,getHeight()-35);
            for (const auto& marker : owner.state.locators)
            {
                const auto bounds=locatorBounds (marker);
                if (bounds.getRight()<0 || bounds.getX()>=width) continue;
                g.setColour (marker.id==selected?orange:juce::Colour (0xffb2b2b2));
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
            for (auto* child : std::initializer_list<juce::Component*> { &mixers, &horizontal, &vertical, &masterStrip }) addAndMakeVisible (child);
            mixers.setInterceptsMouseClicks (false, true);
            horizontal.addListener (this); vertical.addListener (this);
        }
        ~LauncherGrid() override { horizontal.removeListener (this); vertical.removeListener (this); }
        void sync()
        {
            buildColumns();
            owner.syncTrackRows (mixers, rows, true);
            bool same = groupStrips.size() == owner.state.groups.size();
            for (size_t i = 0; same && i < groupStrips.size(); ++i) same = groupStrips[i]->getId() == owner.state.groups[i].id;
            if (! same)
            {
                groupStrips.clear();
                for (const auto& group : owner.state.groups) { groupStrips.push_back (std::make_unique<GroupHeader> (owner, group.id, true)); mixers.addAndMakeVisible (*groupStrips.back()); }
            }
            for (auto& strip : groupStrips) strip->sync();
            masterStrip.sync();
            resized(); repaint();
        }
        // Display columns: a group's column precedes its members, and a folded group hides them.
        struct Column { int track = -1, group = -1; };
        void buildColumns()
        {
            columns.clear(); juce::String open;
            for (int t = 0; t < (int) owner.state.tracks.size(); ++t)
            {
                const auto& track = owner.state.tracks[(size_t) t];
                if (track.groupId != open && track.groupId.isNotEmpty())
                    for (int g = 0; g < (int) owner.state.groups.size(); ++g)
                        if (owner.state.groups[(size_t) g].id == track.groupId) columns.push_back ({ -1, g });
                open = track.groupId;
                const auto* group = owner.groupInfo (track.groupId);
                if (group == nullptr || ! group->folded) columns.push_back ({ t, -1 });
            }
        }
        int columnCount() const { return (int) columns.size(); }
        int columnOf (int track) const { for (size_t c = 0; c < columns.size(); ++c) if (columns[c].track == track) return (int) c; return -1; }
        int groupColumnOf (int group) const { for (size_t c = 0; c < columns.size(); ++c) if (columns[c].group == group) return (int) c; return -1; }
        int columnX (int column) const { return column * columnWidth - (int) offsetX; }
        int columnAt (int x) const
        {
            const int c = (int) std::floor ((x + offsetX) / columnWidth);
            return x >= 0 && x < tracksWidth() && c >= 0 && c < (int) columns.size() ? c : -1;
        }
        void resized() override
        {
            const auto grid = gridArea();
            mixers.setBounds (0, grid.getBottom() + stopHeight, tracksWidth(), mixerHeight());
            for (int i = 0; i < (int) rows.size(); ++i)
            {
                const int c = columnOf (i);
                rows[(size_t) i]->setVisible (c >= 0); rows[(size_t) i]->setBounds (columnX (juce::jmax (0, c)), 0, columnWidth, mixerHeight());
            }
            for (int g = 0; g < (int) groupStrips.size(); ++g)
            {
                const int c = groupColumnOf (g);
                groupStrips[(size_t) g]->setVisible (c >= 0); groupStrips[(size_t) g]->setBounds (columnX (juce::jmax (0, c)), 0, columnWidth, mixerHeight());
            }
            horizontal.setBounds (0, getHeight() - scrollSize, tracksWidth(), scrollSize);
            vertical.setBounds (getWidth() - scrollSize, gridTop, scrollSize, grid.getHeight());
            masterStrip.setBounds (masterX(), grid.getBottom() + stopHeight, sceneWidth, mixerHeight());
            const auto totalWidth = juce::jmax ((double) tracksWidth(), (double) columns.size() * columnWidth);
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
            const int column = columnAt (point.x);
            const int track = column >= 0 ? columns[(size_t) column].track : -1;
            const int scene = (int) ((point.y - gridTop) / (double) rowHeight + offsetScene);
            return track >= 0 && track < (int) owner.state.tracks.size() && ! owner.state.tracks[(size_t) track].isReturn && scene < (int) owner.state.scenes.size() ? std::pair<int, int> { track, scene } : std::pair<int, int> { -1, -1 };
        }
        void paint (juce::Graphics& g) override
        {
            // Cells sit on a dark ground with one-pixel gutters, like a console grid.
            g.fillAll (gutter);
            const auto grid = gridArea();
            const auto dark = juce::Colour (0xff141414);
            g.saveState(); g.reduceClipRegion (0, 0, tracksWidth(), getHeight());
            for (int c = 0; c < (int) columns.size(); ++c)
            {
                const int x = columnX (c);
                if (x + columnWidth <= 0 || x >= tracksWidth()) continue;
                if (columns[(size_t) c].group >= 0) { paintGroupColumn (g, columns[(size_t) c].group, x); continue; }
                const int t = columns[(size_t) c].track;
                const auto& track = owner.state.tracks[(size_t) t];
                const bool selectedTrack = t == owner.selectedTrack;
                const auto header = juce::Rectangle<int> (x, 0, columnWidth, headerHeight).reduced (1, 0).withTrimmedBottom (1);
                g.setColour (selectedTrack ? track.colour.brighter (0.25f) : track.colour); g.fillRect (header);
                g.setColour (dark); g.setFont (font (11.0f, selectedTrack));
                g.drawText (track.name, header.reduced (6, 0), juce::Justification::centredLeft, true);
                if (track.isReturn)
                {
                    g.setColour (slotColour); g.fillRect (x + 1, gridTop, columnWidth - 2, grid.getHeight() + stopHeight);
                    continue;
                }
                g.saveState(); g.reduceClipRegion (grid.withWidth (tracksWidth()));
                for (int scene = 0; scene < (int) owner.state.scenes.size(); ++scene)
                {
                    const auto bounds = slotBounds (t, scene);
                    if (! bounds.intersects (grid)) continue;
                    const auto* slot = slotAt (t, scene);
                    const bool filled = slot != nullptr && slot->hasClip;
                    const bool selected = selectedTrack && owner.state.scenes[(size_t) scene].id == owner.selectedSceneId;
                    const bool queued = slot != nullptr && (slot->queued || slot->stopQueued);
                    const bool blink = owner.ticks % 16 < 8;
                    const auto cell = bounds.reduced (1, 0).withTrimmedBottom (1);
                    if (filled)
                    {
                        const auto clipColour = slot->clip.active ? track.colour : juce::Colour (0xff6a6a6a);
                        g.setColour (queued && blink ? clipColour.brighter (0.35f) : clipColour); g.fillRect (cell);
                        g.setColour (slot->playing ? green.brighter (0.2f) : queued ? juce::Colours::white : dark);
                        if (slot->stopQueued) g.fillRect (cell.getX() + 6, cell.getCentreY() - 4, 8, 8);
                        else drawTriangle (g, cell.getX() + 6, cell.getCentreY() - 5);
                        if (slot->playing) { g.setColour (dark.withAlpha (0.35f)); g.fillRect (cell.withTrimmedLeft (cell.getWidth() - 3)); }
                        const bool follows = slot->clip.follow.action != Session::FollowAction::off;
                        g.setColour (dark); g.setFont (font (10.5f));
                        g.drawText ((slot->clip.active ? juce::String() : juce::String ("OFF / ")) + slot->clip.name,
                                    cell.withTrimmedLeft (20).withTrimmedRight (follows ? 34 : 4), juce::Justification::centredLeft, true);
                        if (follows)
                        {
                            g.setFont (font (9.0f));
                            g.drawText (juce::String (slot->clip.follow.afterBeats, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "b >",
                                        cell.withLeft (cell.getRight() - 33).reduced (2, 0), juce::Justification::centredRight, true);
                        }
                    }
                    else if (slot != nullptr && slot->recording)
                    {
                        // A recording slot blinks red until its take ends on the grid.
                        g.setColour (red.withAlpha (0.35f)); g.fillRect (cell);
                        g.setColour (red.withAlpha (blink ? 1.0f : 0.55f)); g.fillEllipse ((float) cell.getX() + 6, (float) cell.getCentreY() - 4, 8, 8);
                        g.setColour (ink); g.setFont (font (10.5f)); g.drawText ("Recording", cell.withTrimmedLeft (20), juce::Justification::centredLeft, true);
                    }
                    else
                    {
                        g.setColour (slotColour); g.fillRect (cell);
                        if (track.armed && ! queued)
                        {
                            // On an armed track an empty slot's button records into it.
                            g.setColour (juce::Colour (0xff9a4a46)); g.drawEllipse ((float) cell.getX() + 6, (float) cell.getCentreY() - 4, 8, 8, 1.5f);
                        }
                        else
                        {
                            g.setColour (queued ? orange.withAlpha (blink ? 1.0f : 0.45f) : juce::Colour (0xff4d4d4d));
                            g.fillRect (cell.getX() + 7, cell.getCentreY() - 3, 6, 6);
                        }
                    }
                    if (selected) { g.setColour (orange); g.drawRect (cell.expanded (1, 0).withTrimmedTop (-1), 2); }
                }
                g.restoreState();
                const auto stop = juce::Rectangle<int> (x, grid.getBottom(), columnWidth, stopHeight).reduced (1, 0).withTrimmedBottom (1);
                g.setColour (slotColour.darker (0.15f)); g.fillRect (stop);
                g.setColour (track.launcherActive ? orange : muted); g.fillRect (stop.getCentreX() - 4, stop.getCentreY() - 4, 8, 8);
            }
            g.restoreState();
            // Master column: scene launchers, with Stop All Clips under them.
            const int sceneX = masterX();
            const auto master = juce::Rectangle<int> (sceneX, 0, sceneWidth, headerHeight).reduced (1, 0).withTrimmedBottom (1);
            g.setColour (juce::Colour (0xff4a4a4a)); g.fillRect (master);
            g.setColour (ink); g.setFont (font (11.0f, true)); g.drawText ("Master", master.reduced (6, 0), juce::Justification::centredLeft);
            g.saveState(); g.reduceClipRegion (sceneX, gridTop, sceneWidth, grid.getHeight());
            for (int scene = 0; scene < (int) owner.state.scenes.size(); ++scene)
            {
                const auto cell = juce::Rectangle<int> (sceneX, sceneY (scene), sceneWidth, rowHeight).reduced (1, 0).withTrimmedBottom (1);
                const bool selected = owner.state.scenes[(size_t) scene].id == owner.selectedSceneId;
                g.setColour (selected ? juce::Colour (0xff4a4a4a) : juce::Colour (0xff383838)); g.fillRect (cell);
                g.setColour (selected ? orange : ink.withAlpha (0.8f)); drawTriangle (g, cell.getX() + 6, cell.getCentreY() - 5);
                g.setColour (ink); g.setFont (font (10.5f)); g.drawText (owner.state.scenes[(size_t) scene].name, cell.withTrimmedLeft (20).withTrimmedRight (6), juce::Justification::centredLeft, true);
            }
            g.restoreState();
            const auto stopAll = stopAllBounds();
            g.setColour (juce::Colour (0xff383838)); g.fillRect (stopAll);
            g.setColour (ink.withAlpha (0.85f)); g.fillRect (stopAll.getX() + 7, stopAll.getCentreY() - 4, 8, 8);
            g.setFont (font (10.0f)); g.drawText ("Stop All Clips", stopAll.withTrimmedLeft (22), juce::Justification::centredLeft, true);
            if (owner.state.scenes.empty()) { g.setColour (muted); g.setFont (font (13.0f)); g.drawText ("Insert a scene to begin launching clips.", grid.reduced (20), juce::Justification::centred); }
        }
        // A group column: header with disclosure triangle, slots that launch the members' row, and a stop button.
        void paintGroupColumn (juce::Graphics& g, int index, int x)
        {
            const auto& group = owner.state.groups[(size_t) index];
            const auto grid = gridArea();
            const auto dark = juce::Colour (0xff141414);
            const auto header = juce::Rectangle<int> (x, 0, columnWidth, headerHeight).reduced (1, 0).withTrimmedBottom (1);
            g.setColour (group.colour); g.fillRect (header);
            juce::Path fold; const auto fx = (float) header.getX() + 7, fy = (float) header.getCentreY();
            if (group.folded) fold.addTriangle (fx, fy - 4, fx, fy + 4, fx + 6, fy);
            else fold.addTriangle (fx - 1, fy - 3, fx + 7, fy - 3, fx + 3, fy + 3);
            g.setColour (dark); g.fillPath (fold);
            g.setFont (font (11.0f, true)); g.drawText (group.name, header.withTrimmedLeft (18).reduced (4, 0), juce::Justification::centredLeft, true);
            const bool blink = owner.ticks % 16 < 8;
            bool active = false;
            g.saveState(); g.reduceClipRegion (grid.withWidth (tracksWidth()));
            for (int scene = 0; scene < (int) owner.state.scenes.size(); ++scene)
            {
                const auto cell = juce::Rectangle<int> (x, sceneY (scene), columnWidth, rowHeight).reduced (1, 0).withTrimmedBottom (1);
                if (! cell.intersects (grid)) continue;
                bool clips = false, playing = false, queued = false;
                for (int t = 0; t < (int) owner.state.tracks.size(); ++t)
                    if (owner.state.tracks[(size_t) t].groupId == group.id)
                        if (const auto* slot = slotAt (t, scene))
                        { clips = clips || slot->hasClip; playing = playing || slot->playing; queued = queued || slot->queued || slot->stopQueued; }
                if (clips)
                {
                    const auto fill = group.colour.withMultipliedSaturation (0.6f).darker (0.2f);
                    g.setColour (queued && blink ? fill.brighter (0.35f) : fill); g.fillRect (cell);
                    g.setColour (playing ? green.brighter (0.2f) : queued ? juce::Colours::white : dark); drawTriangle (g, cell.getX() + 6, cell.getCentreY() - 5);
                }
                else
                {
                    g.setColour (slotColour.darker (0.1f)); g.fillRect (cell);
                    g.setColour (juce::Colour (0xff4d4d4d)); g.fillRect (cell.getX() + 7, cell.getCentreY() - 3, 6, 6);
                }
                if (owner.state.scenes[(size_t) scene].id == owner.selectedSceneId) { g.setColour (orange.withAlpha (0.5f)); g.drawRect (cell, 1); }
            }
            g.restoreState();
            for (const auto& track : owner.state.tracks) active = active || (track.groupId == group.id && track.launcherActive);
            const auto stop = juce::Rectangle<int> (x, grid.getBottom(), columnWidth, stopHeight).reduced (1, 0).withTrimmedBottom (1);
            g.setColour (slotColour.darker (0.15f)); g.fillRect (stop);
            g.setColour (active ? orange : muted); g.fillRect (stop.getCentreX() - 4, stop.getCentreY() - 4, 8, 8);
        }
        // Geometry used by pointer handlers and their tests.
        juce::Point<float> groupFoldPoint (int group) const { return { (float) columnX (groupColumnOf (group)) + 8.0f, headerHeight / 2.0f }; }
        juce::Point<float> groupLaunchPoint (int group, int scene) const { return { (float) columnX (groupColumnOf (group)) + 10.0f, (float) sceneY (scene) + rowHeight / 2.0f }; }
        juce::Point<float> groupStopPoint (int group) const { return { (float) columnX (groupColumnOf (group)) + columnWidth / 2.0f, (float) (gridArea().getBottom() + stopHeight / 2) }; }
        juce::Point<float> slotCentre (int track, int scene) const { return slotBounds (track, scene).getCentre().toFloat(); }
        juce::Point<float> launchPoint (int track, int scene) const { const auto b = slotBounds (track, scene); return { (float) b.getX() + 10.0f, (float) b.getCentreY() }; }
        juce::Point<float> trackStopPoint (int track) const { return { (float) (columnX (columnOf (track)) + columnWidth / 2), (float) (gridArea().getBottom() + stopHeight / 2) }; }
        juce::Point<float> stopAllPoint() const { return stopAllBounds().getCentre().toFloat(); }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (owner.session.isExporting()) return;
            grabKeyboardFocus();
            if (const int column = columnAt (event.x); column >= 0 && columns[(size_t) column].group >= 0)
            {
                const auto& group = owner.state.groups[(size_t) columns[(size_t) column].group];
                const auto local = event.x - columnX (column);
                if (event.y < headerHeight) { if (local < 20) owner.report (owner.session.setGroupFolded (group.id, ! group.folded)); }
                else if (event.y < gridArea().getBottom())
                {
                    const int scene = (int) ((event.y - gridTop) / (double) rowHeight + offsetScene);
                    if (scene >= 0 && scene < (int) owner.state.scenes.size())
                    {
                        owner.selectedSceneId = owner.state.scenes[(size_t) scene].id;
                        if (local < 25) owner.report (owner.session.launchGroupSlot (group.id, scene));
                    }
                }
                else if (event.y < gridArea().getBottom() + stopHeight) owner.report (owner.session.stopGroup (group.id));
                owner.refresh(); repaint(); return;
            }
            const auto cell = cellAt (event.getPosition());
            if (cell.first >= 0)
            {
                owner.selectLauncherSlot (cell.first, cell.second);
                if (event.mods.isPopupMenu()) { slotMenu (cell.first, cell.second); return; }
                if (event.x - slotBounds (cell.first, cell.second).getX() < 25)
                {
                    // Gate and Repeat clips follow the button, so the release is sent on mouse up.
                    pressedCell = cell; owner.report (owner.session.pressSlot (cell.first, cell.second)); owner.refresh();
                }
                repaint(); return;
            }
            if (stopAllBounds().contains (event.getPosition())) { owner.report (owner.session.stopAllLauncherClips()); owner.refresh(); return; }
            const auto scene = sceneAt (event.getPosition());
            if (scene >= 0)
            {
                owner.selectedSceneId = owner.state.scenes[(size_t) scene].id;
                if (event.mods.isPopupMenu()) sceneMenu (scene);
                else if (event.x - masterX() < 22) { owner.report (owner.session.launchScene (scene)); owner.refresh(); }
                repaint(); return;
            }
            if (event.x >= masterX() && event.x < masterX() + sceneWidth && event.y >= gridTop && event.mods.isPopupMenu())
            {
                juce::PopupMenu menu; menu.addItem (1, "Insert Scene");
                auto safe = juce::Component::SafePointer<Impl> (&owner);
                menu.showMenuAsync (juce::PopupMenu::Options(), [safe] (int choice) { if (safe != nullptr && choice == 1) safe->addSceneFromUI(); });
                return;
            }
            const int column = columnAt (event.x);
            const auto track = column >= 0 ? columns[(size_t) column].track : -1;
            if (event.x < tracksWidth() && track >= 0 && track < (int) owner.state.tracks.size())
            {
                owner.selectTrack (track);
                if (! owner.state.tracks[(size_t) track].isReturn && event.y >= gridArea().getBottom() && event.y < gridArea().getBottom() + stopHeight) { owner.report (owner.session.stopLauncherTrack (track)); owner.refresh(); }
                repaint();
            }
        }
        void mouseUp (const juce::MouseEvent&) override { releasePressed (pressedCell); }
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
            const int scene = sceneAt (event.getPosition()); if (scene >= 0) { owner.renameSceneDialog (scene); return; }
            // Double-clicking below the last scene inserts another, as in the master column.
            if (event.x >= masterX() && event.x < masterX() + sceneWidth && event.y >= gridTop && event.y < gridArea().getBottom()) owner.addSceneFromUI();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (owner.session.isExporting()) return true;
            const auto code = key.getKeyCode();
            if (code == juce::KeyPress::returnKey)
            {
                if (keyCell.first >= 0) return true; // Key repeat while Enter is held.
                const auto scene = owner.selectedSceneIndex();
                if (scene >= 0 && key.getModifiers().isShiftDown()) owner.report (owner.session.launchScene (scene));
                else if (scene >= 0 && owner.currentTrack() != nullptr && ! owner.currentTrack()->isReturn)
                {
                    keyCell = { owner.selectedTrack, scene }; owner.report (owner.session.pressSlot (owner.selectedTrack, scene));
                }
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
        bool keyStateChanged (bool) override
        {
            if (keyCell.first >= 0 && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::returnKey)) releasePressed (keyCell);
            return false;
        }
        void focusLost (FocusChangeType) override { releasePressed (keyCell); }
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
            if (const int column = columnOf (owner.selectedTrack); column >= 0)
            { const auto x = column * columnWidth; if (x < offsetX) offsetX = x; if (x + columnWidth > offsetX + tracksWidth()) offsetX = x + columnWidth - tracksWidth(); }
            resized(); repaint();
        }
    private:
        Impl& owner;
        // Launch buttons held by the mouse and by Enter, released into Gate and Repeat clips.
        std::pair<int, int> pressedCell { -1, -1 }, keyCell { -1, -1 };
        void releasePressed (std::pair<int, int>& cell)
        {
            if (cell.first < 0) return;
            const auto held = std::exchange (cell, { -1, -1 });
            if (owner.session.isExporting()) return;
            owner.report (owner.session.releaseSlot (held.first, held.second)); owner.refresh();
        }
    public:
        static constexpr int headerHeight = 22, gridTop = headerHeight, columnWidth = 120, rowHeight = 24,
                             sceneWidth = 132, baseMixerHeight = 252, stopHeight = 22, scrollSize = 10;
        // The mixer grows by a row of send knobs for every three returns.
        int mixerHeight() const
        {
            int returns = 0; for (const auto& track : owner.state.tracks) returns += track.isReturn;
            return baseMixerHeight + (returns + 2) / 3 * 40;
        }
        // The Master column follows the last track column, or the right edge once tracks scroll.
        int masterX() const { return juce::jlimit (0, tracksWidth(), (int) columns.size() * columnWidth - (int) offsetX); }
    private:
        static inline const juce::Colour gutter { 0xff1a1a1a }, slotColour { 0xff2c2c2c };
        juce::Component mixers;
        std::vector<std::unique_ptr<TrackRow>> rows;
        std::vector<std::unique_ptr<GroupHeader>> groupStrips;
        std::vector<Column> columns;
        // The Master channel at the foot of the Master column: volume fader and output meter.
        class MasterStrip final : public juce::Component
        {
        public:
            explicit MasterStrip (Impl& o) : owner (o)
            {
                setName ("Session master strip");
                gain.setName ("Session master volume"); gain.setSliderStyle (juce::Slider::LinearVertical);
                gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16); gain.setRange (-100, 6, .1); gain.setSkewFactorFromMidPoint (-12);
                gain.setDoubleClickReturnValue (true, 0);
                gain.textFromValueFunction = [] (double v) { return v <= -100 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
                gain.onDragStart = [this] { owner.session.beginEditGesture ("Master volume"); };
                gain.onDragEnd = [this] { owner.session.endEditGesture(); };
                gain.onValueChange = [this] { owner.report (owner.session.setMasterGain ((float) gain.getValue())); };
                gain.updateText(); addAndMakeVisible (gain);
                crossfader.setName ("Crossfader"); crossfader.setSliderStyle (juce::Slider::LinearHorizontal);
                crossfader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); crossfader.setRange (-1, 1, .001); crossfader.setDoubleClickReturnValue (true, 0);
                crossfader.setTooltip ("Crossfader: left plays side A, right plays side B. Double-click to centre.");
                crossfader.onDragStart = [this] { owner.session.beginEditGesture ("Crossfader"); };
                crossfader.onDragEnd = [this] { owner.session.endEditGesture(); };
                crossfader.onValueChange = [this] { owner.report (owner.session.setCrossfader ((float) crossfader.getValue())); };
                addAndMakeVisible (crossfader);
            }
            void sync()
            {
                if (! crossfader.isMouseButtonDown()) crossfader.setValue (owner.state.crossfader, juce::dontSendNotification);
                crossfader.setEnabled (! owner.session.isExporting());
                if (! gain.isMouseButtonDown()) gain.setValue (owner.state.masterGainDb, juce::dontSendNotification);
                gain.setEnabled (owner.state.masterVolumeAutomation.points.empty() && ! owner.session.isExporting());
                peakL = owner.state.masterPeakL; peakR = owner.state.masterPeakR; repaint();
            }
            void resized() override { gain.setBounds (6, 106, 54, getHeight() - 162); crossfader.setBounds (16, getHeight() - 34, getWidth() - 32, 22); }
            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colour (0xff383838));
                g.setColour (muted); g.setFont (font (9.0f, true)); g.drawText ("MASTER", 6, 8, getWidth() - 12, 16, juce::Justification::centred);
                g.drawText ("A", 4, getHeight() - 34, 12, 22, juce::Justification::centred); g.drawText ("B", getWidth() - 16, getHeight() - 34, 12, 22, juce::Justification::centred);
                const juce::Rectangle<int> meter { 66, 110, 10, juce::jmax (10, getHeight() - 182) };
                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto bar = meter.withWidth (4).translated (channel * 6, 0);
                    const auto level = channel == 0 ? peakL : peakR;
                    const auto fraction = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 60.0f);
                    g.setColour (juce::Colour (0xff181818)); g.fillRect (bar);
                    g.setColour (level >= 1.0f ? red : green); g.fillRect (bar.withTop (bar.getBottom() - juce::roundToInt (bar.getHeight() * fraction)));
                }
                g.setColour (line); g.drawRect (getLocalBounds());
            }
        private:
            Impl& owner;
            juce::Slider gain, crossfader;
            float peakL = 0, peakR = 0;
        } masterStrip { owner };
        juce::ScrollBar horizontal { false }, vertical { true };
        double offsetX = 0, offsetScene = 0;
        int tracksWidth() const { return juce::jmax (1, getWidth() - sceneWidth - scrollSize); }
        juce::Rectangle<int> gridArea() const { return { 0, gridTop, tracksWidth(), juce::jmax (rowHeight, getHeight() - gridTop - mixerHeight() - stopHeight - scrollSize) }; }
        int sceneY (int scene) const { return gridTop + (int) std::round ((scene - offsetScene) * rowHeight); }
        juce::Rectangle<int> stopAllBounds() const { return juce::Rectangle<int> (masterX(), gridArea().getBottom(), sceneWidth, stopHeight).reduced (1, 0).withTrimmedBottom (1); }
        juce::Rectangle<int> slotBounds (int track, int scene) const
        { const int column = columnOf (track); return column < 0 ? juce::Rectangle<int>() : juce::Rectangle<int> { columnX (column), sceneY (scene), columnWidth, rowHeight }; }
        const Session::SlotInfo* slotAt (int track, int scene) const
        {
            if (track < 0 || track >= (int) owner.state.tracks.size()) return nullptr;
            const auto& slots = owner.state.tracks[(size_t) track].slots;
            return scene >= 0 && scene < (int) slots.size() ? &slots[(size_t) scene] : nullptr;
        }
        int sceneAt (juce::Point<int> point) const
        {
            if (point.x < masterX() || point.x >= masterX() + sceneWidth || point.y < gridTop || point.y >= gridArea().getBottom()) return -1;
            const int scene = (int) ((point.y - gridTop) / (double) rowHeight + offsetScene);
            return scene < (int) owner.state.scenes.size() ? scene : -1;
        }
        static void drawTriangle (juce::Graphics& g, int x, int y) { juce::Path path; path.addTriangle ((float) x, (float) y, (float) x, (float) y + 10, (float) x + 9, (float) y + 5); g.fillPath (path); }
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

    // Audio clip detail: Clip and Sample boxes (warp, tempo, gain, reverse) beside the waveform.
    // The clip view's Launch box for a Session clip: launch mode, legato and launch grid.
    class LaunchBox final : public juce::Component
    {
    public:
        static constexpr int width = 150;
        explicit LaunchBox (Impl& o) : owner (o)
        {
            setName ("Launch box");
            for (auto* c : std::initializer_list<juce::Component*> { &mode, &legato, &grid }) addAndMakeVisible (c);
            mode.setName ("Launch mode");
            for (auto* text : { "Trigger", "Gate", "Toggle", "Repeat" }) mode.addItem (text, mode.getNumItems() + 1);
            mode.setTooltip ("Launch mode: Trigger starts on press, Gate plays while held, Toggle starts and stops on press, Repeat retriggers on the grid while held");
            grid.setName ("Launch quantization");
            for (auto* text : { "Global", "None", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32" }) grid.addItem (text, grid.getNumItems() + 1);
            grid.setTooltip ("Launch quantization for this clip; Global follows the control bar");
            legato.setName ("Launch legato"); legato.setClickingTogglesState (true);
            legato.setWantsKeyboardFocus (false); legato.setMouseClickGrabsKeyboardFocus (false);
            legato.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a)); legato.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
            legato.setTooltip ("Legato: start in phase with the clip already playing on this track");
            mode.onChange = [this] { apply(); }; grid.onChange = [this] { apply(); }; legato.onClick = [this] { apply(); };
        }
        void setClip (const Session::ClipInfo& shown)
        {
            clip = shown;
            mode.setSelectedId ((int) clip.launch.mode + 1, juce::dontSendNotification);
            grid.setSelectedId ((int) clip.launch.quantisation + 1, juce::dontSendNotification);
            legato.setToggleState (clip.launch.legato, juce::dontSendNotification);
            for (auto* c : std::initializer_list<juce::Component*> { &mode, &legato, &grid }) c->setEnabled (! owner.session.isExporting());
        }
        void resized() override
        {
            auto area = getLocalBounds().reduced (6, 0).withTrimmedTop (28);
            area.removeFromTop (14); mode.setBounds (area.removeFromTop (22));
            area.removeFromTop (8); legato.setBounds (area.removeFromTop (22));
            area.removeFromTop (20); grid.setBounds (area.removeFromTop (22));
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff303030)); g.fillRect (getLocalBounds());
            g.setColour (juce::Colour (0xff4a4a4a)); g.fillRect (getLocalBounds().withHeight (22));
            g.setColour (line); g.drawRect (getLocalBounds());
            g.setColour (ink); g.setFont (font (11.0f, true));
            g.drawText ("Launch", getLocalBounds().withHeight (22).reduced (6, 0), juce::Justification::centredLeft);
            g.setColour (muted); g.setFont (font (10.0f));
            g.drawText ("Mode", 6, mode.getY() - 14, getWidth() - 12, 13, juce::Justification::bottomLeft);
            g.drawText ("Quantization", 6, grid.getY() - 14, getWidth() - 12, 13, juce::Justification::bottomLeft);
        }
    private:
        Impl& owner;
        Session::ClipInfo clip;
        juce::ComboBox mode, grid;
        juce::TextButton legato { "Legato" };
        void apply()
        {
            const Session::LaunchSettings settings { static_cast<Session::LaunchMode> (mode.getSelectedId() - 1),
                                                     static_cast<Session::ClipQuantisation> (grid.getSelectedId() - 1), legato.getToggleState() };
            if (settings == clip.launch) return;
            owner.report (owner.session.setClipLaunchSettings (clip.id, settings)); owner.refresh();
        }
    };

    class AudioClipView final : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit AudioClipView (Impl& o) : owner (o), thumbnail (512, o.formats, o.thumbnailCache)
        {
            setName ("Audio clip view");
            for (auto* c : std::initializer_list<juce::Component*> { &warp, &mode, &bpm, &halve, &twice, &gain, &reverse }) addAndMakeVisible (c);
            addChildComponent (launch);
            for (auto* b : { &warp, &halve, &twice, &reverse }) { b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
            warp.setName ("Clip warp"); warp.setClickingTogglesState (true); warp.setTooltip ("Warp: follow the project tempo (length fixed in beats)");
            warp.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a)); warp.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
            warp.onClick = [this] { apply (warp.getToggleState() ? selectedMode() : Session::WarpMode::off, clip.warp.bpm); };
            mode.setName ("Warp mode"); mode.addItem ("Stretch", 1); mode.addItem ("Efficient", 2);
            mode.setTooltip ("Stretch quality: Stretch (default) or Efficient (lower CPU)");
            mode.onChange = [this] { if (clip.warp.mode != Session::WarpMode::off) apply (selectedMode(), clip.warp.bpm); };
            bpm.setName ("Clip tempo"); bpm.setEditable (false, true, false); bpm.setJustificationType (juce::Justification::centred);
            bpm.setColour (juce::Label::backgroundColourId, background); bpm.setColour (juce::Label::outlineColourId, line);
            bpm.setTooltip ("The source's own tempo (Seg. BPM). Double-click to type it.");
            bpm.onTextChange = [this]
            {
                const auto text = bpm.getText().trim().toStdString(); char* end = nullptr; const auto value = std::strtod (text.c_str(), &end);
                if (! text.empty() && end == text.c_str() + text.size() && std::isfinite (value)) apply (clip.warp.mode, value);
                else sync();
            };
            halve.setTooltip ("Halve the clip tempo (plays twice as long)"); twice.setTooltip ("Double the clip tempo (plays twice as fast)");
            halve.onClick = [this] { apply (clip.warp.mode, clip.warp.bpm / 2); };
            twice.onClick = [this] { apply (clip.warp.mode, clip.warp.bpm * 2); };
            gain.setName ("Clip gain"); gain.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); gain.setRange (-100, 24, .1);
            gain.setSkewFactorFromMidPoint (-12); gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
            gain.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack); gain.setDoubleClickReturnValue (true, 0);
            gain.textFromValueFunction = [] (double v) { return v <= -100 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
            gain.onDragStart = [this] { owner.session.beginEditGesture ("Clip gain"); gesture = true; };
            gain.onDragEnd = [this] { if (gesture) owner.session.endEditGesture(); gesture = false; };
            gain.onValueChange = [this]
            {
                auto settings = clip.audio; settings.gainDb = (float) gain.getValue();
                if (settings == clip.audio) return;
                owner.report (owner.session.setAudioClipSettings (clip.id, settings)); owner.refresh();
            };
            reverse.setName ("Clip reverse"); reverse.setClickingTogglesState (true);
            reverse.onClick = [this] { owner.report (owner.session.setAudioClipReversed (clip.id, reverse.getToggleState())); owner.refresh(); };
            thumbnail.addChangeListener (this);
        }
        ~AudioClipView() override { thumbnail.removeChangeListener (this); if (gesture) owner.session.endEditGesture(); }
        bool sync()
        {
            const auto* selected = owner.selectedAudioClip();
            if (selected == nullptr) return false;
            clip = *selected;
            // Session clips show their Launch box between the Clip and Sample boxes.
            if (launch.isVisible() != owner.showSession) { launch.setVisible (owner.showSession); resized(); }
            if (owner.showSession) launch.setClip (clip);
            if (clip.file != shownFile) { shownFile = clip.file; thumbnail.setSource (clip.file.existsAsFile() ? new juce::FileInputSource (clip.file) : nullptr); }
            const bool warped = clip.warp.mode != Session::WarpMode::off;
            warp.setToggleState (warped, juce::dontSendNotification); warp.setEnabled (clip.canWarp && ! owner.session.isExporting());
            mode.setSelectedId (clip.warp.mode == Session::WarpMode::efficient ? 2 : 1, juce::dontSendNotification); mode.setEnabled (warped);
            if (! bpm.isBeingEdited()) bpm.setText (juce::String (clip.warp.bpm, 2), juce::dontSendNotification);
            for (auto* c : std::initializer_list<juce::Component*> { &bpm, &halve, &twice }) c->setEnabled (clip.canWarp);
            if (! gain.isMouseButtonDown()) gain.setValue (clip.audio.gainDb, juce::dontSendNotification);
            gain.updateText();
            reverse.setToggleState (clip.reversed, juce::dontSendNotification); reverse.setEnabled (clip.canReverse || clip.reversed);
            repaint(); return true;
        }
        void resized() override
        {
            launch.setBounds (clipBox().getRight() + 6, 6, LaunchBox::width, getHeight() - 12);
            auto sample = sampleBox();
            sample.removeFromTop (24);
            auto row = sample.removeFromTop (22).reduced (6, 0);
            warp.setBounds (row.removeFromLeft (52)); row.removeFromLeft (4); mode.setBounds (row);
            sample.removeFromTop (6);
            row = sample.removeFromTop (22).reduced (6, 0);
            twice.setBounds (row.removeFromRight (26)); halve.setBounds (row.removeFromRight (26)); row.removeFromRight (4);
            bpm.setBounds (row.withTrimmedLeft (54));
            sample.removeFromTop (6);
            row = sample.removeFromTop (70).reduced (6, 0);
            gain.setBounds (row.removeFromLeft (64)); row.removeFromLeft (6);
            reverse.setBounds (row.withSizeKeepingCentre (row.getWidth(), 22));
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff262626));
            for (auto box : { clipBox(), sampleBox() })
            {
                g.setColour (juce::Colour (0xff303030)); g.fillRect (box);
                g.setColour (juce::Colour (0xff4a4a4a)); g.fillRect (box.withHeight (22));
                g.setColour (line); g.drawRect (box);
            }
            g.setColour (ink); g.setFont (font (11.0f, true));
            g.drawText ("Clip", clipBox().withHeight (22).reduced (6, 0), juce::Justification::centredLeft);
            g.drawText ("Sample", sampleBox().withHeight (22).reduced (6, 0), juce::Justification::centredLeft);
            auto details = clipBox().withTrimmedTop (28).reduced (8, 0);
            g.setFont (font (11.0f));
            g.setColour (owner.selectedTrackColour()); g.fillRect (details.removeFromTop (20));
            g.setColour (juce::Colour (0xff141414)); g.drawText (clip.name, details.translated (0, -20).withHeight (20).reduced (5, 0), juce::Justification::centredLeft, true);
            g.setColour (muted);
            const auto line1 = "Start  " + juce::String (clip.start, 3) + " s", line2 = "Length  " + juce::String (clip.length, 3) + " s";
            g.drawText (line1, details.removeFromTop (20).withTrimmedTop (4), juce::Justification::centredLeft);
            g.drawText (line2, details.removeFromTop (18), juce::Justification::centredLeft);
            g.drawText (clip.file.getFileName(), details.removeFromTop (18), juce::Justification::centredLeft, true);
            g.drawText ("Seg. BPM", sampleBox().getX() + 6, bpm.getY(), 52, bpm.getHeight(), juce::Justification::centredLeft);
            // Waveform of the visible source span, with beat lines when warped.
            const auto wave = waveBounds();
            g.setColour (juce::Colour (0xff1c1c1c)); g.fillRect (wave);
            const auto sourceSpan = clip.warp.mode == Session::WarpMode::off ? clip.length : clip.length * owner.state.tempo / clip.warp.bpm;
            const auto offset = clip.warp.mode == Session::WarpMode::off ? clip.sourceOffset : clip.sourceOffset * owner.state.tempo / clip.warp.bpm;
            if (thumbnail.getTotalLength() > 0 && sourceSpan > 0)
            {
                g.setColour (owner.selectedTrackColour().withAlpha (0.85f));
                thumbnail.drawChannels (g, wave.reduced (0, 6), offset, offset + sourceSpan, 0.95f);
            }
            if (clip.warp.mode != Session::WarpMode::off && sourceSpan > 0)
            {
                const auto beats = sourceSpan * clip.warp.bpm / 60.0;
                for (int beat = 0; beat <= (int) beats; ++beat)
                {
                    const auto x = wave.getX() + (float) (beat / beats) * wave.getWidth();
                    g.setColour (beat % 4 == 0 ? juce::Colour (0x66ffffff) : juce::Colour (0x22ffffff)); g.drawVerticalLine ((int) x, (float) wave.getY(), (float) wave.getBottom());
                }
            }
            g.setColour (line); g.drawRect (wave);
        }
    private:
        Impl& owner;
        juce::AudioThumbnail thumbnail;
        juce::File shownFile;
        Session::ClipInfo clip;
        juce::TextButton warp { "WARP" }, halve { ":2" }, twice { "*2" }, reverse { "Reverse" };
        juce::ComboBox mode;
        juce::Label bpm;
        juce::Slider gain;
        LaunchBox launch { owner };
        bool gesture = false;
        int launchSpace() const { return launch.isVisible() ? LaunchBox::width + 6 : 0; }
        juce::Rectangle<int> clipBox() const { return { 6, 6, 170, getHeight() - 12 }; }
        juce::Rectangle<int> sampleBox() const { return { 182 + launchSpace(), 6, 200, getHeight() - 12 }; }
        juce::Rectangle<int> waveBounds() const { return { 390 + launchSpace(), 6, juce::jmax (40, getWidth() - 396 - launchSpace()), getHeight() - 12 }; }
        Session::WarpMode selectedMode() const { return mode.getSelectedId() == 2 ? Session::WarpMode::efficient : Session::WarpMode::stretch; }
        void apply (Session::WarpMode warpMode, double tempo)
        {
            owner.report (owner.session.setAudioClipWarp (clip.id, { warpMode, tempo })); owner.refresh(); sync();
        }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    };

    class PianoRoll final : public juce::Component, private juce::ScrollBar::Listener
    {
    public:
        explicit PianoRoll (Impl& o) : owner (o)
        {
            setName ("Piano roll editor");
            setWantsKeyboardFocus (true);
            for (auto* c : std::initializer_list<juce::Component*> { &snap, &noteLength, &velocity, &noteTools, &laneChoice, &laneView, &horizontal, &vertical }) addAndMakeVisible (c);
            laneChoice.setName ("Lane"); laneChoice.setTooltip ("The lane under the notes: velocities or a MIDI controller. Draw points with the mouse; double-click or right-click a point to delete it.");
            laneChoice.onChange = [this]
            {
                const auto id = laneChoice.getSelectedId();
                laneView.setVisible (id != hiddenLane);
                if (id != hiddenLane) laneView.lane = laneForChoice (id);
                resized(); repaint();
            };
            refreshLaneChoices (nullptr);
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
            snap.setEnabled (active); noteLength.setEnabled (active); velocity.setEnabled (active); noteTools.setEnabled (active); laneChoice.setEnabled (active);
            refreshLaneChoices (clip);
            updateScroll(); repaint(); laneView.repaint();
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
            noteTools.setBounds (698, 7, 100, 23); laneChoice.setBounds (842, 7, 116, 23);
            if (laneView.isVisible()) laneView.setBounds (0, getHeight() - scrollSize - laneHeight, getWidth() - scrollSize, laneHeight);
            snap.setBounds (278, 7, 66, 23); noteLength.setBounds (400, 7, 76, 23); velocity.setBounds (544, 5, 142, 27);
            horizontal.setBounds (keysWidth, getHeight() - scrollSize, getWidth() - keysWidth - scrollSize, scrollSize);
            vertical.setBounds (getWidth() - scrollSize, gridTop, scrollSize, gridBounds().getHeight());
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
            g.drawText ("Lane", 810, 0, 30, headerHeight, juce::Justification::centredLeft);
            g.drawText ((selectedNoteIds.empty() ? juce::String() : juce::String ((int) selectedNoteIds.size()) + " selected  |  ")
                        + "Insert " + juce::String (insertionBeat + 1, 2), 966, 0, juce::jmax (0, getWidth() - 980), headerHeight, juce::Justification::centredRight);
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
                g.setColour (black ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff353535));
                g.fillRect (keysWidth, y, grid.getWidth(), rowHeight);
                g.setColour (black ? juce::Colour (0xff232323) : juce::Colour (0xffc6c6c6));
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
                const auto barBeats = owner.state.timeSignature.beatsPerBar();
                const bool bar = std::abs (beat / barBeats - std::round (beat / barBeats)) < 0.001;
                g.setColour (bar ? juce::Colour (0xff6e6e6e) : juce::Colour (0xff434343));
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
                                                    : juce::Colour (0xff5c5c5c);
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
            const auto barBeats = owner.state.timeSignature.beatsPerBar();
            for (double beat = std::floor (viewBeat / barBeats) * barBeats; beat <= viewBeat + grid.getWidth() / pixelsPerBeat; beat += barBeats)
                g.drawText (juce::String (juce::roundToInt (beat / barBeats) + 1), (int) (keysWidth + (beat - viewBeat) * pixelsPerBeat) + 4, headerHeight, 45, rulerHeight, juce::Justification::centredLeft);
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
    // The lane under the notes: note velocities as bars, or one controller lane as steps.
    // Edits preview locally and commit on release as one undo step.
    class LaneView final : public juce::Component
    {
    public:
        explicit LaneView (PianoRoll& r) : roll (r) { setName ("Controller lane"); }
        // -1 shows velocities; otherwise a controller number or Session::pitchBendLane / channelPressureLane.
        int lane = -1;
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff262626));
            g.setColour (line); g.drawHorizontalLine (0, 0.0f, (float) getWidth());
            g.setColour (raised); g.fillRect (0, 1, keysWidth, getHeight() - 1);
            g.setColour (muted); g.setFont (font (9.5f, true));
            g.drawFittedText (laneName (lane).toUpperCase(), juce::Rectangle<int> (4, 4, keysWidth - 6, getHeight() - 8), juce::Justification::topLeft, 3);
            const auto* clip = roll.owner.currentMidiClip();
            if (clip == nullptr) return;
            const auto area = plot();
            g.saveState(); g.reduceClipRegion (area.withTrimmedTop (-4));
            for (double beat = std::floor (roll.viewBeat); beat <= roll.viewBeat + area.getWidth() / roll.pixelsPerBeat; beat += 1)
            {
                g.setColour (std::fmod (beat, 4.0) == 0 ? juce::Colour (0xff4e4e4e) : juce::Colour (0xff353535));
                g.drawVerticalLine ((int) x (beat), (float) area.getY(), (float) area.getBottom());
            }
            if (lane == Session::pitchBendLane) { g.setColour (juce::Colour (0xff3d3d3d)); g.drawHorizontalLine ((int) y (0), (float) area.getX(), (float) area.getRight()); }
            if (lane < 0)
            {
                for (const auto& note : notes())
                {
                    const bool selected = roll.selectedNoteIds.contains (note.id);
                    const auto top = y (juce::roundToInt (note.velocity * 127)), at = x (note.startBeat);
                    g.setColour ((selected ? orange : juce::Colour (0xff8cc4b5)).withAlpha (note.active ? 1.0f : .35f));
                    g.fillRect (at, top, 2.0f, (float) area.getBottom() - top);
                    g.fillEllipse (at - 2.5f, top - 2.5f, 6, 6);
                }
            }
            else
            {
                const auto points = this->points();
                const auto end = x (clip->lengthBeats);
                juce::Path steps;
                for (size_t i = 0; i < points.size(); ++i)
                {
                    const auto from = x (points[i].beat), to = i + 1 < points.size() ? x (points[i + 1].beat) : end;
                    const auto level = y (points[i].value);
                    if (i == 0) steps.startNewSubPath (from, level); else steps.lineTo (from, level);
                    steps.lineTo (to, level);
                }
                g.setColour (orange); g.strokePath (steps, juce::PathStrokeType (1.5f));
                for (size_t i = 0; i < points.size(); ++i)
                {
                    g.setColour ((int) i == dragged ? ink : orange);
                    g.fillRect (x (points[i].beat) - 3, y (points[i].value) - 3, 6.0f, 6.0f);
                }
            }
            g.restoreState();
            g.setColour (background.withAlpha (.7f));
            if (const auto end = x (clip->lengthBeats); end < area.getRight()) g.fillRect (end, (float) area.getY(), area.getRight() - end, (float) area.getHeight());
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto* clip = roll.owner.currentMidiClip();
            dragged = -1; velocityNote.clear(); preview.reset(); previewNotes.clear(); drawing = false;
            if (clip == nullptr || e.x < keysWidth) return;
            clipId = clip->id;
            if (lane < 0)
            {
                // The nearest velocity bar within reach; selected notes follow it together.
                double best = 6;
                for (const auto& note : clip->notes)
                    if (const auto distance = std::abs (x (note.startBeat) - e.position.x); distance < best) { best = distance; velocityNote = note.id; }
                if (velocityNote.isEmpty()) return;
                previewNotes = clip->notes; dragVelocity (e); return;
            }
            auto points = this->points();
            const int hit = pointAt (points, e.position);
            if (hit >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1))
            {
                points.erase (points.begin() + hit); commit (points); return;
            }
            if (e.mods.isPopupMenu()) return;
            preview = points;
            if (hit >= 0) dragged = hit;
            else { drawing = true; paintPoint (e); }
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (lane < 0) { if (velocityNote.isNotEmpty()) dragVelocity (e); return; }
            if (! preview) return;
            if (drawing) { paintPoint (e); repaint(); return; }
            if (dragged < 0) return;
            auto& points = *preview;
            const auto* clip = roll.owner.currentMidiClip();
            const double low = dragged > 0 ? points[(size_t) dragged - 1].beat + 1.0 / 960 : 0;
            const double high = (size_t) dragged + 1 < points.size() ? points[(size_t) dragged + 1].beat - 1.0 / 960 : (clip ? clip->lengthBeats : 0);
            points[(size_t) dragged] = { juce::jlimit (low, juce::jmax (low, high), snapped (beatAt (e.position.x), e.mods)), valueAt (e.position.y) };
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (lane < 0)
            {
                if (velocityNote.isNotEmpty() && ! previewNotes.empty())
                {
                    std::vector<Session::MidiNoteInfo> changed;
                    const bool group = roll.selectedNoteIds.contains (velocityNote);
                    for (const auto& note : previewNotes) if (note.id == velocityNote || (group && roll.selectedNoteIds.contains (note.id))) changed.push_back (note);
                    roll.updateNotes (clipId, changed, "Note velocity");
                }
                velocityNote.clear(); previewNotes.clear(); return;
            }
            if (preview) { auto points = *preview; preview.reset(); dragged = -1; drawing = false; commit (points); }
        }
        static juce::String laneName (int lane)
        {
            if (lane < 0) return "Velocity";
            if (lane == Session::pitchBendLane) return "Pitch Bend";
            if (lane == Session::channelPressureLane) return "Pressure";
            if (lane == 1) return "Mod Wheel";
            if (lane == 7) return "Volume";
            if (lane == 10) return "Pan";
            if (lane == 11) return "Expression";
            if (lane == 64) return "Sustain";
            return "CC " + juce::String (lane);
        }
    private:
        PianoRoll& roll;
        juce::String clipId, velocityNote;
        std::optional<std::vector<Session::ControllerPoint>> preview;
        std::vector<Session::MidiNoteInfo> previewNotes;
        int dragged = -1;
        bool drawing = false;
        juce::Rectangle<int> plot() const { return { keysWidth, 6, juce::jmax (1, getWidth() - keysWidth), juce::jmax (1, getHeight() - 10) }; }
        int low() const { return lane == Session::pitchBendLane ? -8192 : lane < 0 ? 1 : 0; }
        int high() const { return lane == Session::pitchBendLane ? 8191 : 127; }
        float x (double beat) const { return (float) (keysWidth + (beat - roll.viewBeat) * roll.pixelsPerBeat); }
        float y (int value) const { const auto a = plot(); return (float) a.getBottom() - (float) (value - low()) / (float) (high() - low()) * (float) a.getHeight(); }
        double beatAt (float px) const { return roll.viewBeat + (px - keysWidth) / roll.pixelsPerBeat; }
        int valueAt (float py) const { const auto a = plot(); return juce::jlimit (low(), high(), low() + juce::roundToInt ((a.getBottom() - py) / (float) a.getHeight() * (float) (high() - low()))); }
        double snapped (double beat, juce::ModifierKeys mods) const
        {
            const auto* clip = roll.owner.currentMidiClip();
            const auto grid = roll.snapBeats();
            return juce::jlimit (0.0, clip ? clip->lengthBeats : 0.0, mods.isAltDown() ? beat : std::round (beat / grid) * grid);
        }
        std::vector<Session::MidiNoteInfo> notes() const
        {
            if (! previewNotes.empty()) return previewNotes;
            if (const auto* clip = roll.owner.currentMidiClip()) return clip->notes;
            return {};
        }
        std::vector<Session::ControllerPoint> points() const
        {
            if (preview) return *preview;
            if (const auto* clip = roll.owner.currentMidiClip())
                for (const auto& l : clip->controllers) if (l.controller == lane) return l.points;
            return {};
        }
        int pointAt (const std::vector<Session::ControllerPoint>& points, juce::Point<float> p) const
        {
            for (int i = (int) points.size(); --i >= 0;)
                if (std::abs (x (points[(size_t) i].beat) - p.x) <= 5 && std::abs (y (points[(size_t) i].value) - p.y) <= 6) return i;
            return -1;
        }
        // Drawing sets the point on the grid step under the pointer, replacing one already there.
        void paintPoint (const juce::MouseEvent& e)
        {
            auto& points = *preview;
            const auto beat = snapped (beatAt (e.position.x), e.mods);
            const Session::ControllerPoint point { beat, valueAt (e.position.y) };
            const auto at = std::lower_bound (points.begin(), points.end(), beat - 1e-9, [] (const auto& p, double b) { return p.beat < b; });
            if (at != points.end() && std::abs (at->beat - beat) < 1e-9) *at = point; else points.insert (at, point);
        }
        void dragVelocity (const juce::MouseEvent& e)
        {
            const auto value = juce::jlimit (0.01f, 1.0f, (float) valueAt (e.position.y) / 127.0f);
            const bool group = roll.selectedNoteIds.contains (velocityNote);
            for (auto& note : previewNotes) if (note.id == velocityNote || (group && roll.selectedNoteIds.contains (note.id))) note.velocity = value;
            repaint();
        }
        void commit (const std::vector<Session::ControllerPoint>& points)
        {
            roll.owner.report (roll.owner.session.setMidiControllerLane (clipId, lane, points));
            roll.owner.refresh(); repaint();
        }
    };
        LaneView laneView { *this };
        juce::ComboBox snap, noteLength, laneChoice;
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
        juce::Rectangle<int> gridBounds() const
        { return { keysWidth, gridTop, juce::jmax (1, getWidth() - keysWidth - scrollSize), juce::jmax (1, getHeight() - gridTop - scrollSize - (laneView.isVisible() ? laneHeight : 0)) }; }
        static constexpr int laneHeight = 92, hiddenLane = 99;
        // Lane menu: velocity, common controllers, then any other controller the clip uses.
        static constexpr int commonLanes[] { Session::pitchBendLane, 1, 7, 10, 11, 64, Session::channelPressureLane };
        int laneForChoice (int id) const { return id == 1 ? -1 : id >= 2 && id < 2 + (int) std::size (commonLanes) ? commonLanes[id - 2] : id - 100; }
        void refreshLaneChoices (const Session::ClipInfo* clip)
        {
            const auto common = [] (int lane) { return std::find (std::begin (commonLanes), std::end (commonLanes), lane) != std::end (commonLanes); };
            juce::Array<int> others;
            if (clip != nullptr) for (const auto& l : clip->controllers) if (! common (l.controller)) others.add (l.controller);
            if (others == laneOthers && laneChoice.getNumItems() > 0) return;
            laneOthers = others;
            const auto keep = laneChoice.getSelectedId();
            laneChoice.clear (juce::dontSendNotification);
            laneChoice.addItem ("Velocity", 1);
            for (int i = 0; i < (int) std::size (commonLanes); ++i) laneChoice.addItem (LaneView::laneName (commonLanes[i]), 2 + i);
            for (const auto lane : others) laneChoice.addItem (LaneView::laneName (lane), 100 + lane);
            juce::PopupMenu more;
            for (int cc = 0; cc < 120; ++cc) if (! common (cc) && ! others.contains (cc)) more.addItem (100 + cc, "CC " + juce::String (cc));
            laneChoice.getRootMenu()->addSubMenu ("Other controller", more);
            laneChoice.addSeparator(); laneChoice.addItem ("Hide lane", hiddenLane);
            laneChoice.setSelectedId (keep > 0 && laneChoice.indexOfItemId (keep) >= 0 ? keep : 1, juce::dontSendNotification);
        }
        juce::Array<int> laneOthers;
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

    // A Sampler's sample: waveform with draggable start/end markers, file loading and drops.
    class SampleView final : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener
    {
    public:
        explicit SampleView (Impl& o) : owner (o), thumbnail (512, o.formats, o.thumbnailCache)
        {
            setName ("Sampler sample");
            load.setName ("Load sample"); load.setTooltip ("Choose an audio file for this Sampler. You can also drop one here.");
            load.setWantsKeyboardFocus (false); load.setMouseClickGrabsKeyboardFocus (false);
            load.onClick = [this] { choose(); };
            addAndMakeVisible (load);
            thumbnail.addChangeListener (this);
        }
        ~SampleView() override { thumbnail.removeChangeListener (this); if (gesture) owner.session.endEditGesture(); }
        void show (const Session::PluginInfo& info)
        {
            plugin = info;
            if (info.sample != shown) { shown = info.sample; thumbnail.setSource (shown.existsAsFile() ? new juce::FileInputSource (shown) : nullptr); }
            load.setEnabled (! owner.session.isExporting());
            repaint();
        }
        void resized() override { load.setBounds (getWidth() - 62, 3, 58, 18); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff1c1c1c)); g.fillRect (getLocalBounds());
            g.setColour (ink); g.setFont (font (10.5f, true));
            g.drawText (shown == juce::File() ? juce::String ("No sample") : shown.getFileName(), 6, 3, getWidth() - 74, 18, juce::Justification::centredLeft, true);
            const auto wave = waveBounds();
            if (thumbnail.getTotalLength() <= 0)
            {
                g.setColour (muted); g.setFont (font (10.0f));
                g.drawFittedText ("Drop an audio file here", wave, juce::Justification::centred, 2);
                g.setColour (line); g.drawRect (wave); return;
            }
            g.setColour (owner.selectedTrackColour().withAlpha (0.85f));
            thumbnail.drawChannels (g, wave.reduced (0, 4), 0, thumbnail.getTotalLength(), 0.95f);
            const auto& v = plugin.effect.values;
            const auto x = [&] (float fraction) { return wave.getX() + (int) std::lround (fraction * wave.getWidth()); };
            const auto start = std::min (v[instruments::sampler::start], v[instruments::sampler::end]), end = std::max (v[instruments::sampler::start], v[instruments::sampler::end]);
            // Outside the played region is dimmed, as are the markers' handles.
            g.setColour (juce::Colour (0x99000000));
            g.fillRect (wave.withRight (x (start))); g.fillRect (wave.withLeft (x (end)));
            g.setColour (juce::Colour (0xffffab3d));
            for (const auto f : { start, end }) { g.drawVerticalLine (x (f), (float) wave.getY(), (float) wave.getBottom()); g.fillRect (x (f) - (f == start ? 0 : 6), wave.getY(), 6, 6); }
            if (v[instruments::sampler::mode] == 0 && v[instruments::sampler::loop] == 1)
            {
                const auto loopX = x (start + v[instruments::sampler::loopStart] * (end - start));
                g.setColour (juce::Colour (0xff48d17e)); g.drawVerticalLine (loopX, (float) wave.getY(), (float) wave.getBottom());
                g.drawText ("L", loopX + 2, wave.getBottom() - 14, 12, 12, juce::Justification::centredLeft);
            }
            g.setColour (line); g.drawRect (wave);
        }
        // Dragging a marker edits Start or End as one undo step.
        void mouseDown (const juce::MouseEvent& event) override
        {
            dragging = -1;
            if (thumbnail.getTotalLength() <= 0 || ! waveBounds().contains (event.getPosition())) return;
            const auto& v = plugin.effect.values; const auto wave = waveBounds();
            const auto distance = [&] (size_t index) { return std::abs (event.x - (wave.getX() + v[index] * wave.getWidth())); };
            dragging = distance (instruments::sampler::start) <= distance (instruments::sampler::end) ? (int) instruments::sampler::start : (int) instruments::sampler::end;
            owner.session.beginEditGesture ("Sample region"); gesture = true;
            mouseDrag (event);
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (dragging < 0) return;
            const auto wave = waveBounds();
            auto settings = plugin.effect;
            settings.values[(size_t) dragging] = juce::jlimit (0.0f, 1.0f, (float) (event.x - wave.getX()) / (float) juce::jmax (1, wave.getWidth()));
            if (settings == plugin.effect) return;
            owner.report (owner.session.setNativeEffectSettings (plugin.id, settings)); owner.refresh();
        }
        void mouseUp (const juce::MouseEvent&) override { if (gesture) owner.session.endEditGesture(); gesture = false; dragging = -1; }
        bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() == 1 && isAudio (juce::File (files[0])); }
        void filesDropped (const juce::StringArray& files, int, int) override { loadFile (juce::File (files[0])); }
        void loadFile (const juce::File& file)
        {
            if (const auto* track = owner.currentTrack(); track != nullptr && track->isMidi) { owner.report (owner.session.setInstrumentSample (track->id, file)); owner.refresh(); }
        }
        juce::Rectangle<int> waveBounds() const { return getLocalBounds().withTrimmedTop (24).reduced (4, 4); }
    private:
        Impl& owner;
        juce::AudioThumbnail thumbnail;
        juce::File shown;
        Session::PluginInfo plugin;
        juce::TextButton load { "Load..." };
        std::unique_ptr<juce::FileChooser> chooser;
        int dragging = -1;
        bool gesture = false;
        void choose()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load sample", shown.existsAsFile() ? shown.getParentDirectory() : juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            auto safe = juce::Component::SafePointer<SampleView> (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe] (const juce::FileChooser& c) { if (safe != nullptr && c.getResult() != juce::File()) safe->loadFile (c.getResult()); });
        }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    };

    // A Drum Sampler's 4x4 pad grid (pad 1, C1, bottom left) with the selected pad's controls.
    class DrumPanel final : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        static constexpr int padWidth = 56, padHeight = 40, gridWidth = 4 * padWidth, controlsWidth = 300;
        explicit DrumPanel (Impl& o) : owner (o)
        {
            setName ("Drum pads");
            for (int i = 0; i < 4; ++i)
            {
                auto& knob = knobs[(size_t) i]; addAndMakeVisible (knob);
                knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
                knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                knob.onDragStart = [this] { owner.session.beginEditGesture ("Drum pad"); gesture = true; };
                knob.onDragEnd = [this] { if (gesture) owner.session.endEditGesture(); gesture = false; };
                knob.onValueChange = [this, i] { change ((size_t) i, (float) knobs[(size_t) i].getValue()); };
            }
            knobs[0].setName ("Pad tune"); knobs[0].setRange (-48, 48, .01); knobs[0].setDoubleClickReturnValue (true, 0);
            knobs[0].textFromValueFunction = [] (double v) { return juce::String (v, 2) + " st"; };
            knobs[1].setName ("Pad length"); knobs[1].setRange (.01, 1, .001); knobs[1].setDoubleClickReturnValue (true, 1);
            knobs[1].textFromValueFunction = [] (double v) { return juce::String (v * 100, 1) + "%"; };
            knobs[2].setName ("Pad level"); knobs[2].setRange (0, 1, .001); knobs[2].setDoubleClickReturnValue (true, 1);
            knobs[2].textFromValueFunction = [] (double v) { return juce::String (v * 100, 1) + "%"; };
            knobs[3].setName ("Pad pan"); knobs[3].setRange (-1, 1, .01); knobs[3].setDoubleClickReturnValue (true, 0);
            knobs[3].textFromValueFunction = [] (double v) { return std::abs (v) < .005 ? juce::String ("C") : juce::String ((int) std::round (std::abs (v) * 100)) + (v < 0 ? "L" : "R"); };
            for (auto& knob : knobs) knob.updateText();
            choke.setName ("Pad choke"); choke.setTooltip ("Pads in the same choke group silence each other");
            for (auto* text : { "No choke", "Choke 1", "Choke 2", "Choke 3", "Choke 4" }) choke.addItem (text, choke.getNumItems() + 1);
            choke.onChange = [this] { change (instruments::drumSampler::choke, (float) (choke.getSelectedId() - 1)); };
            clear.setName ("Clear pad"); clear.setTooltip ("Remove this pad's sample");
            clear.onClick = [this] { if (const auto* track = owner.currentTrack()) { owner.report (owner.session.setDrumPadSample (track->id, selected, {})); owner.refresh(); } };
            for (auto* c : std::initializer_list<juce::Component*> { &choke, &clear }) addAndMakeVisible (c);
        }
        ~DrumPanel() override { if (gesture) owner.session.endEditGesture(); }
        void show (const Session::PluginInfo& info)
        {
            plugin = info;
            const auto base = (size_t) selected * instruments::drumSampler::padStride;
            for (size_t i = 0; i < knobs.size(); ++i)
                if (! knobs[i].isMouseButtonDown()) knobs[i].setValue (plugin.effect.values[base + i], juce::dontSendNotification);
            for (auto& knob : knobs) knob.updateText();
            choke.setSelectedId (juce::roundToInt (plugin.effect.values[base + instruments::drumSampler::choke]) + 1, juce::dontSendNotification);
            clear.setEnabled (padFile (selected) != juce::File() && ! owner.session.isExporting());
            repaint();
        }
        int selectedPad() const noexcept { return owner.selectedDrumPad; }
        // Pad 1 is bottom left and pads count left to right, bottom to top.
        juce::Rectangle<int> padBounds (int pad) const { return { 4 + (pad % 4) * padWidth, 4 + (3 - pad / 4) * padHeight, padWidth - 3, padHeight - 3 }; }
        void resized() override
        {
            auto area = getLocalBounds().withTrimmedLeft (gridWidth + 16).reduced (0, 4);
            auto name = area.removeFromTop (20); juce::ignoreUnused (name);
            auto row = area.removeFromTop (74);
            for (auto& knob : knobs) knob.setBounds (row.removeFromLeft (66).withTrimmedTop (14));
            area.removeFromTop (6);
            auto buttons = area.removeFromTop (22);
            choke.setBounds (buttons.removeFromLeft (110)); buttons.removeFromLeft (6); clear.setBounds (buttons.removeFromLeft (70));
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff1c1c1c)); g.fillRect (getLocalBounds().withWidth (gridWidth + 8));
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            for (int pad = 0; pad < Session::drumPadCount; ++pad)
            {
                const auto b = padBounds (pad); const auto file = padFile (pad);
                const auto note = instruments::drumSampler::firstNote + pad;
                g.setColour (file != juce::File() ? owner.selectedTrackColour().withMultipliedSaturation (.7f) : juce::Colour (0xff3a3a3a)); g.fillRect (b);
                if (pad == selected) { g.setColour (orange); g.drawRect (b, 2); }
                g.setColour (file != juce::File() ? juce::Colour (0xff141414) : muted); g.setFont (font (9.5f, true));
                g.drawText (juce::String (names[note % 12]) + juce::String (note / 12 - 2), b.reduced (4, 2).removeFromTop (12), juce::Justification::topLeft);
                g.setFont (font (9.0f));
                g.drawFittedText (file != juce::File() ? file.getFileNameWithoutExtension() : juce::String ("Drop"), b.reduced (4, 2).withTrimmedTop (12), juce::Justification::centredLeft, 2);
            }
            const auto file = padFile (selected);
            g.setColour (ink); g.setFont (font (10.5f, true));
            g.drawText ("Pad " + juce::String (selected + 1) + (file != juce::File() ? "  " + file.getFileName() : juce::String ("  (empty)")),
                        gridWidth + 16, 4, getWidth() - gridWidth - 20, 18, juce::Justification::centredLeft, true);
            g.setColour (muted); g.setFont (font (9.5f));
            const char* labels[] { "Tune", "Length", "Level", "Pan" };
            for (size_t i = 0; i < knobs.size(); ++i) g.drawText (labels[i], knobs[i].getX(), knobs[i].getY() - 14, knobs[i].getWidth(), 13, juce::Justification::centred);
        }
        void mouseDown (const juce::MouseEvent& event) override
        { for (int pad = 0; pad < Session::drumPadCount; ++pad) if (padBounds (pad).contains (event.getPosition())) { selected = pad; show (plugin); return; } }
        bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() == 1 && isAudio (juce::File (files[0])); }
        void filesDropped (const juce::StringArray& files, int x, int y) override
        {
            for (int pad = 0; pad < Session::drumPadCount; ++pad)
                if (padBounds (pad).contains (x, y))
                {
                    selected = pad;
                    if (const auto* track = owner.currentTrack()) { owner.report (owner.session.setDrumPadSample (track->id, pad, juce::File (files[0]))); owner.refresh(); }
                    return;
                }
        }
    private:
        Impl& owner;
        Session::PluginInfo plugin;
        int& selected = owner.selectedDrumPad;
        bool gesture = false;
        std::array<juce::Slider, 4> knobs;
        juce::ComboBox choke;
        juce::TextButton clear { "Clear" };
        juce::File padFile (int pad) const { return pad < (int) plugin.padSamples.size() ? plugin.padSamples[(size_t) pad] : juce::File(); }
        void change (size_t control, float value)
        {
            auto settings = plugin.effect;
            settings.values[(size_t) selected * instruments::drumSampler::padStride + control] = value;
            if (settings == plugin.effect) return;
            owner.report (owner.session.setNativeEffectSettings (plugin.id, settings)); owner.refresh();
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
            instrument.setName ("Track instrument"); instrument.setTooltip ("Choose the sound source for this MIDI track. Changing it is one undo step.");
            instrument.addItem ("Halftrack Synth", 1);
            for (int kind = (int) Session::EffectKind::polySynth; kind < Session::effectKindCount; ++kind)
                instrument.addItem (effectName ((Session::EffectKind) kind), 2 + kind - (int) Session::EffectKind::polySynth);
            instrument.onChange = [this]
            {
                const auto* track = owner.currentTrack(); if (track == nullptr || ! track->isMidi) return;
                const int id = instrument.getSelectedId();
                const std::optional<Session::EffectKind> kind = id <= 1 ? std::nullopt
                    : std::optional<Session::EffectKind> ((Session::EffectKind) ((int) Session::EffectKind::polySynth + id - 2));
                owner.report (owner.session.setTrackInstrument (track->id, kind)); owner.refresh();
            };
            addAndMakeVisible (instrument);
            edit.setName ("Edit instrument"); edit.setTooltip ("Open every control of this instrument.");
            edit.onClick = [this] { owner.report (owner.session.showInstrumentEditor (owner.selectedTrack)); };
            addChildComponent (edit);
            addChildComponent (sample);
            addChildComponent (drumPads);
        }
        void sync()
        {
            const auto* track = owner.currentTrack();
            if (track == nullptr || ! track->isMidi) { setVisible (false); return; }
            setVisible (true);
            native = track->instrument.isNativeEffect; nativeSettings = track->instrument.effect;
            // The Drum Sampler's pad grid replaces the generic control layout.
            if (! native || drumSampler()) { nativeEditor.reset(); nativeEditorId.clear(); }
            else if (nativeEditorId != track->instrument.id)
            {
                nativeEditor = makeNativeEffectEditor (owner.session, track->instrument.id, true,
                    [safe = juce::Component::SafePointer<Impl> (&owner), id = track->instrument.id] (int parameter) { if (safe != nullptr) safe->showAutomation (false, id, parameter); });
                addAndMakeVisible (*nativeEditor); nativeEditorId = track->instrument.id;
            }
            instrument.setSelectedId (native ? 2 + (int) nativeSettings.kind - (int) Session::EffectKind::polySynth : 1, juce::dontSendNotification);
            sample.setVisible (sampler());
            if (sampler()) sample.show (track->instrument);
            drumPads.setVisible (drumSampler());
            if (drumSampler()) drumPads.show (track->instrument);
            instrument.setEnabled (! owner.session.isExporting());
            waveform.setVisible (! native); edit.setVisible (native);
            for (auto& control : controls) control.setVisible (! native);
            waveform.setSelectedId (track->synth.waveform + 1, juce::dontSendNotification);
            const float values[] { track->synth.cutoffHz, track->synth.resonance, track->synth.attackSeconds, track->synth.decaySeconds, track->synth.sustain, track->synth.releaseSeconds, track->synth.level, track->synth.pulseWidth * 100 };
            for (int i = 0; i < 8; ++i) controls[(size_t) i].setValue (values[i], juce::dontSendNotification);
            controls[7].setEnabled (track->synth.waveform == 1);
            resized(); repaint(); // Layout depends on built-in versus native mode, not only size.
        }
        int preferredWidth() const
        {
            if (drumSampler()) return DrumPanel::gridWidth + DrumPanel::controlsWidth + 24;
            return native && nativeEditor ? juce::jmax (330, nativeEditor->getWidth() + 4 + sampleWidth()) : 536;
        }
        bool sampler() const { return native && nativeSettings.kind == Session::EffectKind::sampler; }
        bool drumSampler() const { return native && nativeSettings.kind == Session::EffectKind::drumSampler; }
        int sampleWidth() const { return sampler() ? 250 : 0; }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff303030));
            g.setColour (juce::Colour (0xff4a4a4a)); g.fillRect (0, 0, getWidth(), 26);
            g.setColour (ink); g.setFont (font (11.0f, true));
            g.drawText (native ? juce::String (effectName (nativeSettings.kind)) : juce::String ("Halftrack Synth"), 9, 0, 185, 26, juce::Justification::centredLeft);
            if (native) { g.setColour (line); g.drawRect (getLocalBounds()); return; }
            g.setColour (muted); g.setFont (font (9.5f));
            const auto width = (getWidth() - 16) / 8;
            for (int i = 0; i < 8; ++i) g.drawText (names[i], 8 + i * width, 30, width, 14, juce::Justification::centred);
            g.setColour (line); g.drawRect (getLocalBounds());
        }
        void resized() override
        {
            waveform.setBounds (getWidth() - 118, 3, 107, 20);
            instrument.setBounds (getWidth() - (native ? 118 : 262), 3, native ? 107 : 138, 20);
            edit.setBounds (getWidth() - 236, 3, 112, 20);
            sample.setBounds (2, 28, sampleWidth() - 4, juce::jmax (40, getHeight() - 30));
            drumPads.setBounds (2, 28, getWidth() - 4, juce::jmax (40, getHeight() - 30));
            if (nativeEditor) nativeEditor->setTopLeftPosition (2 + sampleWidth(), 28);
            const auto width = (getWidth() - 16) / 8;
            for (int i = 0; i < 8; ++i) controls[(size_t) i].setBounds (8 + i * width, 43, width - 2, getHeight() - 46);
        }
    private:
        Impl& owner;
        juce::ComboBox waveform, instrument;
        juce::TextButton edit { "Edit instrument..." };
        bool native = false;
        Session::EffectSettings nativeSettings;
        std::unique_ptr<juce::Component> nativeEditor;
        juce::String nativeEditorId;
        SampleView sample { owner };
        DrumPanel drumPads { owner };
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

    // One device in the inline chain: a title bar (on/off, name, window, remove)
    // over the device's own compact controls.
    // A device or rack in a device chain.
    class DeviceView : public juce::Component
    {
    public:
        virtual const juce::String& getId() const noexcept = 0;
        virtual int preferredWidth() const = 0;
        virtual void sync (const Session::PluginInfo&) = 0;
    };

    class DevicePanel final : public DeviceView
    {
    public:
        DevicePanel (Impl& o, const Session::PluginInfo& info) : owner (o), pluginId (info.id), native (info.isNativeEffect)
        {
            setWantsKeyboardFocus (true);
            for (auto* button : { &power, &open, &remove })
            { addAndMakeVisible (*button); button->setWantsKeyboardFocus (false); button->setMouseClickGrabsKeyboardFocus (false); }
            power.setClickingTogglesState (true); power.setName ("Device on");
            power.setTooltip ("Turn this device on or off (bypass)");
            power.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a)); power.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
            open.setTooltip ("Open this device in its own window"); remove.setTooltip ("Remove this device");
            power.onClick = [this] { owner.report (owner.session.setDeviceEnabled (pluginId, power.getToggleState())); owner.refresh(); };
            open.onClick = [this] { owner.report (owner.session.showDeviceEditor (pluginId)); };
            remove.onClick = [this] { owner.report (owner.session.removeDevice (pluginId)); owner.refresh(); };
            if (native)
            {
                body = makeNativeEffectEditor (owner.session, info.id, true,
                    [safe = juce::Component::SafePointer<Impl> (&owner), id = info.id] (int parameter) { if (safe != nullptr) safe->showAutomation (false, id, parameter); });
                addAndMakeVisible (*body);
            }
            sync (info);
        }
        const juce::String& getId() const noexcept override { return pluginId; }
        int preferredWidth() const override { return native && body ? body->getWidth() + 2 : 184; }
        void sync (const Session::PluginInfo& info) override
        {
            name = info.name; enabled = info.enabled;
            midi = info.isNativeEffect && effectIsMidi (info.effect.kind);
            power.setToggleState (info.enabled, juce::dontSendNotification);
            repaint();
        }
        void resized() override
        {
            auto title = getLocalBounds().removeFromTop (titleHeight).reduced (2);
            power.setBounds (title.removeFromLeft (18)); remove.setBounds (title.removeFromRight (18)); open.setBounds (title.removeFromRight (22));
            if (body) body->setTopLeftPosition (1, titleHeight);
        }
        void paint (juce::Graphics& g) override
        {
            const bool selected = owner.selectedDevices.contains (pluginId);
            g.fillAll (juce::Colour (0xff303030));
            g.setColour (selected ? juce::Colour (0xff5e5e5e) : enabled ? juce::Colour (0xff4a4a4a) : juce::Colour (0xff383838)); g.fillRect (0, 0, getWidth(), titleHeight);
            g.setColour (enabled ? ink : muted); g.setFont (font (11.0f, true));
            g.drawText (name, 24, 0, getWidth() - 70, titleHeight, juce::Justification::centredLeft, true);
            if (! native)
            {
                g.setColour (muted); g.setFont (font (10.5f));
                g.drawFittedText (enabled ? juce::String (midi ? "Processing MIDI." : "Processing audio.") + "\nOpen its window for every control." : "Bypassed.",
                                  getLocalBounds().withTrimmedTop (titleHeight).reduced (10), juce::Justification::centred, 3);
            }
            if (! enabled) { g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRect (getLocalBounds().withTrimmedTop (titleHeight)); }
            g.setColour (selected ? orange : line); g.drawRect (getLocalBounds());
        }
        // Clicking the title selects the device (Shift extends, Cmd toggles); right-click opens its menu.
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.y > titleHeight) return;
            if (! e.mods.isPopupMenu()) { owner.selectDevice (pluginId, e.mods); grabKeyboardFocus(); return; }
            owner.deviceMenu (pluginId, *this);
        }
        bool keyPressed (const juce::KeyPress& key) override { return owner.deviceKey (key); }
        static constexpr int titleHeight = 22;
    private:
        Impl& owner;
        juce::String pluginId, name;
        bool native = false, enabled = true, midi = false;
        juce::TextButton power { "" }, open { "^" }, remove { "x" };
        std::unique_ptr<juce::Component> body;
    };

    // One chain in a rack's chain list: activator, solo, name, volume and pan.
    class ChainRow final : public juce::Component
    {
    public:
        ChainRow (Impl& o, juce::String rack, juce::String chain) : owner (o), rackId (std::move (rack)), chainId (std::move (chain))
        {
            for (auto* b : { &activator, &solo })
            {
                addAndMakeVisible (*b); b->setClickingTogglesState (true); b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false);
                b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
            }
            activator.setName ("Chain activator"); activator.setTooltip ("Turn this chain on or off");
            activator.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a));
            solo.setName ("Chain solo"); solo.setTooltip ("Solo this chain");
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff6fa8ff));
            activator.onClick = [this] { edit ([this] (auto& s) { s.mute = ! activator.getToggleState(); }); };
            solo.onClick = [this] { edit ([this] (auto& s) { s.solo = solo.getToggleState(); }); };
            for (auto* s : { &volume, &pan })
            {
                addAndMakeVisible (*s); s->setSliderStyle (juce::Slider::LinearBar); s->setWantsKeyboardFocus (false);
                s->setColour (juce::Slider::trackColourId, juce::Colour (0xff59606a)); s->setColour (juce::Slider::textBoxTextColourId, ink);
                s->onDragStart = [this] { owner.session.beginEditGesture ("Edit chain"); dragging = true; };
                s->onDragEnd = [this] { if (dragging) { dragging = false; owner.session.endEditGesture(); } };
            }
            volume.setName ("Chain volume"); volume.setRange (-100, 6, .1); volume.setSkewFactorFromMidPoint (-12); volume.setDoubleClickReturnValue (true, 0);
            volume.textFromValueFunction = [] (double v) { return v <= -100 ? juce::String ("-inf") : juce::String (v, 1); };
            volume.onValueChange = [this] { edit ([this] (auto& s) { s.gainDb = (float) volume.getValue(); }); };
            pan.setName ("Chain pan"); pan.setRange (-1, 1, .02); pan.setDoubleClickReturnValue (true, 0);
            pan.textFromValueFunction = [] (double v) { const int n = juce::roundToInt (std::abs (v) * 50); return n == 0 ? juce::String ("C") : juce::String (n) + (v < 0 ? "L" : "R"); };
            pan.onValueChange = [this] { edit ([this] (auto& s) { s.pan = (float) pan.getValue(); }); };
            volume.updateText(); pan.updateText();
        }
        const juce::String& getId() const noexcept { return chainId; }
        void sync (const Session::RackChainInfo& info, bool isSelected)
        {
            settings = info.settings; selected = isSelected;
            activator.setToggleState (! info.settings.mute, juce::dontSendNotification);
            solo.setToggleState (info.settings.solo, juce::dontSendNotification);
            if (! dragging) { volume.setValue (info.settings.gainDb, juce::dontSendNotification); pan.setValue (info.settings.pan, juce::dontSendNotification); }
            repaint();
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (2, 2);
            activator.setBounds (r.removeFromLeft (16)); r.removeFromLeft (2); solo.setBounds (r.removeFromLeft (16));
            pan.setBounds (r.removeFromRight (36)); r.removeFromRight (2); volume.setBounds (r.removeFromRight (48));
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (selected ? juce::Colour (0xff4f4f4f) : juce::Colour (0xff363636));
            g.setColour (settings.mute ? muted : ink); g.setFont (font (10.5f));
            g.drawText (settings.name, 40, 0, getWidth() - 132, getHeight(), juce::Justification::centredLeft, true);
            g.setColour (line); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.selectedChains[rackId] = chainId; owner.deviceChain.sync();
            if (e.mods.isPopupMenu()) owner.chainMenu (rackId, chainId, *this);
        }
        void mouseDoubleClick (const juce::MouseEvent&) override { owner.renameChainDialog (chainId); }
    private:
        template <typename Change> void edit (Change&& change)
        {
            auto next = settings; change (next);
            owner.report (owner.session.setRackChain (chainId, next));
        }
        Impl& owner;
        juce::String rackId, chainId;
        Session::ChainSettings settings;
        bool selected = false, dragging = false;
        juce::TextButton activator { "" }, solo { "S" };
        juce::Slider volume, pan;
    };

    // An Audio Effect Rack: a title strip, the macro knobs, the chain list and the selected chain's devices.
    class RackPanel final : public DeviceView
    {
    public:
        RackPanel (Impl& o, const Session::PluginInfo& info) : owner (o), rackId (info.id)
        {
            setWantsKeyboardFocus (true);
            for (auto* b : { &power, &remove, &macroToggle, &chainToggle, &addChain, &addDevice })
            { addAndMakeVisible (*b); b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
            power.setClickingTogglesState (true); power.setName ("Rack on"); power.setTooltip ("Turn this rack on or off (bypass)");
            power.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a)); power.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a1a));
            power.onClick = [this] { owner.report (owner.session.setDeviceEnabled (rackId, power.getToggleState())); owner.refresh(); };
            remove.setTooltip ("Remove this rack and its chains");
            remove.onClick = [this] { owner.report (owner.session.removeDevice (rackId)); owner.refresh(); };
            for (auto* t : { &macroToggle, &chainToggle })
            {
                t->setClickingTogglesState (true); t->setToggleState (true, juce::dontSendNotification);
                t->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5b5b5b));
                t->onClick = [this] { owner.deviceChain.sync(); };
            }
            macroToggle.setName ("Show macros"); macroToggle.setTooltip ("Show or hide the macro knobs");
            chainToggle.setName ("Show chains"); chainToggle.setTooltip ("Show or hide the chain list");
            addChain.setName ("Add chain"); addChain.setTooltip ("Add an empty chain");
            addChain.onClick = [this]
            {
                juce::String id; owner.report (owner.session.addRackChain (rackId, id));
                if (id.isNotEmpty()) owner.selectedChains[rackId] = id;
                owner.refresh();
            };
            addDevice.setName ("Add chain device"); addDevice.setTooltip ("Add an audio effect to the selected chain");
            addDevice.onClick = [this] { if (selected.isNotEmpty()) owner.addChainDeviceMenu (selected, addDevice); };
            for (int m = 0; m < Session::rackMacroCount; ++m)
            {
                auto& k = knobs[(size_t) m]; addAndMakeVisible (k); k.setName ("Macro " + juce::String (m + 1));
                k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); k.setRange (0, 1, 0);
                k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 40, 12);
                k.setColour (juce::Slider::rotarySliderFillColourId, macroColour);
                k.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                k.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 127)); };
                k.valueFromTextFunction = [] (const juce::String& t) { return juce::jlimit (0.0, 1.0, t.getDoubleValue() / 127); };
                k.setDoubleClickReturnValue (true, 0); k.updateText();
                k.onDragStart = [this] { owner.session.beginEditGesture ("Macro"); dragging = true; };
                k.onDragEnd = [this] { if (dragging) { dragging = false; owner.session.endEditGesture(); } };
                k.onValueChange = [this, m] { owner.report (owner.session.setRackMacro (rackId, m, (float) knobs[(size_t) m].getValue())); };
                k.addMouseListener (this, false);
                auto& n = names[(size_t) m]; addAndMakeVisible (n); n.setFont (font (9.5f)); n.setJustificationType (juce::Justification::centred);
                n.setColour (juce::Label::textColourId, ink); n.setMinimumHorizontalScale (.7f); n.setInterceptsMouseClicks (false, false);
            }
            sync (info);
        }
        const juce::String& getId() const noexcept override { return rackId; }
        const juce::String& selectedChain() const noexcept { return selected; }
        int preferredWidth() const override
        {
            int width = strip + 2 + (macroToggle.getToggleState() ? macroWidth : 0) + (chainToggle.getToggleState() ? chainWidth : 0);
            for (const auto& d : devices) width += d->preferredWidth() + 4;
            return width + (selected.isNotEmpty() ? addWidth : 0) + 4;
        }
        void sync (const Session::PluginInfo& info) override
        {
            name = info.name; enabled = info.enabled;
            power.setToggleState (info.enabled, juce::dontSendNotification);
            // Keep the chosen chain, falling back to the first.
            auto& choice = owner.selectedChains[rackId];
            const auto found = std::find_if (info.chains.begin(), info.chains.end(), [&] (const auto& c) { return c.id == choice; });
            if (found == info.chains.end()) choice = info.chains.empty() ? juce::String() : info.chains.front().id;
            selected = choice;
            std::vector<juce::String> ids; for (const auto& c : info.chains) ids.push_back (c.id);
            bool same = ids.size() == rows.size();
            for (size_t i = 0; same && i < ids.size(); ++i) same = rows[i]->getId() == ids[i];
            if (! same)
            {
                rows.clear();
                for (const auto& c : info.chains) { rows.push_back (std::make_unique<ChainRow> (owner, rackId, c.id)); addAndMakeVisible (*rows.back()); }
            }
            for (size_t i = 0; i < rows.size(); ++i) rows[i]->sync (info.chains[i], info.chains[i].id == selected);
            const Session::RackChainInfo* chain = nullptr;
            for (const auto& c : info.chains) if (c.id == selected) chain = &c;
            std::vector<juce::String> deviceIds; if (chain) for (const auto& d : chain->devices) deviceIds.push_back (d.id);
            same = deviceIds.size() == devices.size();
            for (size_t i = 0; same && i < deviceIds.size(); ++i) same = devices[i]->getId() == deviceIds[i];
            if (! same)
            {
                devices.clear();
                if (chain) for (const auto& d : chain->devices) { devices.push_back (std::make_unique<DevicePanel> (owner, d)); addAndMakeVisible (*devices.back()); }
            }
            else if (chain) for (size_t i = 0; i < devices.size(); ++i) devices[i]->sync (chain->devices[i]);
            for (int m = 0; m < Session::rackMacroCount && m < (int) info.macros.size(); ++m)
            {
                const auto& macro = info.macros[(size_t) m];
                if (! knobs[(size_t) m].isMouseButtonDown()) knobs[(size_t) m].setValue (macro.value, juce::dontSendNotification);
                names[(size_t) m].setText (macro.name, juce::dontSendNotification);
                juce::StringArray targets;
                for (const auto& mapping : macro.mappings)
                    if (const auto* device = Session::findDevice (owner.state, mapping.pluginId); device && device->isNativeEffect)
                        targets.add (device->name + ": " + effectParameters (device->effect.kind)[(size_t) mapping.parameter].label);
                knobs[(size_t) m].setTooltip (targets.isEmpty() ? macro.name + " (not mapped). Right-click a device control to map it."
                                                                : macro.name + " controls " + targets.joinIntoString (", "));
                mappings[(size_t) m] = macro.mappings;
            }
            addDevice.setVisible (selected.isNotEmpty());
            resized(); repaint();
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto left = r.removeFromLeft (strip);
            power.setBounds (left.removeFromTop (20).reduced (2)); remove.setBounds (left.removeFromBottom (20).reduced (2));
            macroToggle.setBounds (left.removeFromTop (22).reduced (2)); chainToggle.setBounds (left.removeFromTop (22).reduced (2));
            r.removeFromLeft (2);
            const bool showMacros = macroToggle.getToggleState(), showChains = chainToggle.getToggleState();
            auto macroArea = showMacros ? r.removeFromLeft (macroWidth) : juce::Rectangle<int>();
            for (int m = 0; m < Session::rackMacroCount; ++m)
            {
                knobs[(size_t) m].setVisible (showMacros); names[(size_t) m].setVisible (showMacros);
                if (! showMacros) continue;
                auto cell = juce::Rectangle<int> (macroArea.getX() + 4 + (m % 4) * 50, macroArea.getY() + 8 + (m / 4) * 104, 48, 100);
                names[(size_t) m].setBounds (cell.removeFromBottom (16));
                knobs[(size_t) m].setBounds (cell.reduced (2, 8));
            }
            auto chainArea = showChains ? r.removeFromLeft (chainWidth) : juce::Rectangle<int>();
            addChain.setVisible (showChains);
            for (auto& row : rows) row->setVisible (showChains);
            if (showChains)
            {
                auto header = chainArea.removeFromTop (20);
                addChain.setBounds (header.removeFromRight (24).reduced (2));
                auto list = chainArea.reduced (2, 0);
                for (auto& row : rows) row->setBounds (list.removeFromTop (22));
            }
            int x = r.getX() + 2;
            for (auto& d : devices) { d->setBounds (x, 0, d->preferredWidth(), getHeight()); x += d->preferredWidth() + 4; }
            addDevice.setBounds (x, getHeight() / 2 - 40, addWidth - 4, 80);
        }
        void paint (juce::Graphics& g) override
        {
            const bool focused = owner.selectedDevices.contains (rackId);
            g.fillAll (juce::Colour (0xff2b2b2b));
            g.setColour (focused ? juce::Colour (0xff5e5e5e) : enabled ? juce::Colour (0xff4a4a4a) : juce::Colour (0xff383838));
            g.fillRect (0, 0, strip, getHeight());
            g.setColour (enabled ? ink : muted); g.setFont (font (11.0f, true));
            // The rack name reads upward along the title strip.
            g.saveState(); g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 0, 0).translated (0.0f, (float) getHeight() - 22));
            g.drawText (name, 0, 0, getHeight() - 90, strip, juce::Justification::centredLeft, true);
            g.restoreState();
            int x = strip + 2;
            if (macroToggle.getToggleState()) { g.setColour (juce::Colour (0xff333333)); g.fillRect (x, 0, macroWidth, getHeight()); x += macroWidth; }
            if (chainToggle.getToggleState())
            {
                g.setColour (juce::Colour (0xff2f2f2f)); g.fillRect (x, 0, chainWidth, getHeight());
                g.setColour (muted); g.setFont (font (9.5f, true)); g.drawText ("CHAINS", x + 6, 0, chainWidth - 30, 20, juce::Justification::centredLeft);
                if (rows.empty())
                {
                    g.setFont (font (10.5f));
                    g.drawFittedText ("No chains. Audio passes through.\nAdd one with +.", juce::Rectangle<int> (x, 24, chainWidth, 60).reduced (8, 0), juce::Justification::centredTop, 3);
                }
                x += chainWidth;
            }
            if (selected.isNotEmpty() && devices.empty())
            {
                g.setColour (muted); g.setFont (font (10.5f));
                g.drawFittedText ("Drop audio effects here.\nAn empty chain passes its input.", juce::Rectangle<int> (x + addWidth + 4, 0, 150, getHeight()), juce::Justification::centredLeft, 3);
            }
            if (! enabled) { g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRect (getLocalBounds().withTrimmedLeft (strip)); }
            g.setColour (focused ? orange : line); g.drawRect (getLocalBounds());
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int m = 0; m < Session::rackMacroCount; ++m)
                if (e.eventComponent == &knobs[(size_t) m])
                {
                    if (e.mods.isPopupMenu()) owner.macroMenu (rackId, m, mappings[(size_t) m], knobs[(size_t) m]);
                    return;
                }
            if (e.eventComponent != this || e.x > strip) return;
            if (! e.mods.isPopupMenu()) { owner.selectDevice (rackId, e.mods); grabKeyboardFocus(); return; }
            owner.deviceMenu (rackId, *this);
        }
        bool keyPressed (const juce::KeyPress& key) override { return owner.deviceKey (key); }
        static inline const juce::Colour macroColour { 0xff6ccf6c };
    private:
        static constexpr int strip = 24, macroWidth = 208, chainWidth = 232, addWidth = 30;
        Impl& owner;
        juce::String rackId, name, selected;
        bool enabled = true, dragging = false;
        juce::TextButton power { "" }, remove { "x" }, macroToggle { "M" }, chainToggle { "C" }, addChain { "+" }, addDevice { "+" };
        std::array<juce::Slider, Session::rackMacroCount> knobs;
        std::array<juce::Label, Session::rackMacroCount> names;
        std::array<std::vector<Session::MacroMapping>, Session::rackMacroCount> mappings;
        std::vector<std::unique_ptr<ChainRow>> rows;
        std::vector<std::unique_ptr<DevicePanel>> devices;
    };

    class DeviceChain final : public juce::Component
    {
    public:
        explicit DeviceChain (Impl& o) : owner (o), synthPanel (o), sendsPanel (o)
        {
            addAndMakeVisible (addButton); addAndMakeVisible (manageButton); addAndMakeVisible (automationButton); addAndMakeVisible (chainView); addAndMakeVisible (sendsPanel);
            chainView.setViewedComponent (&chain, false); chainView.setScrollBarsShown (false, true); chainView.setScrollBarThickness (8);
            chain.addAndMakeVisible (synthPanel);
            addButton.onClick = [this] { owner.addPluginMenu(); };
            manageButton.onClick = [this] { owner.showPlugins(); };
            automationButton.setName ("Track automation");
            automationButton.setTooltip ("Draw track volume, track pan, or master volume changes across the arrangement.");
            automationButton.onClick = [this] { owner.showAutomation(); };
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
            masterGain.onDragStart = [this]
            {
                const auto current = owner.session.snapshot();
                if (masterProjectGeneration != current.projectGeneration) { owner.refresh(); return; }
                if (! current.masterVolumeAutomation.points.empty() || owner.session.isExporting()) return;
                masterGestureGeneration = current.projectGeneration; owner.session.beginEditGesture ("Master volume");
            };
            masterGain.onDragEnd = [this]
            {
                if (masterGestureGeneration == owner.session.snapshot().projectGeneration) owner.session.endEditGesture();
                masterGestureGeneration.reset();
            };
            masterGain.onValueChange = [this]
            {
                const auto generation = owner.session.snapshot().projectGeneration;
                if (masterProjectGeneration == generation && (! masterGestureGeneration || *masterGestureGeneration == generation))
                    owner.report (owner.session.setMasterGain ((float) masterGain.getValue()));
                owner.refresh();
            };
        }
        void resized() override
        {
            masterGain.setBounds(getWidth()-98,32,49,getHeight()-42);
            manageButton.setBounds (getWidth() - 212, 5, 94, 22);
            addButton.setBounds (getWidth() - 320, 5, 101, 22);
            automationButton.setBounds (getWidth() - 427, 5, 100, 22);
            sendsPanel.setBounds (9, 32, 232, getHeight() - 40);
            chainView.setBounds (chainLeft(), 32, getWidth() - chainLeft() - 114, getHeight() - 36);
            layoutChain();
        }
        void sync()
        {
            const auto* track = owner.currentTrack();
            sendsPanel.sync();
            const bool projectChanged = masterProjectGeneration != owner.state.projectGeneration;
            if (projectChanged) masterGain.hideTextBox (true);
            masterProjectGeneration = owner.state.projectGeneration;
            if (projectChanged || ! masterGain.isMouseButtonDown()) masterGain.setValue (owner.state.masterGainDb, juce::dontSendNotification);
            masterGain.setEnabled (owner.state.masterVolumeAutomation.points.empty() && ! owner.session.isExporting());
            masterGain.setTooltip (owner.state.masterVolumeAutomation.points.empty()
                ? "Master output level. Double-click for 0 dB. The bottom of the fader is silence."
                : "Master volume has automation. Choose Master in Automation and clear its points to edit this fader.");
            automationButton.setEnabled (! owner.session.isExporting());
            addButton.setEnabled (track != nullptr);
            // Rebuild panels only when the device list itself changes.
            std::vector<juce::String> ids;
            if (track != nullptr) for (const auto& p : track->plugins) ids.push_back (p.id);
            bool same = ids.size() == panels.size();
            for (size_t i = 0; same && i < ids.size(); ++i) same = panels[i]->getId() == ids[i];
            if (! same)
            {
                panels.clear();
                if (track != nullptr)
                    for (const auto& p : track->plugins)
                    {
                        if (p.isRack) panels.push_back (std::make_unique<RackPanel> (owner, p)); else panels.push_back (std::make_unique<DevicePanel> (owner, p));
                        chain.addAndMakeVisible (*panels.back());
                    }
            }
            else if (track != nullptr) for (size_t i = 0; i < panels.size(); ++i) panels[i]->sync (track->plugins[i]);
            synthPanel.sync(); layoutChain();
            // A frozen track's devices play from its render and cannot change until it is unfrozen.
            const bool frozen = track != nullptr && track->frozen;
            for (auto* c : std::initializer_list<juce::Component*> { &chain, &synthPanel })
            { c->setEnabled (! frozen); c->setAlpha (frozen ? .45f : 1.0f); }
            addButton.setEnabled (track != nullptr && ! frozen);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff262626));
            g.setColour (line); g.drawHorizontalLine (0, 0.0f, (float) getWidth());
            g.setColour (muted); g.setFont (font (10.0f, true));
            const auto* track = owner.currentTrack();
            auto title = track == nullptr ? juce::String ("DEVICES") : "DEVICES  /  " + track->name.toUpperCase();
            g.drawText (title, 14, 5, juce::jmax (150, getWidth() - 455), 22, juce::Justification::centredLeft);
            if (track == nullptr || (! track->isMidi && track->plugins.empty()))
            {
                const auto hint = chainView.getBounds().reduced (6, 8);
                g.setColour (juce::Colour (0xff3a3a3a)); g.drawRect (hint);
                g.setColour (muted); g.setFont (font (12.0f));
                g.drawText (track == nullptr ? "Select a track to view its devices." : "Drop Audio Effects Here", hint.withTrimmedBottom (24), juce::Justification::centred);
                g.setFont (font (10.5f));
                g.drawText ("Use + Device or the browser. Audio flows left to right.", hint.withTrimmedTop (30), juce::Justification::centred);
            }
            paintMaster (g);
        }
    private:
        Impl& owner;
        juce::TextButton addButton { "+ Device" }, manageButton { "Manage..." }, automationButton { "Automation" };
        SynthPanel synthPanel;
        juce::Slider masterGain;
        std::optional<uint64_t> masterGestureGeneration;
        uint64_t masterProjectGeneration = 0;
        juce::Viewport chainView;
        juce::Component chain;
        std::vector<std::unique_ptr<DeviceView>> panels;
        SendsPanel sendsPanel;
        int chainLeft() const { return sendsPanel.isVisible() ? 250 : 9; }
        // MIDI effects, then the instrument, then audio devices, left to right.
        void layoutChain()
        {
            const auto* track = owner.currentTrack();
            const int height = juce::jmax (1, chainView.getHeight() - 10);
            int x = 4;
            auto place = [&] (juce::Component& c, int width) { c.setBounds (x, 2, width, height); x += width + 4; };
            size_t i = 0;
            if (track != nullptr) for (; i < panels.size() && i < track->plugins.size() && track->plugins[i].isNativeEffect && effectIsMidi (track->plugins[i].effect.kind); ++i)
                place (*panels[i], panels[i]->preferredWidth());
            synthPanel.setVisible (track != nullptr && track->isMidi);
            if (synthPanel.isVisible()) place (synthPanel, synthPanel.preferredWidth());
            for (; i < panels.size(); ++i) place (*panels[i], panels[i]->preferredWidth());
            chain.setSize (juce::jmax (x, chainView.getWidth()), height + 2);
        }
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
    juce::TextButton tapButton { "TAP" }, loopButton { "Loop" }, clickButton { "Metro" }, clickSettingsButton { "..." }, audioButton { "Audio" }, addTrackButton { "+ Audio" }, addMidiButton { "+ MIDI" }, newMidiClipButton { "+ MIDI Clip" },
                     sessionTab { "Session" }, arrangementTab { "Arrange" }, pianoTab { "Clip" }, importButton { "Import Audio" },
                     filesButton { "Files" }, pluginsButton { "Plug-ins" }, folderButton { "Choose Folder..." }, upButton { "Up" },
                     zoomIn { "+" }, zoomOut { "-" }, fitButton { "Fit" };
    juce::Label tempo, meterLabel, positionLabel, projectLabel, loopStartLabel, loopLengthLabel;
    juce::ComboBox quantisation;
    juce::TextButton backToArrangement { "Back to Arrangement" };
    juce::Rectangle<int> meterBounds, cpuBounds;
    juce::TextEditor browserSearch;
    juce::ListBox browserList;
    BrowserCategories browserCategories { *this };
    Canvas canvas;
    Ruler ruler;
    LauncherGrid launcher;
    PianoRoll pianoRoll;
    AudioClipView audioClipView { *this };
    LaunchBox midiLaunch { *this };
    int selectedDrumPad = 0; // The Drum Sampler pad that browser samples load into.
    DeviceChain deviceChain;
    juce::Viewport viewport;
    juce::ScrollBar horizontalScroll { false };
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::Component::SafePointer<juce::DialogWindow>> dialogs;
    std::vector<juce::Component::SafePointer<juce::DocumentWindow>> automationWindows;
    std::vector<juce::Component::SafePointer<juce::AlertWindow>> alerts;
    juce::Rectangle<int> toolbarBounds, browserBounds;
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
    BrowserCategory browserCategory = BrowserCategory::instruments;
    bool pluginBrowser() const noexcept { return browserCategory == BrowserCategory::plugins; }
    bool sampleBrowser() const noexcept { return browserCategory == BrowserCategory::samples; }
    // Built-in device rows: a header names a group; kind -2 is the built-in synth, -3 the Utility.
    struct DeviceRow { juce::String label; bool header = false; int kind = -1; };
    std::vector<DeviceRow> deviceItems;
    bool showPiano = false, showSession = false;
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
    void closeClipView() { showPiano = false; resized(); }
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
        auto* dialog = new juce::AlertWindow ("Clip Follow Action", "After the chosen beats, play action A (at its chance) or else action B. Next and Previous stop at the ends; Cycle wraps; Any picks any clip and Other any other clip. Empty slots are skipped.", juce::MessageBoxIconType::NoIcon, this);
        const juce::StringArray actions { "Stop", "Play again", "Previous clip", "Next clip", "First clip", "Last clip", "Cycle to next clip", "Any clip", "Other clip" };
        juce::StringArray first { "Off" }, second { "No action" }; first.addArray (actions); second.addArray (actions);
        dialog->addComboBox ("action", first, "Action A");
        dialog->getComboBoxComponent ("action")->setSelectedItemIndex ((int) settings.action, juce::dontSendNotification);
        dialog->addTextEditor ("beats", juce::String (settings.afterBeats > 0 ? settings.afterBeats : 4, 6).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), "After beats");
        dialog->addComboBox ("action-b", second, "Action B");
        dialog->getComboBoxComponent ("action-b")->setSelectedItemIndex ((int) settings.actionB, juce::dontSendNotification);
        dialog->addTextEditor ("chance", juce::String (settings.chanceA * 100, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), "Chance of A (%)");
        for (const char* name : { "beats", "chance" }) dialog->getTextEditor (name)->setEnabled (settings.action != Session::FollowAction::off);
        dialog->getComboBoxComponent ("action-b")->setEnabled (settings.action != Session::FollowAction::off);
        dialog->addButton ("Apply", 1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        dialog->setLookAndFeel (&theme); alerts.emplace_back (dialog);
        auto safe = juce::Component::SafePointer<Impl> (this);
        auto safeDialog = juce::Component::SafePointer<juce::AlertWindow> (dialog);
        dialog->getComboBoxComponent ("action")->onChange = [safeDialog]
        {
            if (! safeDialog) return;
            const bool on = safeDialog->getComboBoxComponent ("action")->getSelectedItemIndex() > 0;
            for (const char* name : { "beats", "chance" }) safeDialog->getTextEditor (name)->setEnabled (on);
            safeDialog->getComboBoxComponent ("action-b")->setEnabled (on);
        };
        dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, safeDialog, id] (int result)
        {
            if (! safe || ! safeDialog || result != 1) return;
            const auto action = static_cast<Session::FollowAction> (safeDialog->getComboBoxComponent ("action")->getSelectedItemIndex());
            Session::FollowSettings next { action, 0 };
            if (action != Session::FollowAction::off)
            {
                const auto number = [] (const juce::String& field, double& value)
                {
                    const auto text = field.trim().toStdString(); char* end = nullptr; value = std::strtod (text.c_str(), &end);
                    return ! text.empty() && end == text.c_str() + text.size() && std::isfinite (value);
                };
                double chance = 100;
                if (! number (safeDialog->getTextEditorContents ("beats"), next.afterBeats))
                { safe->report (juce::Result::fail ("Enter a complete numeric beat duration.")); return; }
                if (! number (safeDialog->getTextEditorContents ("chance"), chance) || chance < 0 || chance > 100)
                { safe->report (juce::Result::fail ("Enter a chance for action A from 0 to 100%.")); return; }
                next.actionB = static_cast<Session::FollowAction> (safeDialog->getComboBoxComponent ("action-b")->getSelectedItemIndex());
                next.chanceA = chance / 100;
            }
            safe->report (safe->session.setClipFollowAction (id, next)); safe->refresh();
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
            browserFolder = audioFiles.front().getParentDirectory(); browserCategory = BrowserCategory::samples; updateBrowser(); resized(); launcher.scrollSelectionIntoView();
        }
    }

    const Session::TrackInfo* currentTrack() const
    {
        return selectedTrack >= 0 && selectedTrack < (int) state.tracks.size() ? &state.tracks[(size_t) selectedTrack] : nullptr;
    }
    // The audio clip an open clip view edits: the selected arrangement clip or Session slot.
    const Session::ClipInfo* selectedAudioClip() const
    {
        const auto* track = currentTrack();
        if (track == nullptr) return nullptr;
        if (showSession) { const auto* slot = selectedSlot(); return slot != nullptr && slot->hasClip && slot->clip.isAudio ? &slot->clip : nullptr; }
        if (selectedClip < 0 || selectedClip >= (int) track->clips.size()) return nullptr;
        return track->clips[(size_t) selectedClip].isAudio ? &track->clips[(size_t) selectedClip] : nullptr;
    }
    juce::Colour selectedTrackColour() const { const auto* track = currentTrack(); return track != nullptr ? track->colour : muted; }
    // The Session MIDI clip an open clip view edits, which has launch settings.
    const Session::ClipInfo* sessionMidiClip() const { return showSession && selectedLauncherClipId.isNotEmpty() ? currentMidiClip() : nullptr; }
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
        const auto barBeats = state.timeSignature.beatsPerBar();
        const auto startBeat = std::floor (seconds * state.tempo / 60.0 / barBeats + 1e-9) * barBeats;
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
        return beat * state.timeSignature.beatsPerBar();
    }
    // Freeze toggles the track's freeze; Flatten replaces a frozen track with its render.
    void freezeTrackFromUI (const juce::String& trackId, bool flatten)
    {
        const auto index = launcherIndices (trackId, {}).first;
        if (index < 0) return;
        const bool frozen = state.tracks[(size_t) index].frozen;
        report (flatten ? session.flattenTrack (trackId) : session.setTrackFrozen (trackId, ! frozen)); refresh();
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
        const auto* info = indices.first >= 0 ? &state.tracks[(size_t) indices.first] : nullptr;
        menu.addSeparator(); menu.addItem (3, "Duplicate track", info != nullptr && ! info->isReturn && ! info->frozen);
        menu.addSeparator();
        menu.addItem (4, info != nullptr && info->frozen ? "Unfreeze Track" : "Freeze Track", info != nullptr && ! info->isReturn && (info->frozen || ! info->clips.empty()));
        menu.addItem (5, "Flatten", info != nullptr && info->frozen);
        menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<Impl> (this), id] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1 || result == 2) safe->reorderTrack (id, result == 1 ? -1 : 1);
            else if (result == 3) safe->duplicateTrackFromUI (id);
            else if (result == 4 || result == 5) safe->freezeTrackFromUI (id, result == 5);
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
    // Bars.beats.sixteenths, counting beats in the meter's note value (eighths in 6/8). Positions count
    // from 1; lengths count from 0. Beats here are quarter notes.
    static juce::String formatMusicalTime (double beats, bool position, Session::TimeSignature meter = {})
    {
        const auto sixteenths = juce::jmax (0LL, (long long) std::floor (beats * 4 + 1.0e-6));
        const long long perBeat = juce::jmax (1, 16 / meter.denominator), perBar = (long long) meter.numerator * 16 / juce::jmin (16, meter.denominator);
        const auto bars = sixteenths / perBar, beat = (sixteenths % perBar) / perBeat, sixteenth = sixteenths % perBeat;
        const int offset = position ? 1 : 0;
        return juce::String (bars + offset) + ". " + juce::String (beat + offset) + ". " + juce::String (sixteenth + offset);
    }
    static std::optional<double> parseMusicalTime (const juce::String& text, bool position, Session::TimeSignature meter = {})
    {
        auto fields = juce::StringArray::fromTokens (text.trim(), ".", "");
        fields.trim(); fields.removeEmptyStrings();
        if (fields.isEmpty() || fields.size() > 3) return std::nullopt;
        std::array<int, 3> value { position ? 1 : 0, position ? 1 : 0, position ? 1 : 0 };
        for (int i = 0; i < fields.size(); ++i)
        {
            if (! fields[i].containsOnly ("0123456789")) return std::nullopt;
            value[(size_t) i] = fields[i].getIntValue();
        }
        const int offset = position ? 1 : 0, perBeat = juce::jmax (1, 16 / meter.denominator);
        if (value[0] < offset || value[1] < offset || value[2] < offset || value[1] - offset >= meter.numerator || value[2] - offset >= perBeat) return std::nullopt;
        return (value[0] - offset) * meter.beatsPerBar() + (value[1] - offset) * 4.0 / meter.denominator + (value[2] - offset) / 4.0;
    }
    void refresh()
    {
        const auto previous = state;
        const auto selectedId = currentTrack() != nullptr ? currentTrack()->id : juce::String();
        state = session.snapshot();
        buildLanes();
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
        quantisation.setSelectedId (state.launchQuantisation == Session::LaunchQuantisation::none ? 1 : state.launchQuantisation == Session::LaunchQuantisation::beat ? 2 : 3, juce::dontSendNotification);
        {
            const bool overridden = std::any_of (state.tracks.begin(), state.tracks.end(), [] (const auto& track) { return track.launcherActive; });
            backToArrangement.setEnabled (overridden); backToArrangement.setToggleState (overridden, juce::dontSendNotification);
        }
        if (! tempo.isBeingEdited()) tempo.setText (juce::String (state.tempo, 2), juce::dontSendNotification);
        if (! meterLabel.isBeingEdited()) meterLabel.setText (juce::String (state.timeSignature.numerator) + " / " + juce::String (state.timeSignature.denominator), juce::dontSendNotification);
        const auto beatPosition = juce::jmax (0.0, state.position) * state.tempo / 60.0;
        positionLabel.setText (state.countInBeatsRemaining>0
            ? "Count in " + juce::String((int)std::ceil(state.countInBeatsRemaining))
            : formatMusicalTime (beatPosition, true, state.timeSignature), juce::dontSendNotification);
        if (! loopStartLabel.isBeingEdited()) loopStartLabel.setText (formatMusicalTime (state.loopStart * state.tempo / 60.0, true, state.timeSignature), juce::dontSendNotification);
        if (! loopLengthLabel.isBeingEdited()) loopLengthLabel.setText (formatMusicalTime ((state.loopEnd - state.loopStart) * state.tempo / 60.0, false, state.timeSignature), juce::dontSendNotification);
        projectLabel.setText (state.name + (state.dirty ? "  *" : ""), juce::dontSendNotification);
        if (state.tracks.size() != previous.tracks.size() || lanesHeight != canvas.getHeight() && lanesHeight > viewport.getHeight()) resized();
        for (int i = timeSelection.trackIds.size(); --i >= 0;) if (trackIndex (timeSelection.trackIds[i]) < 0) timeSelection.trackIds.remove (i);
        for (auto it = multiSelectedTracks.begin(); it != multiSelectedTracks.end();)
            it = std::any_of (state.tracks.begin(), state.tracks.end(), [&] (const auto& t) { return t.id == *it; }) ? std::next (it) : multiSelectedTracks.erase (it);
        canvas.syncRows();
        if (selectedSceneIndex() < 0) selectedSceneId = state.scenes.empty() ? juce::String() : state.scenes.front().id;
        launcher.sync();
        pianoRoll.sync();
        if (showPiano && (audioClipView.isVisible() != (selectedAudioClip() != nullptr) || midiLaunch.isVisible() != (selectedAudioClip() == nullptr && sessionMidiClip() != nullptr))) resized();
        if (audioClipView.isVisible()) audioClipView.sync();
        if (midiLaunch.isVisible()) if (const auto* clip = sessionMidiClip()) midiLaunch.setClip (*clip);
        newMidiClipButton.setEnabled (currentTrack() != nullptr && currentTrack()->isMidi && ! currentTrack()->isReturn);
        importButton.setEnabled (currentTrack() == nullptr || ! currentTrack()->isReturn);
        if (! selectedDevices.isEmpty() && deviceSiblings (selectedDevices[0]).empty()) selectedDevices.clear();
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
        viewport.setViewPosition (0, juce::jmax (0, laneY (selectedTrack) - viewport.getHeight() + trackHeight));
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

    juce::StringArray getMenuBarNames() override { return { "File", "Edit", "Create", "View", "Session" }; }
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
            // Time edits use the time selection, or the loop brace when nothing is selected.
            popup.addSeparator();
            const bool selection = timeSelection.active() && ! showSession && ! state.recording;
            popup.addItem (76, "Split", selection); popup.addItem (77, "Duplicate Selection", selection);
            popup.addItem (78, "Delete Selection", selection); popup.addItem (79, "Loop Selection", selection);
            popup.addItem (80, "Consolidate", selection);
            popup.addSeparator();
            const bool range = (selection || state.loopEnd > state.loopStart) && ! state.recording;
            const juce::String source = selection ? "" : " (Loop Range)";
            popup.addItem (74, "Insert Silence" + source, range); popup.addItem (75, "Delete Time" + source, range);
        }
        else if (index == 2)
        {
            popup.addItem (30, "Insert Audio Track"); popup.addItem (33, "Insert MIDI Track"); popup.addItem (35, "Insert Return Track"); popup.addItem (34, "Insert MIDI Clip", currentTrack() != nullptr && currentTrack()->isMidi); popup.addItem (31, "Delete Selected Track", currentTrack() != nullptr);
            popup.addSeparator();
            popup.addItem (38, "Duplicate Selected Track", currentTrack() != nullptr && !currentTrack()->isReturn);
            popup.addItem (36, "Move Selected Track Earlier", selectedTrack > 0);
            popup.addItem (37, "Move Selected Track Later", selectedTrack >= 0 && selectedTrack+1 < (int)state.tracks.size());
            popup.addSeparator();
            popup.addItem (72, "Group Tracks", currentTrack() != nullptr && ! currentTrack()->isReturn && currentTrack()->groupId.isEmpty());
            popup.addItem (73, "Ungroup Tracks", currentTrack() != nullptr && currentTrack()->groupId.isNotEmpty());
            popup.addSeparator(); popup.addItem (32, "Add Plug-in...", currentTrack() != nullptr);
        }
        else if (index == 3)
        {
            popup.addItem (45, "Session"); popup.addItem (43, "Arrangement"); popup.addItem (44, "Piano Roll"); popup.addItem (46, "Automation..."); popup.addItem (47, "Master Volume Automation..."); popup.addSeparator();
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
            case 74: { const auto [a, b] = editRange(); report (session.insertTime (a, b - a)); refresh(); break; }
            case 75: { const auto [a, b] = editRange(); report (session.deleteTime (a, b - a)); timeSelection = {}; refresh(); break; }
            case 76: case 77: case 78: case 79: case 80: selectionCommand (id); break;
            case 30: showPiano = false; addTrack(); resized(); break;
            case 33: addMidiTrack(); break;
            case 35: addReturnTrack(); break;
            case 34: if (showSession) createLauncherMidiClip(); else createMidiClip (state.position); break;
            case 31: if (selectedTrack >= 0) { session.closePluginEditors(); report (session.removeTrack (selectedTrack)); selectedClip = -1; } break;
            case 32: addPluginMenu(); break;
            case 36: case 37: if (const auto* track = currentTrack()) reorderTrack (track->id, id == 36 ? -1 : 1); break;
            case 38: if (const auto* track = currentTrack()) duplicateTrackFromUI (track->id); break;
            case 72: groupSelectedTracks(); break;
            case 73: ungroupSelectedTrack(); break;
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
            case 46: showAutomation(); break;
            case 47: showAutomation (true); break;
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
    class AudioExportDialog final : public juce::AlertWindow
    {
    public:
        AudioExportDialog (juce::Component& parent, Theme& look, juce::Range<double> loop)
            : AlertWindow ("Export Audio", {}, juce::MessageBoxIconType::NoIcon, &parent), capturedLoop (loop)
        {
            setLookAndFeel (&look);
            addComboBox ("range", { "Entire arrangement", "Loop range", "Custom range" }, {});
            addComboBox ("rendered", { "Master", "All Individual Tracks" }, {});
            addTextEditor ("range-start", juce::String (loop.getStart(), 6), {});
            addTextEditor ("range-end", juce::String (loop.getEnd(), 6), {});
            customStart = getTextEditorContents ("range-start"); customEnd = getTextEditorContents ("range-end");
            addComboBox ("format", { "WAV", "AIFF", "FLAC (lossless)" }, {});
            addComboBox ("channels", { "Stereo", "Mono (combine both channels)" }, {});
            addComboBox ("normalization", { "Off", "Peak ceiling: -1 dB", "Peak ceiling: -3 dB", "Peak ceiling: 0 dB" }, {});
            addComboBox ("sample-rate", { "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz", "192 kHz" }, {});
            getComboBoxComponent ("sample-rate")->setSelectedItemIndex (1, juce::dontSendNotification);
            addComboBox ("bit-depth", { "16-bit", "24-bit", "32-bit" }, {});
            getComboBoxComponent ("bit-depth")->setSelectedItemIndex (1, juce::dontSendNotification);
            addComboBox ("effect-tail", { "0 seconds (none)", "1 second", "2 seconds", "5 seconds", "10 seconds", "30 seconds", "60 seconds" }, {});
            getComboBoxComponent ("effect-tail")->setSelectedItemIndex (2, juce::dontSendNotification);
            addButton ("Choose Destination...", 1, juce::KeyPress (juce::KeyPress::returnKey));
            addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            error.setName ("Export validation"); error.setColour (juce::Label::textColourId, orange);
            error.setJustificationType (juce::Justification::centredLeft); addAndMakeVisible (error);
            const juce::StringArray order { "range", "rendered", "range-start", "range-end", "format", "channels", "sample-rate", "bit-depth", "normalization", "effect-tail" };
            const juce::StringArray titles { "Export range", "Rendered track", "Start in seconds", "End in seconds", "File format", "Output channels", "Sample rate", "Bit depth", "Normalize exported peak", "Extra reverb and delay tail" };
            getComboBoxComponent ("rendered")->setDescription ("Export the Master mix, or every track, group and return to its own file in a folder.");
            for (int i = 0; i < order.size(); ++i)
                if (auto* control = controlNamed (order[i])) { control->setExplicitFocusOrder (i + 1); control->setTitle (titles[i]); }
            getComboBoxComponent ("range")->setDescription ("Export the entire arrangement, captured loop endpoints, or a custom time interval.");
            getTextEditor ("range-end")->setDescription ("End time, up to 172800 seconds. Selected ranges add no extra tail.");
            getButton (0)->setExplicitFocusOrder (11); getButton (1)->setExplicitFocusOrder (12);
            getComboBoxComponent ("range")->onChange = [this] { updateRangeControls(); };
            getComboBoxComponent ("format")->onChange = [this]
            {
                const bool canUse32 = getComboBoxComponent ("format")->getSelectedItemIndex() == 0;
                auto* depth = getComboBoxComponent ("bit-depth"); depth->setItemEnabled (3, canUse32);
                if (! canUse32 && depth->getSelectedId() == 3) depth->setSelectedId (2, juce::dontSendNotification);
            };
            updateRangeControls (true);
            centreAroundComponent (&parent, 640, 550); layoutControls();
        }
        ~AudioExportDialog() override { setLookAndFeel (nullptr); }
        void resized() override { layoutControls(); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (panel); g.setColour (line); g.drawRect (getLocalBounds());
            g.setColour (orange); g.fillRect (20, 20, 4, 19);
            g.setColour (ink); g.setFont (font (18, true)); g.drawText ("EXPORT AUDIO", 36, 15, getWidth() - 56, 30, juce::Justification::centredLeft);
            g.setColour (muted); g.setFont (font (12));
            g.drawFittedText ("Render the arrangement through its track and master effects.\nExport settings leave the project mix unchanged.", 20, 51, getWidth() - 40, 36, juce::Justification::topLeft, 2);
            auto label = [&] (const juce::String& name, const juce::String& text)
            {
                if (auto* component = controlNamed (name))
                    g.drawText (text, component->getX(), component->getY() - 20, component->getWidth(), 18, juce::Justification::centredLeft);
            };
            label ("range", "Export range"); label ("rendered", "Rendered track"); label ("range-start", "Start (seconds)"); label ("range-end", "End (seconds)");
            label ("format", "File format"); label ("channels", "Output channels"); label ("sample-rate", "Sample rate");
            label ("bit-depth", "Bit depth"); label ("normalization", "Normalize exported peak"); label ("effect-tail", "Extra reverb / delay tail");
            const auto range = getComboBoxComponent ("range")->getSelectedItemIndex();
            g.drawFittedText (range == 0 ? "Extra tail time is added after the arrangement ends."
                : range == 1 ? "Uses the loop endpoints captured when this window opened. No extra tail is added."
                             : "Only the chosen interval is exported. No extra tail is added.",
                20, 222, getWidth() - 40, 32, juce::Justification::topLeft, 2);
        }
        juce::Component* controlNamed (const juce::String& name)
        {
            if (auto* combo = getComboBoxComponent (name)) return combo;
            return getTextEditor (name);
        }
        void layoutControls()
        {
            const auto width = getWidth(); const int half = (width - 60) / 2;
            auto place = [&] (const char* name, int column, int y, bool full = false)
            { if (auto* control = controlNamed (name)) control->setBounds (20 + column * (half + 20), y, full ? width - 40 : half, 28); };
            place ("range", 0, 120); place ("rendered", 1, 120); place ("range-start", 0, 182); place ("range-end", 1, 182);
            place ("format", 0, 278); place ("channels", 1, 278);
            place ("sample-rate", 0, 338); place ("bit-depth", 1, 338);
            place ("normalization", 0, 398); place ("effect-tail", 1, 398);
            error.setBounds (20, 438, width - 40, 49);
            if (getNumButtons() >= 2)
            {
                getButton (0)->setBounds (width - 286, 502, 172, 28); getButton (1)->setBounds (width - 102, 502, 82, 28);
            }
        }
        void updateRangeControls (bool initialise = false)
        {
            const auto range = getComboBoxComponent ("range")->getSelectedItemIndex();
            // AlertWindow queues its initial selection notification before our
            // callback is assigned. An unchanged selection must preserve errors.
            if (! initialise && range == previousRange) return;
            auto* tail = getComboBoxComponent ("effect-tail");
            if (previousRange == 0 && range != 0) entireTail = tail->getSelectedItemIndex();
            if (range != 0) tail->setSelectedItemIndex (0, juce::dontSendNotification);
            else if (previousRange != 0) tail->setSelectedItemIndex (entireTail, juce::dontSendNotification);
            if (previousRange == 2)
            { customStart = getTextEditorContents ("range-start"); customEnd = getTextEditorContents ("range-end"); }
            getTextEditor ("range-start")->setTextToShowWhenEmpty (range == 0 ? "Beginning" : "Start in seconds", muted);
            getTextEditor ("range-end")->setTextToShowWhenEmpty (range == 0 ? "Last clip end" : "End in seconds", muted);
            if (range == 0)
            { getTextEditor ("range-start")->setText ({}, false); getTextEditor ("range-end")->setText ({}, false); }
            else if (range == 1)
            {
                getTextEditor ("range-start")->setText (juce::String (capturedLoop.getStart(), 6), false);
                getTextEditor ("range-end")->setText (juce::String (capturedLoop.getEnd(), 6), false);
            }
            else if (range == 2)
            {
                getTextEditor ("range-start")->setText (customStart, false); getTextEditor ("range-end")->setText (customEnd, false);
            }
            getTextEditor ("range-start")->setEnabled (range == 2); getTextEditor ("range-end")->setEnabled (range == 2);
            getComboBoxComponent ("effect-tail")->setEnabled (range == 0);
            previousRange = range; error.setText ({}, juce::dontSendNotification); repaint();
        }
        const juce::Range<double> capturedLoop;
        juce::Label error;
        std::optional<Session::AudioExportSettings> accepted;
    private:
        juce::String customStart, customEnd;
        int previousRange = 0, entireTail = 2;
    };
    static juce::Result readAudioExportSettings (AudioExportDialog& dialog, Session::AudioExportSettings& settings)
    {
        constexpr double rates[] { 44100, 48000, 88200, 96000, 192000 };
        constexpr int depths[] { 16, 24, 32 };
        constexpr double tails[] { 0, 1, 2, 5, 10, 30, 60 };
        settings = {};
        settings.format = static_cast<Session::ExportFormat> (juce::jlimit (0, 2, dialog.getComboBoxComponent ("format")->getSelectedItemIndex()));
        settings.sampleRate = rates[juce::jlimit (0, 4, dialog.getComboBoxComponent ("sample-rate")->getSelectedItemIndex())];
        settings.bitDepth = depths[juce::jlimit (0, 2, dialog.getComboBoxComponent ("bit-depth")->getSelectedItemIndex())];
        settings.tailSeconds = tails[juce::jlimit (0, 6, dialog.getComboBoxComponent ("effect-tail")->getSelectedItemIndex())];
        settings.mono = dialog.getComboBoxComponent ("channels")->getSelectedItemIndex() == 1;
        const auto normalization = dialog.getComboBoxComponent ("normalization")->getSelectedItemIndex();
        settings.normalizePeak = normalization > 0;
        settings.peakCeilingDb = normalization == 2 ? -3.0f : normalization == 3 ? 0.0f : -1.0f;
        settings.stems = dialog.getComboBoxComponent ("rendered")->getSelectedItemIndex() == 1;
        const auto mode = dialog.getComboBoxComponent ("range")->getSelectedItemIndex();
        if (mode == 0) return juce::Result::ok();
        auto start = dialog.capturedLoop.getStart(), end = dialog.capturedLoop.getEnd();
        if (mode == 2)
        {
            auto number = [] (juce::String text, double& result)
            {
                const auto value = text.trim().toStdString(); char* endOfNumber = nullptr;
                result = std::strtod (value.c_str(), &endOfNumber);
                return ! value.empty() && endOfNumber == value.c_str() + value.size() && std::isfinite (result);
            };
            if (! number (dialog.getTextEditorContents ("range-start"), start) || ! number (dialog.getTextEditorContents ("range-end"), end))
                return juce::Result::fail ("Enter complete numeric start and end times in seconds.");
        }
        if (! std::isfinite (start) || ! std::isfinite (end) || start < 0 || end <= start || end > Session::maximumExportRangeSeconds)
            return juce::Result::fail ("Use 0 ≤ start < end ≤ 172800 seconds (48 hours).");
        if (std::llround (end * settings.sampleRate) <= std::llround (start * settings.sampleRate))
            return juce::Result::fail ("Choose a range containing at least one frame at the selected sample rate.");
        settings.timeRange = juce::Range<double> (start, end); settings.tailSeconds = 0;
        return juce::Result::ok();
    }
    bool exportProjectMatches (uint64_t generation)
    {
        if (session.snapshot().projectGeneration == generation) return true;
        status = "Project changed. Reopen Export Audio to export the current project.";
        statusFrames = 24 * 10; repaint(); return false;
    }
    void exportDialog()
    {
        if (session.isExporting()) return;
        const auto current = session.snapshot(); const auto generation = current.projectGeneration;
        auto* alert = new AudioExportDialog (*this, theme, { current.loopStart, current.loopEnd });
        alerts.emplace_back (alert);
        auto safe = juce::Component::SafePointer<Impl> (this);
        auto dialog = juce::Component::SafePointer<AudioExportDialog> (alert);
        // Replace the native button's exit callback so invalid input never closes
        // the settings window or advances to a destination chooser. Return uses
        // this same button callback through AlertWindow::keyPressed.
        alert->getButton (0)->onClick = [safe, dialog, generation]
        {
            if (safe == nullptr || dialog == nullptr) return;
            if (! safe->exportProjectMatches (generation))
            { dialog->error.setText ("Project changed. Cancel and reopen Export Audio.", juce::dontSendNotification); return; }
            Session::AudioExportSettings settings;
            const auto result = readAudioExportSettings (*dialog, settings);
            if (result.failed()) { dialog->error.setText (result.getErrorMessage(), juce::dontSendNotification); return; }
            dialog->accepted = settings; dialog->exitModalState (1);
        };
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, dialog, generation] (int result)
        {
            if (result != 1 || safe == nullptr || dialog == nullptr || ! dialog->accepted) return;
            const auto settings = *dialog->accepted;
            juce::MessageManager::callAsync ([safe, settings, generation]
            {
                if (safe != nullptr && safe->exportProjectMatches (generation)) safe->chooseExportDestination (settings, generation);
            });
        }), true);
    }
    void performAudioExport (const juce::File& destination, const Session::AudioExportSettings& settings, uint64_t generation)
    {
        if (! exportProjectMatches (generation)) return;
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
    // Renders every track into a folder, asking before replacing earlier stems there.
    void performStemExport (const juce::File& folder, const Session::AudioExportSettings& settings, uint64_t generation, bool overwrite = false)
    {
        if (! exportProjectMatches (generation)) return;
        juce::StringArray files;
        const auto result = session.exportStems (folder, settings, files, overwrite);
        if (result.failed() && ! overwrite && result.getErrorMessage().contains ("already exists"))
        {
            auto* alert = new juce::AlertWindow ("Replace Existing Stems?", result.getErrorMessage().upToFirstOccurrenceOf (" Choose", false, false)
                                                 + " Replace every stem with the same name in " + folder.getFileName() + "?", juce::MessageBoxIconType::WarningIcon, this);
            alert->setLookAndFeel (&theme);
            alert->addButton ("Replace", 1);
            alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            alerts.emplace_back (alert);
            auto safe = juce::Component::SafePointer<Impl> (this);
            alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, folder, settings, generation] (int choice)
            { if (choice == 1 && safe != nullptr) safe->performStemExport (folder, settings, generation, true); }), true);
            return;
        }
        if (result.failed() && result.getErrorMessage() == "Export cancelled.") { status = "Export cancelled."; statusFrames = 24 * 5; }
        else report (result, juce::String (files.size()) + " stems exported to " + folder.getFileName() + ".");
        refresh();
    }
    void exportAudioSelection (const juce::File& selected, Session::AudioExportSettings settings, uint64_t generation)
    {
        if (selected == juce::File() || ! exportProjectMatches (generation)) return;
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
            alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, destination, settings, generation] (int result)
            {
                if (result == 1 && safe != nullptr) safe->performAudioExport (destination, settings, generation);
            }), true);
            return;
        }
        performAudioExport (destination, settings, generation);
    }
    void chooseExportDestination (Session::AudioExportSettings settings, uint64_t generation)
    {
        if (! exportProjectMatches (generation)) return;
       #if HALFTRACK_TESTS_ENABLED
        if (testExportDestination) { testExportDestination (settings, generation); return; }
       #endif
        if (settings.stems)
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a Folder for the Stems", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
            auto safe = juce::Component::SafePointer<Impl> (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [safe, settings, generation] (const juce::FileChooser& fc)
                { if (safe != nullptr && fc.getResult() != juce::File()) safe->performStemExport (fc.getResult(), settings, generation); });
            return;
        }
        const auto extension = exportExtension (settings.format);
        chooser = std::make_unique<juce::FileChooser> ("Export Arrangement as " + extension.toUpperCase(), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (session.snapshot().name + "." + extension), "*." + extension);
        auto safe = juce::Component::SafePointer<Impl> (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, settings, generation] (const juce::FileChooser& fc)
            {
                if (safe != nullptr) safe->exportAudioSelection (fc.getResult(), settings, generation);
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
    // Opens the automation window for the track, Master, or one native device control.
    void showAutomation (bool master = false, const juce::String& pluginId = {}, int parameter = -1)
    {
        if (session.isExporting()) return;
        refresh();
        const auto* track = currentTrack();
        // A modeless window keeps the arrangement transport available while editing.
        class Window final : public juce::DocumentWindow
        {
        public:
            Window (std::unique_ptr<AutomationEditor> editor, juce::LookAndFeel& theme, juce::Component& parent)
                : DocumentWindow ("Halftrack - Automation", panel, juce::DocumentWindow::closeButton)
            {
                setLookAndFeel (&theme);
                setUsingNativeTitleBar (true);
                setResizable (true, false);
                setResizeLimits (820, 420, 2400, 1600);
                setContentOwned (editor.release(), true);
                centreAroundComponent (&parent, 900, 460);
                setVisible (true);
            }
            ~Window() override { clearContentComponent(); setLookAndFeel (nullptr); }
            void closeButtonPressed() override { delete this; }
        };
        std::erase_if (automationWindows, [] (const auto& window) { return window == nullptr; });
        auto editor = master || track == nullptr ? std::make_unique<AutomationEditor> (session)
                    : pluginId.isNotEmpty() ? std::make_unique<AutomationEditor> (session, track->id, pluginId, parameter)
                                            : std::make_unique<AutomationEditor> (session, track->id);
        automationWindows.emplace_back (new Window (std::move (editor), theme, *this));
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
    // Devices and racks. Selection lives in one device list: the selected track's or a chain's.
    juce::StringArray selectedDevices;
    std::map<juce::String, juce::String> selectedChains; // Rack ID to the chain whose devices it shows.
    std::vector<juce::String> deviceSiblings (const juce::String& id) const
    {
        if (const auto* track = currentTrack())
        {
            std::vector<juce::String> top; for (const auto& p : track->plugins) top.push_back (p.id);
            if (std::find (top.begin(), top.end(), id) != top.end()) return top;
            for (const auto& p : track->plugins)
                for (const auto& c : p.chains)
                {
                    std::vector<juce::String> ids; for (const auto& d : c.devices) ids.push_back (d.id);
                    if (std::find (ids.begin(), ids.end(), id) != ids.end()) return ids;
                }
        }
        return {};
    }
    void selectDevice (const juce::String& id, juce::ModifierKeys mods)
    {
        const auto siblings = deviceSiblings (id);
        const bool sameList = ! selectedDevices.isEmpty() && std::find (siblings.begin(), siblings.end(), selectedDevices[0]) != siblings.end();
        if (mods.isShiftDown() && sameList)
        {
            const auto anchor = std::find (siblings.begin(), siblings.end(), selectedDevices[0]) - siblings.begin();
            const auto target = std::find (siblings.begin(), siblings.end(), id) - siblings.begin();
            const auto first = selectedDevices[0];
            selectedDevices.clear(); selectedDevices.add (first);
            for (auto i = std::min (anchor, target); i <= std::max (anchor, target); ++i) selectedDevices.addIfNotAlreadyThere (siblings[(size_t) i]);
        }
        else if (mods.isCommandDown() && sameList) { if (selectedDevices.contains (id)) selectedDevices.removeString (id); else selectedDevices.add (id); }
        else { selectedDevices.clear(); selectedDevices.add (id); }
        deviceChain.repaint();
    }
    // Selected devices in their list order.
    juce::StringArray orderedSelection() const
    {
        juce::StringArray ordered;
        if (selectedDevices.isEmpty()) return ordered;
        for (const auto& id : deviceSiblings (selectedDevices[0])) if (selectedDevices.contains (id)) ordered.add (id);
        return ordered;
    }
    void groupSelectedDevices()
    {
        const auto ids = orderedSelection();
        if (ids.isEmpty() || selectedTrack < 0) return;
        juce::String rack; report (session.groupDevices (selectedTrack, ids, rack));
        if (rack.isNotEmpty()) { selectedDevices.clear(); selectedDevices.add (rack); }
        refresh();
    }
    void ungroupSelectedRack()
    {
        for (const auto& id : orderedSelection())
            if (const auto* device = Session::findDevice (state, id); device && device->isRack)
            { report (session.ungroupRack (id)); selectedDevices.clear(); refresh(); return; }
    }
    void deleteSelectedDevices()
    {
        const auto ids = orderedSelection();
        for (const auto& id : ids) report (session.removeDevice (id));
        selectedDevices.clear(); refresh();
    }
    bool deviceKey (const juce::KeyPress& key)
    {
        if (session.isExporting()) return true;
        const auto code = key.getKeyCode();
        const auto cmd = key.getModifiers().isCommandDown(), shift = key.getModifiers().isShiftDown();
        if (cmd && code == 'G') { if (shift) ungroupSelectedRack(); else groupSelectedDevices(); return true; }
        if (! cmd && (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)) { deleteSelectedDevices(); return true; }
        return false;
    }
    void deviceMenu (const juce::String& id, juce::Component& target)
    {
        const auto siblings = deviceSiblings (id);
        const auto* device = Session::findDevice (state, id);
        if (device == nullptr) return;
        const int index = (int) (std::find (siblings.begin(), siblings.end(), id) - siblings.begin());
        if (! selectedDevices.contains (id)) { selectedDevices.clear(); selectedDevices.add (id); deviceChain.repaint(); }
        const bool topLevel = currentTrack() != nullptr && std::any_of (currentTrack()->plugins.begin(), currentTrack()->plugins.end(), [&] (const auto& p) { return p.id == id; });
        const bool midi = device->isNativeEffect && effectIsMidi (device->effect.kind);
        juce::PopupMenu menu;
        menu.addItem (1, "Move left", index > 0); menu.addItem (2, "Move right", index + 1 < (int) siblings.size());
        menu.addSeparator();
        menu.addItem (5, "Group", topLevel && ! midi && ! device->isRack);
        if (device->isRack) menu.addItem (6, "Ungroup", device->chains.size() <= 1);
        if (device->isRack) menu.addItem (7, "Add chain");
        menu.addSeparator(); menu.addItem (3, device->enabled ? "Deactivate" : "Activate"); menu.addItem (4, "Delete");
        juce::Component::SafePointer<Impl> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, id] (int choice)
        {
            if (! safe || choice == 0) return;
            const auto now = safe->deviceSiblings (id);
            const int at = (int) (std::find (now.begin(), now.end(), id) - now.begin());
            const auto* current = Session::findDevice (safe->state, id);
            if (current == nullptr) return;
            if (choice <= 2) safe->report (safe->session.moveDevice (id, at + (choice == 1 ? -1 : 1)));
            else if (choice == 3) safe->report (safe->session.setDeviceEnabled (id, ! current->enabled));
            else if (choice == 4) { safe->report (safe->session.removeDevice (id)); safe->selectedDevices.clear(); }
            else if (choice == 5) safe->groupSelectedDevices();
            else if (choice == 6) safe->ungroupSelectedRack();
            else if (choice == 7) { juce::String chain; safe->report (safe->session.addRackChain (id, chain)); if (chain.isNotEmpty()) safe->selectedChains[id] = chain; }
            safe->refresh();
        });
    }
    void chainMenu (const juce::String& rackId, const juce::String& chainId, juce::Component& target)
    {
        juce::PopupMenu menu; menu.addItem (1, "Rename"); menu.addItem (2, "Delete");
        juce::Component::SafePointer<Impl> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, rackId, chainId] (int choice)
        {
            if (! safe) return;
            if (choice == 1) safe->renameChainDialog (chainId);
            if (choice == 2) { safe->report (safe->session.removeRackChain (chainId)); safe->selectedChains.erase (rackId); safe->refresh(); }
        });
    }
    const Session::RackChainInfo* findChain (const juce::String& chainId) const
    {
        for (const auto& track : state.tracks) for (const auto& p : track.plugins) for (const auto& c : p.chains) if (c.id == chainId) return &c;
        return nullptr;
    }
    // Asks for a name and applies it; the dialog closes on Cancel or Escape.
    void nameDialog (const juce::String& title, const juce::String& initial, std::function<juce::Result (const juce::String&)> apply)
    {
        if (session.isExporting()) return;
        auto* dialog = new juce::AlertWindow (title, "Enter a name.", juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor ("name", initial, "Name");
        dialog->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey)); dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        dialog->setLookAndFeel (&theme); alerts.emplace_back (dialog);
        auto safe = juce::Component::SafePointer<Impl> (this); auto safeDialog = juce::Component::SafePointer<juce::AlertWindow> (dialog);
        dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, safeDialog, apply] (int result)
        {
            if (! safe || ! safeDialog || result != 1) return;
            safe->report (apply (safeDialog->getTextEditorContents ("name"))); safe->refresh();
        }), true);
    }
    void renameChainDialog (const juce::String& chainId)
    {
        const auto* chain = findChain (chainId);
        if (chain == nullptr) return;
        nameDialog ("Rename chain", chain->settings.name, [this, chainId] (const juce::String& text)
        {
            const auto* current = findChain (chainId);
            if (current == nullptr) return juce::Result::fail ("Chain not found.");
            auto settings = current->settings; settings.name = text;
            return session.setRackChain (chainId, settings);
        });
    }
    void macroMenu (const juce::String& rackId, int macro, const std::vector<Session::MacroMapping>& mappings, juce::Component& target)
    {
        juce::PopupMenu menu; menu.addItem (1, "Rename");
        if (! mappings.empty()) menu.addSeparator();
        for (size_t i = 0; i < mappings.size(); ++i)
            if (const auto* device = Session::findDevice (state, mappings[i].pluginId); device && device->isNativeEffect)
                menu.addItem (100 + (int) i, "Remove mapping: " + device->name + " " + effectParameters (device->effect.kind)[(size_t) mappings[i].parameter].label);
        juce::Component::SafePointer<Impl> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, rackId, macro, mappings] (int choice)
        {
            if (! safe) return;
            if (choice == 1)
            {
                const auto* rack = Session::findDevice (safe->state, rackId);
                if (rack == nullptr || ! juce::isPositiveAndBelow (macro, (int) rack->macros.size())) return;
                safe->nameDialog ("Rename macro", rack->macros[(size_t) macro].name, [safe, rackId, macro] (const juce::String& text)
                { return safe ? safe->session.renameRackMacro (rackId, macro, text) : juce::Result::ok(); });
            }
            else if (choice >= 100 && choice - 100 < (int) mappings.size())
            { safe->report (safe->session.unmapRackMacro (mappings[(size_t) (choice - 100)].pluginId, mappings[(size_t) (choice - 100)].parameter)); safe->refresh(); }
        });
    }
    // Audio effects for a chain: the Utility and native audio effects.
    void addChainDeviceMenu (const juce::String& chainId, juce::Component& target)
    {
        juce::PopupMenu popup; popup.addItem (1, "Utility");
        juce::StringArray categories;
        for (int i = 0; i < Session::effectKindCount; ++i)
            if (const auto kind = static_cast<Session::EffectKind> (i); ! effectIsMidi (kind) && ! effectIsInstrument (kind)) categories.addIfNotAlreadyThere (effectCategory (kind));
        for (const auto& category : categories)
        {
            juce::PopupMenu group;
            for (int i = 0; i < Session::effectKindCount; ++i)
                if (const auto kind = static_cast<Session::EffectKind> (i); ! effectIsMidi (kind) && ! effectIsInstrument (kind) && category == effectCategory (kind))
                    group.addItem (100 + i, effectName (kind));
            popup.addSubMenu (category, group);
        }
        juce::Component::SafePointer<Impl> safe (this);
        popup.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, chainId] (int choice)
        {
            if (! safe || choice == 0) return;
            if (choice == 1) safe->report (safe->session.addUtilityToChain (chainId));
            else safe->report (safe->session.addNativeEffectToChain (chainId, static_cast<Session::EffectKind> (choice - 100)));
            safe->refresh();
        });
    }
    void addPluginMenu()
    {
        if (selectedTrack < 0 || selectedTrack >= static_cast<int> (state.tracks.size())) return;
        auto types = session.knownPlugins().getTypes();
        juce::PopupMenu popup;
        popup.addItem (100001, "Utility  (Built-in)");
        popup.addItem (100099, "Audio Effect Rack  (Built-in)");
        juce::StringArray categories;
        const bool midiTrack = state.tracks[static_cast<size_t> (selectedTrack)].isMidi;
        for (int i = 0; i < Session::effectKindCount; ++i)
            if (midiTrack || ! effectIsMidi (static_cast<Session::EffectKind> (i))) categories.addIfNotAlreadyThere (effectCategory (static_cast<Session::EffectKind> (i)));
        for (const auto& category : categories)
        {
            juce::PopupMenu group;
            for (int i = 0; i < Session::effectKindCount; ++i)
                if (category == effectCategory (static_cast<Session::EffectKind> (i))) group.addItem (100002+i, effectName (static_cast<Session::EffectKind> (i)));
            popup.addSubMenu (category + "  (Built-in)", group);
        }
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
        const bool native = (choice >= 100001 && choice < 100002 + Session::effectKindCount) || choice == 100099;
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
        else if (choice == 100099) { juce::String rack; report (session.groupDevices (track, {}, rack)); refresh(); }
        else if (choice >= 100002 && choice < 100002 + Session::effectKindCount)
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
    void setBrowserCategory (BrowserCategory category)
    {
        browserCategory = category; browserList.deselectAllRows(); updateBrowser(); resized();
    }
    void updateBrowser()
    {
        browserItems.clear(); pluginItems.clear(); deviceItems.clear();
        const auto query = browserSearch.getText().trim();
        folderButton.setButtonText (pluginBrowser() ? "Manage / Scan..." : "Choose Folder...");
        upButton.setEnabled (browserFolder != juce::File() && browserFolder.getParentDirectory() != browserFolder);
        browserSearch.setTextToShowWhenEmpty ("Search (" + juce::String (browserCategory == BrowserCategory::instruments ? "Instruments"
            : browserCategory == BrowserCategory::audioEffects ? "Audio Effects" : browserCategory == BrowserCategory::midiEffects ? "MIDI Effects"
            : pluginBrowser() ? "Plug-ins" : "Samples") + ")", muted);
        if (browserFolder != scannedBrowserFolder) startBrowserScan();
        const auto matches = [&query] (const juce::String& label) { return query.isEmpty() || label.containsIgnoreCase (query); };
        if (pluginBrowser())
        {
            for (const auto& type : session.knownPlugins().getTypes()) if (matches (type.name)) pluginItems.push_back (type);
            std::sort (pluginItems.begin(), pluginItems.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        }
        else if (sampleBrowser())
        {
            for (const auto& item : allBrowserItems) if (matches (item.file.getFileName())) browserItems.push_back (item);
        }
        else if (browserCategory == BrowserCategory::instruments)
        {
            if (matches ("Halftrack Synth")) deviceItems.push_back ({ "Halftrack Synth", false, -2 });
            for (int kind = (int) Session::EffectKind::polySynth; kind < Session::effectKindCount; ++kind)
                if (matches (effectName ((Session::EffectKind) kind))) deviceItems.push_back ({ effectName ((Session::EffectKind) kind), false, kind });
        }
        else
        {
            const bool midi = browserCategory == BrowserCategory::midiEffects;
            juce::StringArray groups;
            for (int kind = 0; kind < Session::effectKindCount; ++kind)
                if (effectIsMidi ((Session::EffectKind) kind) == midi && ! effectIsInstrument ((Session::EffectKind) kind)) groups.addIfNotAlreadyThere (effectCategory ((Session::EffectKind) kind));
            if (! midi) groups.add ("Utilities");
            for (const auto& group : groups)
            {
                std::vector<DeviceRow> rows;
                for (int kind = 0; kind < Session::effectKindCount; ++kind)
                    if (group == effectCategory ((Session::EffectKind) kind) && matches (effectName ((Session::EffectKind) kind))) rows.push_back ({ effectName ((Session::EffectKind) kind), false, kind });
                if (group == "Utilities" && matches ("Utility")) rows.push_back ({ "Utility", false, -3 });
                if (rows.empty()) continue;
                if (! midi) deviceItems.push_back ({ group, true, -1 });
                deviceItems.insert (deviceItems.end(), rows.begin(), rows.end());
            }
        }
        browserList.updateContent(); browserList.repaint(); browserCategories.repaint(); repaint();
    }
    int getNumRows() override
    { return pluginBrowser() ? (int) pluginItems.size() : sampleBrowser() ? (int) browserItems.size() : (int) deviceItems.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        if (! pluginBrowser() && ! sampleBrowser())
        {
            const auto& item = deviceItems[(size_t) row];
            if (item.header)
            {
                g.setColour (muted); g.setFont (font (10.0f, true));
                juce::Path open; open.addTriangle (6.0f, (float) height / 2 - 2, 12.0f, (float) height / 2 - 2, 9.0f, (float) height / 2 + 2); g.fillPath (open);
                g.drawText (item.label.toUpperCase(), 17, 0, width - 20, height, juce::Justification::centredLeft, true);
                return;
            }
            if (selected) { g.setColour (raised); g.fillRect (0, 0, width, height); }
            const int indent = browserCategory == BrowserCategory::audioEffects ? 14 : 4;
            g.setColour (selected ? orange : muted); g.drawRect (indent + 3, height / 2 - 5, 10, 10); g.fillRect (indent + 5, height / 2 - 3, 6, 2);
            g.setColour (selected ? ink : juce::Colour (0xffc6c6c6)); g.setFont (font (11.5f));
            g.drawText (item.label, indent + 19, 0, width - indent - 22, height, juce::Justification::centredLeft, true);
            return;
        }
        if (selected) { g.setColour (raised); g.fillRect (0, 0, width, height); }
        g.setColour (selected ? orange : muted);
        g.setFont (font (11.5f));
        auto label = pluginBrowser() ? pluginItems[(size_t) row].name : browserItems[(size_t) row].file.getFileName();
        const bool folder = ! pluginBrowser() && browserItems[(size_t) row].directory;
        if (folder)
        {
            g.drawRect (6, height / 2 - 4, 11, 9); g.fillRect (6, height / 2 - 6, 5, 2);
        }
        else if (pluginBrowser())
        {
            g.drawRect (7, height / 2 - 5, 9, 11); g.drawHorizontalLine (height / 2 - 2, 5.0f, 18.0f); g.drawHorizontalLine (height / 2 + 3, 5.0f, 18.0f);
        }
        else
        {
            for (int i = 0; i < 5; ++i) { const int h = i % 2 == 0 ? 6 : 12; g.drawVerticalLine (6 + i * 2, (float) (height - h) / 2, (float) (height + h) / 2); }
        }
        g.setColour (selected ? ink : juce::Colour (0xffc6c6c6));
        g.drawText (label, 25, 0, width - 29, height, juce::Justification::centredLeft, true);
    }
    // Double-click or Return inserts the item, creating a MIDI track when one is required.
    void activateBrowserRow (int row)
    {
        if (row < 0 || row >= getNumRows()) return;
        if (pluginBrowser())
        {
            if (selectedTrack < 0) addTrack();
            report (session.addPlugin (selectedTrack, pluginItems[(size_t) row])); refresh();
        }
        else if (sampleBrowser())
        {
            auto item = browserItems[(size_t) row];
            if (item.directory) { browserFolder = item.file; browserSearch.clear(); updateBrowser(); }
            // A sample chosen for a MIDI track goes into its Drum Sampler's selected pad, or its Sampler.
            else if (const auto* track = currentTrack(); track != nullptr && track->isMidi)
            {
                if (track->instrument.isNativeEffect && track->instrument.effect.kind == Session::EffectKind::drumSampler)
                    report (session.setDrumPadSample (track->id, selectedDrumPad, item.file));
                else report (session.setInstrumentSample (track->id, item.file));
                refresh();
            }
            else if (showSession) importLauncherFiles ({ item.file.getFullPathName() }, selectedTrack, selectedSceneIndex());
            else importFiles ({ item.file.getFullPathName() }, selectedTrack, state.position);
        }
        else
        {
            const auto item = deviceItems[(size_t) row]; if (item.header) return;
            const bool needsMidi = item.kind == -2 || (item.kind >= 0 && (effectIsInstrument ((Session::EffectKind) item.kind) || effectIsMidi ((Session::EffectKind) item.kind)));
            const auto* track = currentTrack();
            if (needsMidi && (track == nullptr || ! track->isMidi))
            {
                if (item.kind == -2 || effectIsInstrument ((Session::EffectKind) item.kind) || track == nullptr || ! track->isMidi) { addMidiTrack(); refresh(); }
                track = currentTrack(); if (track == nullptr || ! track->isMidi) return;
            }
            if (track == nullptr) { addTrack(); refresh(); track = currentTrack(); if (track == nullptr) return; }
            if (item.kind == -2) report (session.setTrackInstrument (track->id, std::nullopt));
            else if (item.kind == -3) report (session.addUtility (selectedTrack));
            else if (effectIsInstrument ((Session::EffectKind) item.kind)) report (session.setTrackInstrument (track->id, (Session::EffectKind) item.kind));
            else report (session.addNativeEffect (selectedTrack, (Session::EffectKind) item.kind));
            refresh();
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
        const auto index = inside ? trackAtY (p.y) : -1;
        const auto time = inside ? juce::jmax (0.0, viewStart + p.x / pixelsPerSecond) : state.position;
        importFiles (files, index, std::round (time / gridSeconds()) * gridSeconds());
    }
};


#if HALFTRACK_TESTS_ENABLED
void MainComponent::Impl::runExportInteractionChecks (const juce::File& audioFixture)
{
    auto check = [] (bool condition, const char* message)
    { if (! condition) throw std::runtime_error (message); std::cout << "PASS export UI " << message << std::endl; };
    auto waitFor = [] (const std::function<bool()>& completed)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
        while (! completed() && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        return completed();
    };
    auto drainQueuedCallbacks = [&]
    {
        // exitModalState queues the manager callback first. That callback posts
        // destination selection. A second queued marker follows both stages.
        auto reached = std::make_shared<bool> (false);
        juce::MessageManager::callAsync ([reached]
        { juce::MessageManager::callAsync ([reached] { *reached = true; }); });
        check (waitFor ([reached] { return *reached; }), "queued modal and destination callbacks reach the completion barrier");
    };
    check (session.newProject().wasOk() && session.importAudio (audioFixture, 0).wasOk(), "prepare real audio export UI fixture"); refresh();
    session.setLoop (false, .01, .09); refresh();
    int destinationRequests = 0;
    std::optional<Session::AudioExportSettings> capturedSettings;
    uint64_t capturedGeneration = 0;
    testExportDestination = [&] (const Session::AudioExportSettings& settings, uint64_t generation)
    { ++destinationRequests; capturedSettings = settings; capturedGeneration = generation; };
    auto open = [&]
    {
        exportDialog(); auto* window = dynamic_cast<AudioExportDialog*> (alerts.back().getComponent());
        check (window != nullptr, "production export options open");
        return juce::Component::SafePointer<AudioExportDialog> (window);
    };
    auto cancel = [&] (juce::Component::SafePointer<AudioExportDialog> window)
    { if (window) window->exitModalState (0); drainQueuedCallbacks(); };
    auto dialog = open();
    Session::AudioExportSettings settings;
    check (readAudioExportSettings (*dialog, settings).wasOk() && ! settings.timeRange && settings.tailSeconds == 2
        && ! settings.mono && ! settings.normalizePeak, "entire arrangement preserves prior stereo and tail defaults");
    check (dialog->getTextEditorContents ("range-start").isEmpty() && dialog->getTextEditorContents ("range-end").isEmpty()
        && dialog->getComboBoxComponent ("range")->getTitle() == "Export range",
        "entire export does not mislabel loop endpoints and keeps accessible control titles");
    check (dialog->getWidth() == 640 && dialog->getHeight() == 550, "compact export dialog fits a laptop display");
    for (auto* child : dialog->getChildren())
        if (dynamic_cast<juce::ComboBox*> (child) || dynamic_cast<juce::TextEditor*> (child) || dynamic_cast<juce::Button*> (child))
            check (dialog->getLocalBounds().contains (child->getBounds()), "every interactive export control is inside the dialog");
    auto* format = dialog->getComboBoxComponent ("format"); auto* depth = dialog->getComboBoxComponent ("bit-depth");
    check (format->getNumItems() == 3 && dialog->getComboBoxComponent ("channels")->getNumItems() == 2
        && dialog->getComboBoxComponent ("normalization")->getNumItems() == 4, "format, channels and peak options remain available");
    depth->setSelectedId (3, juce::dontSendNotification); format->setSelectedId (2, juce::sendNotificationSync);
    check (! depth->isItemEnabled (3) && depth->getSelectedId() == 2, "AIFF changes unsupported 32-bit WAV depth to 24-bit");
    format->setSelectedId (3, juce::sendNotificationSync); check (! depth->isItemEnabled (3), "FLAC disables unsupported 32-bit output");
    format->setSelectedId (1, juce::sendNotificationSync); check (depth->isItemEnabled (3), "WAV restores its 32-bit choice");
    format->setSelectedId (3, juce::sendNotificationSync);
    dialog->getComboBoxComponent ("channels")->setSelectedItemIndex (1, juce::sendNotificationSync);
    dialog->getComboBoxComponent ("normalization")->setSelectedItemIndex (2, juce::sendNotificationSync);
    dialog->getComboBoxComponent ("effect-tail")->setSelectedItemIndex (0, juce::sendNotificationSync);
    check (readAudioExportSettings (*dialog, settings).wasOk() && settings.format == Session::ExportFormat::flac && settings.mono
        && settings.normalizePeak && settings.peakCeilingDb == -3 && settings.tailSeconds == 0,
        "production dialog transfers format, channels, peak and tail settings");
    const auto entireSettings = settings;
    dialog->getComboBoxComponent ("effect-tail")->setSelectedItemIndex (2, juce::sendNotificationSync);
    session.setLoop (false, .02, .08); refresh();
    auto* range = dialog->getComboBoxComponent ("range"); range->setSelectedItemIndex (1, juce::sendNotificationSync);
    check (readAudioExportSettings (*dialog, settings).wasOk() && settings.timeRange
        && settings.timeRange->getStart() == .01 && settings.timeRange->getEnd() == .09 && settings.tailSeconds == 0
        && ! session.snapshot().looping && ! dialog->getComboBoxComponent ("effect-tail")->isEnabled()
        && dialog->getComboBoxComponent ("effect-tail")->getSelectedItemIndex() == 0
        && ! dialog->getTextEditor ("range-start")->isEnabled(), "loop export uses captured endpoints even while looping is off");
    range->setSelectedItemIndex (2, juce::sendNotificationSync);
    check (dialog->getTextEditor ("range-start")->isEnabled() && dialog->getTextEditor ("range-end")->isEnabled(), "custom range enables both time fields");
    dialog->getTextEditor ("range-start")->setText ("0.025", false); dialog->getTextEditor ("range-end")->setText ("0.075", false);
    range->setSelectedItemIndex (1, juce::sendNotificationSync); range->setSelectedItemIndex (2, juce::sendNotificationSync);
    check (dialog->getTextEditorContents ("range-start") == "0.025" && dialog->getTextEditorContents ("range-end") == "0.075",
        "switching through loop range retains custom input");
    range->setSelectedItemIndex (0, juce::sendNotificationSync);
    check (readAudioExportSettings (*dialog, settings).wasOk() && ! settings.timeRange && settings.tailSeconds == 2
        && dialog->getComboBoxComponent ("effect-tail")->isEnabled()
        && dialog->getComboBoxComponent ("effect-tail")->getSelectedItemIndex() == 2, "entire arrangement restores the selected extra tail");
    range->setSelectedItemIndex (2, juce::sendNotificationSync);
    const auto* chooserBeforeInvalid = chooser.get();
    const std::array<std::pair<const char*, const char*>, 10> invalid {{
        { "", "1" }, { "nan", "1" }, { "0", "inf" }, { "-1", "1" }, { "2", "1" },
        { "1", "1" }, { "0", "172801" }, { "1oops", "2" }, { "0", "0.00000001" }, { "0", "1e999" }
    }};
    for (const auto& [start, end] : invalid)
    {
        dialog->getTextEditor ("range-start")->setText (start, false); dialog->getTextEditor ("range-end")->setText (end, false);
        dialog->getButton (0)->onClick(); drainQueuedCallbacks();
        check (dialog && dialog->isCurrentlyModal() && ! dialog->accepted && dialog->error.getText().isNotEmpty()
            && destinationRequests == 0 && chooser.get() == chooserBeforeInvalid, "invalid custom range remains editable without a destination chooser");
    }
    check (static_cast<juce::Component&> (*dialog).keyPressed (juce::KeyPress (juce::KeyPress::returnKey)), "Return invokes the native destination button"); drainQueuedCallbacks();
    check (dialog && dialog->isCurrentlyModal() && ! dialog->accepted && destinationRequests == 0,
        "Return cannot bypass range validation or close invalid settings");
    dialog->getTextEditor ("range-start")->setText ("172799", false); dialog->getTextEditor ("range-end")->setText ("172800", false);
    check (readAudioExportSettings (*dialog, settings).wasOk() && settings.timeRange->getEnd() == Session::maximumExportRangeSeconds,
        "custom endpoint reaches the supported 48-hour maximum");
    dialog->getTextEditor ("range-start")->setText ("0.025", false); dialog->getTextEditor ("range-end")->setText ("0.075", false);
    check (readAudioExportSettings (*dialog, settings).wasOk(), "corrected range validates without reopening the dialog");
    dialog->error.setText ({}, juce::dontSendNotification);
    if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
    {
        const juce::File directory (juce::String::fromUTF8 (configured)); check (directory.createDirectory().wasOk(), "create export screenshot directory");
        auto stream = directory.getChildFile ("export-options-dialog.png").createOutputStream(); juce::PNGImageFormat png;
        check (stream && stream->setPosition (0) && stream->truncate().wasOk()
            && png.writeImageToStream (dialog->createComponentSnapshot (dialog->getLocalBounds()), *stream), "capture actual compact range export dialog");
    }
    const auto generation = session.snapshot().projectGeneration;
    dialog->getButton (0)->onClick();
    check (waitFor ([&] { return destinationRequests > 0; }), "destination callback completes after accepting valid settings");
    check (destinationRequests == 1 && capturedSettings && capturedGeneration == generation
        && capturedSettings->timeRange && capturedSettings->timeRange->getStart() == .025 && capturedSettings->timeRange->getEnd() == .075
        && capturedSettings->format == Session::ExportFormat::flac && capturedSettings->sampleRate == 48000 && capturedSettings->bitDepth == 24
        && capturedSettings->mono && capturedSettings->normalizePeak && capturedSettings->peakCeilingDb == -3 && capturedSettings->tailSeconds == 0,
        "validated range, format and mix settings survive the asynchronous destination callback");
    {
        // Rendered track: All Individual Tracks exports stems into a folder and asks before replacing them.
        AudioExportDialog stemsDialog (*this, theme, { 0, 1 });
        stemsDialog.getComboBoxComponent ("rendered")->setSelectedItemIndex (1, juce::sendNotificationSync);
        Session::AudioExportSettings stemSettings;
        check (readAudioExportSettings (stemsDialog, stemSettings).wasOk() && stemSettings.stems, "All Individual Tracks selects stem export");
        stemsDialog.setVisible (false);
        juce::TemporaryFile stemFolder;
        performStemExport (stemFolder.getFile(), stemSettings, session.snapshot().projectGeneration);
        const auto exported = stemFolder.getFile().findChildFiles (juce::File::findFiles, false, "*.wav");
        check (exported.size() == (int) session.snapshot().tracks.size() + 1, "a stem export writes one file per track plus Master");
        const auto alertsBefore = alerts.size();
        performStemExport (stemFolder.getFile(), stemSettings, session.snapshot().projectGeneration);
        check (alerts.size() == alertsBefore + 1 && alerts.back() != nullptr, "exporting stems again asks before replacing the files");
        alerts.back()->exitModalState (1); drainQueuedCallbacks();
        check (status.contains ("stems exported"), "confirming replaces the stems");
        stemFolder.getFile().deleteRecursively();
    }
    juce::TemporaryFile destination (".flac"); check (destination.getFile().replaceWithText ("keep this existing audio"), "prepare export destination collision");
    const auto selectedPath = destination.getFile().withFileExtension ("unexpected");
    exportAudioSelection (selectedPath, *capturedSettings, capturedGeneration);
    check (alerts.back() && destination.getFile().loadFileAsString() == "keep this existing audio", "extension correction requests permission before replacing another file");
    alerts.back()->exitModalState (0); drainQueuedCallbacks();
    check (destination.getFile().loadFileAsString() == "keep this existing audio", "cancelled overwrite preserves destination");
    auto queuedSettings = *capturedSettings;
    exportAudioSelection (selectedPath, queuedSettings, capturedGeneration);
    queuedSettings.timeRange.reset(); queuedSettings.mono = false; queuedSettings.format = Session::ExportFormat::wav;
    alerts.back()->exitModalState (1); drainQueuedCallbacks();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (destination.getFile()));
    check (reader && reader->sampleRate == 48000 && reader->bitsPerSample == 24 && reader->lengthInSamples == 2400
        && reader->numChannels == 1 && ! selectedPath.exists(), "overwrite confirmation retains copied range and writes exactly the selected mono FLAC interval");
    juce::AudioBuffer<float> audio (1, 2400); check (reader->read (&audio, 0, 2400, 0, true, false), "decode selected-range output");
    check (audio.getMagnitude (0, 2400) > .69f && audio.getMagnitude (0, 2400) <= std::pow (10.0f, -3.0f / 20),
        "dialog-selected peak normalization affects the actual selected-range audio");
    reader.reset();
    juce::TemporaryFile entireDestination (".flac");
    exportAudioSelection (entireDestination.getFile(), entireSettings, generation);
    reader.reset (formats.createReaderFor (entireDestination.getFile()));
    check (reader && reader->lengthInSamples == 4800 && reader->numChannels == 1 && reader->bitsPerSample == 24,
        "entire arrangement still exports its complete prior extent"); reader.reset();
    const auto beforeSaveAs = destinationRequests;
    dialog = open(); dialog->getComboBoxComponent ("effect-tail")->setSelectedItemIndex (0, juce::dontSendNotification);
    dialog->getButton (0)->onClick();
    juce::TemporaryFile saveAs (".halftrack"); check (session.saveProject (saveAs.getFile()).wasOk(), "save the current project while destination dispatch is pending");
    const bool saveDispatchCompleted = waitFor ([&] { return destinationRequests > beforeSaveAs; });
    check (saveDispatchCompleted && destinationRequests == beforeSaveAs + 1 && capturedGeneration == generation,
        "Save As keeps the pending export attached to the same project");
    const auto beforeStale = destinationRequests;
    dialog = open(); check (session.newProject().wasOk(), "replace project while export settings remain open"); refresh();
    dialog->getButton (0)->onClick(); drainQueuedCallbacks();
    check (dialog != nullptr, "stale export settings remain alive for correction");
    check (dialog->isCurrentlyModal(), "stale export settings remain modal");
    check (! dialog->accepted, "stale export settings do not accept a render packet");
    check (destinationRequests == beforeStale, "stale export settings do not request a destination");
    check (dialog->error.getText().contains ("Project changed"), "stale export settings explain the changed project"); cancel (dialog);
    dialog = open(); dialog->getButton (0)->onClick();
    check (session.newProject().wasOk(), "replace project before queued destination callback"); refresh(); drainQueuedCallbacks();
    check (destinationRequests == beforeStale, "queued export settings cannot switch to a replacement project");
    juce::TemporaryFile untouched (".flac"); check (untouched.getFile().replaceWithText ("preserve stale destination"), "prepare stale chooser destination");
    exportAudioSelection (untouched.getFile(), entireSettings, generation);
    check (untouched.getFile().loadFileAsString() == "preserve stale destination" && ! session.snapshot().dirty && ! session.canUndo(),
        "stale chooser selection preserves destination and replacement project history");
    check (session.importAudio (audioFixture, 0).wasOk(), "prepare stale overwrite export source"); refresh();
    const auto beforeReplace = session.snapshot().projectGeneration;
    exportAudioSelection (untouched.getFile().withFileExtension ("unexpected"), entireSettings, beforeReplace);
    check (alerts.back() != nullptr, "open overwrite confirmation before project replacement");
    check (session.newProject().wasOk(), "replace project before overwrite confirmation"); refresh();
    alerts.back()->exitModalState (1); drainQueuedCallbacks();
    check (untouched.getFile().loadFileAsString() == "preserve stale destination" && ! session.snapshot().dirty && ! session.canUndo(),
        "stale overwrite confirmation cannot render or replace a file from another project");
    testExportDestination = {};
    check (session.newProject().wasOk() && session.importAudio (audioFixture, 0).wasOk(), "restore a populated project after export UI checks"); refresh();
}

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
    // Mouse events carry integer pixels; expect the beat under the pixel actually clicked.
    const auto clickedBeat = (std::floor (arrangementRuler.x) - 55.0) / beatWidth;
    check (std::abs (state.position - clickedBeat * 60.0 / state.tempo) < 0.001 && std::abs (clickedBeat - 2) < 1.0 / beatWidth + 1e-9,
           "arrangement MIDI ruler seeks the global transport");
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
    {
        // The lane under the notes: dragging a velocity bar, then drawing and deleting a pitch-bend point.
        auto* laneView = findNamed (pianoRoll, "Controller lane");
        auto* laneMenu = dynamic_cast<juce::ComboBox*> (findNamed (pianoRoll, "Lane"));
        check (laneView != nullptr && laneView->isVisible() && laneMenu != nullptr && laneMenu->getSelectedId() == 1, "the piano roll shows a velocity lane under the notes");
        auto laneEvent = [&] (juce::Point<float> at, juce::Point<float> down, bool dragged = false, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                     laneView, laneView, now, down, now, clicks, dragged);
        };
        const auto before = currentMidiClip()->notes.front().velocity;
        const auto noteX = (float) (55.0 + currentMidiClip()->notes.front().startBeat * beatWidth);
        laneView->mouseDown (laneEvent ({ noteX, 50 }, { noteX, 50 })); laneView->mouseDrag (laneEvent ({ noteX, 6 }, { noteX, 50 }, true)); laneView->mouseUp (laneEvent ({ noteX, 6 }, { noteX, 50 }, true));
        check (currentMidiClip()->notes.front().velocity == 1.0f, "dragging a velocity bar to the top sets full velocity");
        pianoRoll.keyPressed (undo);
        check (currentMidiClip()->notes.front().velocity == before, "one undo restores the velocity");
        laneMenu->setSelectedId (2, juce::sendNotificationSync);
        const auto beat2 = juce::Point<float> ((float) (55 + 2 * beatWidth), 6);
        laneView->mouseDown (laneEvent (beat2, beat2)); laneView->mouseUp (laneEvent (beat2, beat2));
        const auto bend = [&] { return currentMidiClip()->controllers; };
        check (bend().size() == 1 && bend()[0].controller == Session::pitchBendLane && bend()[0].points == std::vector<Session::ControllerPoint> { { 2, 8191 } },
               "clicking the Pitch Bend lane adds a full upward bend at beat 2");
        laneView->mouseDown (laneEvent (beat2, beat2, false, 2)); laneView->mouseUp (laneEvent (beat2, beat2, false, 2));
        check (bend().empty(), "double-clicking a point deletes it");
        laneMenu->setSelectedId (99, juce::sendNotificationSync);
        check (! laneView->isVisible(), "Hide lane gives the notes the full height");
        laneMenu->setSelectedId (1, juce::sendNotificationSync);
    }
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
    auto* chooser = dynamic_cast<juce::ComboBox*> (findNamed (deviceChain, "Track instrument"));
    auto* editInstrument = dynamic_cast<juce::Button*> (findNamed (deviceChain, "Edit instrument"));
    check (chooser != nullptr && editInstrument != nullptr && chooser->getNumItems() == 1 + Session::effectKindCount - (int) Session::EffectKind::polySynth,
           "instrument chooser lists the built-in synth and every native instrument");
    chooser->setSelectedId (3, juce::sendNotificationSync); refresh();
    check (currentTrack()->instrument.isNativeEffect && currentTrack()->instrument.effect.kind == Session::EffectKind::fmSynth
               && editInstrument->isVisible() && ! waveform->isVisible() && ! cutoff->isVisible(),
           "choosing FM Synth replaces the built-in controls with the native instrument panel");
    status.clear(); editInstrument->onClick();
    check (status.isEmpty(), "edit button opens the native instrument editor without an error");
    session.closePluginEditors();
    if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
    {
        const juce::File directory (juce::String::fromUTF8 (configured));
        if (directory.createDirectory().wasOk())
        {
            juce::PNGImageFormat png; const auto output = directory.getChildFile ("instrument-chain.png");
            if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                && png.writeImageToStream (deviceChain.createComponentSnapshot (deviceChain.getLocalBounds(), true, 1.0f), *stream))
                std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
        }
    }
    {
        // Inline device view: the native instrument's own controls live in the chain.
        auto* operatorLevel = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "opALevel"));
        check (operatorLevel != nullptr && operatorLevel->isVisible() && operatorLevel->getParentComponent()->isVisible(), "native instrument controls are shown inline in the device view");
        operatorLevel->setValue (.25, juce::sendNotificationSync); refresh();
        check (std::abs (currentTrack()->instrument.effect.values[2] - .25f) < 1e-6f, "an inline instrument control edits the real instrument");
        session.undo(); refresh();
    }
    session.undo(); refresh();
    check (! currentTrack()->instrument.isNativeEffect && chooser->getSelectedId() == 1 && waveform->isVisible()
               && session.snapshot().tracks[(size_t) selectedTrack].synth.pulseWidth == 0.64f,
           "undo restores the built-in synth with its previous settings");
    {
        // Sampler: the instrument panel shows the sample, accepts a dropped file and edits the region by dragging.
        chooser->setSelectedId (2 + (int) Session::EffectKind::sampler - (int) Session::EffectKind::polySynth, juce::sendNotificationSync); refresh();
        auto* view = dynamic_cast<SampleView*> (findNamed (deviceChain, "Sampler sample"));
        check (currentTrack()->instrument.effect.kind == Session::EffectKind::sampler && view != nullptr && view->isVisible()
               && findNamed (*view, "Load sample") != nullptr && currentTrack()->instrument.sample == juce::File(),
               "choosing Sampler shows an empty sample view with a load button");
        check (view->isInterestedInFileDrag ({ audioFixture.getFullPathName() }) && ! view->isInterestedInFileDrag ({ audioFixture.withFileExtension ("txt").getFullPathName() }),
               "the sample view accepts one audio file");
        view->filesDropped ({ audioFixture.getFullPathName() }, 10, 10); refresh();
        check (currentTrack()->instrument.sample == audioFixture, "dropping an audio file loads it into the Sampler");
        const auto wave = view->waveBounds();
        const auto at = juce::Point<float> ((float) wave.getX() + wave.getWidth() * .5f, (float) wave.getCentreY());
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent drag (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                     view, view, now, at.translated ((float) -wave.getWidth() * .5f, 0), now, 1, true);
        // Start is nearer the left edge, so a press there drags Start to the middle.
        const juce::MouseEvent press (juce::Desktop::getInstance().getMainMouseSource(), at.translated ((float) -wave.getWidth() * .45f, 0), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                      1.0f, 0, 0, 0, 0, view, view, now, at.translated ((float) -wave.getWidth() * .45f, 0), now, 1, false);
        view->mouseDown (press); view->mouseDrag (drag); view->mouseUp (drag); refresh();
        const auto start = currentTrack()->instrument.effect.values[instruments::sampler::start];
        check (std::abs (start - .5f) < .01f && currentTrack()->instrument.effect.values[instruments::sampler::end] == 1, "dragging the start marker moves the region start");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("sampler.png");
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (deviceChain.createComponentSnapshot (deviceChain.getLocalBounds(), true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
            }
        session.undo(); refresh();
        check (currentTrack()->instrument.effect.values[instruments::sampler::start] == 0, "one undo step restores the region dragged in one gesture");
        session.undo(); session.undo(); refresh();
        check (! currentTrack()->instrument.isNativeEffect && ! view->isVisible(), "undo removes the sample and the Sampler");
    }
    {
        // Drum Sampler: a 4x4 pad grid takes dropped samples per pad and edits the selected pad.
        chooser->setSelectedId (2 + (int) Session::EffectKind::drumSampler - (int) Session::EffectKind::polySynth, juce::sendNotificationSync); refresh();
        auto* pads = dynamic_cast<DrumPanel*> (findNamed (deviceChain, "Drum pads"));
        check (currentTrack()->instrument.effect.kind == Session::EffectKind::drumSampler && pads != nullptr && pads->isVisible() && findNamed (deviceChain, "opALevel") == nullptr,
               "choosing Drum Sampler shows the pad grid instead of the generic controls");
        check (pads->padBounds (0).getY() > pads->padBounds (12).getY() && pads->padBounds (0).getX() < pads->padBounds (3).getX(), "pad 1 sits bottom left and pads rise row by row");
        const auto second = pads->padBounds (1).getCentre();
        pads->filesDropped ({ audioFixture.getFullPathName() }, second.x, second.y); refresh();
        check (currentTrack()->instrument.padSamples.size() == 16 && currentTrack()->instrument.padSamples[1] == audioFixture && pads->selectedPad() == 1,
               "dropping a file on pad 2 loads it there and selects the pad");
        auto* tune = dynamic_cast<juce::Slider*> (findNamed (*pads, "Pad tune"));
        auto* choke = dynamic_cast<juce::ComboBox*> (findNamed (*pads, "Pad choke"));
        check (tune != nullptr && choke != nullptr, "the selected pad has tune and choke controls");
        tune->setValue (7, juce::sendNotificationSync); choke->setSelectedId (3, juce::sendNotificationSync); refresh();
        const auto& values = currentTrack()->instrument.effect.values;
        check (values[1 * instruments::drumSampler::padStride + instruments::drumSampler::tune] == 7
               && values[1 * instruments::drumSampler::padStride + instruments::drumSampler::choke] == 2 && values[instruments::drumSampler::tune] == 0,
               "pad controls edit only the selected pad");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("drum-sampler.png");
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (deviceChain.createComponentSnapshot (deviceChain.getLocalBounds(), true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
            }
        dynamic_cast<juce::Button*> (findNamed (*pads, "Clear pad"))->onClick(); refresh();
        check (currentTrack()->instrument.padSamples[1] == juce::File(), "Clear empties the selected pad");
        for (int step = 0; step < 5; ++step) session.undo();
        refresh();
        check (! currentTrack()->instrument.isNativeEffect && ! pads->isVisible(), "undo returns to the built-in synth");
    }
    {
        // Freeze and Flatten from the track menu: devices lock while frozen, Flatten leaves audio.
        const int frozenTrack = selectedTrack; const auto frozenId = currentTrack()->id;
        const auto before = state.tracks[(size_t) frozenTrack].clips.size();
        check (session.addMidiClip (frozenId, 0, 4).wasOk(), "give the MIDI track an Arrangement clip"); refresh();
        check (session.addMidiNote (state.tracks[(size_t) frozenTrack].clips.back().id, 60, 0, 1, .8f).wasOk(), "add a note to freeze"); refresh();
        freezeTrackFromUI (frozenId, false); refresh();
        auto* synthPanel = findNamed (deviceChain, "Halftrack synthesizer");
        check (state.tracks[(size_t) frozenTrack].frozen && synthPanel != nullptr && ! synthPanel->isEnabled(), "Freeze Track freezes the track and locks its devices");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                showArrangementView(); refresh();
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("frozen-track.png");
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (createComponentSnapshot (getLocalBounds(), true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
            }
        freezeTrackFromUI (frozenId, true); refresh();
        const auto& flat = state.tracks[(size_t) frozenTrack];
        check (! flat.isMidi && ! flat.frozen && flat.clips.size() == 1 && flat.clips[0].isAudio && synthPanel->isEnabled(), "Flatten replaces the track with an audio clip of its render");
        for (int step = 0; step < 4; ++step) session.undo();
        refresh();
        check (state.tracks[(size_t) frozenTrack].isMidi && ! state.tracks[(size_t) frozenTrack].frozen && state.tracks[(size_t) frozenTrack].clips.size() == before,
               "undo restores the unfrozen MIDI track");
    }
    {
        // Insert Silence and Delete Time act on the loop range across the arrangement.
        const int timeTrack = selectedTrack; const auto timeId = currentTrack()->id;
        check (session.addMidiClip (timeId, 4, 2).wasOk(), "a MIDI clip at two seconds"); refresh();
        const auto clipId = state.tracks[(size_t) timeTrack].clips.back().id;
        const auto clipStart = [&] { for (const auto& c : state.tracks[(size_t) timeTrack].clips) if (c.id == clipId) return c.start; return -1.0; };
        session.setLoop (state.looping, .5, 1); refresh();
        menuItemSelected (74, 1); refresh();
        check (std::abs (clipStart() - 2.5) < 1e-6, "Insert Silence moves later clips by the loop length");
        menuItemSelected (75, 1); refresh();
        check (std::abs (clipStart() - 2) < 1e-6, "Delete Time moves them back");
        showArrangementView(); refresh();
        check (handleKey (juce::KeyPress ('I', juce::ModifierKeys::commandModifier, 0)), "Cmd/Ctrl+I inserts silence in the Arrangement"); refresh();
        check (std::abs (clipStart() - 2.5) < 1e-6, "the shortcut inserts the loop length");
        for (int step = 0; step < 3; ++step) session.undo();
        refresh();
        check (std::abs (clipStart() - 2) < 1e-6, "each time edit is one undo step");
        deleteClipById (clipId); refresh();
    }
    {
        // Dragging over empty lane space selects time; Cmd/Ctrl+D duplicates it after itself and Delete clears it.
        showArrangementView(); refresh();
        const int previousTrack = selectedTrack;
        check (session.addMidiTrack ("Selection").wasOk(), "add an empty MIDI track"); refresh();
        selectTrack ((int) state.tracks.size() - 1); refresh();
        const int t = selectedTrack; const auto trackId = currentTrack()->id;
        check (session.addMidiClip (trackId, 2, 2).wasOk(), "a one-second MIDI clip at 1 s"); refresh();
        const auto now = juce::Time::getCurrentTime();
        const juce::ModifierKeys alt (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
        auto canvasEvent = [&] (juce::Point<float> at, juce::Point<float> down, bool dragged)
        { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, alt, 1.0f, 0, 0, 0, 0, &canvas, &canvas, now, down, now, 1, dragged); };
        const auto xAt = [&] (double seconds) { return (float) ((seconds - viewStart) * pixelsPerSecond); };
        const auto y = (float) (laneY (t) + laneHeight (t) / 2);
        const juce::Point<float> down { xAt (.5), y }, up { xAt (2.5), y };
        canvas.mouseDown (canvasEvent (down, down, false)); canvas.mouseDrag (canvasEvent (up, down, true)); canvas.mouseUp (canvasEvent (up, down, true));
        const auto tolerance = 1.5 / pixelsPerSecond;
        check (timeSelection.active() && std::abs (timeSelection.start - .5) < tolerance && std::abs (timeSelection.end - 2.5) < tolerance
               && timeSelection.trackIds == juce::StringArray { trackId }, "dragging over empty lane space selects that time on the track");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("time-selection.png");
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (createComponentSnapshot (getLocalBounds(), true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
            }
        const auto starts = [&] { std::vector<double> result; for (const auto& c : state.tracks[(size_t) t].clips) result.push_back (c.start); std::sort (result.begin(), result.end()); return result; };
        const auto before = starts();
        check (handleKey (juce::KeyPress ('D', juce::ModifierKeys::commandModifier, 0)), "Cmd/Ctrl+D is handled"); refresh();
        const auto selectionLength = timeSelection.end - timeSelection.start;
        check (starts().size() == before.size() + 1 && std::abs (starts().back() - (1 + selectionLength)) < tolerance && std::abs (timeSelection.start - 2.5) < tolerance,
               "the selection's clip is copied after it and the selection moves onto the copy");
        check (handleKey (juce::KeyPress (juce::KeyPress::deleteKey)), "Delete is handled"); refresh();
        check (starts() == before, "Delete removes the material in the selection");
        timeSelection.start = .5; timeSelection.end = 2.5;
        check (handleKey (juce::KeyPress ('J', juce::ModifierKeys::commandModifier, 0)), "Cmd/Ctrl+J is handled"); refresh();
        check (state.tracks[(size_t) t].clips.size() == 1 && std::abs (state.tracks[(size_t) t].clips[0].start - .5) < 1e-6
               && std::abs (state.tracks[(size_t) t].clips[0].length - 2) < 1e-6, "Cmd/Ctrl+J consolidates the selection into one clip spanning it");
        session.undo(); refresh();
        session.undo(); session.undo(); refresh();
        check (starts() == before, "undo restores each step");
        timeSelection = {};
        check (session.removeTrack (t).wasOk(), "remove the selection track"); refresh();
        selectTrack (previousTrack); refresh();
    }
    {
        const int instrumentTrack = selectedTrack;
        selectTrack (0); report (session.addNativeEffect (0, Session::EffectKind::saturator)); refresh();
        auto* drive = dynamic_cast<juce::Slider*> (findNamed (deviceChain, "driveDb"));
        auto* power = dynamic_cast<juce::Button*> (findNamed (deviceChain, "Device on"));
        check (drive != nullptr && power != nullptr && drive->isVisible() && power->getToggleState(),
               "inline device panels show each device's controls and a lit on/off switch");
        drive->setValue (12, juce::sendNotificationSync); refresh();
        check (state.tracks[0].plugins.back().effect.values[0] == 12, "an inline device control edits the real device");
        power->setToggleState (false, juce::dontSendNotification); power->onClick(); refresh();
        check (! state.tracks[0].plugins.back().enabled && ! power->getToggleState(), "the device switch bypasses the device");
        session.undo(); session.undo(); session.undo(); refresh();
        check (state.tracks[0].plugins.empty(), "undo removes the inline test device");
        selectTrack (instrumentTrack); refresh();
    }
    {
        // Audio Effect Racks: Shift-click two devices and Cmd/Ctrl+G groups them; chain rows, macros and mapped controls edit the real rack.
        const int instrumentTrack = selectedTrack;
        selectTrack (0);
        report (session.addUtility (0)); report (session.addNativeEffect (0, Session::EffectKind::equalizer)); refresh();
        const auto utilityId = state.tracks[0].plugins[0].id, eqId = state.tracks[0].plugins[1].id;
        selectDevice (utilityId, {}); selectDevice (eqId, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
        check (orderedSelection() == juce::StringArray { utilityId, eqId }, "Shift-click extends the device selection");
        check (deviceKey (juce::KeyPress ('G', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0)), "the device chain handles Cmd/Ctrl+G"); refresh();
        const auto rack = [&] { return state.tracks[0].plugins.empty() ? Session::PluginInfo {} : state.tracks[0].plugins[0]; };
        check (state.tracks[0].plugins.size() == 1 && rack().isRack && rack().chains.size() == 1 && rack().chains[0].devices.size() == 2
               && selectedDevices == juce::StringArray { rack().id }, "Cmd/Ctrl+G groups the selected devices into a rack and selects it");
        auto* toggle = findNamed (deviceChain, "Show macros");
        auto* panel = toggle != nullptr ? dynamic_cast<RackPanel*> (toggle->getParentComponent()) : nullptr;
        check (panel != nullptr && panel->isVisible() && findNamed (*panel, "Macro 1") != nullptr && findNamed (*panel, "outputDb") != nullptr,
               "the rack panel shows its macros and its chain's devices");
        dynamic_cast<juce::Button*> (findNamed (*panel, "Add chain"))->onClick(); refresh();
        check (rack().chains.size() == 2 && panel->selectedChain() == rack().chains[1].id && findNamed (*panel, "outputDb") == nullptr,
               "+ adds a chain and shows its empty device list");
        std::vector<juce::Component*> found;
        std::function<void (juce::Component&, const juce::String&)> findAll = [&] (juce::Component& parent, const juce::String& name)
        { for (auto* child : parent.getChildren()) { if (child->getName() == name) found.push_back (child); findAll (*child, name); } };
        found.clear(); findAll (*panel, "Chain volume");
        check (found.size() == 2, "each chain has a row with a volume control");
        dynamic_cast<juce::Slider*> (found.back())->setValue (-6, juce::sendNotificationSync); refresh();
        check (std::abs (rack().chains[1].settings.gainDb + 6) < .05f, "the chain row's volume edits the chain");
        found.clear(); findAll (*panel, "Chain activator");
        auto* activator = dynamic_cast<juce::Button*> (found.back());
        activator->setToggleState (false, juce::dontSendNotification); activator->onClick(); refresh();
        check (rack().chains[1].settings.mute && ! rack().chains[0].settings.mute, "the activator turns only its chain off");
        selectedChains[rack().id] = rack().chains[0].id; deviceChain.sync();
        check (session.mapRackMacro (rack().id, 0, eqId, 32, -24, 24).wasOk(), "map Macro 1 to the EQ output"); refresh();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100); // Inline editors refresh on the session's change message.
        auto* output = dynamic_cast<juce::Slider*> (findNamed (*panel, "outputDb"));
        check (output != nullptr && output->findColour (juce::Slider::rotarySliderFillColourId) == RackPanel::macroColour, "the mapped control shows the macro colour");
        dynamic_cast<juce::Slider*> (findNamed (*panel, "Macro 1"))->setValue (.25, juce::sendNotificationSync); refresh();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        check (std::abs (rack().chains[0].devices[1].effect.values[32] + 12) < 1e-3f && std::abs (output->getValue() + 12) < 1e-3,
               "turning Macro 1 moves the mapped EQ output");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("audio-effect-rack.png");
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (deviceChain.createComponentSnapshot (deviceChain.getLocalBounds(), true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
            }
        for (int step = 0; step < 8; ++step) session.undo();
        refresh();
        check (state.tracks[0].plugins.empty() && selectedDevices.isEmpty(), "undo removes the rack and its devices");
        selectTrack (instrumentTrack); refresh();
    }
    {
        // Groups: Shift-click extends the track selection, Cmd/Ctrl+G groups it, the header folds it.
        showArrangementView(); refresh();
        const auto firstId = state.tracks[0].id, secondId = state.tracks[1].id;
        const auto now = juce::Time::getCurrentTime();
        auto headerClick = [&] (int index, juce::ModifierKeys mods)
        {
            auto* row = canvas.getChildComponent (index);
            row->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 20.0f, 30.0f }, mods, 1.0f, 0, 0, 0, 0, row, row, now, { 20.0f, 30.0f }, now, 1, false));
        };
        headerClick (0, {}); headerClick (1, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
        check (multiSelectedTracks.contains (firstId) && multiSelectedTracks.contains (secondId), "Shift-click extends the track selection");
        handleKey (juce::KeyPress ('G', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0)); refresh();
        check (state.groups.size() == 1 && state.groups[0].memberIds == juce::StringArray { firstId, secondId } && multiSelectedTracks.empty(),
               "Cmd/Ctrl+G groups the selected tracks");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("arrangement-group.png");
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (createComponentSnapshot (getLocalBounds(), true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
            }
        auto* fold = dynamic_cast<juce::Button*> (findNamed (canvas, "Group fold"));
        check (fold != nullptr && trackAtY (lanes.front().y + 5) == -1 && laneY (0) == groupHeight, "the group header occupies its own lane above its members");
        fold->onClick(); refresh();
        check (state.groups[0].folded && laneHeight (0) == 0 && laneHeight (1) == 0 && ! canvas.getChildComponent (0)->isVisible()
               && canvas.clipBounds (state.tracks[0].clips.empty() ? Session::ClipInfo {} : state.tracks[0].clips[0], 0).getHeight() == 0,
               "folding hides the member lanes and their clips");
        fold->onClick(); refresh();
        check (! state.groups[0].folded && laneHeight (0) == trackHeight, "unfolding restores the member lanes");
        selectTrack (0); menuItemSelected (73, 2);
        check (state.groups.empty() && laneY (0) == 0, "the Create menu ungroups the selected track's group");
        session.undo(); session.undo(); refresh();
        check (state.groups.empty(), "undo leaves no group behind");
    }
    {
        // Browser: categories list built-in devices; activating a row inserts it.
        const int instrumentTrack = selectedTrack;
        const auto row = [this] (const juce::String& label)
        { for (int i = 0; i < getNumRows(); ++i) if (! deviceItems[(size_t) i].header && deviceItems[(size_t) i].label == label) return i; return -1; };
        browserCategories.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), browserCategories.categoryPoint (BrowserCategory::audioEffects).toFloat(),
            juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, &browserCategories, &browserCategories, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false));
        check (browserCategory == BrowserCategory::audioEffects && row ("Chorus") >= 0 && row ("Utility") >= 0 && row ("Arpeggiator") < 0,
               "Audio Effects category lists built-in audio devices only");
        selectTrack (0); activateBrowserRow (row ("Chorus"));
        check (state.tracks[0].plugins.size() == 1 && state.tracks[0].plugins[0].effect.kind == Session::EffectKind::chorus, "activating an audio effect inserts it on the selected track");
        session.undo(); refresh();
        browserSearch.setText ("synth", juce::sendNotificationSync);
        setBrowserCategory (BrowserCategory::instruments);
        check (row ("FM Synth") >= 0 && row ("Drum Kit") < 0, "browser search filters the current category");
        const auto tracks = state.tracks.size(); activateBrowserRow (row ("FM Synth"));
        check (state.tracks.size() == tracks + 1 && currentTrack()->isMidi && currentTrack()->instrument.effect.kind == Session::EffectKind::fmSynth,
               "activating an instrument with an audio track selected creates a MIDI track with it");
        // Instrument choice, the new track's first clip, then the track itself.
        for (int step = 0; step < 3; ++step) session.undo();
        refresh(); browserSearch.setText ({}, juce::sendNotificationSync); updateBrowser();
        check (state.tracks.size() == tracks, "undo removes the browser-created track");
        selectTrack (juce::jmin (instrumentTrack, (int) state.tracks.size() - 1)); refresh();
    }
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
    const auto slotPoint = launcher.slotCentre (midiTrack, 0);
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
    launcher.mouseDown (launcherEvent (launcher.slotCentre (midiTrack, 1)));
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
    auto* launchGrid = dynamic_cast<juce::ComboBox*> (findNamed (*this, "Global launch quantization"));
    check (launchGrid != nullptr, "global launch quantization has a named native control");
    launchGrid->setSelectedId (1, juce::sendNotificationSync);
    check (state.launchQuantisation == Session::LaunchQuantisation::none, "quantization control updates the actual launcher setting");
    auto launchPoint = launcher.launchPoint (midiTrack, 0);
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
    const auto trackStopPoint = launcher.trackStopPoint (midiTrack);
    launcher.mouseDown (launcherEvent (trackStopPoint));
    pumpSyntheticTestDevice (session, 8); refresh();
    check (! state.tracks[(size_t) midiTrack].slots.front().playing,
           "per-track stop control stops the active Session clip");
    selectLauncherSlot (midiTrack, 0);
    launcher.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    pumpSyntheticTestDevice (session, 8); refresh();
    check (state.tracks[(size_t) midiTrack].slots.front().playing,
           "Enter key launches the selected Session slot through the production handler");
    withGeneratedMidiController (session, [&] (const juce::String& controller)
    {
        // Session recording from the grid: an armed track's empty slot button records, and pressing it again ends the take.
        check (session.setTrackInput (midiTrack, controller).wasOk() && session.setTrackArmed (midiTrack, true).wasOk(), "arm the MIDI track for Session recording");
        refresh();
        const auto recordPoint = launcher.launchPoint (midiTrack, 1);
        check (! state.tracks[(size_t) midiTrack].slots[1].hasClip, "the second slot is empty");
        launcher.mouseDown (launcherEvent (recordPoint)); launcher.mouseUp (launcherEvent (recordPoint)); refresh();
        check (state.tracks[(size_t) midiTrack].slots[1].recording, "clicking an armed empty slot's button records into it");
        pumpSyntheticTestDevice (session, 40); refresh();
        check (state.playing && state.tracks[(size_t) midiTrack].slots[1].recording, "recording keeps the transport running");
        launcher.mouseDown (launcherEvent (recordPoint)); launcher.mouseUp (launcherEvent (recordPoint));
        pumpSyntheticTestDevice (session, 60); refresh();
        check (! state.tracks[(size_t) midiTrack].slots[1].recording && state.tracks[(size_t) midiTrack].slots[1].hasClip
               && state.tracks[(size_t) midiTrack].slots[1].clip.isMidi && state.tracks[(size_t) midiTrack].slots[1].playing,
               "clicking the recording slot ends the take and plays it");
        session.undo(); refresh();
        check (! state.tracks[(size_t) midiTrack].slots[1].hasClip, "undo removes the recorded take");
        check (session.setTrackArmed (midiTrack, false).wasOk() && session.setTrackInput (midiTrack, {}).wasOk(), "disarm and disconnect the controller");
    });
    // The generated controller stops the transport when it is removed; relaunch the first clip.
    pumpSyntheticTestDevice (session, 8);
    launcher.mouseDown (launcherEvent (launcher.launchPoint (midiTrack, 0))); launcher.mouseUp (launcherEvent (launcher.launchPoint (midiTrack, 0)));
    pumpSyntheticTestDevice (session, 8); refresh();
    check (state.tracks[(size_t) midiTrack].slots[0].playing, "the first clip plays again after the recording checks");
    {
        // The clip view's Launch box and press/release launch semantics.
        const auto slotState = [&] { return state.tracks[(size_t) midiTrack].slots.front(); };
        launcher.keyStateChanged (false); // Key up after the Enter launch above.
        check (slotState().playing, "releasing Enter leaves a Trigger clip playing");
        launcher.mouseDown (launcherEvent (trackStopPoint)); pumpSyntheticTestDevice (session, 8); refresh();
        selectLauncherSlot (midiTrack, 0); openPianoRoll(); refresh();
        auto* launchMode = dynamic_cast<juce::ComboBox*> (findNamed (midiLaunch, "Launch mode"));
        auto* launchQuantization = dynamic_cast<juce::ComboBox*> (findNamed (midiLaunch, "Launch quantization"));
        auto* legato = dynamic_cast<juce::Button*> (findNamed (midiLaunch, "Launch legato"));
        check (showPiano && midiLaunch.isVisible() && pianoRoll.getX() >= midiLaunch.getRight() && launchMode && launchQuantization && legato
               && launchMode->getText() == "Trigger" && launchQuantization->getText() == "Global" && ! legato->getToggleState(),
               "a Session MIDI clip shows a Launch box with Trigger on the global grid beside the note editor");
        launchMode->setSelectedId (2, juce::sendNotificationSync);
        launchQuantization->setSelectedId (2, juce::sendNotificationSync);
        legato->setToggleState (true, juce::dontSendNotification); legato->onClick();
        check (slotState().clip.launch == Session::LaunchSettings { Session::LaunchMode::gate, Session::ClipQuantisation::none, true },
               "the Launch box edits the clip's launch mode, quantization and legato");
        launcher.mouseDown (launcherEvent (launchPoint)); pumpSyntheticTestDevice (session, 8); refresh();
        check (slotState().playing, "holding a Gate clip's launch button plays it");
        launcher.mouseUp (launcherEvent (launchPoint)); pumpSyntheticTestDevice (session, 8); refresh();
        check (! slotState().playing, "releasing a Gate clip's launch button stops it");
        selectLauncherSlot (midiTrack, 0);
        launcher.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); launcher.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
        pumpSyntheticTestDevice (session, 8); refresh();
        check (slotState().playing, "holding Enter plays the Gate clip, ignoring key repeat");
        launcher.keyStateChanged (false); pumpSyntheticTestDevice (session, 8); refresh();
        check (! slotState().playing, "releasing Enter closes the gate");
        launchMode->setSelectedId (3, juce::sendNotificationSync);
        for (const bool expected : { true, false })
        {
            launcher.mouseDown (launcherEvent (launchPoint)); launcher.mouseUp (launcherEvent (launchPoint));
            pumpSyntheticTestDevice (session, 8); refresh();
            check (slotState().playing == expected, expected ? "a Toggle click starts the clip and survives release" : "a second Toggle click stops the clip");
        }
        for (int step = 0; step < 4; ++step) session.undo();
        refresh();
        check (slotState().clip.launch == Session::LaunchSettings {} && launchMode->getText() == "Trigger" && ! legato->getToggleState(),
               "undo restores Trigger launch settings in the Launch box");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("launch-box.png");
                const auto area = midiLaunch.getBounds().getUnion (pianoRoll.getBounds().withWidth (420));
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (createComponentSnapshot (area, true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
            }
        closeClipView(); refresh();
        check (! midiLaunch.isVisible(), "closing the clip view hides the Launch box");
        launcher.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); launcher.keyStateChanged (false);
        pumpSyntheticTestDevice (session, 8); refresh();
        check (slotState().playing, "a Trigger clip launched with Enter keeps playing after key up");
    }
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
    const auto dropPoint = getLocalPoint (&launcher, launcher.slotCentre (0, 1).toInt());
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
        closeClipView(); // Sends live in the device view, which an open clip editor replaces.
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
    returnPoint.y = launcher.slotCentre (0, 0).y;
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
        if (! editor.hasKeyboardFocus (true))
        {
            const auto* focused = juce::Component::getCurrentlyFocusedComponent();
            std::cerr << "Focus diagnostic: foreground=" << juce::Process::isForegroundProcess()
                << " activeWindow=" << focusWindow.isActiveWindow()
                << " editorShowing=" << editor.isShowing() << " editorEnabled=" << editor.isEnabled()
                << " modalCount=" << juce::Component::getNumCurrentlyModalComponents()
                << " focused=" << (focused ? focused->getName().toStdString() : "none") << std::endl;
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
    {
        // Audio clip view: double-click opens it in the detail area with warp, tempo and gain.
        showArrangementView(); selectedTrack = 0; selectedClip = 0; refresh();
        const auto clipPoint = canvas.clipBounds (state.tracks[0].clips[0], 0).getCentre();
        const auto now = juce::Time::getCurrentTime();
        canvas.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), clipPoint, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0,
                                                   &canvas, &canvas, now, clipPoint, now, 2, false));
        auto* warpButton = dynamic_cast<juce::Button*> (findNamed (audioClipView, "Clip warp"));
        auto* tempoLabel = dynamic_cast<juce::Label*> (findNamed (audioClipView, "Clip tempo"));
        auto* clipGain = dynamic_cast<juce::Slider*> (findNamed (audioClipView, "Clip gain"));
        check (showPiano && audioClipView.isVisible() && ! pianoRoll.isVisible() && warpButton && tempoLabel && clipGain && ! warpButton->getToggleState(),
               "double-clicking an audio clip opens the audio clip view instead of the note editor");
        check (findNamed (audioClipView, "Launch box") != nullptr && ! findNamed (audioClipView, "Launch box")->isVisible() && ! midiLaunch.isVisible(),
               "arrangement clips have no Launch box");
        const auto length = state.tracks[0].clips[0].length, estimate = state.tracks[0].clips[0].warp.bpm;
        warpButton->setToggleState (true, juce::dontSendNotification); warpButton->onClick();
        // Warping at the estimated source tempo rescales the clip onto the project tempo.
        const auto warpedLength = length * estimate / state.tempo;
        check (state.tracks[0].clips[0].warp.mode == Session::WarpMode::stretch && std::abs (state.tracks[0].clips[0].length - warpedLength) < 1e-6,
               "the Warp switch warps the clip at its estimated tempo onto the project tempo");
        tempoLabel->setText (juce::String (estimate / 2), juce::sendNotificationSync);
        check (std::abs (state.tracks[0].clips[0].warp.bpm - estimate / 2) < 1e-9 && std::abs (state.tracks[0].clips[0].length - warpedLength / 2) < 1e-6,
               "a slower source tempo makes the warped clip play twice as fast");
        clipGain->setValue (-6, juce::sendNotificationSync);
        check (std::abs (state.tracks[0].clips[0].audio.gainDb + 6) < 1e-4, "the clip view gain knob edits clip gain");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("audio-clip-view.png");
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (audioClipView.createComponentSnapshot (audioClipView.getLocalBounds(), true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
            }
        for (int step = 0; step < 3; ++step) session.undo();
        refresh(); closeClipView();
        check (state.tracks[0].clips[0].warp.mode == Session::WarpMode::off && state.tracks[0].clips[0].audio.gainDb == 0, "undo restores the unwarped clip");
    }
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
    meterLabel.setText("6/8",juce::sendNotificationSync);
    check(state.timeSignature==Session::TimeSignature{6,8}&&meterLabel.getText()=="6 / 8","typing 6/8 in the meter field sets the time signature");
    meterLabel.setText("5/3",juce::sendNotificationSync);
    check(state.timeSignature==Session::TimeSignature{6,8}&&meterLabel.getText()=="6 / 8","an invalid meter leaves the time signature unchanged");
    check(formatMusicalTime(4.5,true,{6,8})=="2. 4. 1"&&parseMusicalTime("2.4.1",true,{6,8})==4.5&&! parseMusicalTime("1.7.1",true,{6,8})
          &&formatMusicalTime(4.5,true)=="2. 1. 3","positions count bars and eighth-note beats in 6/8");
    meterLabel.setText("4/4",juce::sendNotificationSync);
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
    runExportInteractionChecks (audioFixture);
    {
        // Session group columns: a group column precedes its members, folds them, launches their row and stops them.
        check (session.newProject().wasOk() && session.importAudioToSlot (audioFixture, 0, 0).wasOk() && session.importAudioToSlot (audioFixture, 1, 0).wasOk(),
               "prepare two Session clips for a group");
        juce::String group;
        check (session.groupTracks ({ session.snapshot().tracks[0].id, session.snapshot().tracks[1].id }, group).wasOk(), "group both tracks");
        check (session.setLaunchQuantisation (Session::LaunchQuantisation::none).wasOk(), "launch immediately");
        showSessionView(); refresh();
        const auto groupEvent = [&] (juce::Point<float> at)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                     &launcher, &launcher, now, at, now, 1, false);
        };
        check (launcher.columnCount() == 3 && launcher.groupColumnOf (0) == 0 && launcher.columnOf (0) == 1 && launcher.columnOf (1) == 2,
               "the group column precedes its member columns");
        auto* strip = dynamic_cast<juce::Slider*> (findNamed (launcher, "Group volume"));
        check (strip != nullptr && strip->isVisible() && strip->getParentComponent()->isVisible() && strip->getSliderStyle() == juce::Slider::LinearVertical, "the Session mixer shows a vertical group fader");
        strip->setValue (-6, juce::sendNotificationSync); refresh();
        check (std::abs (state.groups[0].gainDb + 6) < 1e-4, "the group mixer strip edits the group volume");
        session.undo(); refresh();
        check (std::abs (state.groups[0].gainDb) < 1e-4 && std::abs (strip->getValue()) < 1e-3, "undo restores the group volume and its fader");
        launcher.mouseDown (groupEvent (launcher.groupLaunchPoint (0, 0))); launcher.mouseUp (groupEvent (launcher.groupLaunchPoint (0, 0)));
        pumpSyntheticTestDevice (session, 8); refresh();
        check (state.tracks[0].slots[0].playing && state.tracks[1].slots[0].playing, "the group slot launches every member's clip in its row");
        launcher.mouseDown (groupEvent (launcher.groupStopPoint (0))); pumpSyntheticTestDevice (session, 8); refresh();
        check (! state.tracks[0].slots[0].playing && ! state.tracks[1].slots[0].playing, "the group stop button stops its members");
        launcher.mouseDown (groupEvent (launcher.groupFoldPoint (0))); refresh();
        check (state.groups[0].folded && launcher.columnCount() == 1 && launcher.columnOf (0) < 0 && launcher.columnOf (1) < 0,
               "clicking the group's triangle folds its member columns away");
        if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
            if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
            {
                launcher.mouseDown (groupEvent (launcher.groupFoldPoint (0))); refresh();
                juce::PNGImageFormat png; const auto output = directory.getChildFile ("session-group.png");
                if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && png.writeImageToStream (launcher.createComponentSnapshot (launcher.getLocalBounds(), true, 1.0f), *stream))
                    std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
                launcher.mouseDown (groupEvent (launcher.groupFoldPoint (0))); refresh();
            }
        launcher.mouseDown (groupEvent (launcher.groupFoldPoint (0))); refresh();
        check (! state.groups[0].folded && launcher.columnCount() == 3, "clicking again unfolds the members");
        // The Master column follows the last track; its strip edits the master level; sends sit in the strips.
        check (std::abs (launcher.stopAllPoint().x - (3 * LauncherGrid::columnWidth + LauncherGrid::sceneWidth / 2.0f)) < 2, "the Master column follows the last track column");
        auto* masterFader = dynamic_cast<juce::Slider*> (findNamed (launcher, "Session master volume"));
        check (masterFader != nullptr && masterFader->isVisible(), "the Master column has a volume strip");
        masterFader->setValue (-3, juce::sendNotificationSync); refresh();
        check (std::abs (state.masterGainDb + 3) < 1e-3, "the Session master strip edits the master level");
        session.undo(); refresh();
        check (session.addReturnTrack ("Echo").wasOk(), "add a return for the Session sends"); refresh();
        auto* sendA = dynamic_cast<juce::Slider*> (findNamed (launcher, "Send A"));
        check (sendA != nullptr && sendA->isVisible() && launcher.mixerHeight() > LauncherGrid::baseMixerHeight, "track strips gain a send knob for the return");
        sendA->setValue (-6, juce::sendNotificationSync); refresh();
        const auto echo = state.tracks.back().id;
        bool sent = false;
        for (const auto& track : state.tracks) for (const auto& send : track.sends) sent = sent || (send.returnTrackId == echo && std::abs (send.gainDb + 6) < 1e-3);
        check (sent, "a Session send knob edits the real send level");
        auto* sideA = dynamic_cast<juce::Button*> (findNamed (launcher, "Crossfade A"));
        auto* fader = dynamic_cast<juce::Slider*> (findNamed (launcher, "Crossfader"));
        check (sideA != nullptr && sideA->isVisible() && fader != nullptr && fader->isVisible(), "track strips have A/B buttons and the master strip a crossfader");
        sideA->setToggleState (true, juce::dontSendNotification); sideA->onClick(); refresh();
        fader->setValue (1, juce::sendNotificationSync); refresh();
        bool assigned = false; for (const auto& track : state.tracks) assigned = assigned || track.crossfade == Session::CrossfadeAssign::a;
        check (assigned && std::abs (state.crossfader - 1) < 1e-6, "the A button and the crossfader edit the real crossfade");
        auto* trackDelay = dynamic_cast<juce::Slider*> (findNamed (launcher, "Track delay"));
        check (trackDelay != nullptr && trackDelay->isVisible() && trackDelay->getTextFromValue (0) == "0.0 ms", "track strips show their delay in milliseconds");
        trackDelay->setValue (-12.5, juce::sendNotificationSync); refresh();
        bool delayed = false; for (const auto& track : state.tracks) delayed = delayed || std::abs (track.delayMs + 12.5) < 1e-9;
        check (delayed, "the delay field edits the real track delay");
        session.stop(); check (session.newProject().wasOk(), "reset after the group checks"); refresh();
    }
    check (session.importAudioToSlot (audioFixture, 0, 0).wasOk(), "prepare follow-action UI clip"); refresh(); showSessionView(); selectLauncherSlot (0, 0);
    const auto followClipId = selectedSlot()->clip.id;
    {
        openPianoRoll(); refresh();
        auto* box = findNamed (audioClipView, "Launch box");
        auto* mode = dynamic_cast<juce::ComboBox*> (findNamed (audioClipView, "Launch mode"));
        check (audioClipView.isVisible() && box && box->isVisible() && mode && ! midiLaunch.isVisible(), "a Session audio clip shows its Launch box in the audio clip view");
        mode->setSelectedId (4, juce::sendNotificationSync);
        check (selectedSlot()->clip.launch.mode == Session::LaunchMode::repeat, "the audio clip view's Launch box edits the Session clip");
        session.undo(); refresh(); closeClipView(); refresh();
    }
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
    {
        // Action B with a chance: A plays at its percentage and B otherwise.
        clipFollowDialog (followClipId);
        auto dialog = alerts.back(); check (dialog != nullptr && dialog->getComboBoxComponent ("action-b") != nullptr && dialog->getTextEditor ("chance") != nullptr,
                                            "the follow dialog offers action B and its chance");
        dialog->getComboBoxComponent ("action")->setSelectedItemIndex ((int) Session::FollowAction::next, juce::sendNotificationSync);
        dialog->getComboBoxComponent ("action-b")->setSelectedItemIndex ((int) Session::FollowAction::other, juce::sendNotificationSync);
        dialog->getTextEditor ("beats")->setText ("2"); dialog->getTextEditor ("chance")->setText ("30");
        dialog->exitModalState (1); juce::MessageManager::getInstance()->runDispatchLoopUntil (20); refresh();
        const auto follow = selectedSlot()->clip.follow;
        check (follow.action == Session::FollowAction::next && follow.actionB == Session::FollowAction::other && std::abs (follow.chanceA - .3) < 1e-9 && follow.afterBeats == 2,
               "the dialog sets action A, action B and the chance of A");
        clipFollowDialog (followClipId); dialog = alerts.back();
        dialog->getTextEditor ("chance")->setText ("140"); dialog->exitModalState (1); juce::MessageManager::getInstance()->runDispatchLoopUntil (20); refresh();
        check (selectedSlot()->clip.follow == follow, "an out-of-range chance leaves the follow action unchanged");
        session.undo(); refresh();
    }
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
    selectTrack (0); refresh();
    auto* automationRow = canvas.getChildComponent (0);
    auto* automatedGain = dynamic_cast<juce::Slider*> (findNamed (*automationRow, "Track volume"));
    auto* automatedPan = dynamic_cast<juce::Slider*> (findNamed (*automationRow, "Track pan"));
    check (automatedGain && automatedPan && automatedGain->isEnabled() && automatedPan->isEnabled()
        && automatedGain->getMaximum() == 6.0 && automatedGain->getMinimum() == -100.0,
        "manual mixer controls match the native gain range including silence");
    automatedGain->setValue (-100, juce::sendNotificationSync); refresh();
    check (state.tracks[0].gainDb <= -100 && automatedGain->getTextFromValue (-100) == "-inf",
        "manual fader reaches actual silence with a matching readout");
    automatedGain->setValue (0, juce::sendNotificationSync); refresh();
    const auto automationTrackId = state.tracks[0].id;
    check (session.setTrackAutomation (automationTrackId, Session::AutomationTarget::volume,
        { true, {{0, -18}, {4, 0}} }).wasOk(), "prepare a real automation lane for mixer integration"); refresh();
    check (!automatedGain->isEnabled() && automatedPan->isEnabled(), "volume automation protects only its matching manual fader");
    check (session.setTrackAutomation (automationTrackId, Session::AutomationTarget::volume,
        { false, {{0, -18}, {4, 0}} }).wasOk(), "bypass automation without deleting its points"); refresh();
    check (!automatedGain->isEnabled(), "bypassed lane remains protected until its points are cleared");
    check (session.setTrackAutomation (automationTrackId, Session::AutomationTarget::volume, {}).wasOk(),
        "clear automation from the selected mixer"); refresh();
    check (automatedGain->isEnabled(), "clearing the lane restores manual volume editing");
    session.undo(); refresh();
    check (!automatedGain->isEnabled(), "undo clear restores the matching fader protection");
    session.redo(); refresh();
    check (automatedGain->isEnabled(), "redo clear restores manual control");
    menuItemSelected (46, 3);
    check (!automationWindows.empty() && automationWindows.back() != nullptr,
        "View menu opens an automation editor for the selected track");
    auto* automationWindow = automationWindows.back().getComponent();
    check (dynamic_cast<AutomationEditor*> (automationWindow->getContentComponent()) != nullptr
           && !automationWindow->isCurrentlyModal(), "automation editor leaves main transport interactive");
    automationWindow->closeButtonPressed();
    check (automationWindows.back() == nullptr, "automation editor close releases its window safely");
    auto* automationButton = dynamic_cast<juce::Button*> (findNamed (deviceChain, "Track automation"));
    check (automationButton && automationButton->isEnabled(), "device chain exposes enabled automation control");
    auto* automationHit = getComponentAt (getLocalPoint (automationButton, automationButton->getLocalBounds().getCentre()));
    check (automationHit == automationButton || automationButton->isParentOf (automationHit),
        "automation button receives real pointer hits without a covering component");
    automationButton->onClick();
    check (automationWindows.size() == 1 && automationWindows.back() != nullptr,
        "device chain button opens an editor and removes expired window handles");
    automationWindows.back()->closeButtonPressed();
    master->setValue (-9, juce::sendNotificationSync); refresh();
    const Session::AutomationLane masterAutomation { true, {{0, -18}, {4, 0}} };
    check (session.setMasterVolumeAutomation (masterAutomation).wasOk(), "prepare master automation for its output fader"); refresh();
    check (! master->isEnabled() && master->getTooltip().contains ("Master volume has automation"),
        "master automation protects its fader and explains how to restore manual editing");
    const auto protectedMaster = state.masterVolumeAutomation;
    master->setValue (-3, juce::sendNotificationSync); refresh();
    check (state.masterVolumeAutomation == protectedMaster, "a forced manual fader callback cannot rewrite master automation");
    auto bypassedMaster = masterAutomation; bypassedMaster.enabled = false;
    check (session.setMasterVolumeAutomation (bypassedMaster).wasOk(), "bypass master automation with its points retained"); refresh();
    check (! master->isEnabled(), "master fader remains protected while Read is off");
    check (session.setMasterVolumeAutomation ({}).wasOk(), "clear master automation for manual mixing"); refresh();
    check (master->isEnabled() && std::abs (state.masterGainDb + 9) < .001f,
        "Clear restores the retained manual master level and enabled fader");
    session.undo(); refresh(); check (! master->isEnabled(), "undo master Clear restores fader protection");
    session.redo(); refresh(); check (master->isEnabled(), "redo master Clear restores manual control");
    menuItemSelected (47, 3);
    auto* masterWindow = automationWindows.back().getComponent();
    auto* masterDestination = dynamic_cast<juce::ComboBox*> (findNamed (*masterWindow, "automation-track"));
    auto* masterTarget = dynamic_cast<juce::ComboBox*> (findNamed (*masterWindow, "automation-target"));
    check (masterDestination && masterDestination->getText() == "Master" && masterTarget && masterTarget->getNumItems() == 1,
        "Master Volume Automation menu opens an explicit master-only parameter destination");
    masterWindow->closeButtonPressed();
    while (! state.tracks.empty()) { check (session.removeTrack (0).wasOk(), "remove track for empty-project automation navigation"); refresh(); }
    check (automationButton->isEnabled(), "Automation button remains available with no tracks");
    automationButton->onClick();
    auto* emptyDestination = dynamic_cast<juce::ComboBox*> (findNamed (*automationWindows.back(), "automation-track"));
    check (emptyDestination && emptyDestination->getText() == "Master" && emptyDestination->getNumItems() == 1,
        "empty-project Automation button opens Master without a track selection");
    automationWindows.back()->closeButtonPressed();
    menuItemSelected (46, 3);
    emptyDestination = dynamic_cast<juce::ComboBox*> (findNamed (*automationWindows.back(), "automation-track"));
    check (emptyDestination && emptyDestination->getText() == "Master", "empty-project View Automation opens Master");
    automationWindows.back()->closeButtonPressed();
    master->onDragStart(); master->setValue (-3, juce::sendNotificationSync);
    check (session.newProject().wasOk(), "replace project during a master fader drag"); refresh();
    master->setValue (-15, juce::sendNotificationSync); master->onDragEnd(); refresh();
    check (std::abs (state.masterGainDb) < .001f && ! session.canUndo(), "New rejects a retained master fader gesture");
    juce::TemporaryFile masterProject (".halftrack");
    check (session.saveProject (masterProject.getFile()).wasOk(), "save master fader lifecycle fixture");
    master->onDragStart(); master->setValue (-3, juce::sendNotificationSync);
    check (session.loadProject (masterProject.getFile()).wasOk(), "reload project during master fader drag"); refresh();
    master->setValue (-12, juce::sendNotificationSync); master->onDragEnd(); refresh();
    check (std::abs (state.masterGainDb) < .001f && ! session.canUndo(), "Load rejects a retained master fader gesture");
    auto masterLabel = [&]() -> juce::Label*
    {
        for (auto* child : master->getChildren()) if (auto* label = dynamic_cast<juce::Label*> (child)) return label;
        return nullptr;
    };
    for (const bool load : { false, true })
    {
        master->showTextBox(); auto* label = masterLabel();
        check (label && label->getCurrentTextEditor(), "open the actual master fader text editor");
        auto staleText = juce::Component::SafePointer<juce::TextEditor> (label->getCurrentTextEditor());
        staleText->setText ("-21");
        check ((load ? session.loadProject (masterProject.getFile()) : session.newProject()).wasOk(),
            "replace project while a same-level master text edit is pending"); refresh();
        check (staleText == nullptr && ! label->isBeingEdited() && std::abs (state.masterGainDb) < .001f && ! session.canUndo(),
            "project replacement discards stale master text even when its displayed level did not change");
    }

}

int MainComponent::runExportInteractionTests()
{
    try
    {
        juce::TemporaryFile fixture (".wav");
        {
            std::unique_ptr<juce::OutputStream> stream = fixture.getFile().createOutputStream(); juce::WavAudioFormat format;
            auto writer = stream ? format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (16)) : nullptr;
            if (! writer) throw std::runtime_error ("Could not create export UI audio fixture");
            juce::AudioBuffer<float> audio (1, 4800);
            for (int i = 0; i < 4800; ++i) audio.setSample (0, i, .1f * std::sin (static_cast<float> (i) * .031f));
            if (! writer->writeFromAudioSampleBuffer (audio, 0, 4800)) throw std::runtime_error ("Could not write export UI audio fixture");
        }
        Session session (false, makeSyntheticTestDeviceType()); MainComponent component (session);
        component.impl->captureTestErrors = true; component.impl->runExportInteractionChecks (fixture.getFile()); return 0;
    }
    catch (const std::exception& error)
    { std::cerr << "FAIL export UI " << error.what() << std::endl; return 1; }
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
