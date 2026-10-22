#include "Session.h"
#include <iostream>
#include <stdexcept>

// MIDI output ports, through a virtual MIDI port the test makes (the Session sees only ports with
// its name prefix, never the machine's own). A MIDI track without an instrument plays to the port
// on its chosen channel; Sync sends MIDI Clock, Song Position, Start, Continue and Stop. Played
// through the synthetic audio device, so the port receives in real time what the engine sends.
// Platforms without virtual MIDI ports (Windows, Linux without the ALSA sequencer) skip.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS midi ports: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
struct Received final : juce::MidiInputCallback
{
    juce::CriticalSection lock;
    std::vector<juce::MidiMessage> messages;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override { const juce::ScopedLock sl (lock); messages.push_back (message); }
    std::vector<juce::MidiMessage> take()
    {
        juce::Thread::sleep (400); // The engine's dispatcher and the system deliver in real time.
        const juce::ScopedLock sl (lock);
        auto result = std::move (messages); messages.clear(); return result;
    }
};
// Plays from a position for a number of 256-sample blocks at 48 kHz, then stops. The blocks are
// paced in real time, as a device's are: the engine's MIDI dispatcher sends each message at its
// time and drops any that run far ahead of it.
void play (Session& s, double seconds, int blocks)
{
    const auto pace = [&s] (int count)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int block = 0; block < count; ++block)
        {
            pumpSyntheticTestDevice (s, 1);
            while (juce::Time::getMillisecondCounterHiRes() < start + (block + 1) * 256000.0 / 48000.0) juce::Thread::yield();
        }
    };
    // Let any playback rebuild a settings change asked for happen first: a rebuild restarts the
    // clock, which then waits for the next beat to say where it is.
    pumpSyntheticTestDevice (s, 16);
    s.seek (seconds); ok (s.play(), "play");
    pace (blocks);
    s.stop(); pace (8);
}
int count (const std::vector<juce::MidiMessage>& messages, const std::function<bool (const juce::MidiMessage&)>& match)
{ return (int) std::count_if (messages.begin(), messages.end(), match); }
}

