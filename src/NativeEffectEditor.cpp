#include "NativeEffects.h"
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
    NativeEffectEditor (Session& s, juce::String id) : session (s), pluginId (std::move (id))
    {
        const auto initial = settings();
        kind = initial ? initial->kind : Session::EffectKind::equalizer;
        setName (juce::String (effectName (kind)) + " editor");
        look.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe49a58));
        look.setColour (juce::Slider::trackColourId, juce::Colour (0xffe49a58));
        look.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffeee8df));
        look.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff4c5355));
        look.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff353b3e));
        setLookAndFeel (&look);
        const auto specs = effectParameters (kind);
        for (size_t i = 0; i < specs.size(); ++i)
        {
            const auto& spec = specs[i]; labels[i].setText (spec.label, juce::dontSendNotification);
            labels[i].setColour (juce::Label::textColourId, juce::Colour (0xffbbbcb8)); addAndMakeVisible (labels[i]);
            if (spec.choices)
            {
                auto& control = choices[i]; control.setName (spec.key);
                control.addItemList (juce::StringArray::fromTokens (spec.choices, "|", ""), 1);
                control.onChange = [this, i] { edit (i, static_cast<float> (choices[i].getSelectedId()-1)); };
                addAndMakeVisible (control);
            }
            else
            {
                auto& control = sliders[i]; control.setName (spec.key); control.setRange (spec.minimum, spec.maximum);
                if (spec.midpoint > spec.minimum && spec.midpoint < spec.maximum && spec.midpoint != 0)
                    control.setSkewFactorFromMidPoint (spec.midpoint);
                control.setSliderStyle (kind == Session::EffectKind::equalizer ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag);
                control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 23);
                const auto unit = juce::String (spec.unit);
                const bool percent = unit == "%" || juce::String (spec.key) == "mix" || juce::String (spec.key) == "feedback";
                control.setTextValueSuffix (percent ? juce::String() : unit); control.setDoubleClickReturnValue (true, spec.initial);
                if (percent)
                {
                    control.textFromValueFunction = [] (double v) { return juce::String (v*100, 1) + "%"; };
                    control.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue()/100; };
                }
                else control.setNumDecimalPlacesToDisplay (unit.contains ("Hz") || unit.contains ("deg") ? 0 : unit.contains ("dB") || unit.contains ("ms") ? 1 : 2);
                control.updateText(); // The text box predates the formatter; refresh unchanged initial values.
                control.onDragStart = [this] { session.beginEditGesture ("Adjust " + juce::String (effectName (kind))); dragging = true; };
                control.onDragEnd = [this] { finishGesture(); };
                control.onValueChange = [this, i] { edit (i, static_cast<float> (sliders[i].getValue())); };
                addAndMakeVisible (control);
            }
        }
        error.setColour (juce::Label::textColourId, juce::Colour (0xfff4ab84)); addAndMakeVisible (error);
        const auto rows = static_cast<int> ((specs.size() + 3) / 4);
        session.addChangeListener (this); setSize (kind == Session::EffectKind::equalizer ? 1000 : 660,
            kind == Session::EffectKind::equalizer ? 640 : std::max (400, 96 + rows * 145)); synchronise(); startTimerHz (15);
    }
    ~NativeEffectEditor() override
    { stopTimer(); session.removeChangeListener (this); finishGesture(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff272c2f));
        g.setColour (juce::Colour (0xffe49a58)); g.fillRect (24, 22, 5, 18);
        g.setColour (juce::Colour (0xffeee8df)); g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (juce::String (effectName (kind)).toUpperCase(), 42, 12, getWidth()-66, 38, juce::Justification::centredLeft);
        if (kind == Session::EffectKind::equalizer)
        {
            const juce::Rectangle<float> plot (24, 62, 952, 132);
            g.setColour (juce::Colour (0xff202528)); g.fillRect (plot);
            g.setColour (juce::Colour (0xff4c5355));
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
            g.setFont (juce::FontOptions (11.0f)); g.setColour (juce::Colour (0xffbbbcb8));
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
        for (size_t i = 0; i < specs.size(); ++i)
        {
            juce::Rectangle<int> area;
            if (kind == Session::EffectKind::equalizer && i < 32)
                area = { 24+static_cast<int>(i/4)*120, 224+static_cast<int>(i%4)*84, 112, 82 };
            else if (kind == Session::EffectKind::equalizer)
                area = { 24+static_cast<int>(i-32)*160, 560, 148, 62 };
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
        const auto value = settings(); const bool enabled = value.has_value() && ! session.isExporting();
        const auto specs = effectParameters (kind);
        for (size_t i = 0; i < specs.size(); ++i)
        {
            sliders[i].setEnabled (enabled); choices[i].setEnabled (enabled);
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
        repaint();
    }
private:
    std::optional<Session::EffectSettings> settings() const
    {
        for (const auto& track : session.snapshot().tracks) for (const auto& p : track.plugins)
            if (p.id == pluginId && p.isNativeEffect) return p.effect;
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
    Session::EffectKind kind;
    juce::LookAndFeel_V4 look;
    std::array<juce::Slider, 34> sliders;
    std::array<juce::ComboBox, 34> choices;
    std::array<juce::Label, 34> labels;
    juce::Label error;
    bool dragging = false;
};
}
std::unique_ptr<juce::Component> makeNativeEffectEditor (Session& session, const juce::String& id)
{ return std::make_unique<NativeEffectEditor> (session, id); }
#if HALFTRACK_TESTS_ENABLED
int runNativeEffectEditorTests (const juce::File& root)
{
    auto check = [] (bool v, const char* text) { if (! v) throw std::runtime_error (text); std::cout << "PASS effect editor: " << text << std::endl; };
    Session s (false); check (s.addAudioTrack().wasOk(), "create editor track");
    for (int type = 0; type < Session::effectKindCount; ++type)
    {
        const auto kind = static_cast<Session::EffectKind> (type); check (s.addNativeEffect (0, kind).wasOk(), "add editor device");
        const auto info = s.snapshot().tracks[0].plugins.back(); auto editor = makeNativeEffectEditor (s, info.id);
        juce::Slider* slider = nullptr;
        const auto specs = effectParameters (kind); size_t index = 0;
        while (index < specs.size() && specs[index].choices) ++index;
        for (auto* child : editor->getChildren()) if (auto* c = dynamic_cast<juce::Slider*> (child); c && c->getName() == specs[index].key) slider = c;
        check (slider != nullptr, "production control exists");
        slider->onDragStart(); slider->setValue (specs[index].minimum, juce::sendNotificationSync); slider->setValue (specs[index].maximum, juce::sendNotificationSync); slider->onDragEnd();
        check (s.snapshot().tracks[0].plugins.back().effect.values[index] == specs[index].maximum, "control changes actual engine value");
        s.undo(); check (s.snapshot().tracks[0].plugins.back().effect == info.effect, "one undo restores full gesture"); s.redo();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds()); const auto file = root.getChildFile ("effect-editor-"+juce::String(type)+".png");
        juce::FileOutputStream output (file); juce::PNGImageFormat png; check (png.writeImageToStream (image, output), "write original editor snapshot");
        check (s.showPluginEditor (0,type).wasOk(), "native editor window opens");
        check (s.removePlugin (0,type).wasOk(), "remove device with open window");
        static_cast<NativeEffectEditor*> (editor.get())->synchronise(); check (! slider->isEnabled(), "removed device disables retained editor");
        s.undo(); check (s.showPluginEditor (0,type).wasOk(), "undo restores reopenable native editor"); s.closePluginEditors();
    }
    check (s.newProject().wasOk(), "replace project after editor lifecycle checks"); return 0;
}
#endif
}
