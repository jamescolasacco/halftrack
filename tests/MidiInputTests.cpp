#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
namespace te = tracktion::engine;
void check (bool value, const juce::String& message)
{
    if (!value) throw std::runtime_error (message.toStdString());
    std::cout << "PASS MIDI input: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }

// The production facade selects this unique, source-free virtual device. No
// physical MIDI input or system audio driver is opened by this test.
struct VirtualController
{
    Session& session;
    te::DeviceManager& devices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&>
        (session.audioDeviceManager()).engine.getDeviceManager();
    juce::String name = "Halftrack generated MIDI " + juce::Uuid().toString();
    std::shared_ptr<te::VirtualMidiInputDevice> midi;

    explicit VirtualController (Session& owner) : session(owner)
    {
        ok (devices.createVirtualMidiDevice (name), "create isolated virtual controller");
        devices.rescanMidiDeviceListSynchronously();
        for (auto device : devices.getMidiInDevices())
            if (device->getName() == name) midi = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (device);
        check (midi != nullptr, "generated MIDI input discovered");
        midi->setMIDIInputSourceDevices ({});
        midi->setChannelToUse (0); midi->setMinimumLengthMs (0); midi->setManualAdjustmentMs (0);
        midi->setOverridingNoteVelocities (false);
        midi->mergeRecordings = true; midi->replaceExistingClips = false;
        midi->setEnabled(false); devices.rescanMidiDeviceListSynchronously();
        const auto inputs=devices.getMidiInDevices();
        check(std::none_of(inputs.begin(),inputs.end(),[](const auto& device)
            {return device->getDeviceType()==te::InputDevice::physicalMidiDevice;}),"test engine never enumerates physical MIDI inputs");
    }
    ~VirtualController()
    {
        session.stop();
        if (midi) { devices.deleteVirtualMidiDevice (*midi); devices.rescanMidiDeviceListSynchronously(); }
    }
    void send (juce::MidiMessage message, double streamTime)
    {
        message.setTimeStamp (streamTime - midi->getAdjustSecs());
        midi->handleIncomingMidiMessage (message, midi->getMPESourceID());
    }
    void sendNow (juce::MidiMessage message)
    { send (message, devices.getCurrentStreamTime() + 256.0 / 48000); }
    float note()
    {
        // Activate the rebuilt monitoring graph before delivering a controller event.
        pumpSyntheticTestDevice (session, 4);
        sendNow (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 96));
        const float peak = pumpSyntheticTestDevice (session, 20);
        sendNow (juce::MidiMessage::noteOff (1, 69));
        pumpSyntheticTestDevice (session, 30);
        return peak;
    }
};

struct GeneratedInput
{
    Session session {false,makeSyntheticTestDeviceType()};
    VirtualController controller {session};
    te::DeviceManager& devices = controller.devices;
    const juce::String& name = controller.name;
    void send (juce::MidiMessage message, double time) { controller.send(message,time); }
    float note() { return controller.note(); }
    GeneratedInput()
    {
        ok (session.addMidiTrack(), "create destination instrument");
        ok (session.setTrackInput (2, name), "select generated controller through production facade");
        auto patch = session.snapshot().tracks[2].synth;
        patch.waveform = 2; patch.attackSeconds = 0; patch.releaseSeconds = .02f; patch.level = .1f;
        patch.cutoffHz = 20000; patch.resonance = 0;
        ok (session.setSynthSettings (session.snapshot().tracks[2].id, patch), "prepare audible causal instrument");
        pumpSyntheticTestDevice (session, 8);
    }
};

