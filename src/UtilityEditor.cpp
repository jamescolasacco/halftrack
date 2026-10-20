#include "UtilityEditor.h"
#include <array>
#include <optional>
#if HALFTRACK_TESTS_ENABLED
#include <iostream>
#include <stdexcept>
#endif

namespace halftrack
{
namespace
{
class UtilityEditor final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    UtilityEditor (Session& s, juce::String id) : session (s), pluginId (std::move (id))
    {
        setName ("Channel utility editor");
        look.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe49a58));
        look.setColour (juce::Slider::trackColourId, juce::Colour (0xffe49a58));
        look.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe5e5e5));
        look.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff515151));
        look.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffe5e5e5));
        look.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff3a3a3a));
        look.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe5e5e5));
        setLookAndFeel (&look);
        channels.setName ("Channel mode"); channels.addItemList ({ "Stereo", "Left", "Right", "Swap" }, 1);
        stereoMode.setName ("Stereo control mode"); stereoMode.addItemList ({ "Width", "Mid / Side" }, 1);
        addAndMakeVisible (channels); addAndMakeVisible (stereoMode);
        channels.onChange = [this] { edit ([this] (auto& v) { v.channel = static_cast<Session::UtilitySettings::Channel> (channels.getSelectedId() - 1); }); };
        stereoMode.onChange = [this] { edit ([this] (auto& v) { v.midSideMode = stereoMode.getSelectedId() == 2; }); };
        const char* names[] { "Gain", "Balance", "Width", "Mid / Side", "Bass frequency" };
        const double minimums[] { -100, -1, 0, -1, 50 }, maximums[] { 35, 1, 4, 1, 500 };
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            auto& slider = sliders[i]; slider.setName (names[i]);
            slider.setRange (minimums[i], maximums[i], i == 0 ? 0.1 : i == 4 ? 1.0 : 0.01);
            slider.setSliderStyle (i < 2 ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, i < 2 ? 82 : 120, 23);
            slider.onDragStart = [this] { session.beginEditGesture ("Adjust channel utility"); dragging = true; };
            slider.onDragEnd = [this] { finishGesture(); };
            slider.onValueChange = [this, i] { edit ([this, i] (auto& v)
            {
                const auto value = static_cast<float> (sliders[i].getValue());
                switch (i) { case 0: v.gainDb = value; break; case 1: v.balance = value; break;
                    case 2: v.width = value; break; case 3: v.midSide = value; break; case 4: v.bassFrequency = value; break; }
            }); };
            addAndMakeVisible (slider);
        }
        sliders[0].setSkewFactorFromMidPoint (0);
        sliders[4].setSkewFactorFromMidPoint (150);
        sliders[0].textFromValueFunction = [] (double v) { return v <= -100 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
        sliders[0].valueFromTextFunction = [] (const juce::String& t) { return t.containsIgnoreCase ("inf") ? -100.0 : t.getDoubleValue(); };
        sliders[1].textFromValueFunction = [] (double v) { return v == 0 ? juce::String ("Center") : juce::String (std::abs (v) * 100, 0) + (v < 0 ? " L" : " R"); };
        sliders[1].valueFromTextFunction = [] (const juce::String& t) { return t.containsIgnoreCase ("center") ? 0.0 : t.getDoubleValue() * (t.containsIgnoreCase ("L") ? -0.01 : 0.01); };
        sliders[2].textFromValueFunction = [] (double v) { return juce::String (v * 100, 0) + "%"; };
        sliders[2].valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100; };
        sliders[3].textFromValueFunction = [] (double v) { return v == 0 ? juce::String ("Stereo") : juce::String (std::abs (v) * 100, 0) + (v < 0 ? " M" : " S"); };
        sliders[3].valueFromTextFunction = [] (const juce::String& t) { return t.containsIgnoreCase ("stereo") ? 0.0 : t.getDoubleValue() * (t.containsIgnoreCase ("M") ? -0.01 : 0.01); };
        sliders[4].setTextValueSuffix (" Hz");
        for (auto& slider : sliders) slider.updateText();
        const char* toggleNames[] { "Invert L", "Invert R", "Mono", "Bass mono", "Audition bass", "Mute", "DC filter" };
        for (size_t i = 0; i < toggles.size(); ++i)
        {
            toggles[i].setButtonText (toggleNames[i]); toggles[i].setName (toggleNames[i]); addAndMakeVisible (toggles[i]);
            toggles[i].onClick = [this, i] { edit ([this, i] (auto& v)
            {
                const auto value = toggles[i].getToggleState();
                switch (i) { case 0: v.invertLeft = value; break; case 1: v.invertRight = value; break; case 2: v.mono = value; break;
                    case 3: v.bassMono = value; break; case 4: v.bassAudition = value; break; case 5: v.mute = value; break; case 6: v.dcFilter = value; break; }
            }); };
        }
        error.setColour (juce::Label::textColourId, juce::Colour (0xffecae80)); addAndMakeVisible (error);
        sliders[2].setTooltip ("100% preserves stereo width. 0% sums to mono.");
        sliders[3].setTooltip ("M isolates the center; S isolates the difference between channels.");
        toggles[3].setTooltip ("Reduce stereo difference below the selected bass frequency.");
        toggles[4].setTooltip ("Hear only the mono bass while Bass mono is enabled.");
        session.addChangeListener (this); setSize (640, 390); synchronise(); startTimerHz (15);
    }
    ~UtilityEditor() override
    { stopTimer(); session.removeChangeListener (this); finishGesture(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff2b2b2b));
        g.setColour (juce::Colour (0xffe49a58)); g.fillRect (24, 22, 5, 17);
        g.setColour (juce::Colour (0xffe5e5e5)); g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("CHANNEL UTILITY", 40, 15, 350, 32, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (10.0f)); g.setColour (juce::Colour (0xffaeaeae));
        g.drawText ("INPUT", 24, 58, 160, 20, juce::Justification::centredLeft);
        g.drawText ("STEREO", 236, 58, 160, 20, juce::Justification::centredLeft);
        g.drawText ("OUTPUT", 450, 58, 160, 20, juce::Justification::centredLeft);
        g.drawText ("Gain", 450, 109, 82, 20, juce::Justification::centred);
        g.drawText ("Balance", 548, 109, 82, 20, juce::Justification::centred);
        g.drawText ("BASS FREQUENCY", 24, 278, 330, 20, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff4b4b4b)); g.drawHorizontalLine (228, 24, 616);
        g.drawVerticalLine (218, 60, 214); g.drawVerticalLine (432, 60, 214);
    }
    void resized() override
    {
        channels.setBounds (24, 87, 174, 28); stereoMode.setBounds (236, 87, 176, 28);
        toggles[0].setBounds (22, 130, 92, 26); toggles[1].setBounds (112, 130, 98, 26);
        toggles[2].setBounds (22, 175, 170, 26);
        sliders[0].setBounds (450, 132, 82, 82); sliders[1].setBounds (548, 132, 82, 82);
        sliders[2].setBounds (236, 132, 176, 72); sliders[3].setBounds (236, 132, 176, 72);
        toggles[3].setBounds (22, 242, 132, 27); toggles[4].setBounds (172, 242, 152, 27);
        toggles[5].setBounds (544, 242, 86, 27); toggles[6].setBounds (438, 242, 105, 27);
        sliders[4].setBounds (24, 302, 388, 48); error.setBounds (20, 359, 606, 22);
    }
    void synchronise()
    {
        const auto value = settings();
        const bool enabled = value.has_value() && ! session.isExporting();
        channels.setEnabled (enabled); stereoMode.setEnabled (enabled);
        for (auto& toggle : toggles) toggle.setEnabled (enabled);
        for (auto& slider : sliders) slider.setEnabled (enabled);
        if (! value) return;
        const auto& v = *value;
        channels.setSelectedId (static_cast<int> (v.channel) + 1, juce::dontSendNotification);
        stereoMode.setSelectedId (v.midSideMode ? 2 : 1, juce::dontSendNotification);
        const float values[] { v.gainDb, v.balance, v.width, v.midSide, v.bassFrequency };
        for (size_t i = 0; i < sliders.size(); ++i) sliders[i].setValue (values[i], juce::dontSendNotification);
        const bool states[] { v.invertLeft, v.invertRight, v.mono, v.bassMono, v.bassAudition, v.mute, v.dcFilter };
        for (size_t i = 0; i < toggles.size(); ++i) toggles[i].setToggleState (states[i], juce::dontSendNotification);
        const bool stereo = v.channel == Session::UtilitySettings::Channel::stereo || v.channel == Session::UtilitySettings::Channel::swap;
        stereoMode.setEnabled (enabled && stereo);
        sliders[2].setVisible (! v.midSideMode); sliders[3].setVisible (v.midSideMode);
        sliders[2].setEnabled (enabled && stereo); sliders[3].setEnabled (enabled && stereo);
        toggles[4].setEnabled (enabled && v.bassMono); sliders[4].setEnabled (enabled && v.bassMono);
    }
