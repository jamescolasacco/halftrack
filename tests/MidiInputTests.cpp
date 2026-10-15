#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_engine/playback/graph/tracktion_MidiInputDeviceNode.h>
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
        struct DeliveryProbe final : te::InputDeviceInstance::Consumer
        {
            int noteOns = 0;
            void handleIncomingMidiMessage (const juce::MidiMessage& message, te::MPESourceID) override
            { if (message.isNoteOn()) ++noteOns; }
        } probe;
        std::vector<juce::WeakReference<te::InputDeviceInstance>> observed;
        auto& engine = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (session.audioDeviceManager()).engine;
        int targets = 0, monitoring = 0;
        for (auto* edit : engine.getActiveEdits().getEdits())
            for (auto* input : edit->getAllInputDevices())
                if (&input->getInputDevice() == midi.get())
                {
                    observed.emplace_back (input); input->addConsumer (&probe);
                    for (auto id : input->getTargets())
                    {
                        ++targets;
                        if (auto* track = te::findTrackForID (*edit, id); track && input->isLivePlayEnabled (*track)) ++monitoring;
                    }
                }
        sendNow (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 96));
        const float peak = pumpSyntheticTestDevice (session, 20);
        sendNow (juce::MidiMessage::noteOff (1, 69));
        pumpSyntheticTestDevice (session, 30);
        int retained = 0;
        for (auto input : observed) if (input) { ++retained; input->removeConsumer (&probe); }
        std::cout << "MIDI probe: peak=" << peak << " enabled=" << midi->isEnabled()
            << " native-mode=" << (int) midi->getMonitorMode() << " instances=" << observed.size()
            << " retained=" << retained << " targets=" << targets << " monitoring=" << monitoring
            << " delivered-note-ons=" << probe.noteOns;
        for (const auto& track : session.snapshot().tracks)
            if (track.isMidi) std::cout << " [mode=" << (int) track.monitor << " arm=" << track.armed
                << " input=" << (track.inputName == name) << " mute=" << track.mute << " gain=" << track.gainDb << "]";
        std::cout << std::endl;
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

// Exercise graph handoff without timers: an old consumer may retire before the
// replacement receives its first process call, while controller input continues.
void replacementConsumerLifetime()
{
    GeneratedInput f;
    auto& engine = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (f.session.audioDeviceManager()).engine;
    te::InputDeviceInstance* input = nullptr;
    for (auto* edit : engine.getActiveEdits().getEdits())
        for (auto* candidate : edit->getAllInputDevices())
            if (&candidate->getInputDevice() == f.controller.midi.get()) input = candidate;
    check (input != nullptr, "replacement fixture has a native input instance");
    tracktion::graph::PlayHead playHead;
    tracktion::graph::PlayHeadState playHeadState (playHead);
    const auto target = input->getTargets()[0];
    auto makeGraph = [&] (tracktion::graph::NodeGraph* previous)
    {
        auto graph = std::make_unique<tracktion::graph::NodeGraph>();
        auto node = std::make_unique<te::MidiInputDeviceNode> (*input, *f.controller.midi,
            f.controller.midi->getMPESourceID(), playHeadState, target);
        graph->sortedNodes.push_back ({node.get(), node->getNodeProperties().nodeID});
        graph->rootNode = std::move (node);
        static_cast<te::MidiInputDeviceNode*> (graph->rootNode.get())->prepareToPlay ({48000, 256, *graph, previous});
        return graph;
    };
    auto consume = [&] (tracktion::graph::NodeGraph& graph)
    {
        te::MidiMessageArray messages;
        tracktion::graph::Node::ProcessContext context {256, {0,256}, {{},messages}};
        static_cast<te::MidiInputDeviceNode*> (graph.rootNode.get())->process (context);
        int noteOns = 0;
        for (const auto& message : messages) if (message.isNoteOn()) ++noteOns;
        return noteOns;
    };
    auto oldGraph = makeGraph (nullptr);
    auto replacement = makeGraph (oldGraph.get());
    oldGraph.reset();
    f.controller.sendNow (juce::MidiMessage::noteOn (1, 72, (juce::uint8)96));
    check (consume (*replacement) == 1, "first key survives retirement before replacement callback");
    check (consume (*replacement) == 0, "replacement does not repeat the consumed key");

    auto pending = makeGraph (replacement.get());
    auto newest = makeGraph (pending.get());
    f.controller.sendNow (juce::MidiMessage::noteOn (16, 69, (juce::uint8)96));
    replacement.reset();
    f.controller.sendNow (juce::MidiMessage::noteOn (1, 74, (juce::uint8)96));
    check (consume (*newest) == 2, "overlapping replacements retain queued and newly arriving keys exactly once");
    pending.reset(); // Its retirement must not clear the now-active newest owner.
    f.controller.sendNow (juce::MidiMessage::noteOn (1, 76, (juce::uint8)96));
    check (consume (*newest) == 1, "retiring an inactive replacement preserves the active consumer");

    auto superseded = makeGraph (newest.get());
    auto finalGraph = makeGraph (superseded.get());
    newest.reset();
    superseded.reset();
    f.controller.sendNow (juce::MidiMessage::noteOn (1, 77, (juce::uint8)96));
    check (consume (*finalGraph) == 1, "first key survives several replacements retired before audio activation");
    check (consume (*finalGraph) == 0, "retired graph queues cannot replay a consumed key");
}

