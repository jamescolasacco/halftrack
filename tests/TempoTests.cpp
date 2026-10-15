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
        session.setTempo(90);ok(session.saveProject(project),"save tap baseline");ok(session.loadProject(project),"reopen clean tap baseline");
        ok(session.tapTempo(100),"anchor first tap");check(session.snapshot().tempo==90&&!session.snapshot().dirty&&!session.canUndo(),"first tap does not edit the document");
        const std::array<double,6> taps {100.5,101.1,101.8,102.6,103.5,104.5};
        const std::array<double,6> expected {120,120/1.1,180/1.8,240/2.6,240/3.0,240/3.4};
        for(size_t i=0;i<taps.size();++i)
        {
            if(i==5)
            {
                ok(session.tapTempo(103.51),"ignore a too-fast double tap");
                check(session.tapTempo(103.4).failed()&&session.tapTempo(-1).failed()
                    &&session.tapTempo(std::numeric_limits<double>::quiet_NaN()).failed()
                    &&session.tapTempo(std::numeric_limits<double>::infinity()).failed(),"invalid tap times are rejected without changing the anchor");
            }
            ok(session.tapTempo(taps[i]),"accept steady tap interval");
            check(std::abs(session.snapshot().tempo-expected[i])<1e-8,"tempo follows independent mean of the latest four intervals");
        }
        session.undo();check(std::abs(session.snapshot().tempo-80)<1e-8&&session.canRedo(),"accepted tempo undoes once");
        ok(session.tapTempo(200),"first tap after undo resets the estimate");check(std::abs(session.snapshot().tempo-80)<1e-8&&session.canRedo(),"anchor preserves redo");
        session.redo();check(std::abs(session.snapshot().tempo-240/3.4)<1e-8,"redo restores the exact accepted tempo");
        ok(session.tapTempo(300),"first tap after redo");ok(session.tapTempo(300.5),"second tap after redo");check(std::abs(session.snapshot().tempo-120)<1e-8,"redo does not retain an old tap sequence");
        ok(session.tapTempo(304),"long pause resets estimate");ok(session.tapTempo(304.75),"tap after pause");check(std::abs(session.snapshot().tempo-80)<1e-8,"pause excludes earlier intervals");
        session.setTempo(144);ok(session.tapTempo(400),"manual tempo resets tapping");check(session.snapshot().tempo==144,"manual-tempo anchor is unchanged");
        ok(session.tapTempo(400.4),"tap 150 BPM");check(std::abs(session.snapshot().tempo-150)<1e-8&&!session.snapshot().playing,"tap changes tempo without starting transport");
        ok(session.exportWav(render),"render beat-synchronized notes after tapping");timing(render,150);
        ok(session.saveProject(project),"save tapped tempo");ok(session.loadProject(project),"reopen tapped tempo");
        check(std::abs(session.snapshot().tempo-150)<1e-8,"accepted tempo persists");ok(session.tapTempo(400.6),"reopen clears transient timing");
        check(!session.snapshot().dirty&&!session.canUndo(),"reopened first tap does not create history");
        ok(session.newProject(),"new project clears tap timing");ok(session.tapTempo(400.7),"anchor new project");
        check(session.snapshot().tempo==120&&!session.snapshot().dirty&&!session.canUndo(),"new project's first tap is clean");
        ok(session.tapTempo(403.7),"tap at minimum supported tempo");check(std::abs(session.snapshot().tempo-20)<1e-8,"three-second interval reaches 20 BPM");
        session.setTempo(90);ok(session.tapTempo(500),"anchor maximum tempo");ok(session.tapTempo(500+60.0/999),"tap at maximum supported tempo");
        check(std::abs(session.snapshot().tempo-999)<1e-8,"minimum accepted interval reaches 999 BPM");
        session.setTempo(120);ok(session.saveProject(project),"save unchanged tap baseline");ok(session.loadProject(project),"reopen unchanged baseline");
        ok(session.tapTempo(600),"anchor unchanged tempo");ok(session.tapTempo(600.5),"tap existing 120 BPM");ok(session.tapTempo(601),"repeat existing tempo");
        check(!session.snapshot().dirty&&!session.canUndo(),"unchanged tap estimates do not dirty the project or add history");
        std::cout<<"Tempo tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL tempo: "<<e.what()<<std::endl;return 1;}
}
}
