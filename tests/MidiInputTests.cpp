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
float pumpSyntheticTestDeviceObserved (Session&, int blocks, double& firstEntryMilliseconds,
                                       double& firstExitMilliseconds, float& firstPeak);
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

// Observe the actual cached-clock precondition without retrying audio output.
// JUCE's timer thread can wait 300 ms for desktop dispatch, leaving this cache
// older than the native MIDI node's 110 ms stale-input window.
juce::uint32 waitForFrozenMidiClock()
{
    auto cached = juce::Time::getApproximateMillisecondCounter();
    auto stableSince = juce::Time::getMillisecondCounterHiRes();
    const auto deadline = stableSince + 5000;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        const auto latest = juce::Time::getApproximateMillisecondCounter();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (latest != cached) { cached = latest; stableSince = now; }
        if (now - stableSince >= 130) return latest;
        juce::Thread::sleep (1);
    }
    check (false, "cached MIDI clock did not remain unchanged for 130 ms within the five-second precondition deadline");
    return 0;
}

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
        // Only ports a test made itself (named "Halftrack ..."), never the machine's own.
        check(std::none_of(inputs.begin(),inputs.end(),[](const auto& device)
            {return device->getDeviceType()==te::InputDevice::physicalMidiDevice && ! device->getName().startsWith ("Halftrack ");}),"test engine never enumerates the machine's physical MIDI inputs");
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

// Preserve state/release messages in arrival order across a callback gap, while
// suppressing old attacks. Old retained state must not move a fresh key to the
// end of its audio block just because its original timestamp is much earlier.
void interruptedMidiStateOrdering()
{
    GeneratedInput f;
    te::InputDeviceInstance* input = nullptr;
    for (auto* edit : f.devices.engine.getActiveEdits().getEdits())
        for (auto* candidate : edit->getAllInputDevices())
            if (&candidate->getInputDevice() == f.controller.midi.get()) input = candidate;
    check (input != nullptr, "interrupted-state fixture has a native input instance");
    tracktion::graph::PlayHead playHead;
    tracktion::graph::PlayHeadState playHeadState (playHead);
    tracktion::graph::NodeGraph graph;
    auto node = std::make_unique<te::MidiInputDeviceNode> (*input, *f.controller.midi,
        f.controller.midi->getMPESourceID(), playHeadState, input->getTargets()[0]);
    auto* receiver = node.get();
    graph.sortedNodes.push_back ({receiver, receiver->getNodeProperties().nodeID});
    graph.rootNode = std::move (node);
    receiver->prepareToPlay ({48000, 256, graph, nullptr});
    playHead.play ({0, 48000}, false);
    int64_t nextReference = 0;
    auto consume = [&]
    {
        te::MidiMessageArray messages;
        const juce::Range<int64_t> reference {nextReference, nextReference + 256};
        nextReference += 256;
        playHead.setReferenceSampleRange (reference);
        playHeadState.update (reference);
        tracktion::graph::Node::ProcessContext context {256, reference, {{},messages}};
        receiver->process (context);
        std::vector<juce::MidiMessage> result;
        for (const auto& message : messages) result.push_back (message);
        return result;
    };
    auto enqueue = [&] (juce::MidiMessage message, double time)
    {
        message.setTimeStamp (time);
        receiver->handleIncomingMidiMessage (message, f.controller.midi->getMPESourceID());
    };
    juce::Time::getMillisecondCounter();
    consume(); // Establish the stream before deliberately interrupting it.
    const std::vector<juce::MidiMessage> retained {
        juce::MidiMessage::noteOff (1, 72, (juce::uint8)35),
        juce::MidiMessage::noteOn (16, 65, (juce::uint8)0),
        juce::MidiMessage::controllerEvent (1, 64, 127),
        juce::MidiMessage::controllerEvent (1, 64, 0),
        juce::MidiMessage::controllerEvent (16, 66, 127),
        juce::MidiMessage::controllerEvent (16, 66, 0),
        juce::MidiMessage::controllerEvent (1, 69, 127),
        juce::MidiMessage::controllerEvent (1, 69, 0),
        juce::MidiMessage::controllerEvent (1, 120, 0),
        juce::MidiMessage::controllerEvent (16, 121, 0),
        juce::MidiMessage::controllerEvent (1, 123, 0),
        juce::MidiMessage::pitchWheel (16, 11000),
        juce::MidiMessage::controllerEvent (1, 1, 80),
        juce::MidiMessage::programChange (16, 5)
    };
    const auto origin = f.devices.getCurrentStreamTime();
    enqueue (juce::MidiMessage::noteOn (1, 60, (juce::uint8)96), origin);
    for (size_t i = 0; i < retained.size(); ++i)
        enqueue (retained[i], origin + .001 * (i + 1));
    const juce::uint8 sysexData[] {0x7d, 0x01, 0x02};
    for (const auto& message : {juce::MidiMessage::midiClock(), juce::MidiMessage::midiStart(),
                               juce::MidiMessage::midiStop(), juce::MidiMessage::createSysExMessage (sysexData, 3)})
        enqueue (message, origin + .02);
    // Deliberate fault injection exceeds the native 110 ms stale-event window.
    juce::Thread::sleep (160);
    juce::Time::getMillisecondCounter();
    const auto freshKey = juce::MidiMessage::noteOn (1, 77, (juce::uint8)96);
    const auto freshController = juce::MidiMessage::controllerEvent (1, 1, 90);
    enqueue (freshKey, origin + 1.0);
    enqueue (freshController, origin + 1.002);
    enqueue (juce::MidiMessage::midiClock(), origin + 1.003);
    const auto actual = consume();
    check (actual.size() == retained.size() + 3, "interrupted input retains channel state while suppressing stale attacks and system messages");
    auto sameBytes = [] (const juce::MidiMessage& a, const juce::MidiMessage& b)
    { return a.getRawDataSize() == b.getRawDataSize() && std::equal (a.getRawData(), a.getRawData() + a.getRawDataSize(), b.getRawData()); };
    for (size_t i = 0; i < retained.size(); ++i)
        check (sameBytes (actual[i], retained[i]) && actual[i].getTimeStamp() == 0,
            "stale state retains bytes, channel, FIFO order and block-start placement at event " + juce::String ((int)i));
    check (sameBytes (actual[retained.size()], freshKey) && actual[retained.size()].getTimeStamp() == 0,
        "old retained releases do not delay the first fresh key");
    check (sameBytes (actual[retained.size() + 1], freshController) && std::abs (actual[retained.size() + 1].getTimeStamp() - .002) < 1e-10,
        "fresh events retain their relative spacing after old releases");
    check (actual.back().isMidiClock() && std::abs (actual.back().getTimeStamp() - .003) < 1e-10,
        "fresh system messages retain their timing");
    check (consume().empty(), "interrupted state events are consumed exactly once");
    const auto before = waitForFrozenMidiClock();
    const auto sentAt = juce::Time::getMillisecondCounterHiRes();
    enqueue (juce::MidiMessage::noteOn (1, 81, (juce::uint8)96), origin + 2.0);
    const auto after = juce::Time::getApproximateMillisecondCounter();
    const auto refreshed = juce::Time::getMillisecondCounter();
    const auto emitted = consume();
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - sentAt;
    check (emitted.size() == 1 && emitted[0].isNoteOn() && emitted[0].getNoteNumber() == 81,
        "fresh key survives observed stale cached receipt clock; before=" + juce::String (before)
            + " after=" + juce::String (after) + " consume=" + juce::String (refreshed)
            + " elapsed-ms=" + juce::String (elapsed, 3) + " emitted=" + juce::String ((int)emitted.size()));

}

