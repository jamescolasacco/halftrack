#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Insert and Delete Time at 120 BPM (one beat is half a second). An audio clip
// crosses the edit point, a later clip follows, a MIDI clip holds notes on both
// sides, and locators and a volume lane sit before and after the edit.
namespace halftrack
{
namespace
{
constexpr double rate = 48000, pi = 3.14159265358979323846;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS time edit: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
double tone (int n) { return .3 * std::sin (2 * pi * 220 * n / rate); }
void write (const juce::File& f, double seconds)
{
    juce::AudioBuffer<float> x (2, (int) (seconds * rate));
    for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) tone (n));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
bool near (double a, double b) { return std::abs (a - b) < 1e-6; }
struct State
{
    std::vector<Session::ClipInfo> audio, midi;
    std::vector<double> locators;
    Session::AutomationLane volume;
};
State read (Session& s)
{
    const auto snap = s.snapshot(); State st;
    st.audio = snap.tracks[0].clips; st.midi = snap.tracks[1].clips;
    std::sort (st.audio.begin(), st.audio.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    for (const auto& l : snap.locators) st.locators.push_back (l.position);
    std::sort (st.locators.begin(), st.locators.end());
    st.volume = snap.tracks[0].volumeAutomation; return st;
}
}
int runTimeEditTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("time-edit-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto longSource = root.getChildFile ("long.wav"), shortSource = root.getChildFile ("short.wav");
        write (longSource, 2); write (shortSource, 1);
        Session s (false);
        ok (s.importAudio (longSource, 0, 0), "an audio clip from 0 to 2 s"); ok (s.importAudio (shortSource, 0, 3), "a clip from 3 to 4 s");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track"); ok (s.moveTrack (s.snapshot().tracks.back().id, 1), "put it second");
        const auto keys = s.snapshot().tracks[1].id;
        ok (s.addMidiClip (keys, 0, 8), "a MIDI clip over the first four seconds");
        const auto clip = s.snapshot().tracks[1].clips[0].id;
        ok (s.addMidiNote (clip, 60, 1, 2, .8f), "a note across the edit point (beats 1 to 3)");
        ok (s.addMidiNote (clip, 62, 2.5, .5, .8f), "a note inside the deleted time (beat 2.5)");
        ok (s.addMidiNote (clip, 64, 5, 1, .8f), "a note after the edit (beat 5)");
        juce::String id; ok (s.addLocator (1.5, "Inside", id), "a locator inside the deleted time"); ok (s.addLocator (2.5, "After", id), "a locator after it");
        ok (s.setTrackAutomation (s.snapshot().tracks[0].id, Session::AutomationTarget::volume, { true, { { 2.5, -6 }, { 8, -12 } } }), "volume points at beats 2.5 and 8");
        ok (s.setTrackMute (1, true), "silence the MIDI track so renders hear only the audio clips");
        const auto before = read (s);
        check (s.insertTime (-1, 1).failed() && s.insertTime (1, 0).failed() && s.deleteTime (1, std::nan ("")).failed(), "invalid ranges are rejected");

        // Insert half a second at 1 s.
        ok (s.insertTime (1, .5), "insert half a second at 1 s");
        auto st = read (s);
        check (st.audio.size() == 3 && near (st.audio[0].start, 0) && near (st.audio[0].length, 1) && near (st.audio[1].start, 1.5) && near (st.audio[1].length, 1)
               && near (st.audio[1].sourceOffset, 1) && near (st.audio[2].start, 3.5), "the crossing clip splits around the gap and later clips move");
        check (st.midi.size() == 1 && near (st.midi[0].length, 4.5) && st.midi[0].notes.size() == 3, "the MIDI clip grows by the gap");
        std::vector<double> starts; for (const auto& n : st.midi[0].notes) starts.push_back (n.startBeat); std::sort (starts.begin(), starts.end());
        check (near (starts[0], 1) && near (starts[1], 3.5) && near (starts[2], 6), "notes from the insertion point on move one beat later");
        check (st.locators.size() == 2 && near (st.locators[0], 2) && near (st.locators[1], 3), "locators after the point move");
        check (st.volume.points.size() == 2 && near (st.volume.points[0].beat, 3.5) && near (st.volume.points[1].beat, 9), "automation after the point moves");
        const auto render = root.getChildFile ("render.wav");
        ok (s.exportWav (render, rate, 32), "render the inserted gap");
        {
            juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (render));
            juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
            // The volume lane holds its first point's -6 dB until beat 3.5 (1.75 s).
            const auto gain = std::pow (10.0, -6 / 20.0);
            double resumed = 0; for (int n = 200; n < 11000; ++n) resumed = std::max (resumed, std::abs (x.getSample (0, 72000 + n) - gain * tone (48000 + n)));
            check (x.getMagnitude (0, 49000, 22000) < 1e-6 && resumed < 1e-4, "the gap is silent and the clip resumes where it was split");
        }
        s.undo();
        st = read (s);
        check (st.audio.size() == before.audio.size() && near (st.audio[1].start, 3) && st.locators == before.locators && st.volume == before.volume
               && near (st.midi[0].length, 4), "one undo restores everything");

