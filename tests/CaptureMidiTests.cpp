#include "CaptureTempo.h"
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
namespace ct = capturetempo;
// A performance as the research model's checks generate it: a pattern of (beat in the bar, pitch) for some bars, from
// 1 s, each note held for 0.4 beat, with an optional deterministic wobble.
std::vector<ct::Note> performance (double bpm, const std::vector<std::pair<double, int>>& pattern, int bars, double wobble = 0)
{
    const double beat = 60000.0 / bpm;
    std::vector<ct::Note> notes;
    int i = 0;
    for (int bar = 0; bar < bars; ++bar)
        for (const auto& [offset, pitch] : pattern)
            notes.push_back ({ pitch, 100, 1000.0 + (bar * 4 + offset) * beat + wobble * std::sin (i++ * 1.7), beat * 0.8 * 0.5 });
    std::stable_sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.ms < b.ms; });
    return notes;
}
bool near (double a, double b) { return std::abs (a - b) < 1e-9; }
// Capture MIDI's analysis against the research model's executable copy (its outputs for the same generated notes,
// recorded 2026-10-26): the tempo, the loop, and where the notes land.
void checkAnalysis()
{
    const std::vector<std::pair<double, int>> quarters { { 0, 60 }, { 1, 64 }, { 2, 67 }, { 3, 72 } };
    std::vector<std::pair<double, int>> eighths;
    for (int i = 0; i < 8; ++i) eighths.push_back ({ i * 0.5, 60 + (i % 4) * 4 });
    const std::vector<std::pair<double, int>> melody { { 0, 60 }, { 0.5, 62 }, { 1, 64 }, { 2, 67 }, { 2.5, 65 }, { 3, 64 } };
    const auto expect = [] (const juce::String& name, const ct::Capture& c, double tempo, double loopStart, double loopEnd, size_t count)
    {
        check (c.tempo == tempo && c.loopStart == loopStart && c.loopEnd == loopEnd && c.notes.size() == count,
               name + ": " + juce::String (c.tempo, 1) + " BPM, looping " + juce::String (c.loopStart) + " to " + juce::String (c.loopEnd) + " beats with "
                   + juce::String ((int) c.notes.size()) + " notes");
    };
    const auto beatsAre = [] (const ct::Capture& c, std::initializer_list<double> beats)
    {
        size_t i = 0;
        for (const double b : beats) if (i >= c.notes.size() || ! near (c.notes[i++].beat, b)) return false;
        return true;
    };
    auto c = ct::capture (performance (100, quarters, 2), 4, std::nullopt);
    expect ("quarter notes at 100 BPM", c, 100, 0, 8, 8);
    check (beatsAre (c, { 0, 1, 2, 3, 4, 5, 6, 7 }) && near (c.notes[0].durationBeats, .4), "they land on the beats, 0.4 beat long");
    expect ("four bars of quarters at 90", ct::capture (performance (90, quarters, 4), 4, std::nullopt), 90, 0, 16, 16);
    c = ct::capture (performance (128, eighths, 2), 4, std::nullopt);
    expect ("eighths at 128", c, 128, 0, 8, 16);
    check (beatsAre (c, { 0, .5, 1, 1.5, 2, 2.5 }) && near (c.notes.back().beat, 7.5), "on the half beats");
    c = ct::capture (performance (110, melody, 4, 10), 4, std::nullopt);
    expect ("a melody at 110 wobbling by 10 ms", c, 110, 0, 16, 24);
    check (beatsAre (c, { 0, 0.5198481206293706, 0.9985181485181485, 1.989408248001998, 2.517133127289377, 3.024340243090243 })
               && near (c.notes.back().beat, 15.03646925990676) && near (c.notes[0].durationBeats, 0.40128725441225443),
           "not quantised: each note where the fitted tempo map puts it");
    c = ct::capture (performance (70, quarters, 2), 4, std::nullopt);
    expect ("quarters at 70 BPM", c, 140, 0, 16, 8);
    check (beatsAre (c, { 0, 2, 4, 6 }) && near (c.notes[0].durationBeats, .8), "taken as half notes at 140, in the free range of 78 to 164");
    c = ct::capture ({ { 60, 100, 1000, 300 } }, 4, std::nullopt);
    expect ("one short note", c, 120, 0, 4, 1);
    check (near (c.notes[0].durationBeats, .6), "at 120 BPM");
    c = ct::capture ({ { 60, 100, 1000, 3100 } }, 4, std::nullopt);
    expect ("one 3.1 s note", c, 77.4, 0, 4, 1);
    check (near (c.notes[0].durationBeats, 4), "as a bar, the tempo to a tenth");
    std::vector<ct::Note> pickup { { 67, 90, 1000, 250 }, { 69, 90, 1300, 250 } };
    for (int i = 0; i < 8; ++i) pickup.push_back ({ 60 + 2 * i, 100, 1600.0 + i * 600, 400 });
    c = ct::capture (pickup, 4, std::nullopt);
    expect ("two eighth-note pickups before quarters at 100", c, 100, 0, 8, 10);
    check (beatsAre (c, { 0, .5, 1, 2, 3, 4 }) && near (c.notes[0].durationBeats, 0.4166666666666667) && near (c.notes[2].durationBeats, 0.6666666666666666),
           "the pickup lands on the downbeat");
    std::vector<ct::Note> phrases;
    for (int i = 0; i < 8; ++i) phrases.push_back ({ 60, 100, 1000.0 + i * 500, 300 });
    for (int i = 0; i < 8; ++i) phrases.push_back ({ 64, 100, 8000.0 + i * 600, 300 });
    c = ct::capture (phrases, 4, std::nullopt);
    expect ("a phrase, a 3 s pause and another at 100", c, 100, 12, 20, 16);
    check (beatsAre (c, { 0, 0.8571428571428571 }) && near (c.notes.back().beat, 19), "the loop is the last phrase; the first stays before it");
    std::vector<ct::Note> twelveBars;
    for (int i = 0; i < 48; ++i) twelveBars.push_back ({ 60 + (i % 4) * 3, 100, 1000.0 + i * 500, 300 });
    c = ct::capture (twelveBars, 4, std::nullopt);
    expect ("twelve bars at 120", c, 120, 16, 48, 48);
    check (near (c.notes.back().beat, 47), "loop their last eight bars");
    std::vector<ct::Note> chords;
    for (int i = 0; i < 8; ++i)
        for (const int p : { 48, 55, 60, 64 }) chords.push_back ({ p, (double) (90 + p % 7), 1000.0 + i * 500 + (p - 48) * 0.5, 350 });
    std::stable_sort (chords.begin(), chords.end(), [] (const auto& a, const auto& b) { return a.ms < b.ms; });
    c = ct::capture (chords, 4, std::nullopt);
    expect ("rolled chords at 120", c, 120, 0, 8, 32);
    check (beatsAre (c, { 0, 0.007000031218781219, 0.011999979187479188, 0.01599988553113553, 1 }), "their notes keep their spread");
    std::vector<ct::Note> rubato;
    double t = 1000;
    for (int i = 0; i < 16; ++i) { rubato.push_back ({ 60 + i % 5, 100, t, 250 }); t += 60000.0 / (100.0 + 10.0 * std::sin (i / 3.0)) / 2.0; }
    c = ct::capture (rubato, 4, std::nullopt);
    expect ("eighths swaying around 100", c, 104, 0, 8, 16);
    check (beatsAre (c, { 0, 0.5331507034632035, 1.049409704878455, 1.5515120296370297 }) && near (c.notes.back().beat, 7.641663024475524)
               && near (c.notes[0].durationBeats, 0.44429216616716616), "following the tempo map fitted to them");
    for (const double played : { 104.0, 125.0 })
    {
        std::vector<ct::Note> q;
        for (int i = 0; i < 8; ++i) q.push_back ({ 60 + (i % 4) * 4, 100, 1000.0 + i * 60000.0 / played, 300 });
        const auto keep = ct::capture (q, 4, ct::Context { 100, ct::Mode::keep }).tempo;
        const auto clips = ct::capture (q, 4, ct::Context { 100, ct::Mode::setHasClips }).tempo;
        const auto hint = ct::capture (q, 4, ct::Context { 100, ct::Mode::hint }).tempo;
        check (keep == 100 && clips == (played == 104 ? 100 : 125) && hint == played,
               "played at " + juce::String (played) + " in a 100 BPM project: kept " + juce::String (keep) + ", with other clips " + juce::String (clips)
                   + ", as a hint " + juce::String (hint));
    }
    std::vector<ct::Note> history;
    for (int i = 0; i < 4; ++i) history.push_back ({ 50, 100, 1000.0 + i * 400, 200 });
    for (int i = 0; i < 8; ++i) history.push_back ({ 60, 100, 40000.0 + i * 500, 300 });
    check (ct::trimHistory (history).size() == 8 && ct::trimHistory (history, 41000.0).size() == 2,
           "a silence of more than 20 s ends the history, and notes from the press on are left out");
    std::vector<ct::Note> generated;
    for (int i = 0; i < 8; ++i) generated.push_back ({ 50, 100, 1000.0 + i * 5000, 200, false });
    for (int i = 0; i < 8; ++i) generated.push_back ({ 60, 100, 40000.0 + i * 500, 300 });
    const auto kept = ct::trimHistory (generated);
    check (kept.size() == 14 && kept[0].ms == 11000 && ! kept[0].user, "generated notes more than 30 s before the first played one are left out");
    check (ct::beatPosition (1.0 / 12) == 0 && ct::beatPosition (5.0 / 12) == 6 && ct::beatPosition (.25) == 3 && ct::beatPosition (-.25) == -3
               && ct::engineRound (1.0 / 3) == std::trunc ((1.0 / 3 + .5 / 3843840) * 3843840) / 3843840 && ct::roundTo (-2.5, 1) == -3,
           "the grid of twelfths without 1/12, 5/12, 7/12 and 11/12, the engine's tick, and rounding halves away from zero");
}
}