// Even if these audio-derived device types are passed directly to this native
// node (normally the builder selects dedicated nodes), its device classification
// excludes the external receipt-clock branch. Verify real types and delivery;
// do not compare a global cache that JUCE's timer thread can independently write.
void audioDerivedMidiReceiptClockGuards()
{
    Session session (false);
    auto& devices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&>
        (session.audioDeviceManager()).engine.getDeviceManager();
    te::HostedAudioDeviceInterface::Parameters parameters;
    parameters.sampleRate = 48000; parameters.blockSize = 256;
    devices.getHostedAudioDeviceInterface().initialise (parameters);
    devices.rescanMidiDeviceListSynchronously();
    auto* edit = devices.engine.getActiveEdits().getEdits()[0];
    edit->getTransport().ensureContextAllocated();
    auto* context = edit->getCurrentPlaybackContext();
    check (context != nullptr, "receipt guard fixture has a hosted playback context");
    auto& trackDevice = te::getAudioTracks (*edit)[0]->getMidiInputDevice();
    te::MidiInputDevice* hostedDevice = nullptr;
    for (const auto& input : devices.getMidiInDevices())
        if (te::HostedAudioDeviceInterface::isHostedMidiInputDevice (*input)) hostedDevice = input.get();
    check (trackDevice.isTrackDevice() && hostedDevice != nullptr && ! hostedDevice->isTrackDevice(),
        "native track and hosted input types exercise both receipt-clock exclusions");
    for (auto* source : {&trackDevice, hostedDevice})
    {
        std::unique_ptr<te::InputDeviceInstance> instance (source->createInstance (*context));
        tracktion::graph::PlayHead playHead;
        tracktion::graph::PlayHeadState playHeadState (playHead);
        tracktion::graph::NodeGraph graph;
        auto node = std::make_unique<te::MidiInputDeviceNode> (*instance, *source, source->getMPESourceID(),
            playHeadState, te::getAudioTracks (*edit)[0]->itemID);
        auto* receiver = node.get();
        graph.sortedNodes.push_back ({receiver, receiver->getNodeProperties().nodeID});
        graph.rootNode = std::move (node);
        receiver->prepareToPlay ({48000, 256, graph, nullptr});
        const auto label = juce::String (source->isTrackDevice() ? "track" : "hosted");
        check (source->isTrackDevice() || te::HostedAudioDeviceInterface::isHostedMidiInputDevice (*source),
            label + " native input excludes the external receipt-clock classification");
        receiver->handleIncomingMidiMessage (juce::MidiMessage::noteOn (1, 72, (juce::uint8)96), source->getMPESourceID());
        te::MidiMessageArray messages;
        tracktion::graph::Node::ProcessContext processContext {256, {0,256}, {{},messages}};
        receiver->process (processContext);
        check (messages.size() == 1 && messages.begin()->isNoteOn() && messages.begin()->getNoteNumber() == 72,
            label + " native input delivers its first received key");
        messages.clear();
        receiver->process (processContext);
        check (messages.isEmpty(), label + " native input consumes the key exactly once");
    }
}

