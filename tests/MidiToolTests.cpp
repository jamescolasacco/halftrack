#include "NoteTools.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Note tools. Each tool's result is computed by hand for small inputs and compared note by
// note (pitch, start, length, velocity); the Session applies a tool as one undo step.
namespace halftrack
{
namespace
{
using Tool = Session::NoteTool;
using Note = Session::MidiNoteInfo;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS note tools: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
Note note (int pitch, double start, double length, float velocity = .8f) { return { {}, pitch, start, length, velocity, true }; }
bool near (double a, double b) { return std::abs (a - b) < 1e-9; }
// Pitch, start and length must match; velocity matches when expected is positive.
bool same (const std::vector<Note>& actual, const std::vector<Note>& expected, float velocityTolerance = 1e-6f)
{
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i)
        if (actual[i].pitch != expected[i].pitch || ! near (actual[i].startBeat, expected[i].startBeat) || ! near (actual[i].lengthBeats, expected[i].lengthBeats)
            || std::abs (actual[i].velocity - expected[i].velocity) > velocityTolerance) return false;
    return true;
}
juce::String describe (const std::vector<Note>& notes)
{ juce::String s; for (const auto& n : notes) s << n.pitch << "@" << n.startBeat << "+" << n.lengthBeats << "v" << n.velocity << " "; return s.trim(); }
Session::NoteToolSettings settings (Tool tool, std::initializer_list<std::pair<int, double>> values)
{ auto s = notetools::defaults (tool); for (const auto& [i, v] : values) s.values[(size_t) i] = v; return s; }
std::vector<Note> run (Tool tool, std::initializer_list<std::pair<int, double>> values, const std::vector<Note>& notes, double clip = 16)
{ return notetools::run (settings (tool, values), notes, clip); }
void expect (const std::vector<Note>& actual, const std::vector<Note>& expected, const juce::String& text)
{ check (same (actual, expected), text + " (" + describe (actual) + ")"); }
}

