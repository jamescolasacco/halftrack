#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Capture MIDI, through a generated controller and the synthetic device. Notes played into an
// armed or In-monitored MIDI track are kept. Capturing with the transport stopped writes them from
// the insert marker at the current tempo; while it ran, at the beats they were played. Either way
// the clip is bar-aligned and is one undo step; in Session View the clip goes into the first empty
// slot. Unarmed, unmonitored tracks keep nothing.
namespace halftrack
{
namespace te = tracktion::engine;
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS capture MIDI: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
struct Controller
{
    Session& session;
    te::DeviceManager& devices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (session.audioDeviceManager()).engine.getDeviceManager();
    juce::String name = "Halftrack capture MIDI " + juce::Uuid().toString();
    std::shared_ptr<te::VirtualMidiInputDevice> midi;
    explicit Controller (Session& owner) : session (owner)
    {
        ok (devices.createVirtualMidiDevice (name), "create a virtual controller");
        devices.rescanMidiDeviceListSynchronously();
        for (auto device : devices.getMidiInDevices()) if (device->getName() == name) midi = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (device);
        check (midi != nullptr, "the virtual controller is discovered");
        midi->setMIDIInputSourceDevices ({}); midi->setChannelToUse (0); midi->setMinimumLengthMs (0); midi->setManualAdjustmentMs (0);
    }
    ~Controller() { session.stop(); if (midi) { devices.deleteVirtualMidiDevice (*midi); devices.rescanMidiDeviceListSynchronously(); } }
    void now (const juce::MidiMessage& m)
    {
        auto message = m; message.setTimeStamp (devices.getCurrentStreamTime() + 256.0 / 48000 - midi->getAdjustSecs());
        midi->handleIncomingMidiMessage (message, midi->getMPESourceID());
    }
    // Plays a note for about `seconds`, leaving it time to arrive.
    void play (int pitch, double seconds)
    {
        now (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100));
        pumpSyntheticTestDevice (session, std::max (1, (int) std::lround (seconds * blocksPerSecond)));
        now (juce::MidiMessage::noteOff (1, pitch));
        pumpSyntheticTestDevice (session, 4);
    }
};
}

int runCaptureMidiTests (const juce::File&)
{
    try
    {
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const int keys = (int) s.snapshot().tracks.size() - 1;
        const auto keysId = s.snapshot().tracks[(size_t) keys].id;
        const auto clips = [&] { return s.snapshot().tracks[(size_t) keys].clips; };
        Controller controller (s);
        ok (s.setTrackInput (keys, controller.name), "route the controller");
        pumpSyntheticTestDevice (s, 8);
        juce::StringArray created;
        controller.play (60, .1);
        check (! s.hasCapturedMidi() && s.captureMidi (Session::CaptureDestination::arrangement, created).failed(),
               "an unarmed, unmonitored track keeps nothing, so there is nothing to capture");

        // Stopped: from the insert marker at the current tempo.
        ok (s.setTrackArmed (keys, true), "arm the track");
        pumpSyntheticTestDevice (s, 8);
        s.setInsertMarker (2); // Beat 4 at 120 BPM.
        controller.play (60, .25);
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        controller.play (64, .25);
        check (s.hasCapturedMidi(), "notes played into the armed track are kept");
        ok (s.captureMidi (Session::CaptureDestination::arrangement, created), "Capture with the transport stopped");
        auto captured = clips();
        check (created.size() == 1 && captured.size() == 1 && std::abs (captured[0].startBeat - 4) < 1e-6 && std::abs (captured[0].lengthBeats - 4) < 1e-6,
               "one bar-long clip starts at the insert marker");
        auto notes = captured[0].notes;
        std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
        check (notes.size() == 2 && notes[0].pitch == 60 && notes[0].startBeat == 0 && notes[1].pitch == 64 && notes[1].startBeat > .5 && notes[1].startBeat < 3
               && notes[0].lengthBeats > .3 && std::abs (notes[0].velocity - 100.0f / 127) < .01f, "the phrase starts on the clip's first beat in the order it was played");
        check (! s.hasCapturedMidi(), "capturing empties the kept notes");
        s.undo();
        check (clips().empty(), "one undo removes the captured clip");

        // Running: at the beats they were played, in the bar they fall in.
        s.seek (0); ok (s.play(), "play");
        pumpSyntheticTestDevice (s, (int) std::lround (2.75 * blocksPerSecond)); // Beat 5.5.
        controller.play (67, .25);
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        ok (s.captureMidi (Session::CaptureDestination::arrangement, created), "Capture while playing");
        s.stop();
        captured = clips();
        check (captured.size() == 1 && std::abs (captured[0].startBeat - 4) < 1e-6 && std::abs (captured[0].lengthBeats - 4) < 1e-6
               && captured[0].notes.size() == 1 && captured[0].notes[0].pitch == 67 && std::abs (captured[0].notes[0].startBeat - 1.5) < .15,
               "the note keeps its beat, in the second bar");
        s.undo();

        // Session View: the first empty slot; In monitoring without arming also keeps notes.
        ok (s.setTrackArmed (keys, false), "disarm"); ok (s.setTrackMonitor (keys, Session::MonitorMode::on), "monitor In");
        pumpSyntheticTestDevice (s, 8);
        controller.play (72, .25);
        ok (s.captureMidi (Session::CaptureDestination::session, created), "Capture into the Session");
        const auto slot = s.snapshot().tracks[(size_t) keys].slots[0];
        check (slot.hasClip && slot.clip.notes.size() == 1 && slot.clip.notes[0].pitch == 72 && std::abs (slot.clip.lengthBeats - 4) < 1e-6 && clips().empty(),
               "an In-monitored track's notes go into its first empty slot");
        ok (s.setTrackMonitor (keys, Session::MonitorMode::off), "stop monitoring");
        juce::ignoreUnused (keysId);
        std::cout << "Capture MIDI passed" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL capture MIDI: " << e.what() << std::endl; return 1; }
}
}