// A callback interruption must not discard the release of a voice that has
// already sounded. The rendered audio release/silence oracle is unchanged.
void interruptedMidiReleases()
{
    GeneratedInput f;
    ok (f.session.setTrackMonitor (2, Session::MonitorMode::on), "enable interrupted-release monitoring");
    for (int scenario = 0; scenario < 6; ++scenario)
    {
        const int channel = scenario % 2 == 0 ? 1 : 16;
        const bool sustained = scenario == 2 || scenario == 3 || scenario == 5;
        pumpSyntheticTestDevice (f.session, 4);
        if (sustained) f.controller.sendNow (juce::MidiMessage::controllerEvent (channel, 64, 127));
        f.controller.sendNow (juce::MidiMessage::noteOn (channel, 72, (juce::uint8)96));
        check (pumpSyntheticTestDevice (f.session, 20) > .001f, "interrupted-release fixture sounds a held note " + juce::String (scenario));
        if (sustained)
        {
            f.controller.sendNow (juce::MidiMessage::noteOff (channel, 72));
            pumpSyntheticTestDevice (f.session, 30);
            check (pumpSyntheticTestDevice (f.session, 20) > .001f, "pedal retains the released key before interrupted cleanup " + juce::String (scenario));
        }
        switch (scenario)
        {
            case 0: f.controller.sendNow (juce::MidiMessage::noteOff (channel, 72)); break;
            case 1: f.controller.sendNow (juce::MidiMessage::noteOn (channel, 72, (juce::uint8)0)); break;
            case 2: f.controller.sendNow (juce::MidiMessage::controllerEvent (channel, 64, 0)); break;
            case 3: f.controller.sendNow (juce::MidiMessage::controllerEvent (channel, 121, 0)); break;
            case 4: f.controller.sendNow (juce::MidiMessage::controllerEvent (channel, 123, 0)); break;
            case 5: f.controller.sendNow (juce::MidiMessage::controllerEvent (channel, 120, 0)); break;
        }
        juce::Thread::sleep (160);
        juce::Time::getMillisecondCounter();
        pumpSyntheticTestDevice (f.session, 30);
        const auto peak = pumpSyntheticTestDevice (f.session, 20);
        check (peak < 1e-7f, "queued release ends the sounded voice after a callback interruption, case "
            + juce::String (scenario) + "; peak=" + juce::String (peak, 9));
        f.controller.sendNow (juce::MidiMessage::controllerEvent (channel, 121, 0));
    }
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
        struct DeliveryProbe final : te::InputDeviceInstance::Consumer
        {
            int noteOns = 0;
            std::vector<juce::WeakReference<te::InputDeviceInstance>> inputs;
            ~DeliveryProbe() override { for (auto input : inputs) if (input) input->removeConsumer (this); }
            void handleIncomingMidiMessage (const juce::MidiMessage& message, te::MPESourceID) override
            { if (message.isNoteOn()) ++noteOns; }
        } probe;
        int targets = 0, monitoring = 0;
        for (auto* edit : f.devices.engine.getActiveEdits().getEdits())
            for (auto* input : edit->getAllInputDevices())
                if (&input->getInputDevice() == f.controller.midi.get())
                {
                    probe.inputs.emplace_back (input); input->addConsumer (&probe);
                    for (auto id : input->getTargets())
                    {
                        ++targets;
                        if (auto* track = te::findTrackForID (*edit, id); track && input->isLivePlayEnabled (*track)) ++monitoring;
                    }
                }
        if(sustain)f.send(juce::MidiMessage::controllerEvent(channel,64,127),now);
        const double sentAt = juce::Time::getMillisecondCounterHiRes();
        f.send(juce::MidiMessage::noteOn(channel,72,(juce::uint8)96),now);
        double firstEntry = -1, firstExit = -1; float firstPeak = 0;
        const float peak = pumpSyntheticTestDeviceObserved (session, 20, firstEntry, firstExit, firstPeak);
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - sentAt;
        const auto state = session.snapshot().tracks[2];
        check(peak>.001f,"held channel "+juce::String(channel)+" is audible; peak="+juce::String(peak,9)
            +" first-callback-ms="+juce::String(firstEntry < 0 ? -1 : firstEntry - sentAt,2)
            +" first-callback-duration-ms="+juce::String(firstExit < 0 || firstEntry < 0 ? -1 : firstExit - firstEntry,2)
            +" first-peak="+juce::String(firstPeak,9)
            +" dispatch-ms="+juce::String(elapsed,2)+" delivered="+juce::String(probe.noteOns)
            +" instances="+juce::String((int)probe.inputs.size())+" targets="+juce::String(targets)
            +" monitoring="+juce::String(monitoring)+" mode="+juce::String((int)state.monitor)
            +" armed="+juce::String((int)state.armed)+" native-mode="+juce::String((int)f.controller.midi->getMonitorMode()));
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
    try { interruptedMidiStateOrdering(); audioDerivedMidiReceiptClockGuards(); interruptedMidiReleases(); replacementConsumerLifetime(); immediateMonitoringTransitions(); selectionAndMonitoring(); return 0; }
    catch(const std::exception& e){std::cerr<<"FAIL MIDI monitoring: "<<e.what()<<std::endl;return 1;}
}
// Session recording into clip slots: generated events land in the slot take relative to
// its grid punch-in, the take ends on the grid, loops over its length and plays at once.
void sessionRecording()
{
    GeneratedInput f; auto& session = f.session;
    const auto slotState = [&] (int scene) { const auto state = session.snapshot(); return state.tracks[2].slots[(size_t) scene]; };
    check (session.recordSlot (2, 0).failed() && ! slotState (0).recording, "an unarmed track cannot record into a slot");
    ok (session.setTrackArmed (2, true), "arm the Session recording track");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::beat), "record on the one-beat grid");
    ok (session.addMidiClipToSlot (session.snapshot().tracks[2].id, 3, 4), "occupy another slot");
    check (session.recordSlot (2, 3).failed(), "recording needs an empty slot");
    session.seek (0); ok (session.play(), "start the transport");
    const auto origin = f.devices.getCurrentStreamTime() + 256.0 / 48000;
    pumpSyntheticTestDevice (session, 34); // About 0.18 s into the first beat.
    ok (session.recordSlot (2, 0), "record into an empty slot from the next beat");
    check (slotState (0).recording && ! slotState (0).hasClip, "the slot reports that it is recording");
    // At 120 BPM the punch-in is at 0.5 s. The first key is before it and is left out.
    f.send (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 90), origin + .3); f.send (juce::MidiMessage::noteOff (1, 55), origin + .35);
    f.send (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 96), origin + .6); f.send (juce::MidiMessage::noteOff (1, 60), origin + .8);
    f.send (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 64), origin + 1.1); f.send (juce::MidiMessage::noteOff (1, 64), origin + 1.3);
    pumpSyntheticTestDevice (session, 230); // About 1.41 s.
    ok (session.recordSlot (2, 0), "press the recording slot to end the take on the next beat");
    check (slotState (0).recording, "the take continues until its grid stop");
    pumpSyntheticTestDevice (session, 60); // Past the 1.5 s stop.
    auto take = slotState (0);
    check (! take.recording && take.hasClip && take.clip.isMidi, "the finished take becomes a MIDI clip in the slot");
    check (std::abs (take.clip.lengthBeats - 2) < 1e-6, "the take spans the two beats from punch-in to the grid stop (length " + juce::String (take.clip.lengthBeats, 6) + ")");
    check (take.clip.notes.size() == 2 && ! take.clip.notes[0].id.isEmpty() && take.clip.notes[0].id != take.clip.notes[1].id,
           "the take keeps the two keys inside it with editable identities");
    const double starts[] { .2, 1.2 }, lengths[] { .4, .4 };
    for (size_t i = 0; i < 2; ++i)
        check (take.clip.notes[i].pitch == (i == 0 ? 60 : 64) && std::abs (take.clip.notes[i].startBeat - starts[i]) < 2.0 / 24000
               && std::abs (take.clip.notes[i].lengthBeats - lengths[i]) < 2.0 / 24000,
               "recorded key " + juce::String ((int) i + 1) + " sits at its offset from the punch-in (" + juce::String (take.clip.notes[i].startBeat, 6) + ")");
    check (take.playing, "the take plays in loop as soon as it is recorded");
    ok (session.stopLauncherTrack (2), "stop the track"); pumpSyntheticTestDevice (session, 100);
    check (! slotState (0).playing, "the take stops with its track");
    session.undo(); check (! slotState (0).hasClip, "one undo removes the take");
    session.redo(); check (slotState (0).hasClip && slotState (0).clip.id == take.clip.id && slotState (0).clip.notes.size() == 2, "redo restores the take");
    // An empty armed slot records from its launch button, and transport stop keeps the take silent.
    ok (session.pressSlot (2, 1), "the launch button of an empty armed slot records");
    check (slotState (1).recording, "the pressed slot records");
    pumpSyntheticTestDevice (session, 120);
    f.send (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), f.devices.getCurrentStreamTime() + .02);
    pumpSyntheticTestDevice (session, 20);
    f.send (juce::MidiMessage::noteOff (1, 67), f.devices.getCurrentStreamTime() + .02);
    pumpSyntheticTestDevice (session, 20);
    session.stop();
    const auto stopped = slotState (1);
    check (! stopped.recording && stopped.hasClip && stopped.clip.notes.size() == 1 && stopped.clip.notes[0].pitch == 67 && ! stopped.playing,
           "stopping the transport ends the take, keeps its notes and leaves it silent");
    check (session.recordSlot (2, 1).failed(), "a finished take's slot is no longer empty");
    // From a stopped transport the take starts at once and the transport keeps running.
    ok (session.recordSlot (2, 2), "record from a stopped transport");
    pumpSyntheticTestDevice (session, 40);
    check (session.snapshot().playing && slotState (2).recording, "recording from a stopped transport starts playback");
    ok (session.recordSlot (2, 2), "end the take started from a stopped transport");
    pumpSyntheticTestDevice (session, 120); // Past the next beat.
    check (slotState (2).hasClip && slotState (2).playing, "the take from a stopped transport plays");
    session.stop();
}