int runMidiToolTests (const juce::File&)
{
    try
    {
        const std::vector<Note> chord { note (60, 0, 1), note (64, 0, 1), note (67, 0, 1) };
        check (notetools::valid (notetools::defaults (Tool::strum)) && ! notetools::valid (settings (Tool::chop, { { 0, 1 } }))
               && ! notetools::valid (settings (Tool::span, { { 0, 1.5 } })) && ! notetools::valid (settings (Tool::strum, { { 5, 1 } })),
               "defaults are valid; out-of-range, fractional choices and unused values are not");

        expect (run (Tool::strum, { { 0, 0 }, { 1, .125 }, { 2, 0 } }, chord), { note (60, 0, 1), note (64, .0625, .9375), note (67, .125, .875) },
                "Strum delays each higher chord tone and keeps the chord's end");
        expect (run (Tool::strum, { { 0, .125 }, { 1, 0 }, { 2, 0 } }, chord), { note (67, 0, 1), note (64, .0625, .9375), note (60, .125, .875) },
                "a downward strum delays the low tones instead");
        expect (run (Tool::chop, { { 0, 4 }, { 1, .5 } }, { note (60, 0, 1) }), { note (60, 0, .125), note (60, .25, .125), note (60, .5, .125), note (60, .75, .125) },
                "Chop cuts a note into equal parts with a gap");
        const std::vector<Note> spaced { note (60, 0, .25), note (62, 1, .25), note (64, 3, .5) };
        expect (run (Tool::span, { { 0, 0 } }, spaced), { note (60, 0, 1), note (62, 1, 2), note (64, 3, .5) }, "Legato reaches the next note");
        expect (run (Tool::span, { { 0, 1 }, { 1, .5 } }, spaced), { note (60, 0, .5), note (62, 1, 1), note (64, 3, .25) }, "Tenuto fills a share of the distance to the next note");
        expect (run (Tool::span, { { 0, 2 }, { 1, .5 } }, spaced), { note (60, 0, .125), note (62, 1, .125), note (64, 3, .25) }, "Staccato shortens each note");
        const std::vector<Note> line { note (60, 0, 1, .2f), note (62, 1, .5, .5f), note (64, 2, .25, .9f) };
        expect (run (Tool::recombine, { { 0, 0 }, { 1, 0 }, { 2, 1 } }, line), { note (64, 0, 1, .2f), note (60, 1, .5, .5f), note (62, 2, .25, .9f) }, "Recombine rotates pitches by a step");
        expect (run (Tool::recombine, { { 0, 1 }, { 1, 1 } }, line), { note (60, 0, 1, .9f), note (62, 1, .5, .5f), note (64, 2, .25, .2f) }, "Recombine mirrors velocities");
        expect (run (Tool::recombine, { { 0, 2 }, { 1, 0 }, { 2, -1 } }, line), { note (60, 0, .5, .2f), note (62, 1, .25, .5f), note (64, 2, 1, .9f) }, "Recombine rotates lengths backward");
        {
            const auto a = run (Tool::recombine, { { 1, 2 }, { 3, 7 } }, line), b = run (Tool::recombine, { { 1, 2 }, { 3, 7 } }, line);
            std::vector<int> pitches; for (const auto& n : a) pitches.push_back (n.pitch); std::sort (pitches.begin(), pitches.end());
            check (same (a, b) && pitches == std::vector<int> { 60, 62, 64 }, "Shuffle is a permutation that repeats for the same seed");
        }
        expect (run (Tool::velocityShaper, { { 0, 30 }, { 1, 127 }, { 2, 0 } }, line),
                { note (60, 0, 1, 30 / 127.0f), note (62, 1, .5, 79 / 127.0f), note (64, 2, .25, 1) }, "Velocity Shaper ramps velocities across the notes");
        expect (run (Tool::arpeggiate, { { 0, 0 }, { 1, 1 }, { 2, .8 } }, chord), { note (60, 0, .2), note (64, .25, .2), note (67, .5, .2), note (60, .75, .2) },
                "Arpeggiate Up plays the chord upward on sixteenths");
        expect (run (Tool::arpeggiate, { { 0, 2 }, { 1, 1 }, { 2, .8 } }, chord), { note (60, 0, .2), note (64, .25, .2), note (67, .5, .2), note (64, .75, .2) },
                "Up & Down turns at the top without repeating it");
        expect (run (Tool::arpeggiate, { { 0, 4 }, { 1, 1 }, { 2, 1 } }, chord), { note (60, 0, .25), note (67, .25, .25), note (64, .5, .25), note (60, .75, .25) },
                "Converge alternates the outer tones");
        expect (run (Tool::arpeggiate, { { 0, 1 }, { 1, 2 }, { 2, .5 }, { 3, 2 }, { 4, 12 } }, { note (60, 0, 2), note (67, 0, 2) }),
                { note (79, 0, .25), note (72, .5, .25), note (67, 1, .25), note (60, 1.5, .25) }, "two steps an octave apart, played downward on eighths");
        expect (run (Tool::ornament, { { 0, .0625 }, { 1, -1 }, { 2, .5 } }, { note (60, 0, 1), note (62, 1, 1) }),
                { note (60, 0, 1), note (61, .9375, .0625, .4f), note (62, 1, 1) }, "Ornament adds a grace note before each note that has room");
        expect (run (Tool::connect, { { 0, 1 }, { 1, .8 }, { 2, .9 } }, { note (60, 0, .25), note (64, 1, .25) }),
                { note (60, 0, .25), note (61, .25, .225, .64f), note (62, .5, .225, .64f), note (63, .75, .225, .64f), note (64, 1, .25) },
                "Connect fills the gap with passing notes on the grid");
        expect (run (Tool::euclidean, { { 0, 8 }, { 1, 3 }, { 2, 0 }, { 3, 36 }, { 4, 1 }, { 5, .5 }, { 6, .8 } }, {}, 2),
                { note (36, 0, .125), note (36, .75, .125), note (36, 1.5, .125) }, "Euclidean spreads three pulses over eight steps");
        expect (run (Tool::euclidean, { { 0, 8 }, { 1, 3 }, { 2, 1 }, { 3, 36 }, { 4, 1 }, { 5, .5 }, { 6, .8 } }, {}, 2),
                { note (36, .5, .125), note (36, 1.25, .125), note (36, 1.75, .125) }, "rotation shifts the pattern a step earlier");
        check (run (Tool::euclidean, { { 0, 8 }, { 1, 3 } }, {}, 16).size() == 24, "the pattern repeats across the clip");
        {
            const auto a = run (Tool::seed, { { 6, 42 } }, {}), b = run (Tool::seed, { { 6, 42 } }, {}), c = run (Tool::seed, { { 6, 43 } }, {});
            bool inside = a.size() == 16;
            for (const auto& n : a)
                inside = inside && n.pitch >= 48 && n.pitch <= 72 && near (std::fmod (n.startBeat, .25), 0) && n.lengthBeats >= .25 - 1e-9 && n.lengthBeats <= 1 + 1e-9
                      && n.startBeat + n.lengthBeats <= 16 + 1e-9;
            check (inside && same (a, b) && ! same (a, c), "Seed places 16 notes in range on the grid, repeatably for a seed and differently for another");
        }

        // Applying a tool in a clip.
        Session s (false);
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const auto keys = s.snapshot().tracks.back().id;
        ok (s.addMidiClip (keys, 0, 4), "a four-beat clip");
        const auto clip = s.snapshot().tracks.back().clips[0].id;
        juce::StringArray ids;
        ok (s.addMidiNotes (clip, chord, ids), "add a C major chord");
        const auto notesNow = [&] { const auto snapshot = s.snapshot(); return snapshot.tracks.back().clips[0].notes; };
        juce::StringArray result;
        check (s.applyNoteTool (clip, {}, notetools::defaults (Tool::arpeggiate), result).failed(), "a transformation needs chosen notes");
        check (s.applyNoteTool (clip, ids, settings (Tool::chop, { { 0, 1 } }), result).failed() && notesNow().size() == 3, "invalid settings change nothing");
        ok (s.applyNoteTool (clip, ids, settings (Tool::arpeggiate, { { 0, 0 }, { 1, 1 }, { 2, .8 } }), result), "arpeggiate the chord");
        auto notes = notesNow();
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.startBeat < b.startBeat; });
        // Stored velocities are whole MIDI steps.
        check (result.size() == 4 && same (notes, { note (60, 0, .2), note (64, .25, .2), note (67, .5, .2), note (60, .75, .2) }, .5f / 127),
               "the chord is replaced by its arpeggio and the new notes are reported");
        s.undo(); check (notesNow().size() == 3, "one undo restores the chord");
        ok (s.applyNoteTool (clip, {}, settings (Tool::euclidean, { { 0, 8 }, { 1, 3 }, { 3, 36 } }), result), "generate a Euclidean rhythm with nothing chosen");
        check (notesNow().size() == 3 + 6 && result.size() == 6, "a generator adds its notes across the clip and keeps the others");
        s.undo();
        ok (s.applyNoteTool (clip, ids, settings (Tool::euclidean, { { 0, 8 }, { 1, 3 }, { 3, 36 } }), result), "generate over the chosen chord");
        check (notesNow().size() == 6, "a generator replaces the chosen notes");
        std::cout << "Note tools passed." << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL note tools: " << e.what() << std::endl; return 1; }
}
}
