#include "NativeEffects.h"
#include "Racks.h"
#include <complex>
#include <optional>
#if HALFTRACK_TESTS_ENABLED
#include <iostream>
#include <stdexcept>
#endif
namespace halftrack
{
namespace
{
class NativeEffectEditor final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    NativeEffectEditor (Session& s, juce::String id, bool compactLayout = false, std::function<void (int)> automation = {})
        : session (s), pluginId (std::move (id)), compact (compactLayout), showAutomation (std::move (automation))
    {
        const auto initial = settings();
        kind = initial ? initial->kind : Session::EffectKind::equalizer;
        setName (juce::String (effectName (kind)) + " editor");
        look.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe49a58));
        look.setColour (juce::Slider::trackColourId, juce::Colour (0xffe49a58));
        look.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe9e9e9));
        look.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff515151));
        look.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff3a3a3a));
        if (! compact) setLookAndFeel (&look); // Inline editors inherit the host theme.
        const auto specs = effectParameters (kind);
        for (size_t i = 0; i < specs.size(); ++i)
        {
            const auto& spec = specs[i]; labels[i].setText (spec.label, juce::dontSendNotification);
            labels[i].setColour (juce::Label::textColourId, juce::Colour (0xffbbbbbb)); addAndMakeVisible (labels[i]);
            if (compact) { labels[i].setFont (juce::FontOptions (9.5f)); labels[i].setJustificationType (juce::Justification::centred); labels[i].setMinimumHorizontalScale (.75f); }
            if (spec.choices)
            {
                auto& control = choices[i]; control.setName (spec.key);
                control.addItemList (juce::StringArray::fromTokens (spec.choices, "|", ""), 1);
                control.onChange = [this, i] { edit (i, static_cast<float> (choices[i].getSelectedId()-1)); };
                if (showAutomation) control.addMouseListener (this, false);
                addAndMakeVisible (control);
            }
            else
            {
                auto& control = sliders[i]; control.setName (spec.key); control.setRange (spec.minimum, spec.maximum, spec.integer ? 1.0 : 0.0);
                if (spec.midpoint > spec.minimum && spec.midpoint < spec.maximum && spec.midpoint != 0)
                    control.setSkewFactorFromMidPoint (spec.midpoint);
                control.setSliderStyle (kind == Session::EffectKind::equalizer && ! compact ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag);
                if (compact)
                {
                    control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 13);
                    control.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                    control.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
                }
                else control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, effectParameters (kind).size() > 16 ? 86 : 100, 21);
                const auto unit = juce::String (spec.unit);
                const bool percent = effectShowsPercent (spec);
                if (showAutomation) control.addMouseListener (this, false);
                control.setTextValueSuffix (percent ? juce::String() : unit); control.setDoubleClickReturnValue (true, spec.initial);
                if (percent)
                {
                    control.textFromValueFunction = [] (double v) { return juce::String (v*100, 1) + "%"; };
                    control.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue()/100; };
                }
                else control.setNumDecimalPlacesToDisplay (spec.integer || unit.contains ("Hz") || unit.contains ("deg") ? 0 : unit.contains ("dB") || unit.contains ("ms") ? 1 : 2);
                control.updateText(); // The text box predates the formatter; refresh unchanged initial values.
                control.onDragStart = [this] { session.beginEditGesture ("Adjust " + juce::String (effectName (kind))); dragging = true; };
                control.onDragEnd = [this] { finishGesture(); };
                control.onValueChange = [this, i] { edit (i, static_cast<float> (sliders[i].getValue())); };
                addAndMakeVisible (control);
            }
        }
        error.setColour (juce::Label::textColourId, juce::Colour (0xfff4ab84)); addAndMakeVisible (error);
        const auto rows = static_cast<int> ((specs.size() + columns() - 1) / static_cast<size_t> (columns()));
        session.addChangeListener (this);
        if (compact) { setSize (8 + compactColumns() * compactCellWidth, compactHeight); error.setVisible (false); }
        else setSize (kind == Session::EffectKind::equalizer ? 1000 : dense() ? 48 + columns() * 120 : 660,
            kind == Session::EffectKind::equalizer ? 640 : std::max (400, 96 + rows * (dense() ? 112 : 145)));
        synchronise(); startTimerHz (15);
    }
    ~NativeEffectEditor() override
    { stopTimer(); session.removeChangeListener (this); finishGesture(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override
    {
        if (compact) { g.fillAll (juce::Colour (0xff303030)); return; }
        g.fillAll (juce::Colour (0xff2b2b2b));
        g.setColour (juce::Colour (0xffe49a58)); g.fillRect (24, 22, 5, 18);
        g.setColour (juce::Colour (0xffe9e9e9)); g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (juce::String (effectName (kind)).toUpperCase(), 42, 12, getWidth()-66, 38, juce::Justification::centredLeft);
        if (kind == Session::EffectKind::equalizer)
        {
            const juce::Rectangle<float> plot (24, 62, 952, 132);
            g.setColour (juce::Colour (0xff242424)); g.fillRect (plot);
            g.setColour (juce::Colour (0xff515151));
            for (int db : { -24, -12, 0, 12, 24 }) { const auto y = plot.getCentreY()-db*plot.getHeight()/48; g.drawHorizontalLine ((int)y, plot.getX(), plot.getRight()); }
            for (double hz : { 20, 100, 1000, 10000, 20000 })
            { const auto x = plot.getX()+std::log10(hz/20)*plot.getWidth()/3; g.drawVerticalLine ((int)x, plot.getY(), plot.getBottom()); }
            if (auto value = settings())
            {
                juce::Path path;
                // Display is the exact settled transfer function at the selected output rate.
                auto* device = session.audioDeviceManager().getCurrentAudioDevice();
                const auto rate = device ? device->getCurrentSampleRate() : 48000.0;
                for (int x = 0; x <= 952; ++x)
                {
                    const auto frequency = 20*std::pow (1000.0, x/952.0);
                    const auto z = std::polar (1.0, -2*juce::MathConstants<double>::pi*frequency/rate);
                    std::complex<double> response { 1, 0 };
                    for (size_t band = 0; band < 8; ++band)
                    {
                        const auto& v = value->values;
                        const auto c = NativeEffectDSP::eqCoefficients ((int)v[4*band], v[4*band+1], v[4*band+2], v[4*band+3], rate);
                        response *= (c[0]+c[1]*z+c[2]*z*z)/(1.0+c[3]*z+c[4]*z*z);
                    }
                    response = (1.0-value->values[33]) + static_cast<double> (value->values[33])*std::pow (10.0,value->values[32]/20.0)*response;
                    const auto db = 20*std::log10 (std::max (1e-10, std::abs (response)));
                    const auto y = plot.getCentreY()-std::clamp (db,-24.0,24.0)*plot.getHeight()/48;
                    if (x == 0) path.startNewSubPath (plot.getX(), (float)y); else path.lineTo (plot.getX()+x,(float)y);
                }
                g.setColour (juce::Colour (0xffe49a58)); g.strokePath (path, juce::PathStrokeType (1.8f));
            }
            g.setFont (juce::FontOptions (11.0f)); g.setColour (juce::Colour (0xffbbbbbb));
            for (double hz : { 20, 100, 1000, 10000, 20000 })
            {
                const auto x = 24 + std::log10 (hz/20) * 952/3;
                const auto text = hz >= 1000 ? juce::String(hz/1000,0)+" kHz" : juce::String(hz,0)+" Hz";
                g.drawText (text, std::clamp ((int)x-30, 24, 916), 195, 60, 20, juce::Justification::centred);
            }
        }
    }
    void resized() override
    {
        const auto specs = effectParameters (kind);
        if (compact)
        {
            // Column-major grid of three rows, the density of an inline device.
            for (size_t i = 0; i < specs.size(); ++i)
            {
                auto area = juce::Rectangle<int> (4 + static_cast<int> (i / 3) * compactCellWidth, 2 + static_cast<int> (i % 3) * compactRowHeight, compactCellWidth - 2, compactRowHeight - 2);
                labels[i].setBounds (area.removeFromTop (13));
                if (specs[i].choices) choices[i].setBounds (area.withSizeKeepingCentre (area.getWidth(), 20));
                else sliders[i].setBounds (area);
            }
            return;
        }
        for (size_t i = 0; i < specs.size(); ++i)
        {
            juce::Rectangle<int> area;
            if (kind == Session::EffectKind::equalizer && i < 32)
                area = { 24+static_cast<int>(i/4)*120, 224+static_cast<int>(i%4)*84, 112, 82 };
            else if (kind == Session::EffectKind::equalizer)
                area = { 24+static_cast<int>(i-32)*160, 560, 148, 62 };
            else if (dense())
                area = { 24 + static_cast<int> (i % 8) * 120, 62 + static_cast<int> (i / 8) * 112, 112, 106 };
            else area = { 24+static_cast<int>(i%4)*156, 66+static_cast<int>(i/4)*145, 144, 136 };
            labels[i].setBounds (area.removeFromTop (20));
            if (specs[i].choices) choices[i].setBounds (area.withHeight (28));
            else sliders[i].setBounds (area);
        }
        error.setBounds (kind == Session::EffectKind::equalizer ? 360 : 20, getHeight()-25,
            kind == Session::EffectKind::equalizer ? 610 : getWidth()-40, 22);
    }
    void synchronise()
    {
        auto value = settings(); const bool enabled = value.has_value() && ! session.isExporting();
        const auto specs = effectParameters (kind);
        // A control a rack's macro drives shows the macro's value and cannot be edited directly.
        std::array<bool, maximumControls> mapped {};
        if (value)
        {
            const auto snapshot = session.snapshot();
            if (const auto where = racks::locate (snapshot, pluginId); where.rack != nullptr)
                for (size_t i = 0; i < specs.size(); ++i)
                    if (const auto driven = racks::macroValue (*where.rack, pluginId, (int) i))
                    {
                        mapped[i] = true; value->values[i] = *driven;
                        const auto tip = "Mapped to " + racks::macroName (*where.rack, racks::macroOf (*where.rack, pluginId, (int) i));
                        sliders[i].setTooltip (tip); choices[i].setTooltip (tip);
                    }
        }
        for (size_t i = 0; i < specs.size(); ++i)
        {
            sliders[i].setEnabled (enabled && ! mapped[i]); choices[i].setEnabled (enabled && ! mapped[i]);
            if (! mapped[i]) { sliders[i].setTooltip ({}); choices[i].setTooltip ({}); }
            if (value) { sliders[i].setValue (value->values[i], juce::dontSendNotification); choices[i].setSelectedId ((int)value->values[i]+1, juce::dontSendNotification); }
        }
        if (value && kind == Session::EffectKind::equalizer)
            for (size_t band = 0; band < 8; ++band)
            {
                const auto type = value->values[4*band];
                sliders[4*band+1].setEnabled (enabled && type != 0);
                sliders[4*band+2].setEnabled (enabled && type >= 1 && type <= 3);
                sliders[4*band+3].setEnabled (enabled && type != 0);
            }
        if (value && kind == Session::EffectKind::delay)
        {
            sliders[0].setEnabled (enabled && value->values[8] == 0);
            sliders[1].setEnabled (enabled && value->values[7] == 0 && value->values[8] == 0);
            choices[9].setEnabled (enabled && value->values[8] != 0);
            choices[10].setEnabled (enabled && value->values[7] == 0 && value->values[8] != 0);
            sliders[2].setEnabled (enabled && value->values[6] == 0);
            sliders[5].setEnabled (enabled && value->values[6] == 0);
        }
        if (value && (kind == Session::EffectKind::autoFilter || kind == Session::EffectKind::autoPan))
        {
            const auto timing = static_cast<size_t> (effectParameterIndex (kind, kind == Session::EffectKind::autoPan ? "sync" : "lfoSync"));
            const bool synced = value->values[timing] != 0;
            sliders[timing - 1].setEnabled (enabled && ! synced); choices[timing + 1].setEnabled (enabled && synced);
        }
        if (value)
        {
            const auto& v = value->values;
            if (kind == Session::EffectKind::arpeggiator) sliders[6].setEnabled (enabled && v[5] != 0);
            if (kind == Session::EffectKind::velocity) { sliders[6].setEnabled (enabled && v[0] == 2); for (size_t i = 3; i <= 5; ++i) sliders[i].setEnabled (enabled && v[0] != 2); }
            if (kind == Session::EffectKind::noteLength) { sliders[2].setEnabled (enabled && v[1] == 0); choices[3].setEnabled (enabled && v[1] != 0); }
        }
        repaint();
    }
    // Right-clicking a control offers its automation lane.
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! showAutomation || ! event.mods.isPopupMenu()) return;
        for (size_t i = 0; i < effectParameters (kind).size(); ++i)
            if (event.eventComponent == &sliders[i] || event.eventComponent == &choices[i])
            {
                juce::PopupMenu menu; menu.addItem (1, "Show Automation for " + juce::String (effectParameters (kind)[i].label));
                // Inside a rack, a control can be mapped to one of the rack's macros, or unmapped.
                juce::String rackId; int mappedTo = -1;
                {
                    const auto snapshot = session.snapshot();
                    if (const auto where = racks::locate (snapshot, pluginId); where.rack != nullptr)
                    {
                        rackId = where.rack->id; mappedTo = racks::macroOf (*where.rack, pluginId, (int) i);
                        juce::PopupMenu macros;
                        for (int m = 0; m < Session::rackMacroCount; ++m) macros.addItem (100 + m, racks::macroName (*where.rack, m), m != mappedTo);
                        menu.addSeparator(); menu.addSubMenu ("Map to Macro", macros);
                        if (mappedTo >= 0) menu.addItem (2, "Remove Mapping from " + racks::macroName (*where.rack, mappedTo));
                    }
                }
                auto safe = juce::Component::SafePointer<NativeEffectEditor> (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (event.eventComponent), [safe, i, rackId, mappedTo] (int choice)
                {
                    if (safe == nullptr) return;
                    if (choice == 1) safe->requestAutomation ((int) i);
                    else if (choice == 2) safe->mapToMacro ((int) i, rackId, -1, mappedTo);
                    else if (choice >= 100) safe->mapToMacro ((int) i, rackId, choice - 100, mappedTo);
                });
                return;
            }
    }
    void requestAutomation (int parameter) { finishGesture(); if (showAutomation) showAutomation (parameter); }
    // Maps a control to a macro over its whole range (macro -1 removes the mapping from `previous`).
    void mapToMacro (int parameter, const juce::String& rackId, int macro, int previous)
    {
        finishGesture();
        const auto& field = effectParameters (kind)[(size_t) parameter];
        const auto result = macro < 0 ? session.removeRackMacroMapping (rackId, previous, pluginId, parameter)
                                      : session.setRackMacroMapping (rackId, { macro, pluginId, parameter, field.minimum, field.maximum });
        error.setText (result.failed() ? result.getErrorMessage() : juce::String(), juce::dontSendNotification);
        synchronise();
    }
