#include "AudioClipEditor.h"
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
        look.setColour(juce::Label::textColourId,juce::Colour(0xffeee8df));
        look.setColour(juce::Slider::thumbColourId,juce::Colour(0xffe49a58));
        look.setColour(juce::Slider::trackColourId,juce::Colour(0xffe49a58));
        look.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffeee8df));
        look.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff4c5355));
        look.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff353b3e));
        look.setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff202528));
        look.setColour(juce::TextEditor::textColourId,juce::Colour(0xffeee8df));
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
            curve.addItemList ({ "Linear", "Convex", "Concave", "S-curve" }, 1);
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
        addAndMakeVisible (apply); addAndMakeVisible (error);
        error.setColour (juce::Label::textColourId, juce::Colour (0xfff4ab84));
        session.addChangeListener (this); setSize (620, 480); sync();
    }
    ~AudioClipEditor() override { session.removeChangeListener (this); finishGesture(); setLookAndFeel(nullptr); }
    void resized() override
    {
        const auto width = getWidth()-48;
        for (int i = 0; i < 3; ++i)
        {
            labels[(size_t)i].setBounds (24, 160+i*46, 80, 30);
            sliders[(size_t)i].setBounds (104, 160+i*46, width-80-(i == 0 ? 0 : 114), 30);
            if (i > 0) curves[(size_t)i-1].setBounds (getWidth()-130, 160+i*46, 106, 28);
            const auto fieldWidth = (width-24)/3;
            rangeLabels[(size_t)i].setBounds (24+i*(fieldWidth+12), 315, fieldWidth, 22);
            range[(size_t)i].setBounds (24+i*(fieldWidth+12), 339, fieldWidth, 27);
        }
        apply.setBounds (24, 380, 150, 28); error.setBounds (24, 418, width, 48);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff272c2f)); const auto clip = findClip();
        g.setColour (juce::Colour (0xffeee8df)); g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (clip ? clip->name : "Audio clip unavailable", 24, 14, getWidth()-48, 30, juce::Justification::centredLeft, true);
        const juce::Rectangle<float> plot (24, 60, (float)getWidth()-48, 80);
        g.setColour (juce::Colour (0xff202528)); g.fillRect (plot);
        if (clip && clip->length > 0 && clip->canEditAudioFades)
        {
            const auto curve = [] (double x, Session::FadeCurve kind)
            {
                const auto a = x*juce::MathConstants<double>::halfPi;
                switch (kind) { case Session::FadeCurve::convex: return std::sin(a); case Session::FadeCurve::concave: return 1-std::cos(a);
                    case Session::FadeCurve::sCurve: return (1-x)*(1-std::cos(a))+x*std::sin(a); default: return x; }
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
            g.setColour(juce::Colour(0xffe49a58));g.strokePath(envelope,juce::PathStrokeType(1.8f));
            g.setFont(juce::FontOptions(11.0f));g.setColour(juce::Colour(0xffbbbcb8));
            g.drawText("Fade envelope  /  "+juce::String(clip->length,4)+" s",24,140,getWidth()-48,18,juce::Justification::centredRight);
            g.drawText(clip->canEditAudioRange ? "Non-destructive range / source audio stays unchanged" : "Range editing is available for unwarped arrangement clips",24,288,getWidth()-48,20,juce::Justification::centredLeft);
        }
        else if (clip)
        {
            g.setColour(juce::Colour(0xffbbbcb8));g.setFont(juce::FontOptions(13.0f));
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
        for(auto& slider:sliders)slider.setEnabled(enabled);
        for(auto& curve:curves)curve.setEnabled(enabled);
        if (clip && ! clip->canEditAudioFades)
        { sliders[1].setEnabled(false);sliders[2].setEnabled(false);for(auto& curve:curves)curve.setEnabled(false); }
        const bool editable = enabled && clip->canEditAudioRange;
        for(auto& field:range)field.setEnabled(editable); apply.setEnabled(editable);
        if(clip)
        {
            sliders[0].setValue(clip->audio.gainDb,juce::dontSendNotification);
            for(int i=1;i<3;++i)sliders[(size_t)i].setRange(0,juce::jmax(.001,clip->length));
            sliders[1].setValue(clip->audio.fadeInSeconds,juce::dontSendNotification);sliders[2].setValue(clip->audio.fadeOutSeconds,juce::dontSendNotification);
            curves[0].setSelectedId((int)clip->audio.fadeInCurve,juce::dontSendNotification);curves[1].setSelectedId((int)clip->audio.fadeOutCurve,juce::dontSendNotification);
            if(!rangeDirty)
            { range[0].setText(juce::String(clip->start,9),false);range[1].setText(juce::String(clip->length,9),false);range[2].setText(juce::String(clip->sourceOffset,9),false); }
        }
        repaint();
    }
    void showResult(const juce::Result& r){error.setText(r.failed()?r.getErrorMessage():juce::String(),juce::dontSendNotification);}
    void finishGesture(){if(dragging){dragging=false;session.endEditGesture();}}
    void changeListenerCallback(juce::ChangeBroadcaster*) override {sync();}
    Session& session;juce::String clipId;std::array<juce::Slider,3>sliders;std::array<juce::Label,3>labels,rangeLabels;
    std::array<juce::ComboBox,2>curves;std::array<juce::TextEditor,3>range;juce::TextButton apply{"Apply range"};juce::Label error;
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
    auto editor=makeAudioClipEditor(s,id);juce::Slider* gain=nullptr;juce::TextButton* apply=nullptr;std::array<juce::TextEditor*,3>range{};
    for(auto* child:editor->getChildren())
    {
        if(auto* control=dynamic_cast<juce::Slider*>(child);control&&control->getName()=="clipGain")gain=control;
        if(auto* control=dynamic_cast<juce::TextButton*>(child))apply=control;
        if(auto* control=dynamic_cast<juce::TextEditor*>(child))
        {if(control->getName()=="Start (s)")range[0]=control;else if(control->getName()=="Length (s)")range[1]=control;else if(control->getName()=="Source offset (s)")range[2]=control;}
    }
    check(gain&&apply&&range[0]&&range[1]&&range[2],"production controls exist");
    gain->onDragStart();gain->setValue(-12,juce::sendNotificationSync);gain->setValue(-6,juce::sendNotificationSync);gain->onDragEnd();
    check(s.snapshot().tracks[0].clips[0].audio.gainDb==-6,"gain control edits actual clip");s.undo();check(s.snapshot().tracks[0].clips[0].audio.gainDb==0,"one undo restores gain gesture");
    range[0]->setText("0.25");range[1]->setText("0.5");range[2]->setText("0.125");apply->onClick();
    auto clip=s.snapshot().tracks[0].clips[0];check(clip.start==.25&&clip.length==.5&&clip.sourceOffset==.125,"apply submits complete range");
    s.undo();check(s.snapshot().tracks[0].clips[0].length==1,"range edit undoes once");
    s.removeClip(0,0);s.dispatchPendingMessages();check(!gain->isEnabled()&&!apply->isEnabled(),"removed clip disables stale controls");
    s.undo();s.dispatchPendingMessages();check(gain->isEnabled(),"undo restores stable clip identity");
    auto shown=s.snapshot().tracks[0].clips[0].audio;shown.fadeInSeconds=.15;shown.fadeOutSeconds=.25;shown.gainDb=-6;
    shown.fadeOutCurve=Session::FadeCurve::convex;check(s.setAudioClipSettings(id,shown).wasOk(),"configure actual preview envelope");s.dispatchPendingMessages();
    auto png=artifacts.getChildFile("audio-clip-editor.png");juce::PNGImageFormat format;auto out=png.createOutputStream();
    check(out&&format.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),*out),"write actual editor snapshot");return 0;
}
#endif
}