void immediateMonitoringTransitions()
{
    GeneratedInput f;
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        ok (f.session.setTrackMonitor (2, Session::MonitorMode::on), "enable immediate monitoring");
        // A key can arrive before the next desktop event-loop iteration. Do not
        // warm the graph in the immediate/first-callback cases. Cycle 2 instead
        // queues an old key, stalls an established stream, then sends a fresh key.
        // A prepared device may also wait before its very first callback. This
        // event is new even when the graph itself is older than the stale window.
        if (cycle == 2)
        {
            pumpSyntheticTestDevice (f.session, 4);
            f.controller.sendNow (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 96));
        }
        if (cycle == 1 || cycle == 2) { juce::Thread::sleep (160); juce::Time::getMillisecondCounter(); }
        if (cycle == 3)
        {
            pumpSyntheticTestDevice (f.session, 4);
            auto& engine = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (f.session.audioDeviceManager()).engine;
            for (int rebuild = 0; rebuild < 3; ++rebuild)
                for (auto* edit : engine.getActiveEdits().getEdits())
                { edit->restartPlayback(); edit->dispatchPendingUpdatesSynchronously(); }
        }
        f.controller.sendNow (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 96));
        const auto peak = pumpSyntheticTestDevice (f.session, 20);
        check (peak > .001f, "first key after enabling monitoring is audible; peak=" + juce::String (peak, 9));
        if (cycle == 2)
        {
            f.controller.sendNow (juce::MidiMessage::noteOff (1, 72));
            pumpSyntheticTestDevice (f.session, 40);
            check (pumpSyntheticTestDevice (f.session, 20) < 1e-7f, "old queued key is not replayed with the fresh key after a device gap");
        }
        ok (f.session.setTrackMonitor (2, Session::MonitorMode::off), "disable immediate monitoring");
        pumpSyntheticTestDevice (f.session, 40);
        check (pumpSyntheticTestDevice (f.session, 20) < 1e-7f, "immediate monitor disable releases held input");
    }
    ok (f.session.setTrackMonitor (2, Session::MonitorMode::automatic), "prepare immediate Auto input");
    ok (f.session.setTrackArmed (2, true), "arm Auto input without waiting for timers");
    f.controller.sendNow (juce::MidiMessage::noteOn (16, 69, (juce::uint8) 96));
    check (pumpSyntheticTestDevice (f.session, 20) > .001f, "first key after arming Auto monitoring is audible");
    f.session.undo(); pumpSyntheticTestDevice (f.session, 40);
    check (pumpSyntheticTestDevice (f.session, 20) < 1e-7f, "immediate Auto arm undo releases its voice");
    f.session.redo();
    f.controller.sendNow (juce::MidiMessage::noteOn (16, 69, (juce::uint8) 96));
    check (pumpSyntheticTestDevice (f.session, 20) > .001f, "first key after Auto arm redo is audible");
}

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
    session.undo(); check (!session.snapshot().tracks[2].armed, "record-arm undo restores model state");
    check (f.note() < 1e-7f, "record-arm undo also disables Auto monitoring");
    session.redo(); check (session.snapshot().tracks[2].armed, "record-arm redo restores model state");
    check (f.note() > .001f, "record-arm redo restores Auto monitoring");
    ok(session.setTrackMonitor(2,Session::MonitorMode::off),"disable monitored MIDI");
    session.undo(); check (session.snapshot().tracks[2].monitor == Session::MonitorMode::automatic, "monitor undo restores model state");
    check (f.note() > .001f, "monitor undo restores actual MIDI routing");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1200);
    check(session.canRedo(),"monitor redo remains available after idle timers");
    session.redo();check(session.snapshot().tracks[2].monitor==Session::MonitorMode::off,"monitor redo restores model mode");
    const float redoPeak=f.note();check(redoPeak<1e-7f,"monitor redo restores silent input routing; peak="+juce::String(redoPeak,9));
    ok(session.setTrackMonitor(2,Session::MonitorMode::on),"monitor a held note before disabling input");
    pumpSyntheticTestDevice(session,4);f.send(juce::MidiMessage::noteOn(1,72,(juce::uint8)96),f.devices.getCurrentStreamTime()+256.0/48000);
    check(pumpSyntheticTestDevice(session,20)>.001f,"held input note is audible");
    ok(session.setTrackMonitor(2,Session::MonitorMode::off),"turn monitoring Off while a note is held");
    pumpSyntheticTestDevice(session,40);check(pumpSyntheticTestDevice(session,20)<1e-7f,"disabling input releases its held notes");
    auto held=[&](int channel,bool sustain)
    {
        pumpSyntheticTestDevice(session,4);
        const auto now=f.devices.getCurrentStreamTime()+256.0/48000;
        if(sustain)f.send(juce::MidiMessage::controllerEvent(channel,64,127),now);
        f.send(juce::MidiMessage::noteOn(channel,72,(juce::uint8)96),now);
        check(pumpSyntheticTestDevice(session,20)>.001f,"held channel "+juce::String(channel)+" is audible");
        if(sustain){f.send(juce::MidiMessage::noteOff(channel,72),f.devices.getCurrentStreamTime()+256.0/48000);pumpSyntheticTestDevice(session,30);}
    };
    auto released=[&](const juce::String& action)
    {pumpSyntheticTestDevice(session,40);check(pumpSyntheticTestDevice(session,20)<1e-7f,action+" releases sounding input voices");};
    session.undo();check(session.snapshot().tracks[2].monitor==Session::MonitorMode::on,"undo reopens monitoring without resurrecting notes");
    held(16,true);session.redo();released("monitor redo with sustain");
    ok(session.setTrackMonitor(2,Session::MonitorMode::on),"enable input for undo release");held(1,false);session.undo();released("monitor undo");
    session.redo();ok(session.setTrackMonitor(2,Session::MonitorMode::automatic),"prepare armed Auto monitoring");held(1,true);
    ok(session.setTrackArmed(2,false),"disarm sustained Auto input");released("disarming Auto input");
    session.undo();held(16,false);session.redo();released("record-arm redo");
    ok(session.setTrackArmed(2,true),"arm Auto input again");held(1,false);session.undo();released("record-arm undo");
    ok(session.setTrackMonitor(2,Session::MonitorMode::on),"In remains active without arm");held(1,false);
    ok(session.setTrackGain(2,-3),"change unrelated mixer gain");session.undo();
    check(pumpSyntheticTestDevice(session,20)>.001f,"unrelated gain undo does not stop monitored notes");
    ok(session.setTrackMonitor(2,Session::MonitorMode::off),"release unrelated-edit fixture");released("explicit monitor Off");
    ok (session.addMidiTrack(), "create alternate destination");
    ok(session.setTrackMonitor(2,Session::MonitorMode::on),"monitor before moving input assignment");held(1,true);
    ok (session.setTrackInput(3,f.name), "move exclusive input assignment");
    released("moving an active input to another track");
    auto state = session.snapshot();
    check (state.tracks[2].inputName.isEmpty() && !state.tracks[2].armed && state.tracks[3].inputName == f.name
        && !state.tracks[3].armed && state.tracks[3].monitor == Session::MonitorMode::off,
        "controller assignment moves without arming its new target");
    session.undo(); state = session.snapshot();
    check (state.tracks[2].inputName == f.name && state.tracks[3].inputName.isEmpty(), "one undo restores exclusive routing");
    check (session.setTrackInput(2,"missing controller").failed() && session.canRedo(), "unavailable input preserves redo");
    session.redo(); ok(session.setTrackInput(3,{}),"disconnect MIDI input");
    check (session.setTrackArmed(3,true).failed(), "disconnected MIDI track cannot arm");
    while(!session.snapshot().scenes.empty())ok(session.removeScene(0),"remove empty Session scenes");
    ok(session.setTrackInput(2,f.name),"assign source-free MIDI track");
    ok(session.setTrackMonitor(2,Session::MonitorMode::on),"monitor with no clips or scenes");held(16,true);
    ok(session.setTrackMonitor(2,Session::MonitorMode::off),"disable source-free monitoring");released("source-free input Off");
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

