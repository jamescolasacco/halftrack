#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace halftrack
{
namespace
{
using Transform = Session::NoteTransform;
void check (bool b, const juce::String& message)
{ if (! b) throw std::runtime_error(message.toStdString()); std::cout << "PASS note tools: " << message << std::endl; }
void ok (const juce::Result& r, const juce::String& message) { check(r.wasOk(),message+(r.failed()?": "+r.getErrorMessage():"")); }
bool same (std::vector<Session::MidiNoteInfo> a, std::vector<Session::MidiNoteInfo> b)
{
    const auto order=[](const auto& x,const auto& y){return x.id<y.id;};std::sort(a.begin(),a.end(),order);std::sort(b.begin(),b.end(),order);
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(a[i].id!=b[i].id||a[i].pitch!=b[i].pitch||std::abs(a[i].startBeat-b[i].startBeat)>1e-10||std::abs(a[i].lengthBeats-b[i].lengthBeats)>1e-10||a[i].velocity!=b[i].velocity)return false;
    return true;
}
std::vector<Session::MidiNoteInfo> notesFor(Session& s,const juce::String& id)
{
    for(const auto& t:s.snapshot().tracks){for(const auto& c:t.clips)if(c.id==id)return c.notes;for(const auto& slot:t.slots)if(slot.clip.id==id)return slot.clip.notes;}
    throw std::runtime_error("Fixture clip missing");
}
juce::AudioBuffer<float> activationRender (Session& session, const juce::File& file)
{
    ok (session.exportWav (file), "render note activation through the production graph");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->sampleRate == 48000 && reader->numChannels == 2 && reader->lengthInSamples == 96000, "note activation retains complete two-second extent");
    juce::AudioBuffer<float> output (2, 96000);
    check (reader->read (&output, 0, 96000, 0, true, true), "read every note activation frame");
    return output;
}
void noteActivationTests (const juce::File& root)
{
    for (bool slot : {false, true})
    {
        Session s (false); ok (s.addMidiTrack ("Note activation"), "create activation instrument");
        const auto track = s.snapshot().tracks.back().id;
        ok (slot ? s.addMidiClipToSlot (track, 0, 4) : s.addMidiClip (track, 0, 4), "create note activation clip");
        const auto clip = slot ? s.snapshot().tracks.back().slots[0].clip.id : s.snapshot().tracks.back().clips[0].id;
        juce::StringArray ids;
        ok (s.addMidiNotes (clip, {{"",60,0,3,.8f},{"",64,1,.5,.6f}}, ids), "draw active note pair");
        const auto original = notesFor (s, clip);
        ok (s.setMidiNotesActive (clip, {ids[0]}, false), "deactivate only selected note");
        auto notes = notesFor (s, clip);
        check (same (notes, original) && !notes[0].active && notes[1].active, "activation retains identity, pitch, geometry, velocity and other notes");
        s.undo(); check (notesFor(s,clip)[0].active && notesFor(s,clip)[1].active, "one undo restores note activation");
        s.redo(); check (!notesFor(s,clip)[0].active && notesFor(s,clip)[1].active, "redo restores only selected inactive note");
        const auto project = root.getChildFile (slot ? "note-state-session.halftrack" : "note-state-arrangement.halftrack");
        ok (s.saveProject (project), "save mixed note activation"); ok (s.loadProject (project), "reopen note activation");
        check (!notesFor(s,clip)[0].active && notesFor(s,clip)[1].active, "note activation survives project reopening");
        ok (s.setMidiNotesActive (clip, {ids[0]}, false), "equal activation is a no-op");
        ok (s.setMidiNotesActive (clip, {}, true), "empty selection is a no-op");
        check (!s.snapshot().dirty && !s.canUndo(), "no-op activation preserves clean state");
        auto changed = notesFor(s,clip)[0]; changed.pitch=61;
        ok (s.updateMidiNotes (clip,{changed}), "edit inactive note geometry");
        check (!notesFor(s,clip)[0].active, "ordinary note edits preserve inactive state"); s.undo();
        for (const auto& invalid : {juce::StringArray{ids[0],"missing"},juce::StringArray{ids[0],ids[0]},juce::StringArray{""}})
            check (s.setMidiNotesActive(clip,invalid,true).failed() && !notesFor(s,clip)[0].active && s.canRedo(), "invalid selection rejects atomically and preserves redo");
        check (s.setMidiNotesActive("missing",ids,false).failed() && s.canRedo(), "missing clip preserves undo history");
        const auto locate = [&] (auto&& self, juce::XmlElement& node) -> juce::XmlElement*
        {
            if(node.hasTagName("NOTE")&&node.getStringAttribute("halftrackNoteId")==ids[0])return &node;
            for(auto* child=node.getFirstChildElement();child;child=child->getNextElement())if(auto* found=self(self,*child))return found;
            return nullptr;
        };
        for(const auto& invalid : juce::StringArray{"2","-1","nan","true","0 trailing",""})
        {
            auto xml=juce::XmlDocument::parse(project);auto* note=locate(locate,*xml);check(note!=nullptr,"locate native saved note mute");
            note->setAttribute("m",invalid);const auto bad=root.getChildFile("invalid-note-activation.halftrack");check(xml->writeTo(bad),"write invalid note activation fixture");
            check(s.loadProject(bad).failed() && s.canRedo() && !notesFor(s,clip)[0].active,"invalid note mute preserves current document and history");
        }
        const auto midi = root.getChildFile("active-only.mid");ok(s.exportMidiClip(clip,midi),"export only active MIDI events");
        auto input=midi.createInputStream();juce::MidiFile file;check(input && file.readFrom(*input),"decode actual exported MIDI");int noteOns=0;
        for(int t=0;t<file.getNumTracks();++t)for(int n=0;n<file.getTrack(t)->getNumEvents();++n)
            if(const auto& message=file.getTrack(t)->getEventPointer(n)->message;message.isNoteOn()){++noteOns;check(message.getNoteNumber()==64,"inactive note is absent from MIDI export");}
        check(noteOns==1,"MIDI export contains exactly the enabled note");
        if(!slot)
        {
            const auto actual=activationRender(s,root.getChildFile("note-actual.wav"));
            Session oracle(false);ok(oracle.addMidiTrack("Independent active notes"),"create independent activation oracle");
            const auto oracleTrack=oracle.snapshot().tracks.back().id;ok(oracle.addMidiClip(oracleTrack,0,4),"create complete oracle duration");
            ok(oracle.addMidiNote(oracle.snapshot().tracks.back().clips[0].id,64,1,.5,.6f),"draw only the expected active note");
            const auto expected=activationRender(oracle,root.getChildFile("note-expected.wav"));double error=0;
            for(int c=0;c<2;++c)for(int n=0;n<96000;++n){const auto sample=actual.getSample(c,n);if(!std::isfinite(sample))throw std::runtime_error("nonfinite note render");error=std::max(error,std::abs(double(sample)-expected.getSample(c,n)));}
            check(actual.getMagnitude(0,96000)>.01 && error<2e-6,"complete output matches independently drawn enabled notes");
            ok(s.setMidiNotesActive(clip,ids,false),"disable every note");const auto silence=activationRender(s,root.getChildFile("note-silent.wav"));
            check(silence.getMagnitude(0,96000)==0&&silence.getMagnitude(1,0,96000)==0,"every frame is silent when all notes are inactive");s.undo();
        }
        notes=notesFor(s,clip);for(auto& note:notes)note.startBeat+=.5;
        juce::StringArray inserted;ok(s.addMidiNotes(clip,notes,inserted),"paste independent notes with their activation state");
        const auto pasted=notesFor(s,clip);bool retained=false;
        for(const auto& note:pasted)if(note.id==inserted[0])retained=!note.active&&note.id!=ids[0];
        check(retained,"note clipboard insertion preserves mute with a new identity");s.undo();
        juce::StringArray split;ok(s.splitMidiNotes(clip,{ids[0]},1,split),"split inactive note");
        int inactive=0;for(const auto& note:notesFor(s,clip))if(!note.active)++inactive;
        check(split.size()==1&&inactive==2,"both halves retain inactive state");
    }
}
void playingNoteActivationTests()
{
    namespace te=tracktion::engine;
    for(bool slot:{false,true})for(bool sibling:{false,true})
    {
        Session s(false);auto& engine=dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&>(s.audioDeviceManager()).engine;
        te::HostedAudioDeviceInterface::Parameters p;p.sampleRate=48000;p.blockSize=128;p.inputChannels=0;p.outputChannels=2;p.useMidiDevices=false;
        te::test_utilities::EnginePlayer player(engine,p);juce::String clip;juce::StringArray ids;
        for(int t=2;t<4;++t)
        {
            ok(s.addMidiTrack("Independent voice"),"create note activation playback instrument");auto track=s.snapshot().tracks.back();auto synth=track.synth;
            synth.attackSeconds=synth.decaySeconds=0;synth.sustain=1;synth.releaseSeconds=.01f;ok(s.setSynthSettings(track.id,synth),"set bounded note release");
            ok(s.setTrackPan(t,t==2?-1.0f:1.0f),"isolate playback voices by channel");
            ok(slot?s.addMidiClipToSlot(track.id,0,4):s.addMidiClip(track.id,0,4),"create held note clip");track=s.snapshot().tracks.back();
            const auto id=slot?track.slots[0].clip.id:track.clips[0].id;juce::StringArray added;ok(s.addMidiNotes(id,{{"",sibling&&t==3?67:60,0,4,1}},added),"draw held voice");
            if(sibling&&t==2)ok(s.addMidiNote(id,67,0,4,1),"draw unselected held note in the same clip");
            if(t==2){clip=id;ids=added;}
        }
        const auto dispatch=[&]{for(auto* edit:engine.getActiveEdits().getEdits())edit->dispatchPendingUpdatesSynchronously();};
        ok(s.setLaunchQuantisation(Session::LaunchQuantisation::none),"set immediate note activation launch");
        ok(slot?s.launchScene(0):s.play(),"start independent held notes");dispatch();const auto before=player.process(12032);
        check(before.getMagnitude(0,0,12032)>.01&&before.getMagnitude(1,0,12032)>.01,"both held voices are initially audible");
        ok(s.setMidiNotesActive(clip,ids,false),"deactivate sounding note");dispatch();player.process(2048);const auto after=player.process(4096);
        const auto verifyRelease=[&](const juce::AudioBuffer<float>& audio)
        {
            double error=0;
            for(int n=0;n<audio.getNumSamples();++n)
            {
                const auto left=audio.getSample(0,n),right=audio.getSample(1,n);
                if(!std::isfinite(left)||!std::isfinite(right))throw std::runtime_error("nonfinite note transition");
                error=std::max(error,std::abs(double(left)-(sibling?right:0.0)));
            }
            check(audio.getMagnitude(1,0,audio.getNumSamples())>.01&&(sibling?error<2e-6:error==0),
                "only selected held voice releases; remaining note matches independent channel; error="+juce::String(error,12));
        };
        verifyRelease(after);
        s.undo();check(notesFor(s,clip)[0].active,"undo restores active note while playing");dispatch();
        s.stop();s.seek(0);ok(slot?s.launchScene(0):s.play(),"restart restored notes");dispatch();check(player.process(8192).getMagnitude(0,0,8192)>.01,"restored note plays after undo");
        s.redo();dispatch();player.process(2048);verifyRelease(player.process(4096));s.stop();
    }
}
void playingSplitTests()
{
    namespace te=tracktion::engine;
    for(bool slot:{false,true})
    {
        Session s(false);auto& engine=dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&>(s.audioDeviceManager()).engine;
        te::HostedAudioDeviceInterface::Parameters parameters;parameters.sampleRate=48000;parameters.blockSize=128;parameters.inputChannels=0;parameters.outputChannels=2;parameters.useMidiDevices=false;
        te::test_utilities::EnginePlayer player(engine,parameters);
        ok(s.addMidiTrack("Playing split"),"create running split instrument");auto track=s.snapshot().tracks.back();auto synth=track.synth;
        synth.attackSeconds=synth.decaySeconds=0;synth.sustain=1;synth.releaseSeconds=.01f;ok(s.setSynthSettings(track.id,synth),"prepare release timing");
        ok(slot?s.addMidiClipToSlot(track.id,0,8):s.addMidiClip(track.id,0,8),"create running split clip");track=s.snapshot().tracks.back();const auto clip=slot?track.slots[0].clip.id:track.clips[0].id;
        juce::StringArray notes;ok(s.addMidiNotes(clip,{{"",60,0,2,1}},notes),"draw playing held note");
        ok(s.setLaunchQuantisation(Session::LaunchQuantisation::none),"set immediate launch");if(slot)ok(s.launchSlot(2,0),"launch split target");else ok(s.play(),"play split target");
        const auto dispatch=[&]{for(auto* edit:engine.getActiveEdits().getEdits())edit->dispatchPendingUpdatesSynchronously();};dispatch();
        check(player.process(12032).getMagnitude(0,12032)>.01,"held note is sounding before split");
        juce::StringArray inserted;ok(s.splitMidiNotes(clip,notes,1,inserted),"split while its note is sounding");dispatch();
        check(player.process(18048).getMagnitude(0,18048)>.01,"split playback continues through marker");
        check(player.process(4096).getMagnitude(0,4096)>.01,"right half sounds after marker");
        player.process(16000);check(player.process(8192).getMagnitude(0,8192)==0,"split right half releases with no stuck voice");
        s.undo();check(notesFor(s,clip).size()==1,"undo running split restores original note");s.stop();
    }
}
void splitTests(const juce::File& root)
{
    for(bool slots:{false,true})
    {
        Session s(false);ok(s.addMidiTrack("Split"),"create split instrument");const auto track=s.snapshot().tracks.back().id;
        ok(slots?s.addMidiClipToSlot(track,0,8):s.addMidiClip(track,0,8),"create split clip");
        const auto clip=slots?s.snapshot().tracks.back().slots[0].clip.id:s.snapshot().tracks.back().clips[0].id;
        juce::StringArray ids;ok(s.addMidiNotes(clip,{{"",60,0,4,.8f},{"",64,1,4,.5f},{"",67,5,1,.7f}},ids),"create overlapping split fixture");
        const auto findNote=[](auto&& self,juce::XmlElement& node,const juce::String& id)->juce::XmlElement*
        {if(node.hasTagName("NOTE")&&node.getStringAttribute("halftrackNoteId")==id)return &node;for(auto* child=node.getFirstChildElement();child;child=child->getNextElement())if(auto* result=self(self,*child,id))return result;return nullptr;};
        const auto before=notesFor(s,clip);juce::StringArray right{"keep"};
        const auto unchanged=[&]{return same(notesFor(s,clip),before);};
        const auto file=root.getChildFile(slots?"split-session.halftrack":"split-arrangement.halftrack");ok(s.saveProject(file),"save split baseline");
        auto originalXml=juce::XmlDocument::parse(file);auto* metadata=findNote(findNote,*originalXml,ids[0]);check(metadata!=nullptr,"find source note metadata");
        metadata->setAttribute("lift",73);metadata->setAttribute("c",9);metadata->setAttribute("m",1);metadata->setAttribute("halftrackTestMetadata","preserved");check(originalXml->writeTo(file),"write generated note metadata");
        ok(s.loadProject(file),"start split with clean history");
        ok(s.splitMidiNotes(clip,ids,2,right),"split selected notes at beat two");
        auto split=notesFor(s,clip);check(split.size()==5&&right.size()==2,"only intersecting notes split");
        const auto find=[&](const auto& list,const juce::String& id)->Session::MidiNoteInfo{for(const auto& n:list)if(n.id==id)return n;throw std::runtime_error("split note missing");};
        for(int n=0;n<2;++n)
        {
            const auto left=find(split,ids[n]),tail=find(split,right[n]),old=find(before,ids[n]);
            check(left.startBeat==old.startBeat&&left.lengthBeats==2-old.startBeat&&tail.startBeat==2&&tail.lengthBeats==old.startBeat+old.lengthBeats-2
                &&tail.pitch==old.pitch&&left.velocity==old.velocity&&tail.velocity==old.velocity&&!ids.contains(tail.id),"split preserves geometry, pitch, velocity and distinct identity");
        }
        check(find(split,ids[2]).startBeat==5&&find(split,ids[2]).lengthBeats==1,"nonintersecting selection is unchanged");
        s.undo();check(unchanged()&&!s.canUndo()&&s.canRedo(),"one undo rejoins the entire split");s.redo();check(same(notesFor(s,clip),split),"redo retains split identities");
        ok(s.saveProject(file),"save split result");
        auto saved=juce::XmlDocument::parse(file);
        for(const auto& id:juce::StringArray{ids[0],right[0]})
        {const auto* note=findNote(findNote,*saved,id);check(note&&note->getIntAttribute("lift")==73&&note->getIntAttribute("c")==9&&note->getIntAttribute("m")==1&&note->getStringAttribute("halftrackTestMetadata")=="preserved","both halves preserve native release velocity, colour, activation and additional properties");}
        ok(s.loadProject(file),"reopen split result");check(same(notesFor(s,clip),split),"split notes survive project reopening");
        ok(s.splitMidiNotes(clip,{},2,right),"empty split selection");check(right.isEmpty()&&!s.canUndo()&&!s.snapshot().dirty,"empty split does not dirty or add history");
        ok(s.splitMidiNotes(clip,{ids[0]},0,right),"split at start is a no-op");check(!s.canUndo()&&!s.snapshot().dirty,"boundary split creates no zero-length notes");
        ok(s.splitMidiNotes(clip,{ids[2]},5.5,right),"create redo for validation checks");s.undo();
        for(const auto selection:{juce::StringArray{ids[0],"missing"},juce::StringArray{ids[0],ids[0]}})
        {right={"keep"};check(s.splitMidiNotes(clip,selection,1,right).failed()&&right==juce::StringArray{"keep"}&&same(notesFor(s,clip),split)&&s.canRedo(),"invalid IDs reject the entire split and retain redo/output");}
        for(double position:{-1.,9.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),1./1920})
        {right={"keep"};check(s.splitMidiNotes(clip,{ids[0]},position,right).failed()&&right==juce::StringArray{"keep"}&&same(notesFor(s,clip),split)&&s.canRedo(),"invalid/subminimum split rejects without edits");}
        auto expression=std::make_unique<juce::XmlElement>(*saved);auto* protectedNote=findNote(findNote,*expression,ids[1]);check(protectedNote!=nullptr,"find note for expression guard");
        protectedNote->createNewChildElement("testExpression")->setAttribute("value",.5);
        const auto protectedFile=root.getChildFile("split-expression.halftrack");check(expression->writeTo(protectedFile),"write generated expression fixture");
        ok(s.loadProject(protectedFile),"load note carrying extra expression state");right={"keep"};
        check(s.splitMidiNotes(clip,{ids[0],ids[1]},1.5,right).failed()&&right==juce::StringArray{"keep"}&&same(notesFor(s,clip),split)&&!s.canUndo()&&!s.snapshot().dirty,"unsupported expression rejects before any other selected note changes");
        ok(s.loadProject(file),"restore ordinary split fixture");
        if(!slots)
        {
            Session oracle(false);ok(oracle.addMidiTrack("Oracle"),"create independent split oracle");const auto target=oracle.snapshot().tracks.back().id;ok(oracle.addMidiClip(target,0,8),"create oracle clip");
            juce::StringArray created;ok(oracle.addMidiNotes(oracle.snapshot().tracks.back().clips[0].id,{{"",64,1,1,.5f},{"",64,2,3,.5f},{"",67,5,1,.7f}},created),"draw oracle with independent note geometry");
            const auto actual=root.getChildFile("split-actual.wav"),expected=root.getChildFile("split-oracle.wav");ok(s.exportWav(actual),"render split instrument");ok(oracle.exportWav(expected),"render independently drawn oracle");
            juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> a(formats.createReaderFor(actual)),b(formats.createReaderFor(expected));
            check(a&&b&&a->lengthInSamples==192000&&b->lengthInSamples==192000&&a->numChannels==2&&b->numChannels==2,"rendered split retains complete clip duration");
            juce::AudioBuffer<float> x(2,192000),y(2,192000);check(a->read(&x,0,192000,0,true,true)&&b->read(&y,0,192000,0,true,true),"decode complete split outputs");double error=0;
            for(int c=0;c<2;++c)for(int n=0;n<192000;++n){const auto v=x.getSample(c,n);if(!std::isfinite(v))throw std::runtime_error("nonfinite split output");error=std::max(error,std::abs(double(v)-y.getSample(c,n)));}
            check(x.getMagnitude(0,192000)>.01&&error<2e-6,"every split audio sample matches independently drawn note retriggers");
        }
    }
}
}
int runNoteToolsTests (const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("note-tools-"+juce::Uuid().toString());ok(root.createDirectory(),"create fixtures");
        for(bool slot : {false,true})
        {
            Session s(false);ok(s.addMidiTrack("Tools"),"create track");auto track=s.snapshot().tracks.back();
            if(slot){if(s.snapshot().scenes.empty())ok(s.addScene("Notes"),"add scene");ok(s.addMidiClipToSlot(track.id,0,8),"create Session clip");}
            else ok(s.addMidiClip(track.id,4,8),"create arrangement clip away from time zero");
            track=s.snapshot().tracks.back();const auto clip=slot?track.slots[0].clip.id:track.clips[0].id;
            const std::vector<Session::MidiNoteInfo> source { {"",60,.13,.37,.5f},{"",64,.91,.19,.7f},{"",72,2.38,.62,.9f},{"",67,.91,.5,.6f} };
            juce::StringArray ids;ok(s.addMidiNotes(clip,source,ids),"insert irregular note and chord fixtures");const auto original=notesFor(s,clip);
            const auto byId=[&](const std::vector<Session::MidiNoteInfo>& notes,int i){for(const auto& n:notes)if(n.id==ids[i])return n;throw std::runtime_error("note missing");};
            const auto baseline=root.getChildFile(slot?"session.halftrack":"arrangement.halftrack");ok(s.saveProject(baseline),"save transformation baseline");
            const double expectedStarts[8][4]={{.25,1,2.5,1},{.13,.91,2.38,.91},{2.63,2.03,.13,1.72},{.13,.91,2.38,.91},{.13,.91,2.38,.91},{.13,.52,1.255,.52},{.13,1.69,4.63,1.69},{.13,.91,2.38,.91}};
            const double expectedLengths[8][4]={{.37,.19,.62,.5},{.37,.09,.62,.59},{.37,.19,.62,.5},{.37,.19,.62,.5},{.78,1.47,.62,1.47},{.185,.095,.31,.25},{.74,.38,1.24,1},{.25,.25,.25,.25}};
            for(int operation=0;operation<8;++operation)
            {
                ok(s.loadProject(baseline),"reload clean baseline");
                ok(s.transformMidiNotes(clip,ids,static_cast<Transform>(operation),.25),"apply complete group operation "+juce::String(operation));
                const auto result=notesFor(s,clip);bool correct=true;
                for(int i=0;i<4;++i)
                {
                    const auto n=byId(result,i),old=byId(original,i);const int pitch=operation==3?132-old.pitch:old.pitch;
                    correct=correct&&std::abs(n.startBeat-expectedStarts[operation][i])<1e-8&&std::abs(n.lengthBeats-expectedLengths[operation][i])<1e-8&&n.pitch==pitch&&n.velocity==old.velocity;
                }
                check(correct,"known group geometry, pitch, velocity and stable identity");
                s.undo();check(same(notesFor(s,clip),original),"one undo restores entire operation");s.redo();check(same(notesFor(s,clip),result),"one redo restores result");
                const auto changed=root.getChildFile("changed.halftrack");ok(s.saveProject(changed),"save transformed project");ok(s.loadProject(changed),"reopen transformed project");check(same(notesFor(s,clip),result),"transform round trips in both clip contexts");
            }
            ok(s.loadProject(baseline),"reload selection baseline");ok(s.transformMidiNotes(clip,{ids[0]},Transform::quantizeStarts,.25),"transform one selected note");
            auto selected=notesFor(s,clip);check(byId(selected,0).startBeat==.25&&byId(selected,1).startBeat==byId(original,1).startBeat,"selection leaves unselected notes untouched");
            s.undo();check(s.canRedo(),"establish retained redo");
            for(auto invalid : {juce::StringArray{ids[0],ids[0]},juce::StringArray{ids[0],"missing"}})
                check(s.transformMidiNotes(clip,invalid,Transform::reverse,.25).failed()&&same(notesFor(s,clip),original)&&s.canRedo(),"invalid selection rejects atomically and keeps redo");
            for(double invalid : {0.0,-1.0,17.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
                check(s.transformMidiNotes(clip,ids,Transform::reverse,invalid).failed()&&same(notesFor(s,clip),original)&&s.canRedo(),"invalid grid preserves state and redo");
            check(s.transformMidiNotes(clip,ids,static_cast<Transform>(99),.25).failed(),"invalid transform enum rejected");
            ok(s.transformMidiNotes(clip,{},Transform::reverse,.25),"empty API selection is a no-op");check(s.canRedo(),"empty operation does not invent history");
            auto atEdge=byId(original,2);atEdge.startBeat=7.38;atEdge.lengthBeats=.62;ok(s.updateMidiNotes(clip,{atEdge}),"place note at clip edge");
            ok(s.transformMidiNotes(clip,{ids[2]},Transform::quantizeStarts,1),"quantize at edge without clipping duration");check(std::abs(byId(notesFor(s,clip),2).startBeat-7)<1e-9,"nearest in-bounds start preserves note duration");
            const auto beforeStretch=notesFor(s,clip);check(s.transformMidiNotes(clip,ids,Transform::doubleTime,.25).failed()&&same(notesFor(s,clip),beforeStretch),"oversized time stretch rejects the whole selection");
            auto tiny=byId(beforeStretch,0);tiny.startBeat=.1234;tiny.lengthBeats=1.0/960;ok(s.updateMidiNotes(clip,{tiny}),"create minimum-length off-grid note");
            ok(s.transformMidiNotes(clip,{tiny.id},Transform::quantizeEnds,.25),"end quantization preserves positive minimum duration");check(byId(notesFor(s,clip),0).lengthBeats>=1.0/960,"minimum duration is never rounded below model bound");
        }
        splitTests(root);playingSplitTests();noteActivationTests(root);playingNoteActivationTests();
        std::cout<<"Note tools passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL note tools: "<<e.what()<<std::endl;return 1;}
}
}