        // Inserted inside the volume ramp (beats 2.5 to 8), the lane holds its value there across the
        // gap, so it plays as before up to the insertion point and from the gap's end.
        ok (s.insertTime (2, .5), "insert half a second at 2 s, inside the ramp");
        st = read (s);
        const auto& lane = st.volume.points;
        const auto close = [] (float a, float b) { return std::abs (a - b) < 1e-3f; };
        check (lane.size() == 4 && near (lane[0].beat, 2.5) && close (lane[0].value, -6) && near (lane[1].beat, 4) && near (lane[2].beat, 5)
                   && close (lane[1].value, lane[2].value) && lane[1].value < -6.5f && lane[1].value > -11.5f && near (lane[3].beat, 9) && close (lane[3].value, -12),
               "the ramp's value at the insertion point is held across the inserted beat (" + [&] { juce::String p; for (const auto& q : lane) p << juce::String (q.beat, 3) << ":" << juce::String (q.value, 3) << " "; return p.trim(); }() + ")");
        s.undo(); st = read (s);
        check (st.volume == before.volume, "one undo restores the ramp");

        // Delete the second between 1 s and 2 s.
        ok (s.deleteTime (1, 1), "delete from 1 s to 2 s");
        st = read (s);
        check (st.audio.size() == 2 && near (st.audio[0].start, 0) && near (st.audio[0].length, 1) && near (st.audio[1].start, 2), "audio inside the range is removed and later clips move earlier");
        check (st.midi.size() == 1 && near (st.midi[0].length, 3) && st.midi[0].notes.size() == 2, "the MIDI clip shortens and loses the note inside the range");
        Session::MidiNoteInfo first, last; for (const auto& n : st.midi[0].notes) (n.pitch == 60 ? first : last) = n;
        check (first.pitch == 60 && near (first.startBeat, 1) && near (first.lengthBeats, 1) && last.pitch == 64 && near (last.startBeat, 3),
               "a note crossing the start ends there and a later note moves two beats earlier");
        check (st.locators.size() == 1 && near (st.locators[0], 1.5), "a locator inside the range goes and a later one moves");
        check (st.volume.points.size() == 1 && near (st.volume.points[0].beat, 6), "automation inside the range goes and later points move");
        s.undo(); st = read (s);
        check (st.audio.size() == 2 && near (st.audio[0].length, 2) && st.locators == before.locators && st.volume == before.volume && st.midi[0].notes.size() == 3,
               "one undo restores the deleted time");

        // Cut Time deletes as Delete Time does, keeping the time's material on the range clipboard.
        check (s.pasteTime (s.snapshot().tracks[0].id, 3).failed(), "Paste Time needs copied time");
        ok (s.cutTime (1, 1), "cut the second from 1 s to 2 s");
        st = read (s);
        check (st.audio.size() == 2 && near (st.audio[1].start, 2) && near (st.midi[0].length, 3) && st.midi[0].notes.size() == 2 && st.locators.size() == 1
                   && st.volume.points.size() == 1 && near (st.volume.points[0].beat, 6),
               "the time goes from every track, its locator and automation, as Delete Time does");
        check (s.hasRangeClipboard() && near (s.rangeClipboardLength(), 1) && s.rangeClipboardTracks() == (int) s.snapshot().tracks.size(), "and the clipboard holds the second, from every track (" + juce::String (s.rangeClipboardLength(), 3) + " s, " + juce::String (s.rangeClipboardTracks()) + " tracks)");
        s.undo(); st = read (s);
        check (st.audio.size() == 2 && near (st.audio[0].length, 2) && st.locators == before.locators && st.volume == before.volume && st.midi[0].notes.size() == 3,
               "one undo restores the cut time");

        // Paste Time inserts the clipboard's length, as Insert Silence does, and pastes it there.
        const auto audioTrack = s.snapshot().tracks[0].id;
        check (s.pasteTime (keys, 3).failed(), "pasting audio and MIDI from the MIDI track onward is refused");
        st = read (s);
        check (st.audio.size() == 2 && near (st.audio[1].start, 3) && st.midi.size() == 1 && near (st.midi[0].length, 4), "and the refusal leaves the time it inserted undone");
        ok (s.pasteTime (audioTrack, 3), "paste the second at 3 s");
        st = read (s);
        check (st.audio.size() == 3 && near (st.audio[1].start, 3) && near (st.audio[1].length, 1) && near (st.audio[1].sourceOffset, 1) && near (st.audio[2].start, 4),
               "the copied audio fills the new second, and the later clip moves past it");
        int notes = 0; double end = 0;
        for (const auto& c : st.midi) { notes += (int) c.notes.size(); end = std::max (end, c.start + c.length); }
        check (notes == 5 && near (end, 5), "the copied notes fill it on the MIDI track, which grows by the second");
        check (st.locators == before.locators && ! st.volume.points.empty() && near (st.volume.points.back().beat, 10), "earlier locators stay and later automation moves");
        s.undo(); st = read (s);
        check (st.audio.size() == 2 && near (st.audio[1].start, 3) && st.midi.size() == 1 && near (st.midi[0].length, 4) && st.volume == before.volume,
               "one undo removes the pasted time");