private:
    std::optional<Session::UtilitySettings> settings() const
    {
        const auto snapshot = session.snapshot();
        if (const auto* plugin = Session::findDevice (snapshot, pluginId); plugin && plugin->isUtility) return plugin->utility;
        return {};
    }
    template <typename Edit> void edit (Edit&& update)
    {
        if (auto value = settings())
        {
            update (*value); const auto result = session.setUtilitySettings (pluginId, *value);
            error.setText (result.failed() ? result.getErrorMessage() : juce::String(), juce::dontSendNotification);
        }
        synchronise();
    }
    void finishGesture() { if (dragging) { dragging = false; session.endEditGesture(); } }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { synchronise(); }
    void timerCallback() override { synchronise(); }
    juce::LookAndFeel_V4 look;
    Session& session;
    juce::String pluginId;
    juce::ComboBox channels, stereoMode;
    std::array<juce::Slider, 5> sliders;
    std::array<juce::ToggleButton, 7> toggles;
    juce::Label error;
    bool dragging = false;
};
}
std::unique_ptr<juce::Component> makeUtilityEditor (Session& session, const juce::String& id)
{ return std::make_unique<UtilityEditor> (session, id); }
#if HALFTRACK_TESTS_ENABLED
int runUtilityEditorTests (const juce::File& directory)
{
    auto check = [] (bool condition, const char* message)
    { if (! condition) throw std::runtime_error (message); std::cout << "PASS utility UI: " << message << std::endl; };
    Session session (false); check (session.addUtility (0).wasOk(), "create a utility for production editor handlers");
    const auto id = session.snapshot().tracks[0].plugins[0].id;
    auto editor = makeUtilityEditor (session, id);
    auto child = [&] (const juce::String& name) -> juce::Component*
    { for (auto* c : editor->getChildren()) if (c->getName() == name) return c; throw std::runtime_error ("Missing utility control"); };
    auto value = [&] { return session.snapshot().tracks[0].plugins[0].utility; };
    auto* gain = dynamic_cast<juce::Slider*> (child ("Gain")); auto* width = dynamic_cast<juce::Slider*> (child ("Width"));
    auto* channel = dynamic_cast<juce::ComboBox*> (child ("Channel mode"));
    auto* mode = dynamic_cast<juce::ComboBox*> (child ("Stereo control mode"));
    auto* midSide = dynamic_cast<juce::Slider*> (child ("Mid / Side"));
    auto* bass = dynamic_cast<juce::ToggleButton*> (child ("Bass mono"));
    auto* audition = dynamic_cast<juce::ToggleButton*> (child ("Audition bass"));
    check (gain && width && channel && mode && midSide && bass && audition, "all controls expose accessible names and real native widgets");
    gain->onDragStart(); gain->setValue (-6, juce::sendNotificationSync); gain->setValue (-12, juce::sendNotificationSync); gain->onDragEnd();
    check (value().gainDb == -12, "gain handler writes the model"); session.undo(); check (value().gainDb == 0, "one undo restores the complete gain gesture"); session.redo();
    width->setValue (2.5, juce::sendNotificationSync); channel->setSelectedId (2, juce::sendNotificationSync);
    check (! width->isEnabled() && ! mode->isEnabled() && value().width == 2.5f, "single-channel mode disables stereo controls without losing values");
    channel->setSelectedId (1, juce::sendNotificationSync); mode->setSelectedId (2, juce::sendNotificationSync);
    midSide->setValue (1, juce::sendNotificationSync);
    check (midSide->isVisible() && ! width->isVisible() && value().midSide == 1 && value().width == 2.5f, "mid/side mode has its own retained value");
    check (! audition->isEnabled(), "bass audition starts disabled"); bass->setToggleState (true, juce::dontSendNotification); bass->onClick();
    check (audition->isEnabled() && value().bassMono, "bass switch enables frequency and audition");
    const char* names[] { "Invert L", "Invert R", "Mono", "Audition bass", "Mute", "DC filter" };
    for (auto* name : names) { auto* button = dynamic_cast<juce::ToggleButton*> (child (name)); check (button != nullptr, "toggle exists"); button->setToggleState (true, juce::dontSendNotification); button->onClick(); }
    const auto state = value(); check (state.invertLeft && state.invertRight && state.mono && state.bassAudition && state.mute && state.dcFilter, "every utility toggle writes its real setting");
    bass->setToggleState (false, juce::dontSendNotification); bass->onClick(); check (! audition->isEnabled() && value().bassAudition, "disabled bass audition retains its value");
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    auto output = directory.getChildFile ("utility-editor-640x390.png").createOutputStream();
    check (output && juce::PNGImageFormat().writeImageToStream (image, *output), "save the production utility editor snapshot");
    const auto retained = value();
    check (session.removePlugin (0, 0).wasOk(), "remove the edited device");
    static_cast<UtilityEditor*> (editor.get())->synchronise(); check (! gain->isEnabled(), "orphaned editor cannot edit a removed device");
    session.undo(); static_cast<UtilityEditor*> (editor.get())->synchronise(); check (gain->isEnabled() && value() == retained && session.snapshot().tracks[0].plugins[0].id == id, "undo restores the stable device identity");
    editor.reset();
    check (session.showPluginEditor (0, 0).wasOk(), "open the actual built-in device window");
    check (session.removePlugin (0, 0).wasOk(), "removal closes the actual device window safely");
    session.undo(); check (session.showPluginEditor (0, 0).wasOk(), "undo permits reopening the device window");
    check (session.newProject().wasOk(), "new project closes the native device window safely");
    return 0;
}
#endif
}