// Input assignment, arming and monitoring leave a playing transport and its launched Session
// clip running, also when assigning an input enables its device and rebuilds the device list.
void inputChangesKeepPlaying()
{
    Session session {false, makeSyntheticTestDeviceType()};
    VirtualController controller {session}; // Disabled until a track selects it.
    ok (session.addMidiTrack(), "create the launched instrument track");
    const auto trackId = session.snapshot().tracks[2].id;
    auto patch = session.snapshot().tracks[2].synth;
    patch.waveform = 2; patch.attackSeconds = 0; patch.releaseSeconds = .02f; patch.level = .1f;
    patch.cutoffHz = 20000; patch.resonance = 0;
    ok (session.setSynthSettings (trackId, patch), "prepare an audible launched instrument");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "launch without a grid");
    ok (session.addMidiClipToSlot (trackId, 0, 4), "create the launched Session clip");
    const auto clipId = session.snapshot().tracks[2].slots[0].clip.id;
    for (int step = 0; step < 16; ++step)
        ok (session.addMidiNote (clipId, 60, step * .25, .2, .8f), "add a sixteenth note to the launched clip");
    // Rebuilds replace Tracktion's wave input objects, so look the generated input up by name.
    const auto audioInputs = session.inputNames();
    check (! audioInputs.isEmpty(), "the synthetic device offers a generated audio input");
    const auto audioInput = [&]
    {
        for (auto* device : controller.devices.getWaveInputDevices()) if (device->getName() == audioInputs[0]) return device;
        throw std::runtime_error ("generated audio input disappeared");
    };
    audioInput()->setEnabled (false);
    pumpSyntheticTestDevice (session, 8); // Apply the device list changes before playing.
    check (! audioInput()->isEnabled(), "the generated audio input starts disabled");
    ok (session.launchSlot (2, 0), "launch the Session clip");
    pumpSyntheticTestDevice (session, 8);
    check (session.snapshot().playing && session.snapshot().tracks[2].slots[0].playing, "the launched clip plays the transport");
    const auto keepsPlaying = [&] (const juce::Result& result, const juce::String& change)
    {
        ok (result, change);
        const auto before = session.snapshot().position;
        const float peak = pumpSyntheticTestDevice (session, 40);
        const auto state = session.snapshot();
        check (state.playing && state.position > before && state.tracks[2].slots[0].playing && peak > .001f,
               change + " keeps the transport and the launched clip playing (peak " + juce::String (peak) + ")");
    };
    keepsPlaying (session.setTrackInput (2, controller.name), "assigning the disabled controller");
    check (controller.midi->isEnabled(), "assigning the controller enabled its device");
    keepsPlaying (session.setTrackArmed (2, true), "arming the MIDI track");
    keepsPlaying (session.setTrackMonitor (2, Session::MonitorMode::on), "monitoring the MIDI track");
    keepsPlaying (session.setTrackMonitor (2, Session::MonitorMode::off), "ending MIDI monitoring");
    keepsPlaying (session.setTrackArmed (2, false), "disarming the MIDI track");
    keepsPlaying (session.setTrackInput (2, {}), "disconnecting the controller");
    keepsPlaying (session.setTrackInput (0, audioInputs[0]), "assigning the disabled audio input");
    check (audioInput()->isEnabled(), "assigning the audio input enabled its device");
    keepsPlaying (session.setTrackArmed (0, true), "arming the audio track");
    keepsPlaying (session.setTrackMonitor (0, Session::MonitorMode::automatic), "auto-monitoring the audio track");
    keepsPlaying (session.setTrackMonitor (0, Session::MonitorMode::off), "ending audio monitoring");
    keepsPlaying (session.setTrackArmed (0, false), "disarming the audio track");
    keepsPlaying (session.setTrackInput (0, {}), "disconnecting the audio input");
    session.stop();
}

