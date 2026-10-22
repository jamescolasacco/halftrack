#include "AudioClipEditor.h"
#include "Theme.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#if HALFTRACK_TESTS_ENABLED
#include <iostream>
#include <stdexcept>
#endif
namespace halftrack
{
namespace
{
class AudioClipEditor final : public juce::Component, private juce::ChangeListener
{
public:
    AudioClipEditor (Session& s, juce::String id) : session (s), clipId (std::move (id))
    {
        applyLook();
        setLookAndFeel(&look);
        const std::array<const char*, 3> names { "Clip gain", "Fade in", "Fade out" };
        const std::array<const char*, 3> keys { "clipGain", "fadeIn", "fadeOut" };
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            labels[i].setText (names[i], juce::dontSendNotification); addAndMakeVisible (labels[i]);
            auto& control = sliders[i]; control.setName (keys[i]);
            control.setSliderStyle (juce::Slider::LinearHorizontal); control.setTextBoxStyle (juce::Slider::TextBoxRight, false, 92, 24);
            control.setRange (i == 0 ? -100 : 0, i == 0 ? 24 : 1); control.setNumDecimalPlacesToDisplay (i == 0 ? 1 : 4);
            control.setTextValueSuffix (i == 0 ? " dB" : " s"); control.setDoubleClickReturnValue (true, 0);
            control.onDragStart = [this] { session.beginEditGesture ("Adjust audio clip"); dragging = true; };
            control.onDragEnd = [this] { finishGesture(); };
            control.onValueChange = [this, i]
            {
                if (auto clip = findClip())
                {
                    auto value = clip->audio;
                    if (i == 0) value.gainDb = static_cast<float> (sliders[i].getValue());
                    else if (i == 1) value.fadeInSeconds = sliders[i].getValue();
                    else value.fadeOutSeconds = sliders[i].getValue();
                    showResult (session.setAudioClipSettings (clipId, value));
                }
                sync();
            };
            addAndMakeVisible (control);
        }
        for (size_t i = 0; i < curves.size(); ++i)
        {
            auto& curve = curves[i]; curve.setName (i == 0 ? "fadeInCurve" : "fadeOutCurve");
            curve.addItemList ({ "Linear", "Convex", "Concave", "S-curve", "Smooth" }, 1);
            curve.onChange = [this, i]
            {
                if (auto clip = findClip())
                {
                    auto value = clip->audio; auto type = static_cast<Session::FadeCurve> (curves[i].getSelectedId());
                    if (i == 0) value.fadeInCurve = type; else value.fadeOutCurve = type;
                    showResult (session.setAudioClipSettings (clipId, value));
                }
                sync();
            };
            addAndMakeVisible (curve);
            // A default edge fade, as Create Fades on Clip Edges gives: 4 ms along the smoothstep.
            auto& standard = defaults[i]; standard.setButtonText ("Default"); standard.setName (i == 0 ? "defaultFadeIn" : "defaultFadeOut");
            standard.setTooltip ("Default: a 4 ms smooth fade, crossfading with a clip that touches this edge with a default fade of its own");
            standard.onClick = [this, i]
            {
                if (auto clip = findClip())
                {
                    auto value = clip->audio;
                    if (i == 0) value.defaultFadeIn = defaults[i].getToggleState(); else value.defaultFadeOut = defaults[i].getToggleState();
                    showResult (session.setAudioClipSettings (clipId, value));
                }
                sync();
            };
            addAndMakeVisible (standard);
        }
        const std::array<const char*, 3> fields { "Start (s)", "Length (s)", "Source offset (s)" };
        for (size_t i = 0; i < range.size(); ++i)
        {
            rangeLabels[i].setText (fields[i], juce::dontSendNotification); addAndMakeVisible (rangeLabels[i]);
            range[i].setName (fields[i]); range[i].setInputRestrictions (24, "0123456789.-");
            range[i].onTextChange = [this] { rangeDirty = true; };
            addAndMakeVisible (range[i]);
        }
        apply.setName ("applyClipRange"); apply.onClick = [this]
        {
            std::array<double, 3> v {};
            for (size_t i = 0; i < v.size(); ++i)
            {
                const auto value = range[i].getText().trim().toStdString(); char* end = nullptr;
                v[i] = std::strtod (value.c_str(), &end);
                if (value.empty() || end != value.c_str()+value.size() || ! std::isfinite (v[i]))
                { showResult (juce::Result::fail ("Enter complete numeric values for the range.")); return; }
            }
            const auto result = session.setAudioClipRange (clipId, v[0], v[1], v[2]);
            showResult (result); if (result.wasOk()) rangeDirty = false; sync();
        };
        reverse.setName ("reverseAudioClip"); reverse.setButtonText ("Reverse audio");
        reverse.onClick = [this]
        {
            if (const auto clip = findClip()) showResult (session.setAudioClipReversed (clipId, ! clip->reversed));
            sync();
        };
        addAndMakeVisible (reverse); addAndMakeVisible (apply); addAndMakeVisible (error);
        error.setColour (juce::Label::textColourId, themes::colour (0xfff4ab84));
        session.addChangeListener (this); setSize (620, 480); sync();
    }
    // The editor's own look follows the interface theme, as the look-and-feel change on a theme switch says.
    void applyLook()
    {
        look.setColour(juce::Label::textColourId,themes::colour (0xffe9e9e9));
        look.setColour(juce::ToggleButton::textColourId,themes::colour (0xffe9e9e9));
        look.setColour(juce::ToggleButton::tickColourId,themes::colour (0xffe49a58));
        look.setColour(juce::Slider::thumbColourId,themes::colour (0xffe49a58));
        look.setColour(juce::Slider::trackColourId,themes::colour (0xffe49a58));
        look.setColour(juce::Slider::textBoxTextColourId,themes::colour (0xffe9e9e9));
        look.setColour(juce::Slider::textBoxOutlineColourId,themes::colour (0xff515151));
        look.setColour(juce::ComboBox::backgroundColourId,themes::colour (0xff3a3a3a));
        look.setColour(juce::TextEditor::backgroundColourId,themes::colour (0xff242424));
        look.setColour(juce::TextEditor::textColourId,themes::colour (0xffe9e9e9));
    }
    void lookAndFeelChanged() override { applyLook(); }
    ~AudioClipEditor() override { session.removeChangeListener (this); finishGesture(); setLookAndFeel(nullptr); }
    void resized() override
    {
        const auto width = getWidth()-48;
        for (int i = 0; i < 3; ++i)
        {
            labels[(size_t)i].setBounds (24, 160+i*46, 80, 30);
            sliders[(size_t)i].setBounds (104, 160+i*46, width-80-(i == 0 ? 0 : 198), 30);
            if (i > 0) { curves[(size_t)i-1].setBounds (getWidth()-130, 160+i*46, 106, 28); defaults[(size_t)i-1].setBounds (getWidth()-214, 160+i*46, 80, 28); }
            const auto fieldWidth = (width-24)/3;
            rangeLabels[(size_t)i].setBounds (24+i*(fieldWidth+12), 315, fieldWidth, 22);
            range[(size_t)i].setBounds (24+i*(fieldWidth+12), 339, fieldWidth, 27);
        }
        apply.setBounds (24, 380, 150, 28); reverse.setBounds (196,380,180,28); error.setBounds (24, 418, width, 48);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (themes::colour (0xff2b2b2b)); const auto clip = findClip();
        g.setColour (themes::colour (0xffe9e9e9)); g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (clip ? clip->name : "Audio clip unavailable", 24, 14, getWidth()-48, 30, juce::Justification::centredLeft, true);
        const juce::Rectangle<float> plot (24, 60, (float)getWidth()-48, 80);
        g.setColour (themes::colour (0xff242424)); g.fillRect (plot);
        if (clip && clip->length > 0 && clip->canEditAudioFades)
        {
            const auto curve = [] (double x, Session::FadeCurve kind)
            {
                const auto a = x*juce::MathConstants<double>::halfPi;
                switch (kind) { case Session::FadeCurve::convex: return std::sin(a); case Session::FadeCurve::concave: return 1-std::cos(a);
                    case Session::FadeCurve::sCurve: return (1-x)*(1-std::cos(a))+x*std::sin(a);
                    case Session::FadeCurve::smoothstep: return x*x*(3-2*x); default: return x; }
            };
            juce::Path envelope;
            for (int x=0;x<=500;++x)
            {
                const auto time = clip->length*x/500; double level = 1;
                if (clip->audio.fadeInSeconds > 0 && time < clip->audio.fadeInSeconds) level *= curve(time/clip->audio.fadeInSeconds,clip->audio.fadeInCurve);
                if (clip->audio.fadeOutSeconds > 0 && time > clip->length-clip->audio.fadeOutSeconds) level *= curve((clip->length-time)/clip->audio.fadeOutSeconds,clip->audio.fadeOutCurve);
                const auto px=plot.getX()+plot.getWidth()*x/500, py=plot.getBottom()-4-(float)level*(plot.getHeight()-8);
                if(x==0)envelope.startNewSubPath(px,py);else envelope.lineTo(px,py);
            }
            g.setColour(themes::colour (0xffe49a58));g.strokePath(envelope,juce::PathStrokeType(1.8f));
            g.setFont(juce::FontOptions(11.0f));g.setColour(themes::colour (0xffbbbbbb));
            g.drawText("Fade envelope  /  "+juce::String(clip->length,4)+" s",24,140,getWidth()-48,18,juce::Justification::centredRight);
            g.drawText(clip->canEditAudioRange ? "Non-destructive range / source audio stays unchanged" : (clip->reversed ? "Switch off Reverse audio to edit the source range." : "Range editing is available for unwarped arrangement clips"),24,288,getWidth()-48,20,juce::Justification::centredLeft);
        }
        else if (clip)
        {
            g.setColour(themes::colour (0xffbbbbbb));g.setFont(juce::FontOptions(13.0f));
            g.drawText("Session clip gain",plot,juce::Justification::centred);
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("Fade and range editing is available in Arrangement.",24,288,getWidth()-48,20,juce::Justification::centredLeft);
        }
    }
private:
    std::optional<Session::ClipInfo> findClip() const
    {
        for (const auto& track : session.snapshot().tracks)
        {
            for (const auto& c : track.clips) if (c.id == clipId && c.isAudio) return c;
            for (const auto& slot : track.slots) if (slot.hasClip && slot.clip.id == clipId && slot.clip.isAudio) return slot.clip;
        }
        return {};
    }
    void sync()
    {
        const auto clip = findClip(); const bool enabled = clip.has_value() && ! session.isExporting();
        reverse.setEnabled (enabled && clip->canReverse);
        reverse.setToggleState (clip && clip->reversed, juce::dontSendNotification);
        for(auto& slider:sliders)slider.setEnabled(enabled);
        for(auto& curve:curves)curve.setEnabled(enabled);
        for(auto& standard:defaults)standard.setEnabled(enabled);
        if (clip && ! clip->canEditAudioFades)
        { sliders[1].setEnabled(false);sliders[2].setEnabled(false);for(auto& curve:curves)curve.setEnabled(false);for(auto& standard:defaults)standard.setEnabled(false); }
        const bool editable = enabled && clip->canEditAudioRange;
        for(auto& field:range)field.setEnabled(editable); apply.setEnabled(editable);
        if(clip)
        {
            sliders[0].setValue(clip->audio.gainDb,juce::dontSendNotification);
            for(int i=1;i<3;++i)sliders[(size_t)i].setRange(0,juce::jmax(.001,clip->length));
            sliders[1].setValue(clip->audio.fadeInSeconds,juce::dontSendNotification);sliders[2].setValue(clip->audio.fadeOutSeconds,juce::dontSendNotification);
            curves[0].setSelectedId((int)clip->audio.fadeInCurve,juce::dontSendNotification);curves[1].setSelectedId((int)clip->audio.fadeOutCurve,juce::dontSendNotification);
            defaults[0].setToggleState(clip->audio.defaultFadeIn,juce::dontSendNotification);defaults[1].setToggleState(clip->audio.defaultFadeOut,juce::dontSendNotification);
            if(!rangeDirty)
            { range[0].setText(juce::String(clip->start,9),false);range[1].setText(juce::String(clip->length,9),false);range[2].setText(juce::String(clip->sourceOffset,9),false); }
        }
        repaint();
    }
    void showResult(const juce::Result& r){error.setText(r.failed()?r.getErrorMessage():juce::String(),juce::dontSendNotification);}
    void finishGesture(){if(dragging){dragging=false;session.endEditGesture();}}
    void changeListenerCallback(juce::ChangeBroadcaster*) override {sync();}
    Session& session;juce::String clipId;std::array<juce::Slider,3>sliders;std::array<juce::Label,3>labels,rangeLabels;
    std::array<juce::ComboBox,2>curves;std::array<juce::ToggleButton,2>defaults;std::array<juce::TextEditor,3>range;juce::TextButton apply{"Apply range"};juce::ToggleButton reverse;juce::Label error;
    juce::LookAndFeel_V4 look;
    bool dragging=false,rangeDirty=false;
};
}
std::unique_ptr<juce::Component> makeAudioClipEditor(Session& s,const juce::String& id){return std::make_unique<AudioClipEditor>(s,id);}
#if HALFTRACK_TESTS_ENABLED
int runAudioClipEditorTests(const juce::File& source,const juce::File& artifacts)
{
    auto check=[](bool v,const char* text){if(!v)throw std::runtime_error(text);std::cout<<"PASS audio editor: "<<text<<std::endl;};
    Session s(false);check(s.importAudio(source,0).wasOk(),"import editor fixture");const auto id=s.snapshot().tracks[0].clips[0].id;
    auto editor=makeAudioClipEditor(s,id);juce::ToggleButton* reverse=nullptr;juce::Slider* gain=nullptr;juce::TextButton* apply=nullptr;std::array<juce::TextEditor*,3>range{};
    juce::ToggleButton* defaultIn=nullptr;
    for(auto* child:editor->getChildren())
    {
        if(auto* control=dynamic_cast<juce::Slider*>(child);control&&control->getName()=="clipGain")gain=control;
        if(auto* control=dynamic_cast<juce::TextButton*>(child);control&&control->getName()=="applyClipRange")apply=control;
        if(auto* control=dynamic_cast<juce::ToggleButton*>(child);control&&control->getName()=="reverseAudioClip")reverse=control;
        if(auto* control=dynamic_cast<juce::ToggleButton*>(child);control&&control->getName()=="defaultFadeIn")defaultIn=control;
        if(auto* control=dynamic_cast<juce::TextEditor*>(child))
        {if(control->getName()=="Start (s)")range[0]=control;else if(control->getName()=="Length (s)")range[1]=control;else if(control->getName()=="Source offset (s)")range[2]=control;}
    }
    check(gain&&apply&&range[0]&&range[1]&&range[2]&&defaultIn,"production controls exist");
    // The Default switch gives the edge the default 4 ms fade, and off keeps it as an ordinary fade.
    check(!defaultIn->getToggleState(),"the fade-in starts as an ordinary fade here");
    defaultIn->setToggleState(true,juce::dontSendNotification);defaultIn->onClick();
    {auto a=s.snapshot().tracks[0].clips[0].audio;check(a.defaultFadeIn&&std::abs(a.fadeInSeconds-.004)<1e-9&&a.fadeInCurve==Session::FadeCurve::smoothstep&&defaultIn->getToggleState(),"Default gives the fade-in the 4 ms smooth default");}
    defaultIn->setToggleState(false,juce::dontSendNotification);defaultIn->onClick();
    {auto a=s.snapshot().tracks[0].clips[0].audio;check(!a.defaultFadeIn&&std::abs(a.fadeInSeconds-.004)<1e-9,"switched off, it stays a 4 ms fade of its own");}
    s.undo();s.undo();s.dispatchPendingMessages();check(s.snapshot().tracks[0].clips[0].audio.fadeInSeconds==0,"two undo steps return the hard edge");
    gain->onDragStart();gain->setValue(-12,juce::sendNotificationSync);gain->setValue(-6,juce::sendNotificationSync);gain->onDragEnd();
    check(s.snapshot().tracks[0].clips[0].audio.gainDb==-6,"gain control edits actual clip");s.undo();check(s.snapshot().tracks[0].clips[0].audio.gainDb==0,"one undo restores gain gesture");
    range[0]->setText("0.25");range[1]->setText("0.5");range[2]->setText("0.125");apply->onClick();
    auto clip=s.snapshot().tracks[0].clips[0];check(clip.start==.25&&clip.length==.5&&clip.sourceOffset==.125,"apply submits complete range");
    s.undo();check(s.snapshot().tracks[0].clips[0].length==1,"range edit undoes once");
    check(reverse&&reverse->isEnabled(),"reverse control exists and accepts ordinary audio");reverse->onClick();
    check(s.snapshot().tracks[0].clips[0].reversed&&reverse->getToggleState()&&!apply->isEnabled(),"reverse button changes real direction and disables forward-only range");
    s.undo();s.dispatchPendingMessages();check(!reverse->getToggleState()&&apply->isEnabled(),"undo updates reverse and range controls");
    s.removeClip(0,0);s.dispatchPendingMessages();check(!gain->isEnabled()&&!apply->isEnabled()&&!reverse->isEnabled(),"removed clip disables stale controls");
    s.undo();s.dispatchPendingMessages();check(gain->isEnabled(),"undo restores stable clip identity");
    auto shown=s.snapshot().tracks[0].clips[0].audio;shown.fadeInSeconds=.15;shown.fadeOutSeconds=.25;shown.gainDb=-6;
    shown.fadeOutCurve=Session::FadeCurve::convex;check(s.setAudioClipSettings(id,shown).wasOk(),"configure actual preview envelope");s.dispatchPendingMessages();
    auto png=artifacts.getChildFile("audio-clip-editor.png");juce::PNGImageFormat format;auto out=png.createOutputStream();
    check(out&&format.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),*out),"write actual editor snapshot");return 0;
}
#endif
}