void selectionAndMonitoring()
{
    GeneratedInput f; auto& session = f.session;
    check (session.inputNames(true).contains(f.name) && !session.inputNames().contains(f.name), "audio and MIDI input lists are typed");
    check (session.setTrackInput(0,f.name).failed() && session.setTrackInput(2,session.inputNames()[0]).failed(),
        "wrong-type input assignments fail before changing routes");
    check (!session.snapshot().tracks[2].armed && session.snapshot().tracks[2].monitor == Session::MonitorMode::off,
        "explicit assignment starts disarmed and unmonitored");
    check (f.note() < 1e-7f, "Off suppresses generated MIDI input");
    ok (session.setTrackMonitor(2,Session::MonitorMode::on), "enable In monitoring without arming");
    check (f.note() > .001f, "In plays the built-in synth while stopped and unarmed");
    check (pumpSyntheticTestDevice(session,20) < 1e-7f, "note-off releases the monitored voice");
    ok (session.setTrackMonitor(2,Session::MonitorMode::automatic), "enable Auto monitoring");
    check (f.note() < 1e-7f, "Auto suppresses an unarmed track");
    ok (session.setTrackArmed(2,true), "arm MIDI track");
    check (f.note() > .001f, "Auto monitors the armed MIDI track");
    session.undo();check(!session.snapshot().tracks[2].armed && f.note()<1e-7f,"record-arm undo also disables Auto monitoring");
    session.redo();check(session.snapshot().tracks[2].armed && f.note()>.001f,"record-arm redo restores Auto monitoring");
    ok(session.setTrackMonitor(2,Session::MonitorMode::off),"disable monitored MIDI");
    session.undo();check(session.snapshot().tracks[2].monitor==Session::MonitorMode::automatic && f.note()>.001f,
        "monitor undo restores both model and actual MIDI routing");
    session.redo();check(session.snapshot().tracks[2].monitor==Session::MonitorMode::off && f.note()<1e-7f,
        "monitor redo restores silent input routing");
    ok (session.addMidiTrack(), "create alternate destination");
    ok (session.setTrackInput(3,f.name), "move exclusive input assignment");
    auto state = session.snapshot();
    check (state.tracks[2].inputName.isEmpty() && !state.tracks[2].armed && state.tracks[3].inputName == f.name
        && !state.tracks[3].armed && state.tracks[3].monitor == Session::MonitorMode::off,
        "controller assignment moves without arming its new target");
    session.undo(); state = session.snapshot();
    check (state.tracks[2].inputName == f.name && state.tracks[3].inputName.isEmpty(), "one undo restores exclusive routing");
    check (session.setTrackInput(2,"missing controller").failed() && session.canRedo(), "unavailable input preserves redo");
    session.redo(); ok(session.setTrackInput(3,{}),"disconnect MIDI input");
    check (session.setTrackArmed(3,true).failed(), "disconnected MIDI track cannot arm");
}

juce::AudioBuffer<float> readAudio (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    check(reader && reader->sampleRate==48000 && reader->numChannels==2,"read generated recording render");
    juce::AudioBuffer<float> audio(2,(int)reader->lengthInSamples);
    check(reader->read(&audio,0,audio.getNumSamples(),0,true,true),"decode complete recording render"); return audio;
}

