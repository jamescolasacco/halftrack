#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace halftrack
{
namespace
{
namespace te=tracktion::engine;
void check(bool value,const juce::String& message)
{if(!value)throw std::runtime_error(message.toStdString());std::cout<<"PASS clip activation: "<<message<<std::endl;}
void ok(const juce::Result& result,const juce::String& message)
{check(result.wasOk(),message+(result.failed()?": "+result.getErrorMessage():""));}
juce::File writeSignal(const juce::File& root)
{
    const auto file=root.getChildFile("source.wav");juce::AudioBuffer<float> signal(2,96000);
    for(int c=0;c<2;++c)juce::FloatVectorOperations::fill(signal.getWritePointer(c),.125f,96000);
    juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
    check(writer&&writer->writeFromAudioSampleBuffer(signal,0,96000),"write known stereo signal");return file;
}
juce::AudioBuffer<float> render(Session& session,const juce::File& file)
{
    ok(session.exportWav(file),"render actual clip graph");juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    check(reader&&reader->numChannels==2&&reader->sampleRate==48000&&reader->lengthInSamples==96000,"activation retains two-second stereo extent");
    juce::AudioBuffer<float> audio(2,96000);check(reader->read(&audio,0,96000,0,true,true),"decode complete render");return audio;
}
void compare(const juce::AudioBuffer<float>& a,const juce::AudioBuffer<float>& b,double gain,const juce::String& message)
{
    check(a.getNumSamples()==b.getNumSamples()&&a.getNumChannels()==b.getNumChannels(),"matching comparison dimensions");
    double worst=0;for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<a.getNumSamples();++n)
    {const double sample=a.getSample(c,n);if(!std::isfinite(sample))throw std::runtime_error("nonfinite clip output");worst=std::max(worst,std::abs(sample-b.getSample(c,n)*gain));}
    check(gain==0?worst==0:worst<2e-6,message+"; error="+juce::String(worst,9));
}
void offline(const juce::File& root,const juce::File& source,bool midi)
{
    Session s(false);int track=0;
    if(midi)
    {
        ok(s.addMidiTrack(),"add editable MIDI track");track=2;const auto id=s.snapshot().tracks[2].id;
        ok(s.addMidiClip(id,0,4),"add four-beat MIDI clip");const auto clip=s.snapshot().tracks[2].clips[0].id;
        ok(s.addMidiNote(clip,60,.5,1,.8f),"draw first note");ok(s.addMidiNote(clip,67,2,1,.6f),"draw second note");
    }
    else ok(s.importAudio(source,0),"import generated audio");
    const auto before=s.snapshot().tracks[(size_t)track].clips[0];const auto id=before.id;
    check(before.active,"new clip is active");const auto output=root.getChildFile("render.wav");const auto baseline=render(s,output);
    check(baseline.getMagnitude(0,96000)>.001,"active clip produces real output");
    ok(s.setClipActive(id,false),"deactivate clip without removal");check(!s.snapshot().tracks[(size_t)track].clips[0].active,"snapshot reports inactive clip");
    compare(render(s,output),baseline,0,"every inactive frame is silent");
    s.undo();const auto restored=s.snapshot().tracks[(size_t)track].clips[0];
    check(restored.active&&restored.id==id&&restored.start==before.start&&restored.length==before.length&&restored.notes.size()==before.notes.size(),"undo restores activation without changing musical data");
    compare(render(s,output),baseline,1,"undo restores every active frame");s.redo();compare(render(s,output),baseline,0,"redo restores silence");
    const auto project=root.getChildFile(midi?"midi.halftrack":"audio.halftrack");ok(s.saveProject(project),"save inactive clip");ok(s.loadProject(project),"reopen inactive clip");
    check(!s.snapshot().tracks[(size_t)track].clips[0].active,"clip activation persists");ok(s.setClipActive(id,false),"repeat inactive state");
    check(!s.snapshot().dirty&&!s.canUndo(),"unchanged activation preserves clean state");
    ok(s.setClipActive(id,true),"reactivate reopened clip");compare(render(s,output),baseline,1,"reopened activation restores complete audio");s.undo();
    check(s.setClipActive("missing",true).failed()&&s.canRedo(),"missing identity preserves redo");
    for(const auto& invalid:juce::StringArray{"2","-1","nan","true","0 trailing",""})
    {
        auto xml=juce::XmlDocument::parse(project);bool replaced=false;
        const auto replace=[&](auto&& self,juce::XmlElement& node)->void
        {
            if(node.hasTagName(midi?"MIDICLIP":"AUDIOCLIP")){node.setAttribute("mute",invalid);replaced=true;}
            for(auto* child=node.getFirstChildElement();child;child=child->getNextElement())self(self,*child);
        };
        replace(replace,*xml);check(replaced,"locate clip state");const auto bad=root.getChildFile("invalid.halftrack");check(xml->writeTo(bad),"write invalid activation fixture");
        check(s.loadProject(bad).failed()&&s.canRedo()&&!s.snapshot().tracks[(size_t)track].clips[0].active,"malformed activation preserves active document and redo");
    }
    juce::String duplicate;
    if(midi)ok(s.duplicateMidiClip(id,duplicate),"duplicate inactive MIDI clip");else ok(s.duplicateAudioClip(id,duplicate),"duplicate inactive audio clip");
    check(s.snapshot().tracks[(size_t)track].clips.size()==2&&!s.snapshot().tracks[(size_t)track].clips[1].active&&duplicate!=id,"clip copy retains activation with a fresh identity");
    s.undo();check(s.snapshot().tracks[(size_t)track].clips.size()==1&&!s.snapshot().tracks[(size_t)track].clips[0].active,"duplicate undo preserves original inactive clip");
}
void playback(const juce::File& source,bool midi,bool slots)
{
    choc::buffer::InterleavedBuffer<float> memory(2,96000);
    for(unsigned c=0;c<2;++c)for(unsigned n=0;n<96000;++n)memory.getSample(c,n)=.125f;
    Session s(false);auto& engine=dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&>(s.audioDeviceManager()).engine;
    te::HostedAudioDeviceInterface::Parameters parameters;parameters.sampleRate=48000;parameters.blockSize=128;parameters.inputChannels=0;parameters.outputChannels=2;parameters.useMidiDevices=false;
    te::test_utilities::EnginePlayer player(engine,parameters);int track=0;
    if(midi)
    {
        ok(s.addMidiTrack(),"create launcher instrument");track=2;auto state=s.snapshot().tracks[2];auto synth=state.synth;
        synth.attackSeconds=synth.decaySeconds=0;synth.sustain=1;synth.releaseSeconds=.01f;
        ok(s.setSynthSettings(state.id,synth),"prepare held-note release");
        ok(slots?s.addMidiClipToSlot(state.id,0,4):s.addMidiClip(state.id,0,4),"create MIDI playback clip");
        const auto clip=slots?s.snapshot().tracks[2].slots[0].clip:s.snapshot().tracks[2].clips[0];
        ok(s.addMidiNote(clip.id,60,0,4,1),"draw held note");
    }
    else
    {
        ok(slots?s.importAudioToSlot(source,0,0):s.importAudio(source,0),"create audio playback clip");
        const auto clip=slots?s.snapshot().tracks[0].slots[0].clip:s.snapshot().tracks[0].clips[0];
        engine.getAudioFileManager().registerMemoryBuffer(clip.file.getFullPathName().toStdString(),memory.getView(),48000);
    }
    const auto clip=[&]{const auto state=s.snapshot().tracks[(size_t)track];return slots?state.slots[0].clip:state.clips[0];};
    const auto id=clip().id;
    const auto dispatch=[&]{for(auto* edit:engine.getActiveEdits().getEdits())edit->dispatchPendingUpdatesSynchronously();};
    const auto start=[&]{s.stop();s.seek(0);if(slots)ok(s.launchSlot(track,0),"launch clip");else s.play();dispatch();};
    const auto silent=[&]{dispatch();player.process(2048);auto audio=player.process(4096);check(audio.getMagnitude(0,4096)==0,"disabled playback releases held notes and audio");};
    ok(s.setLaunchQuantisation(Session::LaunchQuantisation::none),"select immediate launches");
    ok(s.setClipActive(id,false),"deactivate before playback");start();
    check(player.process(12000).getMagnitude(0,12000)==0,"inactive playback is silent");
    check(clip().id==id&&(!slots||s.snapshot().tracks[(size_t)track].launcherActive),"silent playback retains clip and Session ownership");
    ok(s.setClipActive(id,true),"reactivate playback clip");start();
    check(player.process(12000).getMagnitude(0,12000)>.01,"enabled clip produces audio");
    ok(s.setClipActive(id,false),"deactivate sounding clip");silent();
    s.undo();check(clip().active,"activation undo restores state");start();
    check(player.process(12000).getMagnitude(0,12000)>.01,"activation undo restores playable output");
    s.redo();check(!clip().active,"activation redo restores state");silent();
    if(slots)
    {
        // Keep transport/launch ownership running across multiple mute transitions.
        for(int cycle=0;cycle<2;++cycle)
        {
            ok(s.setClipActive(id,true),"enable without relaunch");dispatch();
            check(player.process(110000).getMagnitude(0,110000)>.01,"subsequent loop events resume without relaunch");
            ok(s.setClipActive(id,false),"disable without relaunch");silent();
        }
    }
    s.stop();
}
void renameCases(const juce::File& root,const juce::File& source)
{
    for(bool midi:{false,true})for(bool slot:{false,true})
    {
        Session s(false);int index=0;
        if(midi)
        {
            ok(s.addMidiTrack(),"create naming instrument");index=2;const auto track=s.snapshot().tracks[2].id;
            ok(slot?s.addMidiClipToSlot(track,0,4):s.addMidiClip(track,0,4),"create named MIDI clip");
        }
        else ok(slot?s.importAudioToSlot(source,0,0):s.importAudio(source,0),"create named audio clip");
        const auto current=[&]{const auto track=s.snapshot().tracks[(size_t)index];return slot?track.slots[0].clip:track.clips[0];};
        const auto id=current().id;if(midi)ok(s.addMidiNote(id,60,0,1,.8f),"draw naming fixture note");
        const auto original=current();juce::MemoryBlock media;if(!midi)check(original.file.loadFileAsData(media),"read original source bytes");
        const juce::String name=juce::String::fromUTF8("Verse • 2 / C♯ <take> & bass");
        ok(s.renameClip(id,"  "+name+"  "),"rename only clip display with trimmed Unicode name");
        const auto renamed=current();check(renamed.name==name&&renamed.id==original.id&&renamed.start==original.start&&renamed.length==original.length&&renamed.active==original.active&&renamed.file==original.file&&renamed.notes.size()==original.notes.size(),"rename preserves clip identity, musical state and source path");
        if(midi)check(renamed.notes[0].id==original.notes[0].id&&renamed.notes[0].pitch==60&&renamed.notes[0].lengthBeats==1,"rename preserves native note identity and geometry");
        else {juce::MemoryBlock after;check(original.file.loadFileAsData(after)&&after==media,"rename never renames or changes source media");}
        s.undo();check(current().name==original.name&&s.canRedo(),"rename undoes once");
        for(const auto& invalid:juce::StringArray{"","   ","\t\n"})check(s.renameClip(id,invalid).failed()&&current().name==original.name&&s.canRedo(),"blank names preserve project and redo");
        check(s.renameClip("missing",name).failed()&&s.canRedo(),"stale clip identity cannot change a replacement");
        s.redo();check(current().name==name,"rename redo restores Unicode name");
        const auto file=root.getChildFile("clip-name-"+juce::String(midi?1:0)+juce::String(slot?1:0)+".halftrack");
        ok(s.saveProject(file),"save clip name");ok(s.loadProject(file),"reopen clip name");check(current().name==name,"XML escapes and restores the exact clip name");
        ok(s.renameClip(id,"  "+name+"  "),"repeat normalized name");check(!s.snapshot().dirty&&!s.canUndo(),"unchanged name stays clean without undo history");
    }
}
void trackIsolation()
{
    Session s(false);auto& engine=dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&>(s.audioDeviceManager()).engine;
    te::HostedAudioDeviceInterface::Parameters parameters;parameters.sampleRate=48000;parameters.blockSize=128;parameters.inputChannels=0;parameters.outputChannels=2;parameters.useMidiDevices=false;
    te::test_utilities::EnginePlayer player(engine,parameters);
    for(int i=0;i<2;++i)
    {
        ok(s.addMidiTrack(),"create isolated instrument");const auto track=s.snapshot().tracks[(size_t)(i+2)];auto synth=track.synth;
        synth.attackSeconds=synth.decaySeconds=0;synth.sustain=1;synth.releaseSeconds=.01f;
        ok(s.setSynthSettings(track.id,synth),"prepare independent held voice");ok(s.setTrackPan(i+2,i==0?-1.0f:1.0f),"separate instrument channels");
        ok(s.addMidiClip(track.id,0,16),"create long independent MIDI clip");
        ok(s.addMidiNote(s.snapshot().tracks[(size_t)(i+2)].clips[0].id,60,0,16,1),"draw same pitch on each track");
    }
    const auto dispatch=[&]{for(auto* edit:engine.getActiveEdits().getEdits())edit->dispatchPendingUpdatesSynchronously();};
    s.play();dispatch();auto both=player.process(12000);
    check(both.getMagnitude(0,0,12000)>.01&&both.getMagnitude(1,0,12000)>.01,"both independent instruments hold notes");
    ok(s.setClipActive(s.snapshot().tracks[2].clips[0].id,false),"disable first instrument clip");dispatch();player.process(2048);auto remaining=player.process(4096);
    check(remaining.getMagnitude(0,0,4096)<1e-7&&remaining.getMagnitude(1,0,4096)>.01,"deactivation releases only the target track's voice");s.stop();
}
}
int runClipActivationTests(const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("clip-activation-"+juce::Uuid().toString());ok(root.createDirectory(),"create isolated fixtures");const auto source=writeSignal(root);
        offline(root,source,false);offline(root,source,true);
        playback(source,false,false);playback(source,true,false);playback(source,false,true);playback(source,true,true);trackIsolation();renameCases(root,source);
        std::cout<<"Clip activation tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL clip activation: "<<error.what()<<std::endl;return 1;}
}
}
