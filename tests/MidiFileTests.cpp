#include "Session.h"
#include <cmath>
#include <iostream>
#include <set>
#include <limits>
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
void clipClipboardTests(const juce::File& root)
{
    juce::MidiMessageSequence original;add(original,juce::MidiMessage::noteOn(3,60,(juce::uint8)95),0);
    add(original,juce::MidiMessage::controllerEvent(3,1,55),.25);const unsigned char payload[]{0x7d,7,8};
    add(original,juce::MidiMessage::createSysExMessage(payload,3),.5);add(original,juce::MidiMessage::noteOff(3,60,(juce::uint8)33),1);add(original,juce::MidiMessage::endOfTrack(),2);
    const auto file=root.getChildFile("clipboard-source.mid"),output=root.getChildFile("clipboard-export.mid"),project=root.getChildFile("clipboard.halftrack");
    write(file,{original},0);Session s(false);std::vector<juce::String>ids;ok(s.importMidiFile(file,2,ids),"import complete clip clipboard source");
    const auto sourceId=ids.front();ok(s.addMidiTrack("Destination"),"create MIDI paste target");const auto target=s.snapshot().tracks.back().id;
    ok(s.saveProject(project),"save clean clipboard fixture");ok(s.loadProject(project),"reopen clean clip clipboard fixture");
    const auto oldNote=s.snapshot().tracks[2].clips[0].notes[0].id;
    ok(s.copyMidiClip(sourceId),"copy entire MIDI clip");check(s.hasMidiClipClipboard()&&!s.snapshot().dirty&&!s.canUndo(),"whole clip copy creates no document history");
    juce::String pasted;ok(s.pasteMidiClip(target,3,pasted),"paste MIDI onto another track");
    auto clip=s.snapshot().tracks.back().clips[0];
    check(pasted!=sourceId&&clip.startBeat==6&&clip.lengthBeats==2&&clip.notes[0].id!=oldNote,"paste retains beats with fresh clip and note identities");
    ok(s.exportMidiClip(pasted,output),"inspect complete pasted MIDI state");check(exported(output)==events(original,960),"clip clipboard preserves controller, SysEx and release velocity");
    s.undo();check(s.snapshot().tracks.back().clips.empty(),"one undo removes the whole pasted clip");s.redo();check(s.snapshot().tracks.back().clips[0].id==pasted,"paste redo restores the same fresh identity");
    ok(s.copyMidiClip(pasted,true),"cut pasted MIDI clip");check(s.snapshot().tracks.back().clips.empty(),"cut removes one clip");s.undo();
    check(s.snapshot().tracks.back().clips[0].id==pasted&&s.canRedo(),"cut undo restores identity and content");
    for(double time:{-1.0,std::numeric_limits<double>::quiet_NaN(),172799.5})
    {juce::String unchanged="keep";check(s.pasteMidiClip(target,time,unchanged).failed()&&unchanged=="keep"&&s.canRedo(),"invalid MIDI paste preserves redo and output identity");}
    juce::String unchanged="keep";check(s.pasteMidiClip(s.snapshot().tracks[0].id,0,unchanged).failed()&&s.copyMidiClip("missing").failed()&&s.canRedo(),"wrong track or missing copy source preserves redo and clipboard");
    ok(s.newProject(),"switch projects with MIDI clip clipboard");s.setTempo(180);ok(s.addMidiTrack("New tempo"),"create new-project MIDI target");
    ok(s.pasteMidiClip(s.snapshot().tracks.back().id,2,pasted),"paste MIDI across projects at another tempo");clip=s.snapshot().tracks.back().clips[0];
    check(clip.startBeat==6&&std::abs(clip.lengthBeats-2)<1e-9&&std::abs(clip.length-2.0/3)<1e-9&&clip.notes[0].id!=oldNote,"cross-project paste preserves musical duration rather than old seconds");
    ok(s.saveProject(project),"save cross-project MIDI clipboard result");ok(s.loadProject(project),"reopen cross-project pasted MIDI");
    ok(s.exportMidiClip(pasted,output),"export reopened pasted clip");check(exported(output)==events(original,960),"portable MIDI clipboard content survives reopen without source-file dependence");
    Session oracle(false);oracle.setTempo(180);std::vector<juce::String>oracleIds;ok(oracle.importMidiFile(file,6,oracleIds),"independently import expected destination-tempo performance");
    const auto a=root.getChildFile("pasted-midi.wav"),b=root.getChildFile("clipboard-oracle.wav");ok(s.exportWav(a),"render copied clip at destination tempo");ok(oracle.exportWav(b),"render independent destination-tempo oracle");
    const auto actual=readAudio(a),expected=readAudio(b);check(actual.getNumSamples()==expected.getNumSamples(),"cross-project MIDI clipboard render duration");double worst=0;
    for(int c=0;c<2;++c)for(int n=0;n<actual.getNumSamples();++n)
    {const auto sample=actual.getSample(c,n);if(!std::isfinite(sample))throw std::runtime_error("nonfinite clipboard sample");worst=std::max(worst,std::abs((double)sample-expected.getSample(c,n)));}
    check(worst<2e-6,"every pasted MIDI synth sample agrees with destination-tempo oracle");
    juce::String duplicate;ok(s.duplicateMidiClip(pasted,duplicate),"duplicate MIDI after source");
    auto clips=s.snapshot().tracks.back().clips;check(clips.size()==2&&clips[1].id==duplicate&&clips[1].id!=pasted
        &&clips[1].notes[0].id!=clips[0].notes[0].id&&std::abs(clips[1].startBeat-8)<1e-9,"duplicate starts after original with independent note identities");
    ok(s.exportMidiClip(duplicate,output),"inspect MIDI duplicate content");check(exported(output)==events(original,960),"duplicate keeps complete native events");
    s.undo();check(s.snapshot().tracks.back().clips.size()==1,"duplicate undoes in one step");s.redo();check(s.snapshot().tracks.back().clips.back().id==duplicate,"duplicate redo keeps stable identity");
    const auto audio=root.getChildFile("clipboard-type.wav");
    {juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream>out(audio.createOutputStream());auto writer=wav.createWriterFor(out,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
    juce::AudioBuffer<float>buffer(2,480);buffer.clear();check(writer&&writer->writeFromAudioSampleBuffer(buffer,0,480),"write clipboard type fixture");}
    ok(s.importAudio(audio,0),"import audio clipboard counterpart");const auto audioId=s.snapshot().tracks[0].clips[0].id;
    ok(s.copyAudioClip(audioId),"audio copy replaces MIDI clip clipboard");check(s.hasAudioClipClipboard()&&!s.hasMidiClipClipboard(),"only most recently copied clip type is active");
    check(s.copyMidiClip(audioId).failed()&&s.hasAudioClipClipboard(),"failed MIDI copy retains prior audio clipboard");
    ok(s.duplicateMidiClip(pasted,duplicate),"duplicate without replacing held audio clipboard");check(s.hasAudioClipClipboard()&&!s.hasMidiClipClipboard(),"duplicate leaves the clipboard intact");
    ok(s.copyMidiClip(pasted),"MIDI copy replaces audio clipboard");check(s.hasMidiClipClipboard()&&!s.hasAudioClipClipboard(),"MIDI replaces the previous clip type");
    ok(s.addMidiClipToSlot(s.snapshot().tracks.back().id,0,4),"prepare unsupported slot clipboard source");
    check(s.copyMidiClip(s.snapshot().tracks.back().slots[0].clip.id).failed()&&s.hasMidiClipClipboard(),"Session clip copy rejection preserves the valid Arrangement clipboard");
}

void clipMovementTests(const juce::File& root)
{
    juce::MidiMessageSequence music;
    add(music,juce::MidiMessage::programChange(3,7),0);
    add(music,juce::MidiMessage::noteOn(3,57,(juce::uint8)100),.125);
    add(music,juce::MidiMessage::controllerEvent(3,1,31),.5);
    add(music,juce::MidiMessage::noteOff(3,57,(juce::uint8)18),1.5);
    add(music,juce::MidiMessage::endOfTrack(),2);
    const auto input=root.getChildFile("move-midi.mid"),output=root.getChildFile("moved-events.mid"),project=root.getChildFile("moved-midi.halftrack");
    write(input,{music},0);Session s(false);std::vector<juce::String>ids;ok(s.importMidiFile(input,2,ids),"import move source");
    const auto id=ids.front(),source=s.snapshot().tracks[2].id;
    const auto noteId=s.snapshot().tracks[2].clips[0].notes[0].id;
    ok(s.addMidiTrack("Destination instrument"),"create MIDI move target");const auto destination=s.snapshot().tracks[3].id;
    auto patch=s.snapshot().tracks[3].synth;patch.waveform=2;patch.cutoffHz=1700;patch.attackSeconds=.025f;patch.releaseSeconds=.08f;
    ok(s.setSynthSettings(destination,patch),"set distinct destination instrument");
    ok(s.setTrackGain(3,-9),"set destination mixer gain");ok(s.setTrackPan(3,-.4f),"set destination mixer pan");
    ok(s.addReturnTrack(),"prepare forbidden return target");const auto returnId=s.snapshot().tracks.back().id;
    ok(s.saveProject(project),"save clean MIDI move baseline");ok(s.loadProject(project),"reopen MIDI move baseline");
    ok(s.moveMidiClip(id,destination,.25),"move MIDI clip between tracks and in time");auto state=s.snapshot();
    check(state.tracks[2].clips.empty()&&state.tracks[3].clips.size()==1&&state.tracks[3].clips[0].id==id
        &&state.tracks[3].clips[0].notes[0].id==noteId&&state.tracks[3].clips[0].start==.25&&state.tracks[3].clips[0].lengthBeats==2,
        "MIDI reparenting retains clip identity, note identity and musical duration");
    check(state.tracks[2].synth.waveform==0&&state.tracks[3].synth.waveform==2,"moving MIDI leaves instruments on their original tracks");
    ok(s.exportMidiClip(id,output),"inspect moved native MIDI events");check(exported(output)==events(music,960),"track move preserves event bytes and release velocity");
    Session oracle(false);std::vector<juce::String>expectedIds;ok(oracle.importMidiFile(input,.5,expectedIds),"independently place expected performance");
    ok(oracle.setSynthSettings(oracle.snapshot().tracks.back().id,patch),"prepare expected destination instrument");
    ok(oracle.setTrackGain(2,-9),"prepare expected destination gain");ok(oracle.setTrackPan(2,-.4f),"prepare expected destination pan");
    const auto a=root.getChildFile("moved-midi.wav"),b=root.getChildFile("moved-midi-oracle.wav");
    ok(s.exportWav(a),"render MIDI through destination instrument and mixer");ok(oracle.exportWav(b),"render independent destination oracle");
    const auto actual=readAudio(a),expected=readAudio(b);check(actual.getNumSamples()==expected.getNumSamples(),"moved MIDI render retains expected duration");
    double worst=0,peak=0;for(int c=0;c<2;++c)for(int n=0;n<actual.getNumSamples();++n)
    {const auto value=actual.getSample(c,n);if(!std::isfinite(value))throw std::runtime_error("Nonfinite moved MIDI sample");worst=std::max(worst,std::abs((double)value-expected.getSample(c,n)));peak=std::max(peak,std::abs((double)value));}
    check(peak>.001&&worst<2e-6,"every moved MIDI sample matches the destination instrument and mixer oracle");
    s.undo();state=s.snapshot();check(state.tracks[2].clips[0].id==id&&state.tracks[2].clips[0].start==1&&state.tracks[3].clips.empty(),"one undo restores MIDI owner and time");
    for(const auto& target:juce::StringArray{"missing",state.tracks[0].id,returnId})
        check(s.moveMidiClip(id,target,0).failed()&&s.canRedo(),"invalid MIDI destination preserves redo");
    for(double time:{-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),172799.5})
        check(s.moveMidiClip(id,destination,time).failed()&&s.canRedo(),"invalid MIDI move time preserves redo");
    check(s.moveMidiClip("missing",destination,0).failed()&&s.canRedo(),"stale MIDI move identity leaves history intact");
    s.redo();check(s.snapshot().tracks[3].clips[0].id==id,"MIDI move redo restores original identity on destination");
    ok(s.saveProject(project),"save moved MIDI owner");ok(s.loadProject(project),"reopen moved MIDI owner");
    ok(s.moveMidiClip(id,destination,.25),"identical MIDI move is a no-op");check(!s.canUndo()&&!s.snapshot().dirty,"identical MIDI move keeps a clean project");
    ok(s.exportMidiClip(id,output),"inspect reopened moved events");check(exported(output)==events(music,960),"save and reopen retain all moved events");
    s.beginEditGesture("Move MIDI through tracks");ok(s.moveMidiClip(id,source,.5),"move gesture first owner");ok(s.moveMidiClip(id,destination,.75),"move gesture second owner");s.endEditGesture();
    s.undo();check(s.snapshot().tracks[3].clips[0].id==id&&s.snapshot().tracks[3].clips[0].start==.25&&s.snapshot().tracks[2].clips.empty(),"one undo restores a multi-owner MIDI gesture");
    ok(s.moveMidiClip(id,destination,172799),"move MIDI exactly to native timeline end");check(s.snapshot().tracks[3].clips[0].start==172799,"boundary MIDI move is not silently clamped");s.undo();
    ok(s.addMidiClipToSlot(source,0,4),"prepare unsupported slot move source");
    check(s.moveMidiClip(s.snapshot().tracks[2].slots[0].clip.id,destination,0).failed(),"Arrangement movement cannot evict a Session MIDI clip");
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
        clipClipboardTests(root);
        clipMovementTests(root);
        std::cout<<"MIDI file tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL MIDI files: "<<e.what()<<std::endl;return 1;}
}
}