        // Duplicate Time inserts a copy of the time just after it, and keeps the clipboard.
        ok (s.copyRange ({ audioTrack }, 0, .25), "copy a quarter second");
        ok (s.duplicateTime (1, 2), "duplicate the second from 1 s to 2 s");
        st = read (s);
        check (st.audio.size() == 3 && near (st.audio[0].length, 2) && near (st.audio[1].start, 2) && near (st.audio[1].length, 1) && near (st.audio[1].sourceOffset, 1)
                   && near (st.audio[2].start, 4), "the second's audio repeats after it, and the later clip moves a second");
        notes = 0; end = 0;
        for (const auto& c : st.midi) { notes += (int) c.notes.size(); end = std::max (end, c.start + c.length); }
        check (notes == 5 && near (end, 5), "so do its notes");
        check (st.locators.size() == 2 && near (st.locators[0], 1.5) && near (st.locators[1], 3.5), "a locator after the copy moves");
        check (near (s.rangeClipboardLength(), .25) && s.rangeClipboardTracks() == 1, "the clipboard is as it was");
        ok (s.exportWav (render, rate, 32), "render the duplicate");
        {
            juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (render));
            juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
            // The copy sounds as the second it copies, automation and all: clear of the clip edges' fades,
            // and of the volume's jump back at the copy's start, which the engine ramps over 15 ms.
            double worst = 0; for (int n = 1200; n < 47600; ++n) worst = std::max (worst, (double) std::abs (x.getSample (0, 96000 + n) - x.getSample (0, 48000 + n)));
            check (x.getMagnitude (0, 48000, 48000) > .05 && worst < 1e-6, "the duplicated second sounds as the second it copies (" + juce::String (worst, 9) + ")");
        }
        s.undo(); st = read (s);
        check (st.audio.size() == 2 && near (st.audio[1].start, 3) && st.locators == before.locators && st.volume == before.volume && st.midi.size() == 1,
               "one undo removes the duplicate");

        // Time signature markers inside the time come with it, as the reference's time commands affect them
        // (its manual, section 6.11), and after pasted time the meter it was pasted into goes on.
        {
            Session m (false);
            ok (m.insertTimeSignatureChange (4, { 3, 4 }), "a 3/4 marker at beat 4");
            ok (m.insertTimeSignatureChange (10, { 4, 4 }), "and 4/4 again at beat 10");
            using Meters = std::vector<std::pair<double, int>>;
            const auto meters = [&] { Meters v; for (const auto& t : m.snapshot().timeSignatures) v.push_back ({ t.beat, t.meter.numerator }); return v; };
            const Meters original { { 0, 4 }, { 4, 3 }, { 10, 4 } };
            ok (m.duplicateTime (1, 3), "duplicate beats 2 to 6");
            check (meters() == Meters { { 0, 4 }, { 4, 3 }, { 8, 3 }, { 14, 4 } }, "the 3/4 marker inside repeats in the copy, and the 4/4 marker after it moves on four beats");
            m.undo();
            check (meters() == original, "one undo takes the copied marker away");
            ok (m.cutTime (1, 2), "cut beats 2 to 6");
            check (meters() == Meters { { 0, 4 }, { 6, 4 } }, "cutting the time takes its marker with it");
            ok (m.pasteTime (m.snapshot().tracks[0].id, 1), "paste the time back at beat 2");
            check (meters() == Meters { { 0, 4 }, { 4, 3 }, { 6, 4 }, { 10, 4 } }, "pasting it brings the marker back, and the 4/4 it went into goes on after it");
            m.undo();
            check (meters() == Meters { { 0, 4 }, { 6, 4 } }, "one undo removes the pasted time and its markers");
        }

        // A warped clip crossing the edit cannot be split, so nothing changes.
        Session warped (false);
        ok (warped.importAudio (longSource, 0, 0), "import a clip to warp");
        ok (warped.setAudioClipWarp (warped.snapshot().tracks[0].clips[0].id, { Session::WarpMode::stretch, 120 }), "warp it");
        check (warped.insertTime (1, .5).failed() && warped.deleteTime (.5, .5).failed() && warped.snapshot().tracks[0].clips.size() == 1,
               "a warped clip crossing the edit rejects the change");
        ok (warped.insertTime (0, 1), "inserting before every clip only moves it");
        check (near (warped.snapshot().tracks[0].clips[0].start, 1), "the warped clip moves whole");
        std::cout << "Time edit passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL time edit: " << e.what() << std::endl; return 1; }
}
}