// Enabling an input device or a change in the MIDI device list rebuilds every playback
// context's device list, and the engine discards active input recordings while rebuilding.
// A Session take leaves the transport out of recording, so it is protected separately:
// input assignment is refused while it records, arming and monitoring another track leave
// it running, and MIDI discovery waits until it ends.
void sessionTakesSurviveDeviceChanges()
{
    Session session {false, makeSyntheticTestDeviceType()};
    VirtualController keys {session}, spare {session}, other {session}; // Disabled until a track selects them.
    ok (session.addMidiTrack(), "create the Session recording MIDI track");
    ok (session.addMidiTrack(), "create a second MIDI track");
    ok (session.setTrackInput (2, keys.name), "assign the generated controller to the recording track");
    ok (session.setTrackInput (3, other.name), "assign another controller to the second MIDI track");
    ok (session.setTrackArmed (2, true), "arm the Session recording track");
    ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "record Session takes without a grid");
    // Rebuilds replace the engine's wave input objects, so look the generated input up by name.
    const auto audioInputs = session.inputNames();
    check (! audioInputs.isEmpty(), "the synthetic device offers a generated audio input");
    const auto audioInput = [&]
    {
        for (auto* device : keys.devices.getWaveInputDevices()) if (device->getName() == audioInputs[0]) return device;
        throw std::runtime_error ("generated audio input disappeared");
    };
    audioInput()->setEnabled (false);
    pumpSyntheticTestDevice (session, 8); // Apply the device list changes before recording.
    const auto slotState = [&] (int track, int scene) { return session.snapshot().tracks[(size_t) track].slots[(size_t) scene]; };
    const auto playKey = [&] (int pitch)
    {
        keys.sendNow (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 96)); pumpSyntheticTestDevice (session, 20);
        keys.sendNow (juce::MidiMessage::noteOff (1, pitch)); pumpSyntheticTestDevice (session, 20);
    };
    const auto refused = [&] (const juce::Result& result, const juce::String& change, int track, int scene)
    {
        check (result.failed() && slotState (track, scene).recording && session.snapshot().playing,
               change + " is refused while the Session take records (" + result.getErrorMessage() + ")");
    };
    const auto discovered = [&] (const juce::String& name)
    {
        const auto inputs = keys.devices.getMidiInDevices();
        return std::any_of (inputs.begin(), inputs.end(), [&] (const auto& device) { return device->getName() == name; });
    };
    // A MIDI device appears while the take records: discovery waits and resumes after the take.
    const auto plugInDuringTake = [&] (int track, int scene)
    {
        const auto name = "Halftrack plugged MIDI " + juce::Uuid().toString();
        ok (keys.devices.createVirtualMidiDevice (name), "a MIDI device appears during the take");
        keys.devices.rescanMidiDeviceListSynchronously();
        pumpSyntheticTestDevice (session, 20); // The engine's own rescan request runs here as well.
        check (slotState (track, scene).recording && session.snapshot().playing && ! discovered (name),
               "MIDI discovery waits while the Session take records");
        return name;
    };
    const auto discoveryResumes = [&] (const juce::String& name)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
        while (! discovered (name) && juce::Time::getMillisecondCounterHiRes() < deadline) pumpSyntheticTestDevice (session, 4);
        check (discovered (name), "deferred MIDI discovery resumes after the take ends");
        for (auto device : keys.devices.getMidiInDevices())
            if (auto plugged = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (device); plugged && plugged->getName() == name)
            { keys.devices.deleteVirtualMidiDevice (*plugged); keys.devices.rescanMidiDeviceListSynchronously(); break; }
    };

    session.seek (0); ok (session.play(), "start the transport");
    pumpSyntheticTestDevice (session, 8);
    ok (session.recordSlot (2, 0), "start a MIDI Session take");
    pumpSyntheticTestDevice (session, 8);
    playKey (60);
    refused (session.setTrackInput (3, spare.name), "assigning a disabled controller to another track", 2, 0);
    check (! spare.midi->isEnabled() && session.snapshot().tracks[3].inputName == other.name,
           "the refused controller assignment enabled no device and kept the track's input");
    refused (session.setTrackInput (0, audioInputs[0]), "assigning a disabled audio input", 2, 0);
    check (! audioInput()->isEnabled(), "the refused audio assignment left the input disabled");
    refused (session.setTrackInput (2, {}), "disconnecting the recording track's input", 2, 0);
    playKey (62);
    // Arming and monitoring rebuild only the playback graph, which keeps the take.
    ok (session.setTrackArmed (3, true), "arm another track during the take");
    ok (session.setTrackMonitor (3, Session::MonitorMode::on), "monitor another track during the take");
    ok (session.setTrackMonitor (3, Session::MonitorMode::off), "end monitoring during the take");
    ok (session.setTrackArmed (3, false), "disarm another track during the take");
    check (slotState (2, 0).recording && session.snapshot().playing, "arming and monitoring keep the take recording");
    playKey (64);
    const auto plugged = plugInDuringTake (2, 0);
    playKey (65);
    ok (session.recordSlot (2, 0), "end the MIDI take");
    pumpSyntheticTestDevice (session, 60);
    const auto take = slotState (2, 0);
    check (! take.recording && take.hasClip && take.clip.isMidi, "the MIDI take becomes a slot clip");
    std::vector<int> pitches;
    for (const auto& note : take.clip.notes) pitches.push_back (note.pitch);
    check (pitches == std::vector<int> { 60, 62, 64, 65 }, "the MIDI take keeps every key played around the input changes ("
           + juce::String ((int) pitches.size()) + " notes)");
    session.stop();
    discoveryResumes (plugged);

    // An audio take from the generated input survives the same changes.
    audioInput()->setEnabled (true);
    pumpSyntheticTestDevice (session, 8);
    ok (session.setTrackInput (0, audioInputs[0]), "assign the generated audio input");
    ok (session.setTrackArmed (0, true), "arm the audio track");
    session.seek (0); ok (session.play(), "restart the transport");
    pumpSyntheticTestDevice (session, 8);
    ok (session.recordSlot (0, 1), "start an audio Session take");
    pumpSyntheticTestDevice (session, 40);
    refused (session.setTrackInput (3, spare.name), "assigning a disabled controller during an audio take", 0, 1);
    const auto pluggedDuringAudio = plugInDuringTake (0, 1);
    pumpSyntheticTestDevice (session, 100);
    ok (session.recordSlot (0, 1), "end the audio take");
    pumpSyntheticTestDevice (session, 60);
    const auto audioTake = slotState (0, 1);
    check (! audioTake.recording && audioTake.hasClip && audioTake.clip.isAudio && audioTake.clip.file.existsAsFile(),
           "the audio take becomes a slot clip with its recording");
    const auto recorded = readAudio (audioTake.clip.file);
    check (std::abs (recorded.getNumSamples() - 160 * 256) <= 512 && recorded.getMagnitude (0, recorded.getNumSamples()) > .15f,
           "the audio take holds the generated input from start to end (" + juce::String (recorded.getNumSamples()) + " samples)");
    session.stop();
    discoveryResumes (pluggedDuringAudio);
}

// An armed MIDI track with a generated controller, recording Session takes without a launch grid.
struct SessionTake
{
    Session session {false, makeSyntheticTestDeviceType()};
    VirtualController keys {session};
    juce::String trackId;
    SessionTake()
    {
        ok (session.addMidiTrack(), "create the Session recording MIDI track");
        trackId = session.snapshot().tracks[2].id;
        ok (session.setTrackInput (2, keys.name), "assign the generated controller to the recording track");
        ok (session.setTrackArmed (2, true), "arm the Session recording track");
        ok (session.setLaunchQuantisation (Session::LaunchQuantisation::none), "record Session takes without a grid");
        pumpSyntheticTestDevice (session, 8);
    }
    int track() const
    {
        const auto tracks = session.snapshot().tracks;
        for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].id == trackId) return (int) i;
        return -1;
    }
    // The scene whose slot on the recording track is recording, wherever edits moved it.
    int recordingScene() const
    {
        const auto index = track();
        if (index < 0) return -1;
        const auto slots = session.snapshot().tracks[(size_t) index].slots;
        for (size_t i = 0; i < slots.size(); ++i) if (slots[i].recording) return (int) i;
        return -1;
    }
    void start (int scene)
    {
        session.seek (0); ok (session.play(), "start the transport");
        pumpSyntheticTestDevice (session, 8);
        ok (session.recordSlot (track(), scene), "start a MIDI Session take");
        pumpSyntheticTestDevice (session, 8);
    }
    void key (int pitch)
    {
        keys.sendNow (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 96)); pumpSyntheticTestDevice (session, 20);
        keys.sendNow (juce::MidiMessage::noteOff (1, pitch)); pumpSyntheticTestDevice (session, 20);
    }
    // Ends the take from its slot and returns the pitches of the finished slot clip.
    std::vector<int> finish (const juce::String& edit)
    {
        const auto scene = recordingScene();
        check (scene >= 0, "the Session take still records after " + edit);
        ok (session.recordSlot (track(), scene), "end the Session take");
        pumpSyntheticTestDevice (session, 60);
        const auto slot = session.snapshot().tracks[(size_t) track()].slots[(size_t) scene];
        check (! slot.recording && slot.hasClip && slot.clip.isMidi, "the take ends as a MIDI slot clip after " + edit);
        session.stop();
        std::vector<int> pitches;
        for (const auto& note : slot.clip.notes) pitches.push_back (note.pitch);
        return pitches;
    }
    // Plays one key before and one after the edit, then ends the take.
    void survives (int scene, const juce::String& edit, const std::function<void()>& apply)
    {
        start (scene); key (60); apply(); key (62);
        const auto pitches = finish (edit);
        check (pitches == std::vector<int> { 60, 62 }, edit + " keeps the Session take with both keys ("
               + juce::String ((int) pitches.size()) + " notes)");
    }
    void refused (const juce::Result& result, const juce::String& edit) const
    {
        check (result.failed() && recordingScene() >= 0 && session.snapshot().playing,
               edit + " is refused while the Session take records (" + result.getErrorMessage() + ")");
    }
};

