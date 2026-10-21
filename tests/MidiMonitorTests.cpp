#include "MidiMonitor.h"
#include "NativeEffects.h"
#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// The MIDI Monitor device. Its text names notes, messages and chords; in a project it passes every
// message through unchanged while it lists the messages it received and the notes held now. A
// rendered clip shows its notes at their times; a generated controller's notes, controllers, bend
// and pressure show as they are played, and the inline display shows, freezes and clears them.
namespace halftrack
{
namespace te = tracktion::engine;
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS MIDI monitor: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
Session::MidiNoteInfo note (int pitch, double start, double length, int velocity)
{ Session::MidiNoteInfo n; n.pitch = pitch; n.startBeat = start; n.lengthBeats = length; n.velocity = velocity / 127.0f; return n; }
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, .5), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return false;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) if (a.getSample (c, n) != b.getSample (c, n)) return false;
    return true;
}
std::vector<Session::MidiMonitorMessage> notesOf (const Session::MidiMonitorReading& reading)
{
    std::vector<Session::MidiMonitorMessage> notes;
    for (const auto& m : reading.messages) if ((m.status & 0xe0) == 0x80) notes.push_back (m);
    return notes;
}
juce::Component* findNamed (juce::Component& parent, const juce::String& name)
{
    if (parent.getName() == name) return &parent;
    for (auto* child : parent.getChildren()) if (auto* found = findNamed (*child, name)) return found;
    return nullptr;
}
// A source-free virtual MIDI controller, as in the MIDI input suite.
struct Controller
{
    Session& session;
    te::DeviceManager& devices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (session.audioDeviceManager()).engine.getDeviceManager();
    juce::String name = "Halftrack monitor MIDI " + juce::Uuid().toString();
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
};
}