private:
    // Instruments and other large devices use a compact eight-column grid.
    bool dense() const noexcept { return effectParameters (kind).size() > 16; }
    static constexpr int compactCellWidth = 60, compactRowHeight = 62, compactHeight = 3 * compactRowHeight + 4;
    int compactColumns() const noexcept { return static_cast<int> ((effectParameters (kind).size() + 2) / 3); }
    int columns() const noexcept { return dense() ? 8 : 4; }
    std::optional<Session::EffectSettings> settings() const
    {
        const auto snapshot = session.snapshot();
        if (const auto where = racks::locate (snapshot, pluginId); where.device != nullptr && where.device->isNativeEffect) return where.device->effect;
        return {};
    }
    void edit (size_t index, float v)
    {
        if (auto value = settings())
        {
            value->values[index] = v; const auto result = session.setNativeEffectSettings (pluginId, *value);
            error.setText (result.failed() ? result.getErrorMessage() : juce::String(), juce::dontSendNotification);
        }
        synchronise();
    }
    void finishGesture() { if (dragging) { dragging = false; session.endEditGesture(); } }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { synchronise(); }
    void timerCallback() override { synchronise(); }
    Session& session;
    juce::String pluginId;
    bool compact = false;
    std::function<void (int)> showAutomation;
    Session::EffectKind kind;
    juce::LookAndFeel_V4 look;
    static constexpr size_t maximumControls = std::tuple_size_v<decltype (Session::EffectSettings::values)>;
    std::array<juce::Slider, maximumControls> sliders;
    std::array<juce::ComboBox, maximumControls> choices;
    std::array<juce::Label, maximumControls> labels;
    juce::Label error;
    bool dragging = false;
};
}
std::unique_ptr<juce::Component> makeNativeEffectEditor (Session& session, const juce::String& id, bool compact, std::function<void (int)> showAutomation)
{ return std::make_unique<NativeEffectEditor> (session, id, compact, std::move (showAutomation)); }
#if HALFTRACK_TESTS_ENABLED
int runNativeEffectEditorTests (const juce::File& root)
{
    auto check = [] (bool v, const char* text) { if (! v) throw std::runtime_error (text); std::cout << "PASS effect editor: " << text << std::endl; };
    Session s (false); check (s.addMidiTrack().wasOk(), "create editor track");
    const int track = (int) s.snapshot().tracks.size() - 1; // New projects begin with two audio tracks.
    for (int type = 0; type < Session::deviceKindCount; ++type)
    {
        const auto kind = static_cast<Session::EffectKind> (type);
        if (Session::isRackKind (kind)) continue;
        const bool instrument = effectIsInstrument (kind);
        const auto before = s.snapshot().tracks[(size_t) track].plugins;
        if (instrument) check (s.setTrackInstrument (s.snapshot().tracks[(size_t) track].id, kind).wasOk(), "choose editor instrument");
        else check (s.addNativeEffect (track, kind).wasOk(), "add editor device");
        const auto devices = s.snapshot().tracks[(size_t) track].plugins; int position = 0;
        while (position < (int) before.size() && devices[(size_t) position].id == before[(size_t) position].id) ++position;
        const auto info = instrument ? s.snapshot().tracks[(size_t) track].instrument : devices[(size_t) position];
        check (info.isNativeEffect && info.effect.kind == kind, "device appears in its chain position");
        int requested = -1;
        auto editor = makeNativeEffectEditor (s, info.id, false, [&requested] (int parameter) { requested = parameter; });
        dynamic_cast<NativeEffectEditor&> (*editor).requestAutomation ((int) effectParameters (kind).size() - 1);
        check (requested == (int) effectParameters (kind).size() - 1, "a control's automation request reaches the host with its index");
        juce::Slider* slider = nullptr;
        const auto specs = effectParameters (kind); size_t index = 0;
        while (index < specs.size() && specs[index].choices) ++index;
        for (auto* child : editor->getChildren()) if (auto* c = dynamic_cast<juce::Slider*> (child); c && c->getName() == specs[index].key) slider = c;
        check (slider != nullptr, "production control exists");
        slider->onDragStart(); slider->setValue (specs[index].minimum, juce::sendNotificationSync); slider->setValue (specs[index].maximum, juce::sendNotificationSync); slider->onDragEnd();
        const auto device = [&]
        {
            const auto snapshot = s.snapshot(); // Keep the temporary alive across the loop.
            if (snapshot.tracks[(size_t) track].instrument.id == info.id) return snapshot.tracks[(size_t) track].instrument;
            for (const auto& p : snapshot.tracks[(size_t) track].plugins) if (p.id == info.id) return p;
            return Session::PluginInfo {};
        };
        check (device().effect.values[index] == specs[index].maximum, "control changes actual engine value");
        s.undo(); check (device().effect == info.effect, "one undo restores full gesture"); s.redo();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds()); const auto file = root.getChildFile ("effect-editor-"+juce::String(type)+".png");
        juce::FileOutputStream output (file); juce::PNGImageFormat png; check (png.writeImageToStream (image, output), "write original editor snapshot");
        const auto open = [&] { return instrument ? s.showInstrumentEditor (track) : s.showPluginEditor (track, position); };
        check (open().wasOk(), "native editor window opens");
        check ((instrument ? s.setTrackInstrument (s.snapshot().tracks[(size_t) track].id, std::nullopt) : s.removePlugin (track, position)).wasOk(), "remove device with open window");
        static_cast<NativeEffectEditor*> (editor.get())->synchronise(); check (! slider->isEnabled(), "removed device disables retained editor");
        s.undo(); check (open().wasOk(), "undo restores reopenable native editor"); s.closePluginEditors();
        if (instrument) check (s.setTrackInstrument (s.snapshot().tracks[(size_t) track].id, std::nullopt).wasOk(), "restore the built-in synth before the next instrument");
    }
    check (s.newProject().wasOk(), "replace project after editor lifecycle checks"); return 0;
}
#endif
}
