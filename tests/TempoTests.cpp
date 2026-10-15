#include "Session.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
namespace
{
void check(bool condition,const juce::String& message)
{
    if(!condition)throw std::runtime_error(message.toStdString());std::cout<<"PASS tempo: "<<message<<std::endl;
}
void ok(const juce::Result& result,const juce::String& message)
{check(result.wasOk(),message+(result.failed()?": "+result.getErrorMessage():""));}
void timing(const juce::File& file,double bpm)
{
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>reader(formats.createReaderFor(file));
    check(reader&&reader->sampleRate==48000&&reader->numChannels==2,"read complete timing render");
    check(std::abs((double)reader->lengthInSamples-3.5*60.0/bpm*48000)<1.1,"render length follows exact beat duration at requested tempo");
    juce::AudioBuffer<float>audio(2,(int)reader->lengthInSamples);check(reader->read(&audio,0,audio.getNumSamples(),0,true,true),"decode timing fixture");
    int first=-1;double peak=0;for(int n=0;n<audio.getNumSamples();++n)for(int c=0;c<2;++c)
    {const auto sample=audio.getSample(c,n);if(!std::isfinite(sample))throw std::runtime_error("Nonfinite tempo render");if(first<0&&std::abs(sample)>1e-8)first=n;peak=std::max(peak,std::abs((double)sample));}
    const auto expected=1.75*60.0/bpm*48000;
    check(peak>.001&&first>=std::floor(expected)-1&&first<=std::ceil(expected)+3,"synth note starts at independently calculated beat sample");
}
}
int runTempoTests(const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("tempo-"+juce::Uuid().toString());ok(root.createDirectory(),"create generated tempo fixtures");
        Session session(false);ok(session.addMidiTrack(),"create tempo-following MIDI track");const auto track=session.snapshot().tracks[2].id;
        ok(session.addMidiClip(track,1.5,2),"create beat-synchronized clip");const auto clipId=session.snapshot().tracks[2].clips[0].id;
        ok(session.addMidiNote(clipId,69,.25,.5,.8f),"create known beat onset");auto patch=session.snapshot().tracks[2].synth;
        patch.waveform=2;patch.attackSeconds=0;patch.releaseSeconds=.02f;patch.cutoffHz=20000;patch.resonance=0;patch.level=.1f;
        ok(session.setSynthSettings(track,patch),"prepare causal sine fixture");juce::String marker;ok(session.addLocator(2.25,"Absolute",marker),"prepare absolute marker");
        const auto render=root.getChildFile("timing.wav"),project=root.getChildFile("tempo.halftrack");
        for(double bpm:{20.0,120.0,300.0,300.01,500.5,999.0})
        {
            const auto previous=session.snapshot().tempo;session.setTempo(bpm);const auto state=session.snapshot();
            check(state.tempo==bpm,"requested tempo is retained without a hidden 300 BPM clamp");const auto& clip=state.tracks[2].clips[0];
            check(clip.id==clipId&&std::abs(clip.startBeat-1.5)<1e-9&&std::abs(clip.lengthBeats-2)<1e-9
                &&std::abs(clip.start-1.5*60/bpm)<1e-9&&std::abs(clip.length-2*60/bpm)<1e-9,"tempo remaps beat-synchronized MIDI placement and duration at "+juce::String(bpm,8)+"; beats="+juce::String(clip.startBeat,12)+","+juce::String(clip.lengthBeats,12)+" seconds="+juce::String(clip.start,12)+","+juce::String(clip.length,12));
            check(state.locators[0].id==marker&&state.locators[0].position==2.25,"absolute marker stays fixed during tempo changes");
            ok(session.exportWav(render),"render exact tempo timing");timing(render,bpm);
            session.undo();check(session.snapshot().tempo==previous,"one undo restores tempo");session.redo();check(session.snapshot().tempo==bpm,"redo restores exact high tempo");
        }
        session.setTempo(120);
        for(int step=1;step<=100;++step)
        {
            session.setTempo(120.0+step*.01);const auto clip=session.snapshot().tracks[2].clips[0];
            check(std::abs(clip.startBeat-1.5)<1e-10&&std::abs(clip.lengthBeats-2)<1e-10,"repeated small tempo changes cannot accumulate MIDI beat drift");
        }
        session.setTempo(999);
        ok(session.saveProject(project),"save 999 BPM project");ok(session.loadProject(project),"reopen 999 BPM project");
        check(session.snapshot().tempo==999,"maximum tempo persists");session.setTempo(999);session.setTempo(1000);
        session.setTempo(std::numeric_limits<double>::quiet_NaN());session.setTempo(std::numeric_limits<double>::infinity());
        check(!session.snapshot().dirty&&!session.canUndo(),"unchanged/clamped maximum and nonfinite requests create no edit");
        session.setTempo(-1);check(session.snapshot().tempo==20,"finite below-range request clamps to documented minimum");session.undo();
        check(session.snapshot().tempo==999&&session.canRedo(),"range clamp is one undoable change");
        for(const auto& invalid:juce::StringArray{"19.99","999.01","nan","inf","300 trailing"})
        {
            auto xml=juce::XmlDocument::parse(project);check(xml!=nullptr,"parse generated valid project");
            auto* edit=xml->getChildByName("EDIT");auto* sequence=edit?edit->getChildByName("TEMPOSEQUENCE"):nullptr;
            auto* tempo=sequence?sequence->getChildByName("TEMPO"):nullptr;
            check(tempo!=nullptr,"locate serialized tempo setting");tempo->setAttribute("bpm",invalid);
            const auto broken=root.getChildFile("invalid.halftrack");check(xml->writeTo(broken),"write isolated invalid tempo");
            check(session.loadProject(broken).failed()&&session.snapshot().tempo==999&&session.canRedo()
                &&session.snapshot().tracks[2].clips[0].id==clipId,"invalid tempo cannot replace active project or redo");
        }
        std::cout<<"Tempo tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL tempo: "<<e.what()<<std::endl;return 1;}
}
}