void countInRecording()
{
    for(double start:{0.0,4.0})
    {
    GeneratedInput f;auto& session=f.session;auto click=session.snapshot().metronome;click.countInBars=1;
    ok(session.setMetronome(click),"select one-bar MIDI count-in");
    ok(session.setTrackArmed(2,true),"arm count-in MIDI input");session.seek(start);ok(session.record(),"start MIDI count-in recording");
    const auto origin=f.devices.getCurrentStreamTime()+256.0/48000;pumpSyntheticTestDevice(session,4);
    f.send(juce::MidiMessage::noteOn(1,60,(juce::uint8)80),origin+1.0);
    f.send(juce::MidiMessage::noteOff(1,60),origin+1.2);
    f.send(juce::MidiMessage::noteOn(1,64,(juce::uint8)90),origin+2.1);
    f.send(juce::MidiMessage::noteOff(1,64),origin+2.4);
    f.send(juce::MidiMessage::noteOn(1,67,(juce::uint8)100),origin+2.6);
    f.send(juce::MidiMessage::noteOff(1,67),origin+2.9);
    pumpSyntheticTestDevice(session,600);session.stop();
    const auto state=session.snapshot();check(state.tracks[2].clips.size()==1,"MIDI count-in produces one clip");
    const auto clip=state.tracks[2].clips[0];check(clip.notes.size()==2,"notes fully inside the count-in are omitted");
    check(clip.notes[0].pitch==64&&std::abs(clip.startBeat+clip.notes[0].startBeat-start*2)<1e-9
        &&std::abs(clip.notes[0].lengthBeats-.3)<1e-9,"a note crossing punch-in is trimmed to the recording start");
    check(clip.notes[1].pitch==67&&std::abs(clip.startBeat+clip.notes[1].startBeat-start*2-.7)<1e-9
        &&std::abs(clip.notes[1].lengthBeats-.6)<1e-9,"post-count-in MIDI keeps exact musical timing");
    session.undo();check(session.snapshot().tracks[2].clips.empty(),"count-in MIDI take undoes once");session.redo();
    check(session.snapshot().tracks[2].clips[0].notes[0].id==clip.notes[0].id,"count-in MIDI redo retains editable identities");
    }
}