int runMidiMonitorTests (const juce::File& artifacts)
{
    try
    {
        // Text.
        using midimonitor::chordName;
        check (midimonitor::noteName (60) == "C3" && midimonitor::noteName (0) == "C-2" && midimonitor::noteName (127) == "G8" && midimonitor::noteName (70) == "A#3",
               "notes are named with C3 as note 60");
        const auto text = [] (int status, int a, int b) { return midimonitor::describe ({ 0, status, a, b }); };
        check (text (0x90, 60, 100) == "Note On C3 100 ch 1" && text (0x80, 60, 0) == "Note Off C3 ch 1" && text (0x93, 62, 0) == "Note Off D3 ch 4"
               && text (0xb0, 1, 64) == "CC 1 64 ch 1" && text (0xe0, 0, 64) == "Pitch Bend 0 ch 1" && text (0xe1, 0x7f, 0x7f) == "Pitch Bend +8191 ch 2"
               && text (0xe0, 0, 0) == "Pitch Bend -8192 ch 1" && text (0xd0, 40, 0) == "Pressure 40 ch 1" && text (0xa0, 64, 30) == "Poly Pressure E3 30 ch 1"
               && text (0xc0, 4, 0) == "Program 5 ch 1",
               "each kind of message is described with its values and channel, a zero-velocity note on as a release");
        check (chordName ({ 60, 64, 67 }) == "C" && chordName ({ 57, 60, 64 }) == "Am" && chordName ({ 64, 67, 72 }) == "C/E" && chordName ({ 55, 59, 62, 65 }) == "G7"
               && chordName ({ 60, 63, 66, 69 }) == "Cdim7" && chordName ({ 62, 65, 69, 72 }) == "Dm7" && chordName ({ 60, 64, 67, 71, 74 }) == "Cmaj9"
               && chordName ({ 48, 55, 60 }) == "C5" && chordName ({ 61, 66, 68 }) == "C#sus4" && chordName ({ 60, 72, 84 }) == "C"
               && chordName ({ 60, 61 }).isEmpty() && chordName ({}).isEmpty(),
               "held notes name their chord from the lowest note, with a slash for an inversion");

        // A rendered clip.
        const auto root = artifacts.getChildFile ("midi-monitor-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        {
            Session s (false); ok (s.addMidiTrack ("Keys"), "add a MIDI track");
            const int track = (int) s.snapshot().tracks.size() - 1; const auto trackId = s.snapshot().tracks.back().id;
            ok (s.addMidiClip (trackId, 0, 4), "add a MIDI clip");
            juce::StringArray ids; ok (s.addMidiNotes (s.snapshot().tracks.back().clips.back().id, { note (60, 0, 1, 100), note (64, .5, 1, 90) }, ids), "write C3 and E3");
            const auto without = render (s, root.getChildFile ("without.wav"));
            ok (s.addNativeEffect (track, Session::EffectKind::midiMonitor), "add a MIDI Monitor");
            const auto id = s.snapshot().tracks.back().plugins.front().id;
            const auto fresh = s.midiMonitor (id);
            check (fresh && fresh->messages.empty() && fresh->total == 0 && fresh->held.empty() && effectParameters (Session::EffectKind::midiMonitor).empty()
                   && effectIsMidi (Session::EffectKind::midiMonitor) && std::string (effectCategory (Session::EffectKind::midiMonitor)) == "MIDI Effects",
                   "a new monitor, a MIDI effect without controls, has seen nothing");
            check (! s.midiMonitor ("missing") && ! s.midiMonitor (trackId), "unknown ids read nothing");
            check (identical (render (s, root.getChildFile ("with.wav")), without), "the monitor passes the clip's notes through unchanged");
            const auto reading = s.midiMonitor (id);
            const auto notes = reading ? notesOf (*reading) : std::vector<Session::MidiMonitorMessage> {};
            // 120 BPM: E3 starts 0.25 s after C3; C3 ends at 0.5 s, E3 at 0.75 s.
            const auto at = [&] (size_t i) { return notes[i].seconds - notes[0].seconds; };
            check (notes.size() == 4 && notes[0].status == 0x90 && notes[0].data1 == 60 && notes[0].data2 == 100 && notes[1].status == 0x90 && notes[1].data1 == 64 && notes[1].data2 == 90
                   && (notes[2].status & 0xf0) == 0x80 && notes[2].data1 == 60 && (notes[3].status & 0xf0) == 0x80 && notes[3].data1 == 64
                   && std::abs (at (1) - .25) < 1.5 / 48000 && std::abs (at (2) - .5) < 1.5 / 48000 && std::abs (at (3) - .75) < 1.5 / 48000,
                   "the render's four note messages are listed in order at their times");
            check (reading->total >= 4 && reading->held.empty(), "nothing is held after the render");
            const auto project = root.getChildFile ("Monitor.halftrack");
            ok (s.saveProject (project), "save the monitor"); ok (s.loadProject (project), "reopen it");
            check (s.snapshot().tracks.back().plugins.size() == 1 && s.snapshot().tracks.back().plugins[0].effect.kind == Session::EffectKind::midiMonitor, "the monitor reopens");
        }

        // A controller played live.
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const int keys = (int) s.snapshot().tracks.size() - 1;
        ok (s.addNativeEffect (keys, Session::EffectKind::midiMonitor), "add a MIDI Monitor");
        const auto id = s.snapshot().tracks.back().plugins.front().id;
        Controller controller (s);
        ok (s.setTrackInput (keys, controller.name), "route the controller");
        ok (s.setTrackMonitor (keys, Session::MonitorMode::automatic), "Auto monitoring"); ok (s.setTrackArmed (keys, true), "arm the track to monitor it");
        pumpSyntheticTestDevice (s, 8);
        for (const auto [pitch, velocity] : { std::pair { 60, 100 }, { 64, 90 }, { 67, 80 } }) controller.now (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) velocity));
        controller.now (juce::MidiMessage::controllerEvent (1, 1, 64));
        controller.now (juce::MidiMessage::pitchWheel (1, 8192 + 512));
        controller.now (juce::MidiMessage::channelPressureChange (1, 40));
        pumpSyntheticTestDevice (s, 8);
        auto reading = s.midiMonitor (id);
        const auto has = [&] (int status, int a, int b)
        { return reading && std::any_of (reading->messages.begin(), reading->messages.end(), [&] (const auto& m) { return m.status == status && m.data1 == a && m.data2 == b; }); };
        check (reading && reading->held.size() == 3 && reading->held[0].pitch == 60 && reading->held[0].velocity == 100 && reading->held[1].pitch == 64 && reading->held[1].velocity == 90
               && reading->held[2].pitch == 67 && reading->held[2].velocity == 80 && reading->chord == "C", "three held keys show with their velocities and spell C");
        check (has (0xb0, 1, 64) && has (0xe0, 0, 68) && has (0xd0, 40, 0), "the controller, bend and pressure messages are listed");
        {
            auto editor = makeNativeEffectEditor (s, id, true, {});
            auto* flow = dynamic_cast<juce::TextEditor*> (findNamed (*editor, "MIDI Monitor flow"));
            auto* chord = dynamic_cast<juce::Label*> (findNamed (*editor, "MIDI Monitor chord"));
            auto* held = dynamic_cast<juce::Label*> (findNamed (*editor, "MIDI Monitor notes"));
            auto* freeze = dynamic_cast<juce::Button*> (findNamed (*editor, "MIDI Monitor freeze"));
            auto* clear = dynamic_cast<juce::Button*> (findNamed (*editor, "MIDI Monitor clear"));
            check (flow && chord && held && freeze && clear, "the inline monitor has its flow, chord, notes, Freeze and Clear");
            check (chord->getText() == "C" && held->getText() == "C3 100  E3 90  G3 80" && flow->getText().contains ("Note On G3 80 ch 1")
                   && flow->getText().contains ("CC 1 64 ch 1") && flow->getText().contains ("Pitch Bend +512 ch 1"), "it shows the chord, held notes and the flow of messages");
            if (const auto* configured = std::getenv ("HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY"))
                if (const juce::File directory (juce::String::fromUTF8 (configured)); directory.createDirectory().wasOk())
                {
                    juce::PNGImageFormat png; const auto output = directory.getChildFile ("midi-monitor-inline.png");
                    if (auto stream = output.createOutputStream(); stream && stream->setPosition (0) && stream->truncate().wasOk()
                        && png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), *stream))
                        std::cout << "UI SNAPSHOT " << output.getFullPathName() << std::endl;
                }
            const auto refresh = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (120); };
            freeze->setToggleState (true, juce::sendNotificationSync);
            const auto frozen = flow->getText();
            controller.now (juce::MidiMessage::controllerEvent (1, 7, 99));
            pumpSyntheticTestDevice (s, 8); refresh();
            check (flow->getText() == frozen && s.midiMonitor (id)->messages.back().data1 == 7, "Freeze holds the flow while messages still arrive");
            freeze->setToggleState (false, juce::sendNotificationSync); refresh();
            check (flow->getText().contains ("CC 7 99 ch 1"), "unfreezing shows what arrived meanwhile");
            clear->triggerClick(); refresh();
            check (flow->getText().isEmpty(), "Clear empties the flow");
            for (const int pitch : { 60, 64, 67 }) controller.now (juce::MidiMessage::noteOff (1, pitch));
            pumpSyntheticTestDevice (s, 8); refresh();
            check (s.midiMonitor (id)->held.empty() && held->getText().isEmpty() && chord->getText().isEmpty() && flow->getText().contains ("Note Off C3 ch 1")
                   && ! flow->getText().contains ("Note On"), "released keys leave the display, and only messages after Clear are listed");
        }
        ok (s.setTrackArmed (keys, false), "disarm the track");
        std::cout << "MIDI monitor passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL MIDI monitor: " << e.what() << std::endl; return 1; }
}
}