int runCaptureMidiTests (const juce::File&)
{
    try
    {
        checkAnalysis();
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

        // Stopped: the reference's analysis finds the tempo played and loops the phrase from the insert marker.
        ok (s.setTrackArmed (keys, true), "arm the track");
        pumpSyntheticTestDevice (s, 8);
        s.setInsertMarker (2); // Beat 4 at 120 BPM.
        // Quarter notes a beat of `blocks` apart (100 blocks: 112.5 BPM), each held half a beat.
        const auto playBeats = [&] (int count, int blocks)
        {
            for (int i = 0; i < count; ++i)
            {
                const int pitch = 60 + (i % 4) * 4;
                controller.now (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100));
                pumpSyntheticTestDevice (s, blocks / 2);
                controller.now (juce::MidiMessage::noteOff (1, pitch));
                pumpSyntheticTestDevice (s, blocks - blocks / 2);
            }
        };
        playBeats (8, 100);
        check (s.hasCapturedMidi(), "notes played into the armed track are kept");
        ok (s.captureMidi (Session::CaptureDestination::arrangement, created), "Capture with the transport stopped");
        auto captured = clips();
        check (s.snapshot().tempo == 112.5, "with no other clips at 120 BPM, the tempo played, 112.5 BPM, becomes the project's (" + juce::String (s.snapshot().tempo) + ")");
        check (created.size() == 1 && captured.size() == 1 && std::abs (captured[0].startBeat - 4) < 1e-6 && std::abs (captured[0].lengthBeats - 8) < 1e-6
                   && captured[0].loop.on && captured[0].loop.start == 0 && std::abs (captured[0].loop.length - 8) < 1e-6 && captured[0].contentStartBeat == 0,
               "a two-bar clip loops them from the insert marker, which keeps its beat");
        auto notes = captured[0].notes;
        std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
        bool onBeats = notes.size() == 8;
        for (size_t i = 0; onBeats && i < notes.size(); ++i) onBeats = std::abs (notes[i].startBeat - (double) i) < .01 && std::abs (notes[i].lengthBeats - .5) < .01;
        check (onBeats && notes[0].pitch == 60 && std::abs (notes[0].velocity - 100.0f / 127) < .01f, "the notes land on the beats, half a beat long, in the order played");
        check (! s.hasCapturedMidi(), "capturing empties the kept notes");
        s.undo();
        check (clips().empty() && s.snapshot().tempo == 120, "one undo removes the clip and restores the tempo");
        // With another track holding a clip the project's tempo is weighed against what was played: 112.5 BPM is
        // within 8.5% of 120, which stays, the notes still on the beats; 90 BPM is not, and becomes the project's tempo
        // in an undo step of its own.
        ok (s.addMidiTrack ("Other"), "add another MIDI track");
        ok (s.addMidiClip (s.snapshot().tracks.back().id, 0, 4), "with a clip");
        playBeats (8, 100);
        ok (s.captureMidi (Session::CaptureDestination::arrangement, created), "Capture beside the other clip");
        captured = clips();
        check (s.snapshot().tempo == 120 && captured.size() == 1 && captured[0].notes.size() == 8 && std::abs (captured[0].lengthBeats - 8) < 1e-6,
               "the project keeps 120 BPM (" + juce::String (s.snapshot().tempo) + "), the clip two bars of beats");
        s.undo();
        check (clips().empty(), "one undo removes it");
        playBeats (8, 125);
        ok (s.captureMidi (Session::CaptureDestination::arrangement, created), "Capture quarter notes at 90 BPM beside it");
        check (s.snapshot().tempo == 90 && clips().size() == 1, "the tempo found, 90 BPM, wins (" + juce::String (s.snapshot().tempo) + ")");
        s.undo();
        check (s.snapshot().tempo == 120 && clips().size() == 1, "undo first restores the tempo");
        s.undo();
        check (clips().empty(), "then removes the clip");
        ok (s.removeTrack ((int) s.snapshot().tracks.size() - 1), "remove the other track");

        // Running: at the beats they were played, in the bar they fall in.
        s.seek (0); ok (s.play(), "play");
        pumpSyntheticTestDevice (s, (int) std::lround (2.5 * blocksPerSecond));
        for (int i = 0; i < 400 && s.snapshot().position < 2.75; ++i) pumpSyntheticTestDevice (s, 1); // Beat 5.5, however late playback started.
        const auto playedBeat = s.snapshot().position * 2;
        controller.play (67, .25);
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        ok (s.captureMidi (Session::CaptureDestination::arrangement, created), "Capture while playing");
        s.stop();
        captured = clips();
        check (captured.size() == 1 && std::abs (captured[0].startBeat - 4) < 1e-6 && std::abs (captured[0].lengthBeats - 4) < 1e-6
               && captured[0].notes.size() == 1 && captured[0].notes[0].pitch == 67 && std::abs (captured[0].notes[0].startBeat - (playedBeat - 4)) < .15,
               "the note keeps its beat, in the second bar");
        s.undo();

        // Session View: the highlighted scene's slot, and the clip plays; In monitoring without arming also keeps notes.
        ok (s.setTrackArmed (keys, false), "disarm"); ok (s.setTrackMonitor (keys, Session::MonitorMode::on), "monitor In");
        pumpSyntheticTestDevice (s, 8);
        controller.play (72, .25);
        int scene = -1;
        ok (s.captureMidi (Session::CaptureDestination::session, created, 0, &scene), "Capture into the Session");
        auto slot = s.snapshot().tracks[(size_t) keys].slots[0];
        check (scene == 0 && slot.hasClip && slot.clip.notes.size() == 1 && slot.clip.notes[0].pitch == 72 && std::abs (slot.clip.lengthBeats - 4) < 1e-6 && clips().empty(),
               "an In-monitored track's notes go into the highlighted scene's slot, a bar long");
        for (int i = 0; i < 400 && ! s.snapshot().tracks[(size_t) keys].slots[0].playing; ++i) pumpSyntheticTestDevice (s, 1);
        check (s.snapshot().tracks[(size_t) keys].slots[0].playing, "and it plays, as a click on its launch button would start it");
        s.stop(); pumpSyntheticTestDevice (s, 8);
        controller.play (74, .25);
        ok (s.captureMidi (Session::CaptureDestination::session, created, 0, &scene), "Capture again from the same scene");
        slot = s.snapshot().tracks[(size_t) keys].slots[1];
        check (scene == 1 && slot.hasClip && slot.clip.notes.size() == 1 && slot.clip.notes[0].pitch == 74,
               "its slot taken, the clip goes into the scene after the track's lowest clip");
        s.stop(); pumpSyntheticTestDevice (s, 8);
        ok (s.setTrackMonitor (keys, Session::MonitorMode::off), "stop monitoring");

        // The notes held now, as step recording takes them.
        pumpSyntheticTestDevice (s, 8);
        controller.now (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90)); pumpSyntheticTestDevice (s, 4);
        check (s.heldInputNotes (keysId).empty(), "an unarmed, unmonitored track has no held notes");
        controller.now (juce::MidiMessage::noteOff (1, 64)); pumpSyntheticTestDevice (s, 4);
        ok (s.setTrackArmed (keys, true), "arm again");
        pumpSyntheticTestDevice (s, 8);
        controller.now (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 90));
        controller.now (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
        pumpSyntheticTestDevice (s, 4);
        auto held = s.heldInputNotes (keysId);
        check (held.size() == 2 && held[0].pitch == 60 && held[0].velocity == 100 && held[1].pitch == 67 && held[1].velocity == 90 && held[0].channel == 1,
               "an armed track's held notes, in pitch order with their velocities");
        controller.now (juce::MidiMessage::noteOff (1, 60)); pumpSyntheticTestDevice (s, 4);
        held = s.heldInputNotes (keysId);
        check (held.size() == 1 && held[0].pitch == 67, "a released note is no longer held");
        controller.now (juce::MidiMessage::noteOff (1, 67)); pumpSyntheticTestDevice (s, 4);
        check (s.heldInputNotes (keysId).empty() && s.heldInputNotes ("missing").empty(), "nothing is held once all are released, or on a missing track");
        controller.now (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 90)); pumpSyntheticTestDevice (s, 4);
        ok (s.setTrackArmed (keys, false), "disarm while a key is held"); pumpSyntheticTestDevice (s, 8);
        controller.now (juce::MidiMessage::noteOff (1, 62)); pumpSyntheticTestDevice (s, 4);
        ok (s.setTrackArmed (keys, true), "arm again"); pumpSyntheticTestDevice (s, 8);
        check (s.heldInputNotes (keysId).empty(), "a key released while the track was disarmed is not still held");
        ok (s.setTrackArmed (keys, false), "disarm");

        // A MIDI device appearing while playing makes the engine rebuild its devices; playback
        // carries on from where it was rather than stopping.
        s.seek (0); ok (s.play(), "play");
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        {
            const auto before = s.snapshot().position;
            Controller another (s);
            pumpSyntheticTestDevice (s, blocksPerSecond / 2);
            check (s.snapshot().playing && s.snapshot().position > before + .3,
                   "a MIDI device appearing while playing leaves playback running (" + juce::String (before, 3) + " s to " + juce::String (s.snapshot().position, 3) + " s, "
                       + (s.snapshot().playing ? "playing" : "stopped") + ")");
        }
        std::cout << "Capture MIDI passed" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL capture MIDI: " << e.what() << std::endl; return 1; }
}
}
