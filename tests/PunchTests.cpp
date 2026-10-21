#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Punch-In and Punch-Out. The synthetic device's generated input is recorded from 0 to about
// 3 s with the loop brace at 1-2 s and the loop off: each take must keep only the punched part,
// at its original timing. A generated MIDI controller's notes and captured automation must be
// bounded by the same range.
namespace halftrack
{
namespace te = tracktion::engine;
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS punch: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
// The track's newly recorded clips: those whose id and file are not in `known`, which then holds
// them all. A pass replaces the main lane's earlier material under it, so pieces of earlier takes
// can have new ids but keep their files.
std::vector<Session::ClipInfo> fresh (Session& s, int track, std::set<juce::String>& known)
{
    std::vector<Session::ClipInfo> result;
    const auto snapshot = s.snapshot();
    for (const auto& clip : snapshot.tracks[(size_t) track].clips)
    {
        const auto file = clip.file.getFullPathName();
        const bool newFile = clip.isMidi || file.isEmpty() || known.count ("file:" + file) == 0;
        if (known.insert (clip.id).second && newFile) result.push_back (clip);
        if (file.isNotEmpty()) known.insert ("file:" + file);
    }
    return result;
}
// Records from 0 to about `seconds`.
void take (Session& s, double seconds)
{
    s.seek (0); ok (s.record(), "record");
    pumpSyntheticTestDevice (s, (int) std::lround (seconds * blocksPerSecond));
    s.stop();
}
// A source-free virtual MIDI controller, as in the MIDI input suite.
struct Controller
{
    Session& session;
    te::DeviceManager& devices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (session.audioDeviceManager()).engine.getDeviceManager();
    juce::String name = "Halftrack punch MIDI " + juce::Uuid().toString();
    std::shared_ptr<te::VirtualMidiInputDevice> midi;
    explicit Controller (Session& owner) : session (owner)
    {
        ok (devices.createVirtualMidiDevice (name), "create a virtual controller");
        devices.rescanMidiDeviceListSynchronously();
        for (auto device : devices.getMidiInDevices()) if (device->getName() == name) midi = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (device);
        check (midi != nullptr, "the virtual controller is discovered");
        midi->setMIDIInputSourceDevices ({}); midi->setChannelToUse (0); midi->setMinimumLengthMs (0); midi->setManualAdjustmentMs (0);
        midi->mergeRecordings = true; midi->replaceExistingClips = false;
    }
    ~Controller() { session.stop(); if (midi) { devices.deleteVirtualMidiDevice (*midi); devices.rescanMidiDeviceListSynchronously(); } }
    void now (const juce::MidiMessage& m)
    {
        auto message = m; message.setTimeStamp (devices.getCurrentStreamTime() + 256.0 / 48000 - midi->getAdjustSecs());
        midi->handleIncomingMidiMessage (message, midi->getMPESourceID());
    }
};
}

int runPunchTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("punch-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Punch").getChildFile ("Punch.halftrack")), "save a recording destination");
        const auto inputs = s.inputNames();
        check (! inputs.isEmpty(), "the synthetic device offers an input");
        ok (s.setTrackInput (0, inputs[0]), "route the input"); ok (s.setTrackArmed (0, true), "arm the track");
        s.setLoop (false, 1, 2);
        check (! s.snapshot().punchIn && ! s.snapshot().punchOut, "the punch switches start off");
        pumpSyntheticTestDevice (s, 8);
        std::set<juce::String> known;

        // Without punching, the take covers the whole pass; it sets the timing reference.
        take (s, 3);
        auto clips = fresh (s, 0, known);
        check (clips.size() == 1 && clips[0].start < .05 && clips[0].length > 2.8, "without punching the take covers the whole pass");
        const auto reference = clips[0];

        ok (s.setPunchIn (true), "Punch-In");
        take (s, 3);
        clips = fresh (s, 0, known);
        check (clips.size() == 1 && std::abs (clips[0].start - 1) < 1e-3 && clips[0].start + clips[0].length > 2.8
               && std::abs ((clips[0].sourceOffset - reference.sourceOffset) - (clips[0].start - reference.start)) < 1e-3,
               "Punch-In keeps the take from the loop start, at its recorded timing");
        s.undo(); check (s.snapshot().tracks[0].clips.size() == 1, "one undo removes the punched take");

        ok (s.setPunchOut (true), "Punch-Out as well");
        take (s, 3);
        clips = fresh (s, 0, known);
        check (clips.size() == 1 && std::abs (clips[0].start - 1) < 1e-3 && std::abs (clips[0].length - 1) < 1e-3, "Punch-In and Punch-Out keep exactly the loop brace");

        ok (s.setPunchIn (false), "Punch-Out alone");
        take (s, 3);
        clips = fresh (s, 0, known);
        check (clips.size() == 1 && clips[0].start < .05 && std::abs (clips[0].start + clips[0].length - 2) < 1e-3, "Punch-Out ends the take at the loop end");

        ok (s.setPunchIn (true), "both again");
        take (s, .8);
        check (fresh (s, 0, known).empty(), "a pass that stops before the loop start keeps nothing");
        s.seek (0); ok (s.record(), "record again");
        check (s.setPunchIn (false).failed() && s.snapshot().punchIn, "the punch switches cannot change while recording");
        s.stop();

        // MIDI: notes at 0.5, 1.5 and 2.5 s; only the middle one is inside the brace.
        ok (s.setTrackArmed (0, false), "disarm the audio track");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const int keys = (int) s.snapshot().tracks.size() - 1;
        {
            Controller controller (s);
            ok (s.setTrackInput (keys, controller.name), "route the controller"); ok (s.setTrackArmed (keys, true), "arm the MIDI track");
            pumpSyntheticTestDevice (s, 8);
            std::set<juce::String> midiKnown; fresh (s, keys, midiKnown);
            s.seek (0); ok (s.record(), "record MIDI");
            // The transport's first blocks, reported if the take goes missing.
            juce::String started;
            for (int i = 0; i < 6; ++i)
            {
                const auto now = s.snapshot();
                started << juce::String (now.position, 3) << (now.playing ? "p" : "") << (now.recording ? "r" : "") << " ";
                pumpSyntheticTestDevice (s, 1);
            }
            juce::String sentAt;
            for (const double at : { .5, 1.5, 2.5 })
            {
                // Until the play head reaches the note's time, however late playback started.
                for (int i = 0; i < 400 && s.snapshot().position < at; ++i) pumpSyntheticTestDevice (s, 1);
                sentAt << juce::String (s.snapshot().position, 3) << (s.snapshot().recording ? "r " : " ");
                controller.now (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
                pumpSyntheticTestDevice (s, blocksPerSecond / 10);
                controller.now (juce::MidiMessage::noteOff (1, 60));
            }
            pumpSyntheticTestDevice (s, blocksPerSecond / 2);
            s.stop();
            const auto midiClips = fresh (s, keys, midiKnown);
            check (midiClips.size() == 1 && std::abs (midiClips[0].start - 1) < 1e-3 && std::abs (midiClips[0].length - 1) < 1e-3,
                   "the MIDI take is trimmed to the brace (" + juce::String ((int) midiClips.size()) + " clips, started " + started.trim() + ", notes sent at " + sentAt.trim()
                       + (midiClips.empty() ? juce::String() : ", from " + juce::String (midiClips[0].start, 3) + " for " + juce::String (midiClips[0].length, 3)) + ")");
            check (midiClips[0].notes.size() == 1 && std::abs (midiClips[0].notes[0].startBeat - 1) < .1, "only the note played inside the brace remains, at its place");
            ok (s.setTrackArmed (keys, false), "disarm the MIDI track");
        }

        // Captured automation: moves before the brace are ignored and a pass ends at its end.
        ok (s.setAutomationArm (true), "Automation Arm");
        s.seek (0); ok (s.record(), "record automation only");
        // The engine's transport position follows the device by a timer, so each move is placed at
        // the position the transport reports when it happens; under load that drifts from `at`.
        std::vector<double> movedAt;
        for (const auto [at, gain] : { std::pair { .5, -20.0f }, { 1.5, -10.0f }, { 2.5, -5.0f } })
        {
            pumpSyntheticTestDevice (s, (int) std::lround ((at - s.snapshot().position) * blocksPerSecond));
            movedAt.push_back (s.snapshot().position);
            ok (s.setTrackGain (0, gain), "move the fader at " + juce::String (at) + " s");
        }
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        s.stop();
        check (movedAt[0] < 1 && movedAt[1] > 1 && movedAt[1] < 2 && movedAt[2] > 2, "the moves fall before, inside and after the brace");
        const auto lane = s.snapshot().tracks[0].volumeAutomation;
        check (lane.points.size() == 2 && std::abs (lane.points[0].beat - 2 * movedAt[1]) < 1e-6 && lane.points[0].value == -10
               && std::abs (lane.points[1].beat - 4) < 1e-6 && lane.points[1].value == -10, "automation records only inside the brace and ends at its end");
        std::cout << "Punch passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL punch: " << e.what() << std::endl; return 1; }
}
}