int runMidiPortTests (const juce::File& artifacts)
{
    try
    {
        const auto prefix = "Halftrack Test Port " + juce::Uuid().toString().substring (0, 8);
        const auto name = prefix + " Out";
        Received received;
        auto receiver = juce::MidiInput::createNewDevice (name, &received);
        if (receiver == nullptr)
        {
            std::cout << "SKIP midi ports: this system cannot make a virtual MIDI port" << std::endl;
            return 0;
        }
        receiver->start();
        const auto root = artifacts.getChildFile ("midi-ports-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType(), prefix);
        pumpSyntheticTestDevice (s, 8);
        const auto ports = s.midiOutputPorts();
        check (ports.size() == 1 && ports[0] == Session::MidiOutputPortInfo { name, false, false }, "the Session lists only the test's port, with Track and Sync off");
        check (s.setMidiOutputPort ("No Such Port", true, false).failed(), "an unknown port is refused");

        // An instrument-less MIDI track plays to the port.
        ok (s.addMidiTrack ("Hardware"), "add a MIDI track");
        const auto track = [&] { const auto now = s.snapshot(); return now.tracks.back(); };
        const auto id = track().id;
        const Session::TrackOutput toPort { Session::OutputKind::midiPort, {}, name, 3 };
        check (track().hasInstrument && s.setTrackOutput (id, toPort).failed(), "a MIDI track with an instrument cannot play to a port");
        ok (s.removeTrackInstrument (id), "remove its instrument");
        check (! track().hasInstrument, "the track has no instrument");
        s.undo(); check (track().hasInstrument, "undo brings the instrument back"); s.redo();
        check (s.setTrackOutput (id, toPort).failed(), "a port whose Track switch is off is not offered");
        ok (s.setMidiOutputPort (name, true, false), "switch the port's Track on");
        check (s.midiOutputPorts()[0].track && ! s.midiOutputPorts()[0].sync, "Track shows on");
        check (s.setTrackOutput (id, { Session::OutputKind::midiPort, {}, name, 0 }).failed() && s.setTrackOutput (id, { Session::OutputKind::midiPort, {}, name, 17 }).failed(),
               "channels outside 1-16 are refused");
        ok (s.setTrackOutput (id, toPort), "MIDI To the port, channel 3");
        check (track().output == toPort, "the track shows the port and channel (" + juce::String ((int) track().output.kind) + " " + track().output.port + " " + juce::String (track().output.midiChannel) + ")");
        s.undo(); check (track().output.kind == Session::OutputKind::master, "undo returns it to Master in one step"); s.redo();
        check (track().output == toPort, "redo plays to the port again");

        ok (s.addMidiClip (id, 0, 8), "an 8-beat clip");
        juce::StringArray noteIds;
        ok (s.addMidiNotes (track().clips[0].id, { { {}, 60, .5, .25, .8f }, { {}, 64, 1, .25, .5f }, { {}, 67, 2, .5, 1 } }, noteIds), "three notes");
        received.take();
        play (s, 0, 48000 * 2 / 256);
        auto messages = received.take();
        std::vector<int> ons, offs;
        for (const auto& m : messages)
        {
            if (m.isNoteOn()) { check (m.getChannel() == 3, "note-on " + juce::String (m.getNoteNumber()) + " arrives on channel 3"); ons.push_back (m.getNoteNumber()); }
            if (m.isNoteOff()) { check (m.getChannel() == 3, "note-off " + juce::String (m.getNoteNumber()) + " arrives on channel 3"); offs.push_back (m.getNoteNumber()); }
        }
        check (ons == std::vector<int> { 60, 64, 67 }, "the notes arrive in order");
        check (std::count (offs.begin(), offs.end(), 60) >= 1 && std::count (offs.begin(), offs.end(), 64) >= 1 && std::count (offs.begin(), offs.end(), 67) >= 1, "and each is released");
        check (count (messages, [] (const auto& m) { return m.isMidiClock(); }) == 0, "with Sync off, no clock is sent");
        {
            const auto first = std::find_if (messages.begin(), messages.end(), [] (const auto& m) { return m.isNoteOn() && m.getNoteNumber() == 60; });
            check (first != messages.end() && first->getVelocity() == 102, "the first note keeps its velocity (" + juce::String (first->getVelocity()) + ")");
        }

        // An open port leaves Session clips launching as before.
        {
            ok (s.addMidiTrack ("Launcher"), "add a MIDI track for a Session clip");
            const int lane = (int) s.snapshot().tracks.size() - 1;
            ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) lane].id, 0), "a Session clip");
            pumpSyntheticTestDevice (s, 16);
            ok (s.launchSlot (lane, 0), "launch it");
            pumpSyntheticTestDevice (s, 48);
            const auto now = s.snapshot();
            check (now.tracks[(size_t) lane].slots.front().playing, "with the port open, the Session clip plays");
            s.stopAllLauncherClips(); s.stop(); pumpSyntheticTestDevice (s, 16);
            ok (s.removeTrack (lane), "remove it");
        }

        // Sync.
        ok (s.setMidiOutputPort (name, true, true), "switch the port's Sync on");
        check (s.midiOutputPorts()[0].sync, "Sync shows on");
        received.take();
        play (s, 0, 48000 * 2 / 256);
        messages = received.take();
        const auto clocks = count (messages, [] (const auto& m) { return m.isMidiClock(); });
        const auto startAt = std::find_if (messages.begin(), messages.end(), [] (const auto& m) { return m.isMidiStart(); });
        const auto positionAt = std::find_if (messages.begin(), messages.end(), [] (const auto& m) { return m.isSongPositionPointer(); });
        const auto firstClock = std::find_if (messages.begin(), messages.end(), [] (const auto& m) { return m.isMidiClock(); });
        check (positionAt != messages.end() && positionAt->getSongPositionPointerMidiBeat() == 0 && startAt != messages.end() && positionAt < startAt && startAt < firstClock,
               "playing from the start sends Song Position 0 and Start before the first clock");
        // Four beats at 120 BPM play in those two seconds: 24 clocks to the beat.
        check (clocks >= 92 && clocks <= 97, "24 clocks to the beat (" + juce::String (clocks) + " in four beats)");
        check (! messages.empty() && messages.back().isMidiStop(), "stopping sends Stop");
        received.take();
        play (s, 4, 48000 / 256);
        messages = received.take();
        const auto resumed = std::find_if (messages.begin(), messages.end(), [] (const auto& m) { return m.isSongPositionPointer(); });
        check (resumed != messages.end() && resumed->getSongPositionPointerMidiBeat() == 32 && count (messages, [] (const auto& m) { return m.isMidiContinue(); }) == 1
                   && count (messages, [] (const auto& m) { return m.isMidiStart(); }) == 0,
               "playing from beat 8 sends Song Position 32 (sixteenths) and Continue");
        s.setTempo (90);
        received.take();
        play (s, 0, 48000 * 2 / 256);
        messages = received.take();
        const auto slower = count (messages, [] (const auto& m) { return m.isMidiClock(); });
        check (slower >= 69 && slower <= 74, "at 90 BPM, three beats play in two seconds (" + juce::String (slower) + " clocks)");
        s.setTempo (120);
        ok (s.setMidiOutputPort (name, true, false), "switch Sync off");
        received.take();
        play (s, 0, 48000 / 256);
        check (count (received.take(), [] (const auto& m) { return m.isMidiClock() || m.isMidiStart(); }) == 0, "and the clock stops");

        // An instrument returns the track to Master; saving keeps the port and channel.
        ok (s.setTrackInstrument (id, std::nullopt), "give the track the built-in synth");
        check (track().hasInstrument && track().output.kind == Session::OutputKind::master, "an instrument returns its output to Master");
        s.undo();
        check (! track().hasInstrument && track().output == toPort, "undo removes it and plays to the port again, in one step");
        const auto project = root.getChildFile ("Ports").getChildFile ("Ports.halftrack");
        ok (s.saveProject (project), "save");
        ok (s.newProject(), "close it");
        ok (s.loadProject (project), "reopen it");
        check (track().name == "Hardware" && ! track().hasInstrument && track().output == toPort, "the track reopens without an instrument, playing to the port on channel 3");
        received.take();
        play (s, 0, 48000 * 2 / 256);
        check (count (received.take(), [] (const auto& m) { return m.isNoteOn() && m.getChannel() == 3; }) == 3, "and its notes play there");
        {
            auto text = project.loadFileAsString();
            check (text.contains ("halftrackMidiOutChannel=\"3\""), "the project names the channel");
            const auto tampered = project.getSiblingFile ("Tampered.halftrack");
            check (tampered.replaceWithText (text.replace ("halftrackMidiOutChannel=\"3\"", "halftrackMidiOutChannel=\"17\"")), "write a project with channel 17");
            check (s.loadProject (tampered).failed() && s.snapshot().projectFile == project, "it is refused and the open project stays");
        }
        ok (s.setMidiOutputPort (name, false, false), "switch the port off");
        receiver->stop();
        std::cout << "MIDI ports passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL midi ports: " << e.what() << std::endl; return 1; }
}
}
