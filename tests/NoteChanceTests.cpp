#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Note chance and velocity range, rendered offline. A looping one-beat Session clip holds one
// note; each pass of the loop is one window of output. Chance decides per pass whether the note
// sounds, and velocity range raises its level by a random amount per pass. The 64 passes repeat
// exactly, reopening gives the same outcome, and both settings are undoable and saved.
namespace halftrack
{
namespace te = tracktion::engine;
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS note chance: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int passSamples = 24000; // One beat at 120 BPM and 48 kHz.
}

int runNoteChanceTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("note-chance-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false);
        auto& engine = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (s.audioDeviceManager()).engine;
        te::HostedAudioDeviceInterface::Parameters p; p.sampleRate = 48000; p.blockSize = 128; p.inputChannels = 0; p.outputChannels = 2; p.useMidiDevices = false;
        te::test_utilities::EnginePlayer player (engine, p);
        const auto dispatch = [&] { for (auto* edit : engine.getActiveEdits().getEdits()) edit->dispatchPendingUpdatesSynchronously(); };
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const int keys = (int) s.snapshot().tracks.size() - 1;
        auto track = s.snapshot().tracks.back();
        auto synth = track.synth; synth.attackSeconds = synth.decaySeconds = 0; synth.sustain = 1; synth.releaseSeconds = .01f;
        ok (s.setSynthSettings (track.id, synth), "a synth with instant attack and a short release");
        ok (s.addMidiClipToSlot (track.id, 0, 1), "a one-beat Session clip");
        const auto clip = [&] { return s.snapshot().tracks[(size_t) keys].slots[0].clip; };
        juce::StringArray added;
        ok (s.addMidiNotes (clip().id, { { "", 60, 0, .5, 64.0f / 127 } }, added), "one note on the first half beat");
        const auto noteId = added[0];
        ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "launch at once");
        // Each pass's peak level, from a fresh launch.
        const auto passes = [&] (int count)
        {
            s.stop(); dispatch(); player.process (4096);
            ok (s.launchScene (0), "launch the clip"); dispatch();
            std::vector<float> peaks;
            for (int i = 0; i < count; ++i)
            {
                const auto out = player.process (passSamples);
                peaks.push_back (out.getMagnitude (0, 0, out.getNumSamples()));
            }
            s.stop(); dispatch();
            return peaks;
        };
        const auto played = [] (const std::vector<float>& peaks) { int n = 0; for (auto peak : peaks) n += peak > .01f; return n; };

        auto base = passes (8);
        check (played (base) == 8 && clip().notes[0].chance == 1 && clip().notes[0].velocityRange == 0, "by default the note sounds on every pass");
        const auto level = base[3];

        check (s.setMidiNotesChance (clip().id, { noteId }, 1.5f).failed() && s.setMidiNotesChance (clip().id, { "missing" }, .5f).failed()
               && s.setMidiNotesVelocityRange (clip().id, { noteId }, 128).failed(), "chances outside 0-100%, ranges outside +-127 and unknown notes are refused");
        ok (s.setMidiNotesChance (clip().id, { noteId }, .5f), "a 50% chance");
        check (clip().notes[0].chance == .5f, "the note reports it");
        const auto half = passes (128);
        const int first = played ({ half.begin(), half.begin() + 64 });
        check (first >= 16 && first <= 48, "the note sounds on about half of 64 passes (" + juce::String (first) + ")");
        bool repeats = true;
        for (int i = 0; i < 64; ++i) repeats = repeats && ((half[(size_t) i] > .01f) == (half[(size_t) i + 64] > .01f));
        check (repeats, "and the 64 passes repeat exactly");
        bool full = true;
        for (auto peak : half) full = full && (peak < .01f || std::abs (peak - level) < level * .02f);
        check (full, "a pass plays the note at its own level or not at all");
        s.undo();
        check (clip().notes[0].chance == 1 && played (passes (8)) == 8, "one undo restores the certain note");
        s.redo();
        ok (s.setMidiNotesChance (clip().id, { noteId }, 0), "a 0% chance");
        check (played (passes (16)) == 0, "the note never sounds");

        // Velocity range: from velocity 64 up to 127.
        ok (s.setMidiNotesChance (clip().id, { noteId }, 1), "back to a certain note");
        ok (s.setMidiNotesVelocityRange (clip().id, { noteId }, 63), "a velocity range of +63");
        const auto ranged = passes (32);
        float low = 1e9f, high = 0;
        for (auto peak : ranged) { low = std::min (low, peak); high = std::max (high, peak); }
        check (played (ranged) == 32 && low >= level * .98f && high <= level * 127 / 64 * 1.02f && high > level * 1.2f,
               "each pass plays louder by up to the range (" + juce::String (low / level, 3) + " to " + juce::String (high / level, 3) + " times)");

        // Reopening: the same note identity gives the same passes.
        ok (s.setMidiNotesChance (clip().id, { noteId }, .5f), "a 50% chance with the range");
        const auto before = passes (64);
        const auto project = root.getChildFile ("Chance").getChildFile ("Chance.halftrack");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check (clip().notes[0].chance == .5f && clip().notes[0].velocityRange == 63, "the settings reopen");
        const auto after = passes (64);
        bool same = true;
        for (int i = 0; i < 64; ++i) same = same && ((before[(size_t) i] > .01f) == (after[(size_t) i] > .01f)) && std::abs (before[(size_t) i] - after[(size_t) i]) < 1e-3f;
        check (same, "and play the same passes at the same levels");
        {
            auto xml = juce::XmlDocument::parse (project);
            check (xml != nullptr, "read the saved project");
            juce::XmlElement* found = nullptr;
            std::function<void (juce::XmlElement&)> find = [&] (juce::XmlElement& e) { if (e.hasTagName ("NOTE")) found = &e; for (auto* c : e.getChildIterator()) find (*c); };
            find (*xml);
            check (found != nullptr, "find the saved note");
            found->setAttribute ("halftrackChance", 2);
            const auto bad = root.getChildFile ("bad.halftrack"); check (xml->writeTo (bad), "write a 200% chance");
            check (s.loadProject (bad).failed() && clip().notes[0].chance == .5f, "the invalid project is refused, keeping the open one");
        }
        std::cout << "Note chance passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL note chance: " << e.what() << std::endl; return 1; }
}
}
