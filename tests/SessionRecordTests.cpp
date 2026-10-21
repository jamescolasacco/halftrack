#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Session Record, through the synthetic device and a generated controller. Pressing it records new
// clips into the scene's empty slots on armed tracks, MIDI and audio together; pressing it again
// ends them on the launch grid, where they loop and play. On an armed MIDI track whose slot holds a
// clip, it launches the clip and overdubs: notes played are added at their looped positions, as
// one undo step, until it is pressed again. Unarmed tracks are left alone.
namespace halftrack
{
namespace te = tracktion::engine;
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS session record: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
struct Controller
{
    Session& session;
    te::DeviceManager& devices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (session.audioDeviceManager()).engine.getDeviceManager();
    juce::String name = "Halftrack session record " + juce::Uuid().toString();
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
    void play (int pitch, double seconds)
    {
        now (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100));
        pumpSyntheticTestDevice (session, std::max (1, (int) std::lround (seconds * blocksPerSecond)));
        now (juce::MidiMessage::noteOff (1, pitch));
        pumpSyntheticTestDevice (session, 4);
    }
};
std::vector<Session::MidiNoteInfo> sorted (std::vector<Session::MidiNoteInfo> notes)
{ std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.startBeat < b.startBeat; }); return notes; }
}

int runSessionRecordTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("session-record-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Session").getChildFile ("Session.halftrack")), "save a recording destination");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const int keys = (int) s.snapshot().tracks.size() - 1;
        Controller controller (s);
        ok (s.setTrackInput (keys, controller.name), "route the controller"); ok (s.setTrackArmed (keys, true), "arm the MIDI track");
        ok (s.setTrackInput (0, s.inputNames()[0]), "route the audio input"); ok (s.setTrackArmed (0, true), "arm audio track 1");
        ok (s.setLaunchQuantisation (Session::LaunchQuantisation::bar), "launch on the bar");
        pumpSyntheticTestDevice (s, 8);
        const auto slot = [&] (int track, int scene) { return s.snapshot().tracks[(size_t) track].slots[(size_t) scene]; };

        // New clips.
        ok (s.sessionRecord (0), "press Session Record");
        check (s.snapshot().sessionRecording && slot (keys, 0).recording && slot (0, 0).recording && ! slot (1, 0).recording,
               "the armed tracks record into the scene's empty slots, the unarmed one does not");
        pumpSyntheticTestDevice (s, 4);
        controller.play (60, .25);
        pumpSyntheticTestDevice (s, blocksPerSecond);
        ok (s.sessionRecord (0), "press it again");
        for (int i = 0; i < 400 && ! (slot (keys, 0).playing && slot (0, 0).playing); ++i) pumpSyntheticTestDevice (s, 1);
        auto keysClip = slot (keys, 0);
        check (keysClip.hasClip && keysClip.playing && ! keysClip.recording && slot (0, 0).hasClip && slot (0, 0).playing && ! s.snapshot().sessionRecording,
               "the takes end on the bar and loop");
        check (std::abs (keysClip.clip.lengthBeats - 4) < 1e-6 && keysClip.clip.notes.size() == 1 && keysClip.clip.notes[0].pitch == 60
               && keysClip.clip.notes[0].startBeat < .25, "the MIDI take holds the note played");
        check (! slot (1, 0).hasClip, "the unarmed track's slot stays empty");

        // Overdubbing the playing MIDI clip.
        ok (s.sessionRecord (0), "press Session Record over the playing clips");
        check (s.snapshot().sessionRecording && slot (keys, 0).overdubbing && ! slot (0, 0).recording && ! slot (0, 0).overdubbing,
               "the MIDI clip overdubs; the audio clip only plays");
        for (int i = 0; i < 800 && std::abs (slot (keys, 0).playPositionBeats - 2) > .02; ++i) pumpSyntheticTestDevice (s, 1);
        const auto at = slot (keys, 0).playPositionBeats;
        controller.play (64, .25);
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        ok (s.sessionRecord (0), "press it again to stop overdubbing");
        check (! s.snapshot().sessionRecording && ! slot (keys, 0).overdubbing && slot (keys, 0).playing, "overdub ends and the clip plays on");
        auto notes = sorted (slot (keys, 0).clip.notes);
        check (notes.size() == 2 && notes[0].pitch == 60 && notes[1].pitch == 64 && std::abs (notes[1].startBeat - at) < .15 && notes[1].lengthBeats > .3,
               "the overdubbed note joins the clip where it was played in the loop (" + juce::String (notes.size() > 1 ? notes[1].startBeat : -1, 3) + " at " + juce::String (at, 3) + ")");
        s.undo();
        check (slot (keys, 0).clip.notes.size() == 1 && slot (keys, 0).clip.notes[0].pitch == 60, "one undo removes the overdub");
        s.redo();

        // A stopped clip launches to overdub, beside a new audio take.
        s.stop();
        ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) keys].id, 1, 4), "put an empty clip in scene 2");
        ok (s.sessionRecord (1), "press Session Record on scene 2");
        for (int i = 0; i < 400 && ! slot (keys, 1).playing; ++i) pumpSyntheticTestDevice (s, 1);
        check (slot (keys, 1).playing && slot (keys, 1).overdubbing && slot (0, 1).recording, "the stopped clip launches and overdubs while audio records anew");
        controller.play (67, .25);
        ok (s.sessionRecord (1), "press it again");
        for (int i = 0; i < 400 && ! slot (0, 1).playing; ++i) pumpSyntheticTestDevice (s, 1);
        check (slot (0, 1).hasClip && slot (0, 1).playing && slot (keys, 1).clip.notes.size() == 1 && slot (keys, 1).clip.notes[0].pitch == 67,
               "the audio take plays and the overdub kept its note");
        s.stop();
        std::cout << "Session record passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL session record: " << e.what() << std::endl; return 1; }
}
}