void recording (const juce::File& root)
{
    GeneratedInput f; auto& session = f.session; const auto project = root.getChildFile("recorded.halftrack");
    ok(session.saveProject(project),"save initial recording destination");
    ok(session.setTrackArmed(2,true),"arm generated MIDI destination");
    session.seek(0); ok(session.record(),"start native MIDI recording");
    check(session.snapshot().recording,"transport enters recording");
    check(session.setTrackInput(2,{}).failed() && session.setTrackArmed(2,false).failed()
        && session.setTrackMonitor(2,Session::MonitorMode::on).failed(),"input routing changes cannot split an active take");
    ok(session.record(),"repeated record is a no-op during an active take");
    // The queued seek is applied to the following 256-frame reference block.
    const auto origin = f.devices.getCurrentStreamTime() + 256.0 / 48000;
    pumpSyntheticTestDevice(session,4); // Apply transport start on the native audio clock.
    f.send(juce::MidiMessage::noteOn(1,60,(juce::uint8)96),origin+.1);
    f.send(juce::MidiMessage::noteOff(1,60,(juce::uint8)41),origin+.3);
    f.send(juce::MidiMessage::noteOn(1,67,(juce::uint8)64),origin+.4);
    f.send(juce::MidiMessage::noteOff(1,67),origin+.65);
    pumpSyntheticTestDevice(session,160); session.stop();
    auto state = session.snapshot();
    check(!state.recording && state.tracks[2].clips.size()==1,"stopping creates a native MIDI clip");
    const auto captured = state.tracks[2].clips[0];
    check(captured.notes.size()==2 && !captured.notes[0].id.isEmpty() && !captured.notes[1].id.isEmpty()
        && captured.notes[0].id!=captured.notes[1].id,"recorded notes have independent editable identities");
    for(size_t i=0;i<2;++i)
    {
        const auto& note = captured.notes[i];
        const double start = i==0 ? .1 : .4, length = i==0 ? .2 : .25;
        check(note.pitch==(i==0?60:67) && std::abs((captured.startBeat+note.startBeat)*.5-start)<1.0/48000
            && std::abs(note.lengthBeats*.5-length)<1.0/48000
            && std::abs(note.velocity-(i==0?96.0:64.0)/127)<1e-6,
            "recorded pitch, velocity, onset and length match generated events: "+juce::String(captured.startBeat+note.startBeat,9)+","+juce::String(note.lengthBeats,9));
    }
    session.undo(); check(session.snapshot().tracks[2].clips.empty(),"one undo removes the entire take");
    check(session.canRedo(),"native recording undo retains redo");
    session.redo(); state=session.snapshot();
    check(state.tracks[2].clips.size()==1 && state.tracks[2].clips[0].id==captured.id
        && state.tracks[2].clips[0].notes[0].id==captured.notes[0].id,"redo preserves recorded clip and note identities");
    ok(session.updateMidiNote(captured.id,captured.notes[0].id,62,captured.notes[0].startBeat,.5,.7f),"edit recorded note through piano-roll model");
    session.undo(); check(session.snapshot().tracks[2].clips[0].notes[0].pitch==60,"recorded note edit has separate undo");
    ok(session.exportMidiClip(captured.id,root.getChildFile("recorded.mid")),"export captured events as Standard MIDI File");
    ok(session.setTrackArmed(2,false),"disarm after recording");
    const auto actualFile=root.getChildFile("recorded.wav"), expectedFile=root.getChildFile("drawn.wav");
    ok(session.exportWav(actualFile),"render captured MIDI through instrument");
    Session oracle(false); ok(oracle.addMidiTrack(),"create independent drawn-note oracle");
    const auto oracleTrack=oracle.snapshot().tracks[2].id;
    ok(oracle.setSynthSettings(oracleTrack,state.tracks[2].synth),"use same synth patch in independent graph");
    ok(oracle.addMidiClip(oracleTrack,captured.startBeat,captured.lengthBeats),"match native recording extent");
    const auto oracleClip=oracle.snapshot().tracks[2].clips[0].id;
    ok(oracle.addMidiNote(oracleClip,60,.2-captured.startBeat,.4,96.0f/127),"draw independently timed first note");
    ok(oracle.addMidiNote(oracleClip,67,.8-captured.startBeat,.5,64.0f/127),"draw independently timed second note");
    ok(oracle.exportWav(expectedFile),"render drawn-note oracle");
    const auto actual=readAudio(actualFile), expected=readAudio(expectedFile);
    check(actual.getNumSamples()==expected.getNumSamples(),"recorded and drawn renders have equal extent");
    double worst=0; for(int c=0;c<2;++c)for(int n=0;n<actual.getNumSamples();++n)
    {
        if(!std::isfinite(actual.getSample(c,n))) throw std::runtime_error("Nonfinite recorded sample");
        worst=std::max(worst,std::abs((double)actual.getSample(c,n)-expected.getSample(c,n)));
    }
    check(actual.getMagnitude(0,actual.getNumSamples())>.001 && worst<2e-6,"every recorded sample matches independently drawn MIDI");
    ok(session.saveProject(project),"save recorded events and routing"); ok(session.loadProject(project),"reopen recorded MIDI project");
    state=session.snapshot();
    check(state.tracks[2].clips[0].id==captured.id && state.tracks[2].clips[0].notes[0].id==captured.notes[0].id
        && !state.tracks[2].armed && state.tracks[2].monitor==Session::MonitorMode::off,
        "reopen preserves editable recorded identities without automatically arming or monitoring");
}
void recordingExits()
{
    GeneratedInput f;auto& session=f.session;
    ok(session.setTrackArmed(2,true),"arm held-note recording");session.seek(0);ok(session.record(),"start held-note take");
    check(!session.canUndo() && !session.canRedo(),"history controls are unavailable during capture");
    session.undo();session.redo();check(session.snapshot().recording && session.snapshot().tracks[2].armed,"history requests preserve active recording");
    const auto origin=f.devices.getCurrentStreamTime()+256.0/48000;pumpSyntheticTestDevice(session,4);
    f.send(juce::MidiMessage::noteOn(1,64,(juce::uint8)100),origin+.1);
    pumpSyntheticTestDevice(session,64);session.seek(2);
    auto state=session.snapshot();check(!state.recording && state.position==2 && state.tracks[2].clips.size()==1,
        "seeking commits the take before locating");
    const auto clip=state.tracks[2].clips[0];
    check(clip.notes.size()==1 && !clip.notes[0].id.isEmpty() && clip.notes[0].lengthBeats>0
        && clip.notes[0].startBeat+clip.notes[0].lengthBeats<=clip.lengthBeats+1e-9,
        "held note is closed at stop with an editable identity");
    session.undo();check(session.snapshot().tracks[2].clips.empty(),"seek-ended recording undoes once");session.redo();
    check(session.snapshot().tracks[2].clips[0].notes[0].id==clip.notes[0].id,"seek-ended take redo retains note identity");
    // End the incoming key too; closing a recorded note does not release a physical key.
    f.send(juce::MidiMessage::noteOff(1,64),f.devices.getCurrentStreamTime());pumpSyntheticTestDevice(session,4);
    session.seek(3);ok(session.record(),"start empty MIDI take");pumpSyntheticTestDevice(session,40);session.stop();
    check(session.snapshot().tracks[2].clips.size()==1,"empty MIDI recording creates no blank clip");
}