void recordingQuantisation(const juce::File& root)
{
    constexpr double grids[]={0,1,.5,1.0/3,.25,1.0/6,.125};
    constexpr double starts[]={.23,.75,1.78,2.13},lengths[]={.17,.4,.21,.55};
    for(int choice=0;choice<7;++choice)
    {
        GeneratedInput f;auto& session=f.session;const auto mode=static_cast<Session::RecordQuantisation>(choice);
        check(session.snapshot().recordQuantisation==Session::RecordQuantisation::none,"new projects disable recording quantization");
        ok(session.setRecordQuantisation(mode),"choose recording grid");
        if(choice>0)
        {
            session.undo();check(session.snapshot().recordQuantisation==Session::RecordQuantisation::none,"recording grid setting undoes once");
            check(session.setRecordQuantisation(static_cast<Session::RecordQuantisation>(99)).failed()&&session.canRedo(),"invalid recording grid preserves redo");
            session.redo();check(session.snapshot().recordQuantisation==mode,"recording grid setting redoes exactly");
        }
        // A remembered native device quantizer must not quantize this document a second time.
        f.controller.midi->quantisation.setType("1 beat");
        ok(session.setTrackArmed(2,true),"arm quantized take");session.seek(0);ok(session.record(),"start take on selected recording grid");
        check(session.setRecordQuantisation(static_cast<Session::RecordQuantisation>((choice+1)%7)).failed(),"recording grid cannot change during a take");
        const auto origin=f.devices.getCurrentStreamTime()+256.0/48000;pumpSyntheticTestDevice(session,4);
        for(int n=0;n<4;++n)
        {
            f.send(juce::MidiMessage::noteOn(1,60+n,(juce::uint8)(80+n)),origin+starts[n]*.5);
            f.send(juce::MidiMessage::noteOff(1,60+n,(juce::uint8)(30+n)),origin+(starts[n]+lengths[n])*.5);
        }
        f.send(juce::MidiMessage::controllerEvent(1,11,75),origin+.37*.5);
        pumpSyntheticTestDevice(session,300);session.stop();const auto state=session.snapshot();
        check(state.tracks[2].clips.size()==1,"quantized take produces one MIDI clip");const auto clip=state.tracks[2].clips[0];
        check(clip.notes.size()==4,"recording quantization retains all notes");
        for(int n=0;n<4;++n)
        {
            auto note=std::find_if(clip.notes.begin(),clip.notes.end(),[n](const auto& value){return value.pitch==60+n;});
            const auto expected=choice==0?starts[n]:std::floor(starts[n]/grids[choice]+.5)*grids[choice];
            check(note!=clip.notes.end()&&std::abs(clip.startBeat+note->startBeat-expected)<1e-9
                &&std::abs(note->lengthBeats-lengths[n])<1e-9&&std::abs(note->velocity-(80+n)/127.0)<1e-6,
                "recorded onset follows independent grid arithmetic while length and velocity are retained");
        }
        const auto midiFile=root.getChildFile("quantized-"+juce::String(choice)+".mid");ok(session.exportMidiClip(clip.id,midiFile),"export quantized recorded events");
        juce::FileInputStream input(midiFile);juce::MidiFile midi;check(midi.readFrom(input),"read quantized event export");
        bool controller=false;int releases=0;
        for(int n=0;n<midi.getTrack(0)->getNumEvents();++n)
        {
            const auto& message=midi.getTrack(0)->getEventPointer(n)->message;
            if(message.isController() && message.getControllerNumber()==11)
                controller=message.getControllerValue()==75&&std::abs(message.getTimeStamp()/midi.getTimeFormat()-.37)<1e-9;
            if(message.isNoteOff()&&message.getNoteNumber()>=60&&message.getNoteNumber()<64)
            {
                check(message.getRawData()[2]==30+message.getNoteNumber()-60,"quantization retains captured release velocity");++releases;
            }
        }
        check(controller&&releases==4,"controller timing remains unquantized");
        session.undo();check(session.snapshot().tracks[2].clips.empty(),"quantized take is one undo action");session.redo();
        check(session.snapshot().tracks[2].clips[0].notes[0].id==clip.notes[0].id,"quantized take redo retains identities");
        if(choice==4)
        {
            const auto actualFile=root.getChildFile("quantized.wav"),expectedFile=root.getChildFile("quantized-oracle.wav");
            ok(session.exportWav(actualFile),"render quantized captured notes");
            Session oracle(false);ok(oracle.addMidiTrack(),"prepare independent quantized-note oracle");auto track=oracle.snapshot().tracks[2].id;
            ok(oracle.setSynthSettings(track,state.tracks[2].synth),"match instrument for quantization oracle");ok(oracle.addMidiClip(track,clip.startBeat,clip.lengthBeats),"match recording extent in oracle");
            const auto id=oracle.snapshot().tracks[2].clips[0].id;
            for(int n=0;n<4;++n)ok(oracle.addMidiNote(id,60+n,std::floor(starts[n]/.25+.5)*.25-clip.startBeat,lengths[n],(80+n)/127.0f),"draw independently quantized note");
            ok(oracle.exportWav(expectedFile),"render independent quantized notes");const auto actual=readAudio(actualFile),expected=readAudio(expectedFile);
            check(actual.getNumSamples()==expected.getNumSamples(),"quantized renders have identical extents");double worst=0;
            for(int c=0;c<2;++c)for(int n=0;n<actual.getNumSamples();++n)
            {if(!std::isfinite(actual.getSample(c,n)))throw std::runtime_error("nonfinite quantized render");worst=std::max(worst,std::abs((double)actual.getSample(c,n)-expected.getSample(c,n)));}
            check(worst<1e-7,"every quantized synth frame matches independently drawn notes");
        }
        const auto project=root.getChildFile("quantized-"+juce::String(choice)+".halftrack");ok(session.saveProject(project),"save quantized take and grid");ok(session.loadProject(project),"reopen quantized take and grid");
        check(session.snapshot().recordQuantisation==mode&&session.snapshot().tracks[2].clips[0].notes.size()==4,"recording grid and quantized notes survive reopen");
        ok(session.setRecordQuantisation(mode),"unchanged recording grid is a no-op");check(!session.canUndo()&&!session.snapshot().dirty,"unchanged recording grid keeps clean history");
        if(choice==4)for(const auto& invalid:juce::StringArray{"-1","7","1.5","nan","4 trailing"})
        {
            auto xml=juce::XmlDocument::parse(project);xml->getChildByName("EDIT")->setAttribute("halftrackRecordQuantisation",invalid);const auto bad=root.getChildFile("bad-record-grid.halftrack");check(xml->writeTo(bad),"write malformed recording grid fixture");
            check(session.loadProject(bad).failed()&&session.snapshot().recordQuantisation==mode&&session.snapshot().tracks[2].clips[0].id==clip.id,"invalid grid cannot replace active project");
        }
    }
    GeneratedInput f;auto& session=f.session;const auto track=session.snapshot().tracks[2].id;
    ok(session.addMidiClip(track,0,4),"prepare overdub target");const auto clipId=session.snapshot().tracks[2].clips[0].id;
    ok(session.addMidiNote(clipId,48,.137,.28,.6f),"prepare deliberately off-grid existing note");const auto oldNote=session.snapshot().tracks[2].clips[0].notes[0];
    ok(session.setRecordQuantisation(Session::RecordQuantisation::quarter),"select quarter-note overdub grid");ok(session.setTrackArmed(2,true),"arm quantized overdub");
    session.setLoop(true,0,2);check(session.record().failed()&&!session.snapshot().recording,"unsupported loop quantization fails before capture");session.setLoop(false,0,2);
    session.seek(0);ok(session.record(),"record quantized overdub");const auto origin=f.devices.getCurrentStreamTime()+256.0/48000;pumpSyntheticTestDevice(session,4);
    f.send(juce::MidiMessage::noteOn(1,65,(juce::uint8)100),origin+.375);f.send(juce::MidiMessage::noteOff(1,65),origin+.5);
    pumpSyntheticTestDevice(session,150);session.stop();const auto notes=session.snapshot().tracks[2].clips[0].notes;
    check(notes.size()==2,"quantized overdub adds only the new note");
    const auto old=std::find_if(notes.begin(),notes.end(),[&](const auto& note){return note.id==oldNote.id;});
    const auto added=std::find_if(notes.begin(),notes.end(),[](const auto& note){return note.pitch==65;});
    check(old!=notes.end()&&old->startBeat==oldNote.startBeat&&old->lengthBeats==oldNote.lengthBeats,"recording grid leaves existing overdub notes untouched");
    check(added!=notes.end()&&std::abs(added->startBeat-1)<1e-9&&std::abs(added->lengthBeats-.25)<1e-9,"new overdub note alone is quantized");
    session.undo();check(session.snapshot().tracks[2].clips[0].notes.size()==1&&session.snapshot().tracks[2].clips[0].notes[0].id==oldNote.id,"undo removes only quantized overdub");
    GeneratedInput boundary;auto& edge=boundary.session;
    ok(edge.setRecordQuantisation(Session::RecordQuantisation::quarter),"set boundary recording grid");ok(edge.setTrackArmed(2,true),"arm boundary recording");
    edge.seek(.1);ok(edge.record(),"record from an off-grid position");const auto edgeOrigin=boundary.devices.getCurrentStreamTime()+256.0/48000;pumpSyntheticTestDevice(edge,4);
    boundary.send(juce::MidiMessage::noteOn(1,60,(juce::uint8)80),edgeOrigin+.025);boundary.send(juce::MidiMessage::noteOff(1,60),edgeOrigin+.15);
    boundary.send(juce::MidiMessage::noteOn(1,62,(juce::uint8)80),edgeOrigin+.67);boundary.send(juce::MidiMessage::noteOff(1,62),edgeOrigin+.79);
    pumpSyntheticTestDevice(edge,150);edge.stop();const auto bounded=edge.snapshot().tracks[2].clips[0];
    check(bounded.notes.size()==2&&std::abs(bounded.notes[0].startBeat)<1e-9&&std::abs(bounded.notes[0].lengthBeats-.25)<1e-9,"backward snapping stops at the captured clip start without shortening the note");
    check(std::abs(bounded.notes[1].startBeat+bounded.notes[1].lengthBeats-bounded.lengthBeats)<1e-9&&std::abs(bounded.notes[1].lengthBeats-.24)<1e-9,"forward snapping stops at the captured clip end without shortening the note");
    ok(edge.setTrackArmed(2,false),"disarm MIDI before audio-only loop check");ok(edge.setTrackInput(0,edge.inputNames()[0]),"assign generated audio for audio-only loop");ok(edge.setTrackArmed(0,true),"arm audio-only loop capture");
    edge.setLoop(true,0,2);ok(edge.record(),"MIDI recording grid does not block audio-only loop capture");pumpSyntheticTestDevice(edge,8);edge.stop();


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
int runMidiMonitoringTests()
{
    try { replacementConsumerLifetime(); immediateMonitoringTransitions(); selectionAndMonitoring(); return 0; }
    catch(const std::exception& e){std::cerr<<"FAIL MIDI monitoring: "<<e.what()<<std::endl;return 1;}
}
int runMidiInputTests(const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("midi-input-"+juce::Uuid().toString()); ok(root.createDirectory(),"create isolated fixtures");
        selectionAndMonitoring(); recording(root); recordingExits(); countInRecording(); recordingQuantisation(root); controllersAndOverdub(root);
        std::cout<<"MIDI input tests passed: "<<root.getFullPathName()<<std::endl; return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL MIDI input: "<<e.what()<<std::endl;return 1;}
}
}
