#include "Session.h"
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
namespace halftrack
{
namespace
{
void check(bool condition,const juce::String& text){if(!condition)throw std::runtime_error(text.toStdString());std::cout<<"PASS MIDI files: "<<text<<std::endl;}
void ok(const juce::Result& r,const juce::String& text){check(r.wasOk(),text+(r.failed()?": "+r.getErrorMessage():""));}
void add(juce::MidiMessageSequence& seq,juce::MidiMessage message,double beat){message.setTimeStamp(beat*960);seq.addEvent(message);}
void write(const juce::File& file,const std::vector<juce::MidiMessageSequence>& tracks,int type=1,bool smpte=false)
{
    juce::MidiFile midi;if(smpte)midi.setSmpteTimeFormat(25,40);else midi.setTicksPerQuarterNote(960);
    for(const auto& track:tracks)midi.addTrack(track);
    auto out=file.createOutputStream();check(out&&out->setPosition(0)&&out->truncate().wasOk()&&midi.writeTo(*out,type),"write generated MIDI fixture");
}
std::vector<juce::String> events(const juce::MidiMessageSequence& seq,double ppq,int channel=0,bool sysex=true)
{
    std::vector<juce::String> result;
    for(int i=0;i<seq.getNumEvents();++i)
    {
        const auto& m=seq.getEventPointer(i)->message;if(m.isMetaEvent())continue;
        if(channel!=0&&!m.isForChannel(channel)&&!(sysex&&m.isSysEx()))continue;
        result.push_back(juce::String(m.getTimeStamp()/ppq,8)+":"+juce::String::toHexString(m.getRawData(),m.getRawDataSize()));
    }
    std::sort(result.begin(),result.end());return result;
}
std::vector<juce::String> exported(const juce::File& file)
{
    juce::FileInputStream in(file);juce::MidiFile midi;int type=-1;check(in.openedOk()&&midi.readFrom(in,false,&type)&&type==0&&midi.getNumTracks()==1&&midi.getTimeFormat()==9600,"export is a standard type 0 MIDI file at documented resolution");
    return events(*midi.getTrack(0),midi.getTimeFormat());
}
juce::AudioBuffer<float> readAudio(const juce::File& file)
{
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>reader(formats.createReaderFor(file));
    check(reader&&reader->numChannels==2&&reader->sampleRate==48000,"read actual synth render");juce::AudioBuffer<float>result(2,(int)reader->lengthInSamples);
    check(reader->read(&result,0,result.getNumSamples(),0,true,true),"decode complete synth render");return result;
}
}
int runMidiFileTests(const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("midi-files-"+juce::Uuid().toString());ok(root.createDirectory(),"create isolated fixtures");
        const auto source=root.getChildFile("source.mid"),output=root.getChildFile("export.mid"),project=root.getChildFile("song.halftrack");
        juce::MidiMessageSequence conductor,keys,bass;
        add(conductor,juce::MidiMessage::tempoMetaEvent(1000000),0);add(conductor,juce::MidiMessage::timeSignatureMetaEvent(3,4),0);
        add(keys,juce::MidiMessage::textMetaEvent(3,"Keys"),0);add(keys,juce::MidiMessage::programChange(2,18),0);
        add(keys,juce::MidiMessage::noteOn(2,60,(juce::uint8)91),0);add(keys,juce::MidiMessage::controllerEvent(2,1,67),.125);
        add(keys,juce::MidiMessage::pitchWheel(2,9000),.25);const unsigned char payload[]{0x7d,1,2,3};add(keys,juce::MidiMessage::createSysExMessage(payload,4),0);
        add(keys,juce::MidiMessage::aftertouchChange(2,60,35),.5);add(keys,juce::MidiMessage::noteOn(9,67,(juce::uint8)110),.5);
        add(keys,juce::MidiMessage::channelPressureChange(2,65),.75);add(keys,juce::MidiMessage::noteOff(2,60,(juce::uint8)40),1);
        add(keys,juce::MidiMessage::noteOff(9,67,(juce::uint8)77),1.5);add(keys,juce::MidiMessage::endOfTrack(),4);
        add(bass,juce::MidiMessage::textMetaEvent(3,"Bass"),0);add(bass,juce::MidiMessage::noteOn(4,36,(juce::uint8)100),1);
        add(bass,juce::MidiMessage::noteOff(4,36,(juce::uint8)12),2);add(bass,juce::MidiMessage::endOfTrack(),4);
        write(source,{conductor,keys,bass});Session s(false);const auto initial=s.snapshot().tracks.size();std::vector<juce::String>ids;
        ok(s.importMidiFile(source,2,ids),"import multitrack multichannel performance");auto state=s.snapshot();
        check(ids.size()==3&&state.tracks.size()==initial+3&&state.tempo==120,"channel parts become separate tracks without changing project tempo");
        std::set<juce::String>noteIds;
        for(size_t i=0;i<ids.size();++i)
        {
            const auto& track=state.tracks[initial+i];const auto& clip=track.clips[0];
            check(track.isMidi&&clip.id==ids[i]&&clip.startBeat==2&&clip.lengthBeats==4&&clip.notes.size()==1,"imported clips retain placement and source duration");
            check(clip.notes[0].id.isNotEmpty()&&noteIds.insert(clip.notes[0].id).second,"imported notes have unique editable identities");
            ok(s.exportMidiClip(ids[i],output),"export imported channel part");
            check(exported(output)==events(i==2?bass:keys,960,i==0?2:i==1?9:4,i!=1),"round trip preserves timing, channel, all event bytes and release velocity");
            if(i==0)
            {
                juce::FileInputStream input(output);juce::MidiFile file;check(file.readFrom(input,false),"read coincident event ordering");
                std::vector<juce::MidiMessage> atZero;
                for(int n=0;n<file.getTrack(0)->getNumEvents();++n){const auto& m=file.getTrack(0)->getEventPointer(n)->message;if(!m.isMetaEvent()&&m.getTimeStamp()==0)atZero.push_back(m);}
                check(atZero.size()==3&&atZero[0].isSysEx()&&atZero[1].isProgramChange()&&atZero[2].isNoteOn(),"patch and system setup precede the note at a shared timestamp");
            }

        }
        s.undo();check(s.snapshot().tracks.size()==initial,"one undo removes entire multitrack import");s.redo();
        check(s.snapshot().tracks[initial].clips[0].id==ids[0],"redo restores imported clip identities");
        ok(s.saveProject(project),"save imported performance");ok(s.loadProject(project),"reopen imported notes and controllers");
        ok(s.exportMidiClip(ids[0],output),"export after project reopen");check(exported(output)==events(keys,960,2),"native project preserves controller and SysEx content");
        check(!s.snapshot().dirty&&!s.canUndo(),"MIDI export creates no document edit");
        ok(s.renameTrack((int)initial,"Changed"),"prepare redo preservation");s.undo();
        for(int bad=0;bad<5;++bad)
        {
            auto invalid=keys;
            if(bad==2)add(invalid,juce::MidiMessage::noteOn(2,61,(juce::uint8)80),2);
            if(bad==3)add(invalid,juce::MidiMessage::noteOn(2,60,(juce::uint8)80),.5);
            if(bad==4)add(invalid,juce::MidiMessage::noteOff(2,62),2);
            const auto malformed=root.getChildFile("invalid.mid");write(malformed,{invalid},bad==0?2:0,bad==1);
            std::vector<juce::String>unchanged{"keep"};check(s.importMidiFile(malformed,0,unchanged).failed()&&unchanged==std::vector<juce::String>{"keep"}
                &&s.canRedo()&&s.snapshot().tracks.size()==initial+3,"invalid format or note pairing preserves project, redo and output IDs");
        }
        const auto excessive=root.getChildFile("too-many-tracks.mid");write(excessive,std::vector<juce::MidiMessageSequence>(65,bass));
        std::vector<juce::String> rejectedIds{"keep"};
        check(s.importMidiFile(excessive,0,rejectedIds).failed()&&rejectedIds==std::vector<juce::String>{"keep"}&&s.canRedo()
            &&s.snapshot().tracks.size()==initial+3,"excessive track import rejects the complete file before adding tracks");
        const auto oversized=root.getChildFile("oversized.mid");
        {auto stream=oversized.createOutputStream();check(stream&&stream->setPosition(16*1024*1024)&&stream->writeByte(0),"prepare bounded-size input fixture");}
        check(s.importMidiFile(oversized,0,rejectedIds).failed()&&s.canRedo(),"oversized MIDI input leaves active edit and redo unchanged");
        const auto invalid=root.getChildFile("broken.mid");check(invalid.replaceWithText("not a MIDI file"),"write malformed input");std::vector<juce::String>untouched;
        check(s.importMidiFile(invalid,0,untouched).failed()&&s.importMidiFile(source,-1,untouched).failed()&&s.canRedo(),"malformed data and invalid placement leave redo intact");
        check(output.replaceWithText("keep existing output"),"prepare existing export destination");
        check(s.exportMidiClip("missing",output).failed()&&output.loadFileAsString()=="keep existing output"&&s.canRedo(),"failed export preserves destination and history");
        const auto directory=root.getChildFile("existing directory");ok(directory.createDirectory(),"prepare directory export rejection");
        check(s.exportMidiClip(ids[0],directory).failed()&&directory.isDirectory()&&s.canRedo(),"export cannot replace a directory");
        const auto note=state.tracks[initial].clips[0].notes[0];
        ok(s.updateMidiNote(ids[0],note.id,65,.25,.5,.5f),"edit an imported note through the existing piano model");
        ok(s.exportMidiClip(ids[0],output),"export edited imported note");
        {juce::FileInputStream input(output);juce::MidiFile midi;check(midi.readFrom(input),"read edited interchange result");bool found=false;
        for(int i=0;i<midi.getTrack(0)->getNumEvents();++i){const auto& m=midi.getTrack(0)->getEventPointer(i)->message;if(m.isNoteOn())found=m.getNoteNumber()==65&&m.getVelocity()==64&&m.getTimeStamp()==2400;}
        check(found,"export uses current edited note values and musical time");}
        s.undo();ok(s.exportMidiClip(ids[0],output),"export imported note edit undo");check(exported(output)==events(keys,960,2),"undo restores imported note while retaining controllers and release velocity");
        ok(s.addMidiClipToSlot(s.snapshot().tracks[initial].id,0,4),"prepare Session MIDI source export");
        const auto slotId=s.snapshot().tracks[initial].slots[0].clip.id;
        ok(s.addMidiNote(slotId,72,.5,1,1),"add editable note to Session source");ok(s.exportMidiClip(slotId,output),"export Session clip source events");
        juce::MidiMessageSequence slotExpected;add(slotExpected,juce::MidiMessage::noteOn(1,72,(juce::uint8)127),.5);add(slotExpected,juce::MidiMessage::noteOff(1,72),1.5);
        check(exported(output)==events(slotExpected,960),"Session export retains one source cycle without baking launch state");
        Session longSong(false);ok(longSong.addMidiTrack("Long source"),"prepare bounded MIDI export");
        ok(longSong.addMidiClip(longSong.snapshot().tracks.back().id,0,30000),"create source longer than standard delta bound at export resolution");
        check(output.replaceWithText("keep long-export destination"),"prepare bounded export destination");
        check(longSong.exportMidiClip(longSong.snapshot().tracks.back().clips[0].id,output).failed()&&output.loadFileAsString()=="keep long-export destination","oversized MIDI timestamp rejects before replacing destination");
        Session imported(false),manual(false);juce::MidiMessageSequence notes;
        add(notes,juce::MidiMessage::noteOn(1,60,(juce::uint8)100),0);add(notes,juce::MidiMessage::noteOff(1,60),1);
        add(notes,juce::MidiMessage::noteOn(1,64,(juce::uint8)75),1.5);add(notes,juce::MidiMessage::noteOff(1,64),2.5);add(notes,juce::MidiMessage::endOfTrack(),4);
        write(source,{notes},0);ok(imported.importMidiFile(source,2,untouched),"import type 0 note performance");
        ok(manual.addMidiTrack("Oracle"),"build independent note oracle track");const auto trackId=manual.snapshot().tracks.back().id;
        ok(manual.addMidiClip(trackId,2,4),"create independent clip bounds");const auto clipId=manual.snapshot().tracks.back().clips[0].id;
        ok(manual.addMidiNote(clipId,60,0,1,100.0f/127),"add expected first note");ok(manual.addMidiNote(clipId,64,1.5,1,75.0f/127),"add expected second note");
        const auto a=root.getChildFile("imported.wav"),b=root.getChildFile("manual.wav");ok(imported.exportWav(a),"render imported MIDI through synth");ok(manual.exportWav(b),"render independently drawn note oracle");
        const auto actual=readAudio(a),expected=readAudio(b);check(actual.getNumSamples()==expected.getNumSamples(),"MIDI render durations agree");double worst=0,peak=0;
        for(int c=0;c<2;++c)for(int n=0;n<actual.getNumSamples();++n){const auto value=actual.getSample(c,n);if(!std::isfinite(value))throw std::runtime_error("Nonfinite imported sample");worst=std::max(worst,std::abs((double)value-expected.getSample(c,n)));peak=std::max(peak,std::abs((double)value));}
        check(peak>.01&&worst<2e-6,"every imported synth sample matches independently created notes");
        std::cout<<"MIDI file tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL MIDI files: "<<e.what()<<std::endl;return 1;}
}
}