void controllersAndOverdub (const juce::File& root)
{
    GeneratedInput f; auto& session=f.session;
    ok(session.setTrackArmed(2,true),"arm controller-event recording");
    session.seek(0); ok(session.record(),"start controller-event take");
    const auto origin=f.devices.getCurrentStreamTime()+256.0/48000;
    pumpSyntheticTestDevice(session,4);
    std::vector<juce::MidiMessage> events {
        juce::MidiMessage::noteOn(1,69,(juce::uint8)96),
        juce::MidiMessage::controllerEvent(1,64,127),
        juce::MidiMessage::pitchWheel(1,10000),
        juce::MidiMessage::channelPressureChange(1,71),
        juce::MidiMessage::aftertouchChange(1,69,59),
        juce::MidiMessage::programChange(1,9),
        juce::MidiMessage::controllerEvent(1,1,57),
        juce::MidiMessage::noteOff(1,69,(juce::uint8)41),
        juce::MidiMessage::controllerEvent(1,64,0)
    };
    const double times[]{.1,.15,.2,.22,.24,.26,.27,.3,.5};
    for(size_t i=0;i<events.size();++i)f.send(events[i],origin+times[i]);
    pumpSyntheticTestDevice(session,160); session.stop();
    auto state=session.snapshot(); check(state.tracks[2].clips.size()==1,"controller take creates one clip");
    const auto captured=state.tracks[2].clips[0];
    const auto midiFile=root.getChildFile("controllers.mid");
    auto verifyEvents=[&]
    {
        ok(session.exportMidiClip(captured.id,midiFile),"export captured controller stream");
        auto input=midiFile.createInputStream();juce::MidiFile file;
        check(input && file.readFrom(*input) && file.getNumTracks()==1,"read captured Standard MIDI File");
        std::vector<juce::MidiMessage> actual;
        for(auto* event:*file.getTrack(0))if(!event->message.isMetaEvent())actual.push_back(event->message);
        check(actual.size()==events.size(),"recording retains every channel event exactly once");
        for(size_t i=0;i<events.size();++i)
        {
            const auto& a=actual[i];const auto& b=events[i];
            check(a.getRawDataSize()==b.getRawDataSize() && std::equal(a.getRawData(),a.getRawData()+a.getRawDataSize(),b.getRawData())
                && std::abs(a.getTimeStamp()/file.getTimeFormat()-(times[i]*2-captured.startBeat))<1.1/file.getTimeFormat(),
                "capture preserves event bytes and musical timestamp at event "+juce::String((int)i));
        }
    };
    verifyEvents();
    session.seek(0);ok(session.record(),"record overlapping overdub");
    const auto secondOrigin=f.devices.getCurrentStreamTime()+256.0/48000;
    pumpSyntheticTestDevice(session,4);
    f.send(juce::MidiMessage::noteOn(1,72,(juce::uint8)80),secondOrigin+.15);
    f.send(juce::MidiMessage::noteOff(1,72),secondOrigin+.35);
    pumpSyntheticTestDevice(session,160);session.stop();
    state=session.snapshot();
    check(state.tracks[2].clips.size()==1 && state.tracks[2].clips[0].id==captured.id
        && state.tracks[2].clips[0].notes.size()==2,"overdub merges notes into the existing overlapping clip");
    const auto overdub=state.tracks[2].clips[0];
    check(overdub.notes[0].id==captured.notes[0].id && overdub.notes[1].id.isNotEmpty()
        && overdub.notes[0].id!=overdub.notes[1].id,"overdub retains original identity and assigns a new note identity");
    session.undo();check(session.snapshot().tracks[2].clips[0].notes.size()==1,"one undo removes only the overdub");
    verifyEvents();session.redo();
    check(session.snapshot().tracks[2].clips[0].notes[1].id==overdub.notes[1].id,"overdub redo restores the same editable note");
    const auto project=root.getChildFile("controllers.halftrack");ok(session.saveProject(project),"save controller take plus overdub");
    ok(session.loadProject(project),"reopen controller take plus overdub");
    check(session.snapshot().tracks[2].clips[0].notes[1].id==overdub.notes[1].id,"overdub identity persists after reopen");
    ok(session.removeMidiNote(captured.id,overdub.notes[1].id),"remove reopened overdub independently");verifyEvents();
}

}
void withGeneratedMidiController (Session& session, const std::function<void(const juce::String&)>& action)
{
    VirtualController controller(session);
    action(controller.name);
}
int runMidiInputTests(const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("midi-input-"+juce::Uuid().toString()); ok(root.createDirectory(),"create isolated fixtures");
        selectionAndMonitoring(); recording(root); recordingExits(); controllersAndOverdub(root);
        std::cout<<"MIDI input tests passed: "<<root.getFullPathName()<<std::endl; return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL MIDI input: "<<e.what()<<std::endl;return 1;}
}
}