// Session takes record through the input devices and leave the transport out of recording, so
// the edits refused during Arrangement recording are checked against them separately. Each edit
// either leaves the take recording until it ends with every key, or is refused during the take.
void editsDuringSessionTakes (const juce::File& root)
{
    {
        // Undo could reach the recording track's arm and input assignment, and its slot would
        // keep recording with no input to stop the take on.
        SessionTake t; auto& session = t.session;
        ok (session.renameScene (7, "Renamed"), "make an edit to redo"); session.undo();
        check (session.canRedo(), "the rename can be redone before the take");
        bool couldUndo = true, couldRedo = true;
        t.survives (0, "undo and redo", [&]
        {
            couldUndo = session.canUndo(); couldRedo = session.canRedo();
            for (int i = 0; i < 3; ++i) session.undo();
            session.redo();
        });
        const auto state = session.snapshot();
        check (! couldUndo && ! couldRedo, "undo and redo are unavailable while the Session take records");
        check (state.tracks[2].armed && state.tracks[2].inputName == t.keys.name && state.scenes[7].name == "Scene 8"
               && state.launchQuantisation == Session::LaunchQuantisation::none,
               "undo and redo during the take changed no earlier edit");
        session.undo(); check (! session.snapshot().tracks[2].slots[0].hasClip, "after the take one undo removes it");
    }
    {
        SessionTake t; auto& session = t.session;
        t.survives (0, "removing another track", [&]
        {
            t.refused (session.removeTrack (t.track()), "removing the recording track");
            ok (session.removeTrack (0), "remove another track during the take");
        });
    }
    {
        SessionTake t; auto& session = t.session;
        t.survives (0, "reordering tracks", [&]
        {
            ok (session.moveTrack (t.trackId, 0), "move the recording track to the top");
            ok (session.moveTrack (session.snapshot().tracks[2].id, 1), "move another track past it");
        });
        check (t.track() == 0, "the recording track keeps its new position");
    }
    {
        SessionTake t; auto& session = t.session;
        juce::String copy;
        t.survives (0, "duplicating the recording track", [&] { ok (session.duplicateTrack (t.trackId, copy), "duplicate the recording track"); });
        for (const auto& track : session.snapshot().tracks)
            if (track.id == copy) check (! track.slots[0].hasClip && ! track.slots[0].recording && ! track.armed,
                                         "the duplicate has an empty, idle copy of the recording slot");
    }
    {
        SessionTake t; auto& session = t.session;
        juce::String group;
        t.survives (0, "grouping the recording track", [&]
        { ok (session.groupTracks ({ t.trackId, session.snapshot().tracks[1].id }, group), "group the recording track"); });
        check (session.snapshot().tracks[(size_t) t.track()].groupId == group, "the recording track is in the new group");
        t.survives (1, "ungrouping the recording track", [&] { ok (session.ungroupTracks (group), "ungroup the recording track"); });
        check (session.snapshot().tracks[(size_t) t.track()].groupId.isEmpty(), "the recording track left the group");
    }
    {
        SessionTake t; auto& session = t.session;
        juce::String copy;
        t.survives (2, "scene edits", [&]
        {
            t.refused (session.removeScene (2), "deleting the recording scene");
            ok (session.removeScene (0), "delete an earlier scene during the take");
            check (t.recordingScene() == 1, "the take follows its scene up a row");
            ok (session.moveScene (session.snapshot().scenes[1].id, 4), "move the recording scene");
            check (t.recordingScene() == 4, "the take moves with its scene");
            ok (session.duplicateScene (session.snapshot().scenes[4].id, copy), "duplicate the recording scene");
            ok (session.addScene(), "add a scene during the take");
        });
        const auto state = session.snapshot();
        check (state.scenes[5].id == copy && ! state.tracks[(size_t) t.track()].slots[5].hasClip,
               "the duplicated scene's slot on the recording track stays empty");
    }
    {
        SessionTake t; auto& session = t.session;
        ok (session.addMidiClipToSlot (t.trackId, 1, 4), "fill another slot on the recording track");
        const auto other = session.snapshot().tracks[2].slots[1].clip.id;
        t.survives (0, "slot edits", [&]
        {
            t.refused (session.addMidiClipToSlot (t.trackId, 0, 4), "creating a clip in the recording slot");
            check (session.moveMidiClip (other, t.trackId, 0).failed() && session.copyMidiClip (other, true).failed(),
                   "slot clips are not moved or cut through the Arrangement clipboard");
            ok (session.clearSlot (t.track(), 0), "clearing the empty recording slot");
            ok (session.clearSlot (t.track(), 1), "clear another slot on the recording track");
        });
    }
    {
        SessionTake t; auto& session = t.session;
        const auto fixture = root.getChildFile ("take-edit-import.mid");
        {
            juce::MidiMessageSequence events;
            events.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0); events.addEvent (juce::MidiMessage::noteOff (1, 48), 960);
            events.updateMatchedPairs();
            juce::MidiFile midi; midi.setTicksPerQuarterNote (960); midi.addTrack (events);
            auto out = fixture.createOutputStream();
            check (out != nullptr && midi.writeTo (*out, 1), "write the generated MIDI import fixture");
        }
        const auto tracks = session.snapshot().tracks.size();
        t.survives (0, "importing a MIDI file", [&]
        {
            std::vector<juce::String> ids;
            ok (session.importMidiFile (fixture, 0, ids), "import a MIDI file during the take");
        });
        check (session.snapshot().tracks.size() == tracks + 1, "the import added its own track");
    }
    {
        // A save during the take holds the project as it was before the take ends.
        SessionTake t; auto& session = t.session;
        const auto file = root.getChildFile ("Saved during a take.halftrack");
        t.survives (0, "saving", [&] { ok (session.saveProject (file), "save the project during the take"); });
        check (session.snapshot().dirty, "the finished take is an unsaved change");
        Session reopened {false, makeSyntheticTestDeviceType()};
        ok (reopened.loadProject (file), "load the project saved during the take");
        const auto saved = reopened.snapshot().tracks[2];
        check (! saved.slots[0].hasClip && ! saved.slots[0].recording, "the project saved during the take reopens with its slot empty and idle");
    }
    {
        // Rendering frees the playback context, which discards every input recording.
        SessionTake t; auto& session = t.session;
        ok (session.addMidiClip (t.trackId, 0, 4), "give the recording track an Arrangement clip to render");
        const auto clipId = session.snapshot().tracks[2].clips[0].id;
        juce::StringArray stems;
        t.survives (0, "rendering", [&]
        {
            t.refused (session.exportAudio (root.getChildFile ("take-edit-mix.wav"), Session::ExportFormat::wav), "exporting audio");
            t.refused (session.exportStems (root.getChildFile ("take-edit-stems"), {}, stems), "exporting stems");
            t.refused (session.setTrackFrozen (t.trackId, true), "freezing a track");
            ok (session.exportMidiClip (clipId, root.getChildFile ("take-edit-clip.mid")), "export a MIDI clip during the take");
        });
        check (! root.getChildFile ("take-edit-mix.wav").exists() && ! root.getChildFile ("take-edit-stems").exists() && stems.isEmpty(),
               "the refused exports wrote nothing");
    }
    {
        // Flattening a frozen MIDI track removes its input, which the take stops on.
        SessionTake t; auto& session = t.session;
        ok (session.addMidiClip (t.trackId, 0, 4), "give the recording track an Arrangement clip to freeze");
        ok (session.setTrackFrozen (t.trackId, true), "freeze the recording track");
        t.survives (0, "flattening", [&] { t.refused (session.flattenTrack (t.trackId), "flattening the recording track"); });
    }
    {
        // An audio take refuses an import into its slot, which the finished take would replace.
        SessionTake t; auto& session = t.session;
        const auto fixture = root.getChildFile ("take-edit-import.wav");
        {
            juce::AudioBuffer<float> audio (2, 4800);
            for (int i = 0; i < audio.getNumSamples(); ++i) for (int c = 0; c < 2; ++c) audio.setSample (c, i, .25f * std::sin ((float) i * .05f));
            juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> stream (fixture.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write the generated audio import fixture");
        }
        const auto audioInputs = session.inputNames();
        check (! audioInputs.isEmpty(), "the synthetic device offers a generated audio input");
        ok (session.setTrackInput (0, audioInputs[0]), "assign the generated audio input");
        ok (session.setTrackArmed (0, true), "arm the audio track");
        session.seek (0); ok (session.play(), "start the transport");
        pumpSyntheticTestDevice (session, 8);
        ok (session.recordSlot (0, 0), "start an audio Session take");
        pumpSyntheticTestDevice (session, 40);
        const auto refusal = session.importAudioToSlot (fixture, 0, 0);
        check (refusal.failed() && session.snapshot().tracks[0].slots[0].recording,
               "importing audio into the recording slot is refused while the take records (" + refusal.getErrorMessage() + ")");
        ok (session.importAudioToSlot (fixture, 0, 1), "import audio into another slot of the recording track");
        pumpSyntheticTestDevice (session, 100);
        ok (session.recordSlot (0, 0), "end the audio take");
        pumpSyntheticTestDevice (session, 60);
        const auto take = session.snapshot().tracks[0].slots[0];
        check (! take.recording && take.hasClip && take.clip.isAudio && take.clip.file.existsAsFile() && take.clip.file != fixture,
               "the audio take becomes its slot's clip with its own recording");
        const auto recorded = readAudio (take.clip.file);
        check (recorded.getNumSamples() > 100 * 256 && recorded.getMagnitude (0, recorded.getNumSamples()) > .15f,
               "the audio take holds the generated input (" + juce::String (recorded.getNumSamples()) + " samples)");
        session.stop();
    }
    {
        // A new project ends the take in the replaced project and leaves no recording behind.
        SessionTake t; auto& session = t.session;
        t.start (0); t.key (60);
        ok (session.newProject(), "start a new project during the take");
        const auto state = session.snapshot();
        bool recording = false;
        for (const auto& track : state.tracks) for (const auto& slot : track.slots) recording |= slot.recording;
        check (! recording && ! state.playing, "the new project has no Session recording");
        ok (session.addMidiTrack(), "add a MIDI track to the new project");
        ok (session.setTrackInput (2, t.keys.name), "inputs can be assigned in the new project");
    }
}

