#include "Session.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>

namespace halftrack
{
void useHardClipEdges();
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool value, const juce::String& message)
{
    if (!value) throw std::runtime_error (message.toStdString());
    std::cout << "PASS track duplicate: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->numChannels == 2 && reader->sampleRate == 48000, "read complete generated stereo audio");
    juce::AudioBuffer<float> audio (2, (int)reader->lengthInSamples);
    check (reader->read (&audio, 0, audio.getNumSamples(), 0, true, true), "decode actual render"); return audio;
}
void compare (const juce::AudioBuffer<float>& actual, const juce::AudioBuffer<float>& source, double gain)
{
    check (actual.getNumSamples() == source.getNumSamples(), "render extent retained"); double worst = 0, peak = 0;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < actual.getNumSamples(); ++n)
    {
        const auto sample = actual.getSample (c, n);
        if (!std::isfinite (sample)) throw std::runtime_error ("Nonfinite duplicated-track sample");
        worst = std::max (worst, std::abs (sample-source.getSample(c,n)*gain)); peak = std::max (peak, std::abs ((double)sample));
    }
    check (peak > .001 && worst < 2e-6, "every sample matches the independent gain expectation");
}
void fresh (const Session::TrackInfo& source, const Session::TrackInfo& copy)
{
    check (source.id != copy.id && source.isMidi == copy.isMidi && !copy.isReturn && source.colour == copy.colour
        && source.gainDb == copy.gainDb && source.pan == copy.pan && source.mute == copy.mute && source.solo == copy.solo
        && source.clips.size() == copy.clips.size() && source.slots.size() == copy.slots.size()
        && source.plugins.size() == copy.plugins.size(), "duplicate has a new track identity and preserved controls/content dimensions");
    std::set<juce::String> identities { source.id, copy.id };
    const auto pair = [&] (const Session::ClipInfo& a, const Session::ClipInfo& b)
    {
        check (identities.insert(a.id).second && identities.insert(b.id).second && a.start==b.start && a.length==b.length
            && a.sourceOffset==b.sourceOffset && a.audio==b.audio && a.notes.size()==b.notes.size(), "clip IDs are independent with preserved timing/settings");
        for (size_t n=0;n<a.notes.size();++n)
            check (identities.insert(a.notes[n].id).second && identities.insert(b.notes[n].id).second
                && a.notes[n].pitch==b.notes[n].pitch && a.notes[n].startBeat==b.notes[n].startBeat
                && a.notes[n].lengthBeats==b.notes[n].lengthBeats && a.notes[n].velocity==b.notes[n].velocity,
                "all copied note identities are independent with unchanged content");
    };
    for (size_t i=0;i<source.clips.size();++i) pair(source.clips[i],copy.clips[i]);
    for (size_t i=0;i<source.slots.size();++i)
    {
        check(identities.insert(source.slots[i].id).second && identities.insert(copy.slots[i].id).second
            && source.slots[i].hasClip==copy.slots[i].hasClip && !copy.slots[i].playing && !copy.slots[i].queued,
            "Session slots have fresh identities without launching their copies");
        if (source.slots[i].hasClip) pair(source.slots[i].clip,copy.slots[i].clip);
    }
    for (size_t i=0;i<source.plugins.size();++i)
        check(identities.insert(source.plugins[i].id).second && identities.insert(copy.plugins[i].id).second
            && source.plugins[i].name==copy.plugins[i].name && source.plugins[i].enabled==copy.plugins[i].enabled
            && source.plugins[i].utility==copy.plugins[i].utility && source.plugins[i].effect==copy.plugins[i].effect,
            "device state and order copy with fresh identities");
    check (!copy.armed && copy.inputName.isEmpty() && copy.monitor==Session::MonitorMode::off && !copy.launcherActive,
        "duplicate has no recording input or runtime Session takeover");
}
void audioTests (const juce::File& root)
{
    const auto input=root.getChildFile("duplicate-source.wav"),output=root.getChildFile("duplicate-mix.wav"),project=root.getChildFile("duplicate.halftrack");
    {
        juce::AudioBuffer<float> audio(2,4800);for(int c=0;c<2;++c)for(int n=0;n<4800;++n)audio.setSample(c,n,((n*17+c*13)%97-48)/1024.0f);
        juce::WavAudioFormat format;std::unique_ptr<juce::OutputStream>stream(input.createOutputStream());
        auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        check(writer&&writer->writeFromAudioSampleBuffer(audio,0,4800),"write independent audio fixture");
    }
    const auto signal=read(input);Session session(false);ok(session.importAudio(input,0),"prepare duplicated source");
    ok(session.importAudioToSlot(input,0,1),"prepare audio Session slot");ok(session.addReturnTrack(),"prepare shared return");
    const auto sourceId=session.snapshot().tracks[0].id,returnId=session.snapshot().tracks[2].id;
    ok(session.addUtility(0),"insert original utility");auto utility=session.snapshot().tracks[0].plugins[0].utility;
    const auto db=[](float value){return 20.0f*std::log10(value);};utility.gainDb=db(.5f);
    ok(session.setUtilitySettings(session.snapshot().tracks[0].plugins[0].id,utility),"set utility before copying");
    const auto* path=std::getenv("HALFTRACK_TEST_PLUGIN");check(path&&juce::File(path).exists(),"generated VST3 fixture configured");
    juce::OwnedArray<juce::PluginDescription> descriptions;
    for(auto* format:session.pluginFormats().getFormats())if(format->getName()=="VST3")format->findAllTypesForFile(descriptions,juce::File(path).getFullPathName());
    check(descriptions.size()==1,"identify generated external fixture only");ok(session.addPlugin(0,*descriptions[0]),"host generated VST3 on source");
    check(session.snapshot().tracks[0].plugins.size()==2&&session.snapshot().tracks[0].plugins[0].isUtility&&!session.snapshot().tracks[0].plugins[1].isUtility,"the VST3 joins the end of the chain, after the utility");
    ok(session.setPluginParameter(0,1,0,.25f),"set nondefault external state");ok(session.setTrackGain(0,db(.5f)),"set source mixer");
    ok(session.setTrackGain(2,db(.5f)),"set return mixer");ok(session.setTrackSendGain(0,returnId,db(.5f)),"set copied send destination");
    ok(session.saveProject(project),"save original chain");ok(session.loadProject(project),"reopen clean original chain");
    ok(session.exportWav(output),"render original utility/VST/mixer/send chain");compare(read(output),signal,.078125);
    const auto before=session.snapshot();juce::String copyId;ok(session.duplicateTrack(sourceId,copyId),"duplicate complete audio track");
    auto copied=session.snapshot();check(copied.tracks.size()==4&&copied.tracks[1].id==copyId,"duplicate appears directly after source");fresh(before.tracks[0],copied.tracks[1]);
    check(copied.tracks[1].sends.size()==1&&copied.tracks[1].sends[0].returnTrackId==returnId
        &&copied.tracks[1].sends[0].gainDb==before.tracks[0].sends[0].gainDb,"copy retains external return destination and gain");
    ok(session.exportWav(output),"render both independent device instances");compare(read(output),signal,.15625);
    session.undo();check(session.snapshot().tracks.size()==3&&session.snapshot().tracks[0].id==sourceId,"single undo removes complete duplicate");
    juce::String untouched="keep";check(session.duplicateTrack("missing",untouched).failed()&&session.duplicateTrack(returnId,untouched).failed()
        &&untouched=="keep"&&session.canRedo(),"invalid source and return duplication preserve redo/output ID");
    session.redo();check(session.snapshot().tracks[1].id==copyId,"redo retains duplicate identity");
    utility.gainDb=0;ok(session.setUtilitySettings(session.snapshot().tracks[1].plugins[0].id,utility),"edit copied utility independently");
    ok(session.setPluginParameter(1,1,0,.75f),"edit copied VST3 independently");
    ok(session.exportWav(output),"render edited duplicate beside unchanged source");compare(read(output),signal,.546875);
    const auto portable=root.getChildFile("portable").getChildFile("copied.halftrack");ok(session.saveProject(portable),"collect duplicate media in portable project");
    ok(session.loadProject(portable),"reopen duplicated native and external devices");
    check(session.snapshot().tracks[1].id==copyId&&session.snapshot().tracks[0].plugins[0].utility.gainDb!=session.snapshot().tracks[1].plugins[0].utility.gainDb,
        "independent device settings persist after reopen");
    ok(session.removeTrack(0),"delete original without deleting copied content");ok(session.exportWav(output),"render surviving duplicate");compare(read(output),signal,.46875);
    session.undo();ok(session.exportWav(output),"render restored original plus independent copy");compare(read(output),signal,.546875);
}
void midiTests (const juce::File& root)
{
    Session session(false);ok(session.addMidiTrack("Subtractive"),"prepare MIDI source");const auto source=session.snapshot().tracks[2].id;
    ok(session.addMidiClip(source,0,2),"create original MIDI clip");auto state=session.snapshot();
    ok(session.addMidiNote(state.tracks[2].clips[0].id,60,0,1,.6f),"add original note");
    ok(session.addMidiClipToSlot(source,0,4),"create original MIDI slot");state=session.snapshot();
    ok(session.addMidiNote(state.tracks[2].slots[0].clip.id,67,.5,1,.5f),"add original Session note");
    auto patch=state.tracks[2].synth;patch.waveform=1;patch.cutoffHz=2300;patch.level=.08f;patch.pulseWidth=.2f;
    ok(session.setSynthSettings(source,patch),"prepare original synth sound");
    const auto original=root.getChildFile("original-synth.wav"),doubled=root.getChildFile("duplicated-synth.wav"),project=root.getChildFile("duplicated-midi.halftrack");
    ok(session.exportWav(original),"render original synth");const auto expected=read(original);state=session.snapshot();juce::String id;
    ok(session.duplicateTrack(source,id),"duplicate MIDI track with synth and both views");fresh(state.tracks[2],session.snapshot().tracks[3]);
    ok(session.exportWav(doubled),"render duplicated synthesizer instances");compare(read(doubled),expected,2);
    auto copy=session.snapshot().tracks[3];const auto note=copy.clips[0].notes[0];
    ok(session.updateMidiNote(copy.clips[0].id,note.id,64,0,.5,.8f),"edit copied note independently");
    ok(session.removeMidiNote(copy.slots[0].clip.id,copy.slots[0].clip.notes[0].id),"edit copied Session notes independently");
    patch.waveform=2;ok(session.setSynthSettings(id,patch),"edit copied synth independently");
    state=session.snapshot();check(state.tracks[2].clips[0].notes[0].pitch==60&&state.tracks[2].slots[0].clip.notes.size()==1
        &&state.tracks[2].synth.waveform==1&&state.tracks[3].synth.waveform==2,"copy edits do not alter original notes or instrument");
    ok(session.saveProject(project),"save independently edited MIDI copy");ok(session.loadProject(project),"reopen independent MIDI copy");
    state=session.snapshot();check(state.tracks[3].id==id&&state.tracks[3].clips[0].notes[0].id==note.id
        &&state.tracks[3].clips[0].notes[0].pitch==64&&state.tracks[3].slots[0].clip.notes.empty(),"independent identities and edits survive reopen");
}
void captureTests(const juce::File& root)
{
    Session session(false,makeSyntheticTestDeviceType());const auto inputs=session.inputNames();check(!inputs.isEmpty(),"generated input fixture available");
    ok(session.setTrackInput(0,inputs[0]),"assign generated input only");ok(session.setTrackArmed(0,true),"arm original generated-input track");
    ok(session.setTrackMonitor(0,Session::MonitorMode::on),"monitor original generated input");const auto original=session.snapshot().tracks[0];juce::String copy;
    ok(session.importAudioToSlot(root.getChildFile("duplicate-source.wav"),0,0),"prepare generated Session takeover");
    ok(session.setLaunchQuantisation(Session::LaunchQuantisation::none),"select immediate generated launch");
    ok(session.launchSlot(0,0),"launch original generated clip");pumpSyntheticTestDevice(session,32);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    check(session.snapshot().tracks[0].launcherActive,"original has real Session ownership before copying");
    ok(session.duplicateTrack(original.id,copy),"duplicate armed source without copying capture assignment");const auto state=session.snapshot();
    check(state.tracks[0].armed&&state.tracks[0].inputName==original.inputName&&state.tracks[0].monitor==Session::MonitorMode::on
        &&state.tracks[1].id==copy&&!state.tracks[1].armed&&state.tracks[1].inputName.isEmpty()&&state.tracks[1].monitor==Session::MonitorMode::off,
        "copy cannot acquire original capture/monitor assignment");
    pumpSyntheticTestDevice(session,8);
    const auto playing=session.snapshot();
    check(playing.tracks[0].launcherActive&&!playing.tracks[1].launcherActive
        &&!playing.tracks[1].slots[0].playing&&!playing.tracks[1].slots[0].queued,"runtime Session takeover and launch are not duplicated");
    session.stop();
}
}
// Save as Default Audio Track: new audio tracks start from the saved devices and mixer, without the
// saved track's clips, as the reference's default track; MIDI tracks and new projects stay plain.
void defaultTrackTests (const juce::File& root)
{
    Session session (false);
    ok (session.importAudio (root.getChildFile ("duplicate-source.wav"), 0), "a clip on the first track");
    const auto first = session.snapshot().tracks[0].id;
    ok (session.addNativeEffect (0, Session::EffectKind::channelEq), "a Channel EQ on it"); ok (session.setTrackGain (0, -6), "at -6 dB");
    ok (session.setInfoText (first, "Vocal chain"), "and an info text");
    const auto file = root.getChildFile ("Defaults").getChildFile ("Creating Tracks").getChildFile ("Audio Track").getChildFile ("Default Audio Track.halftrack");
    ok (session.saveTrackAsDefault (first, file), "save it as the default audio track");
    check (file.existsAsFile() && ! file.loadFileAsString().contains ("AUDIOCLIP") && ! file.loadFileAsString().contains ("Vocal chain"), "the file keeps neither its clip nor its info text");
    session.setDefaultTrackFiles (file, {});
    const auto before = session.snapshot().tracks.size();
    ok (session.addAudioTrack(), "add an audio track");
    auto made = session.snapshot().tracks;
    check (made.size() == before + 1, "one more track");
    const auto& added = made[before];
    check (added.id != first && added.plugins.size() == 1 && added.plugins[0].isNativeEffect && added.plugins[0].effect.kind == Session::EffectKind::channelEq
               && std::abs (added.gainDb + 6) < 1e-3f && added.clips.empty() && added.infoText.isEmpty() && added.plugins[0].id != session.snapshot().tracks[0].plugins[0].id,
           "it has the default's own Channel EQ and level, and no clip");
    session.undo(); check (session.snapshot().tracks.size() == before, "one undo removes it");
    ok (session.addMidiTrack(), "a MIDI track"); check (session.snapshot().tracks.back().plugins.empty(), "has no default and stays plain");
    ok (session.addReturnTrack(), "add a return");
    check (session.saveTrackAsDefault (session.snapshot().tracks.back().id, file).failed() && session.saveTrackAsDefault ("missing", file).failed(), "returns and unknown tracks are not defaults");
    ok (session.newProject(), "a new project");
    check (session.snapshot().tracks.size() == 2 && session.snapshot().tracks[0].plugins.empty(), "keeps its plain tracks");
    file.replaceWithText ("not a track");
    ok (session.addAudioTrack(), "an unreadable default"); check (session.snapshot().tracks.back().plugins.empty(), "makes a plain track");
}
// Tracks of another project, as the reference's browser unfolds a Set: listed from its file, and imported whole.
void importTests (const juce::File& root)
{
    const auto file = root.getChildFile ("Other").getChildFile ("Other.halftrack");
    juce::String keysId;
    {
        Session other (false);
        ok (other.addMidiTrack ("Keys"), "a MIDI track in another project");
        const int keys = (int) other.snapshot().tracks.size() - 1;
        keysId = other.snapshot().tracks[(size_t) keys].id;
        ok (other.addNativeEffect (keys, Session::EffectKind::saturator), "with a Saturator");
        ok (other.addMidiClip (keysId, 0, 4), "and a clip");
        ok (other.addMidiNote (other.snapshot().tracks[(size_t) keys].clips[0].id, 60, 0, 1, .8f), "holding a note");
        ok (other.setTrackAutomation (keysId, Session::AutomationTarget::volume, { true, { { 0, -6 }, { 4, -12 } } }), "and volume automation");
        ok (other.importAudio (root.getChildFile ("duplicate-source.wav"), 0), "audio on its first track");
        juce::String group;
        ok (other.groupTracks ({ other.snapshot().tracks[0].id }, group), "which is grouped");
        ok (other.addReturnTrack(), "a return");
        ok (other.saveProject (file), "save it");
    }
    std::vector<Session::ProjectTrack> listed;
    ok (Session::listProjectTracks (file, listed), "list its tracks from the file");
    const auto keysListed = std::find_if (listed.begin(), listed.end(), [&] (const auto& t) { return t.id == keysId; });
    const auto groupListed = std::find_if (listed.begin(), listed.end(), [] (const auto& t) { return t.group; });
    check (keysListed != listed.end() && keysListed->name == "Keys" && keysListed->midi && keysListed->depth == 0
               && groupListed != listed.end() && groupListed + 1 != listed.end() && (groupListed + 1)->depth == 1 && ! (groupListed + 1)->midi
               && std::any_of (listed.begin(), listed.end(), [] (const auto& t) { return t.returnTrack; }),
           "Keys at the top, the group followed by its audio track a level deeper, and the return");
    Session session (false);
    ok (session.saveProject (root.getChildFile ("Here").getChildFile ("Here.halftrack")), "this project saved");
    const auto start = session.snapshot();
    const auto before = start.tracks.size();
    const auto groups = start.groups.size();
    const auto returns = std::count_if (start.tracks.begin(), start.tracks.end(), [] (const auto& t) { return t.isReturn; });
    juce::StringArray created;
    ok (session.importProjectTracks (file, { keysId }, {}, created), "import Keys");
    auto snap = session.snapshot();
    const auto imported = std::find_if (snap.tracks.begin(), snap.tracks.end(), [&] (const auto& t) { return created.contains (t.id); });
    check (created.size() == 1 && snap.tracks.size() == before + 1 && imported != snap.tracks.end() && imported->name == "Keys" && imported->isMidi
               && imported->clips.size() == 1 && imported->clips[0].notes.size() == 1
               && std::any_of (imported->plugins.begin(), imported->plugins.end(), [] (const auto& p) { return p.effect.kind == Session::EffectKind::saturator; })
               && imported->volumeAutomation.points.size() == 2,
           "it comes whole: its clip and note, its Saturator and its volume automation ("
               + (imported == snap.tracks.end() ? juce::String ("missing") : imported->name + ", " + juce::String ((int) imported->clips.size()) + " clips, "
                  + juce::String ((int) imported->plugins.size()) + " devices, " + juce::String ((int) imported->volumeAutomation.points.size()) + " points") + ")");
    session.undo();
    check (session.snapshot().tracks.size() == before, "one undo removes it");
    ok (session.importProjectTracks (file, {}, {}, created), "import the whole project");
    snap = session.snapshot();
    const auto audio = std::find_if (snap.tracks.begin(), snap.tracks.end(), [&] (const auto& t) { return t.groupId.isNotEmpty() && t.clips.size() == 1 && t.clips[0].isAudio; });
    check (snap.groups.size() == groups + 1 && audio != snap.tracks.end() && audio->clips[0].file.existsAsFile()
               && std::count_if (snap.tracks.begin(), snap.tracks.end(), [] (const auto& t) { return t.isReturn; }) == returns,
           "its group comes with the audio track in it, the audio there, and no return");
    session.undo();
    ok (session.importProjectTracks (file, { keysId }, session.snapshot().tracks[0].id, created), "import Keys before the first track");
    check (session.snapshot().tracks[0].id == created[0], "it goes before the track chosen");
    session.undo();
    const auto junk = root.getChildFile ("Junk.halftrack");
    check (junk.replaceWithText ("<HALFTRACK version=\"1\"/>"), "write a damaged project");
    check (session.importProjectTracks (junk, {}, {}, created).failed() && Session::listProjectTracks (junk, listed).failed() && session.snapshot().tracks.size() == before,
           "a damaged project is refused, changing nothing");
}
int runTrackDuplicateTests (const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root=artifacts.getChildFile("track-duplicate-"+juce::Uuid().toString());ok(root.createDirectory(),"create isolated fixtures");
        audioTests(root);midiTests(root);captureTests(root);defaultTrackTests(root);importTests(root);std::cout<<"Track duplication tests passed: "<<root.getFullPathName()<<std::endl;return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL track duplicate: "<<e.what()<<std::endl;return 1;}
}
}
