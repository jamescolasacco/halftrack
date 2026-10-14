#include "Session.h"
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
        std::cout<<"Note tools passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL note tools: "<<e.what()<<std::endl;return 1;}
}
}