// The remaining edits refused only during Arrangement recording: seeking, the recording track's
// own arm, monitoring and instrument, Arrangement clip and time edits, automation, follow actions
// and the recording settings. Each keeps the take with every key, or ends or refuses it first.
void playbackAndArrangementEditsDuringSessionTakes (const juce::File& root)
{
    {
        // Seeking moves the play head the take is timed by, so it ends the take where it was and
        // stops the transport, as it does during Arrangement recording.
        SessionTake t; auto& session = t.session;
        t.start (0); t.key (60); t.key (62);
        session.seek (3);
        const auto state = session.snapshot();
        const auto slot = state.tracks[2].slots[0];
        std::vector<int> pitches;
        for (const auto& note : slot.clip.notes) pitches.push_back (note.pitch);
        check (! slot.recording && slot.hasClip && slot.clip.isMidi && pitches == std::vector<int> { 60, 62 },
               "seeking ends the Session take with the keys played before it (" + juce::String ((int) pitches.size()) + " notes)");
        check (! state.playing && std::abs (state.position - 3) < 1e-9 && ! slot.playing,
               "the seek stops the transport at the new position and leaves the take silent");
        ok (session.setTrackInput (0, session.inputNames()[0]), "no Session take is left recording after the seek");
    }
    {
        // Monitoring the recording track keeps its take; disarming it is refused, because the take
        // would go on recording a track that shows it is not armed.
        SessionTake t; auto& session = t.session;
        const auto heard = [&] (int pitch)
        {
            t.keys.sendNow (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 96)); const auto peak = pumpSyntheticTestDevice (session, 20);
            t.keys.sendNow (juce::MidiMessage::noteOff (1, pitch)); pumpSyntheticTestDevice (session, 40);
            return peak;
        };
        t.start (0); t.key (60);
        ok (session.setTrackMonitor (t.track(), Session::MonitorMode::on), "monitor the recording track during its take");
        const auto monitored = heard (62);
        check (monitored > .001f, "the recording track's monitored key is heard during the take (peak " + juce::String (monitored) + ")");
        ok (session.setTrackMonitor (t.track(), Session::MonitorMode::automatic), "auto-monitor the recording track during its take");
        ok (session.setTrackMonitor (t.track(), Session::MonitorMode::off), "end monitoring of the recording track during its take");
        t.refused (session.setTrackArmed (t.track(), false), "disarming the recording track");
        ok (session.setTrackArmed (t.track(), true), "arming the armed recording track again changes nothing");
        t.key (64);
        const auto pitches = t.finish ("monitoring the recording track");
        check (pitches == std::vector<int> { 60, 62, 64 }, "monitoring the recording track keeps the take with every key ("
               + juce::String ((int) pitches.size()) + " notes)");
        const auto track = session.snapshot().tracks[2];
        check (track.armed && track.monitor == Session::MonitorMode::off, "the recording track stays armed with monitoring off");
        ok (session.setTrackArmed (t.track(), false), "the track can be disarmed once its take ends");
    }
    {
        SessionTake t; auto& session = t.session;
        t.survives (0, "changing the recording track's instrument", [&]
        {
            ok (session.setTrackInstrument (t.trackId, Session::EffectKind::polySynth), "replace the recording track's instrument during the take");
            check (session.snapshot().tracks[2].instrument.isNativeEffect, "the recording track plays the new instrument");
            ok (session.setTrackInstrument (t.trackId, std::nullopt), "restore the built-in synthesizer during the take");
        });
    }
    {
        // Arrangement clip and time edits change no slot, on the recording track or on others.
        SessionTake t; auto& session = t.session;
        const auto fixture = root.getChildFile ("take-arrangement-edit.wav");
        {
            juce::AudioBuffer<float> audio (2, 96000);
            for (int i = 0; i < audio.getNumSamples(); ++i) for (int c = 0; c < 2; ++c) audio.setSample (c, i, .25f * std::sin ((float) i * .05f));
            juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> stream (fixture.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write the generated Arrangement audio fixture");
        }
        ok (session.importAudio (fixture, 0, 4), "import Arrangement audio onto another track");
        ok (session.addMidiClip (t.trackId, 8, 4), "give the recording track an Arrangement MIDI clip");
        const auto before = session.snapshot();
        const auto audioId = before.tracks[0].clips[0].id, midiId = before.tracks[2].clips[0].id;
        const auto audioTrack = before.tracks[0].id, otherAudioTrack = before.tracks[1].id;
        t.survives (0, "Arrangement clip and time edits", [&]
        {
            juce::String right, duplicate, pasted, midiCopy, midiPasted;
            ok (session.setAudioClipRange (audioId, 4, 1.5, .25), "trim an Arrangement audio clip during the take");
            ok (session.setAudioClipReversed (audioId, true), "reverse an Arrangement audio clip during the take");
            ok (session.setAudioClipReversed (audioId, false), "restore the clip's direction during the take");
            ok (session.splitAudioClip (audioId, 5, right), "split an Arrangement audio clip during the take");
            ok (session.duplicateAudioClip (right, duplicate), "duplicate an Arrangement audio clip during the take");
            ok (session.copyAudioClip (duplicate, true), "cut an Arrangement audio clip during the take");
            ok (session.pasteAudioClip (audioTrack, 12, pasted), "paste an Arrangement audio clip during the take");
            ok (session.moveAudioClip (pasted, otherAudioTrack, 14), "move an Arrangement audio clip during the take");
            ok (session.duplicateMidiClip (midiId, midiCopy), "duplicate the recording track's Arrangement clip during the take");
            ok (session.copyMidiClip (midiCopy, true), "cut the recording track's Arrangement clip during the take");
            ok (session.pasteMidiClip (t.trackId, 16, midiPasted), "paste onto the recording track during the take");
            ok (session.moveMidiClip (midiPasted, t.trackId, 20), "move a clip on the recording track during the take");
            ok (session.insertTime (6, 2), "insert time during the take");
            ok (session.deleteTime (6, 2), "delete time during the take");
        });
        const auto after = session.snapshot();
        check (after.tracks[0].clips.size() == 2 && after.tracks[1].clips.size() == 1 && after.tracks[2].clips.size() == 2,
               "the Arrangement edits made during the take are in place");
    }
    {
        // Track, device and mixer automation.
        SessionTake t; auto& session = t.session;
        ok (session.addNativeEffect (2, Session::EffectKind::delay), "give the recording track a native device");
        const auto device = session.snapshot().tracks[2].plugins[0];
        const auto value = device.effect.values[0];
        t.survives (0, "automation edits", [&]
        {
            ok (session.setTrackAutomation (t.trackId, Session::AutomationTarget::volume, { true, { { 0, -6 }, { 8, 0 } } }),
                "automate the recording track's volume during the take");
            ok (session.setTrackAutomation (t.trackId, Session::AutomationTarget::pan, { true, { { 0, -.5f } } }),
                "automate the recording track's pan during the take");
            ok (session.setDeviceAutomation (device.id, 0, { true, { { 0, value }, { 4, value } } }),
                "automate the recording track's device during the take");
            ok (session.setMasterVolumeAutomation ({ true, { { 0, -3 } } }), "automate the master volume during the take");
        });
        const auto state = session.snapshot();
        check (state.tracks[2].volumeAutomation.points.size() == 2 && state.tracks[2].plugins[0].automation.size() == 1,
               "the automation written during the take is in place");
    }
    {
        // Follow actions on the recording track's other clips, count-in and recording quantization.
        SessionTake t; auto& session = t.session;
        ok (session.addMidiClipToSlot (t.trackId, 1, 4), "fill another slot on the recording track");
        const auto clipId = session.snapshot().tracks[2].slots[1].clip.id;
        Session::MetronomeSettings metronome; metronome.countInBars = 2;
        t.survives (0, "follow action and recording settings", [&]
        {
            ok (session.setClipFollowAction (clipId, { Session::FollowAction::next, 4 }), "set a follow action on the recording track during the take");
            ok (session.setMetronome (metronome), "change the count-in during the take");
            ok (session.setRecordQuantisation (Session::RecordQuantisation::quarter), "change recording quantization during the take");
        });
        const auto state = session.snapshot();
        check (state.metronome.countInBars == 2 && state.recordQuantisation == Session::RecordQuantisation::quarter
               && state.tracks[2].slots[1].clip.follow.action == Session::FollowAction::next,
               "the settings changed during the take are in place");
    }
}

int runMidiInputTests(const juce::File& artifacts)
{
    try
    {
        const auto root=artifacts.getChildFile("midi-input-"+juce::Uuid().toString()); ok(root.createDirectory(),"create isolated fixtures");
        selectionAndMonitoring(); recording(root); recordingExits(); countInRecording(); recordingQuantisation(root); controllersAndOverdub(root); sessionRecording(); inputChangesKeepPlaying(); sessionTakesSurviveDeviceChanges(); editsDuringSessionTakes(root); playbackAndArrangementEditsDuringSessionTakes(root);
        std::cout<<"MIDI input tests passed: "<<root.getFullPathName()<<std::endl; return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL MIDI input: "<<e.what()<<std::endl;return 1;}
}
}
