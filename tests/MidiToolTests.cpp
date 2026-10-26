#include "NoteTools.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <set>
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
// A note the research model of the reference's tools printed: start and length to the last bit, velocity (and release
// velocity) and key.
struct Exact { double start, length; int velocity, pitch; };
bool exactly (const std::vector<Note>& actual, const std::vector<Exact>& expected)
{
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i)
        if (actual[i].startBeat != expected[i].start || actual[i].lengthBeats != expected[i].length || actual[i].pitch != expected[i].pitch
            || juce::roundToInt (actual[i].velocity * 127) != expected[i].velocity || actual[i].releaseVelocity != expected[i].velocity) return false;
    return true;
}
juce::String describeExactly (const std::vector<Note>& notes)
{
    juce::String s;
    for (const auto& n : notes) s << n.pitch << "@" << juce::String (n.startBeat, 17) << "+" << juce::String (n.lengthBeats, 17) << "v" << juce::roundToInt (n.velocity * 127) << " ";
    return s.trim();
}
void expectExactly (const std::vector<Note>& actual, const std::vector<Exact>& expected, const juce::String& text)
{ check (exactly (actual, expected), text + " (" + describeExactly (actual) + ")"); }
notetools::Context range (double start, double end) { notetools::Context c; c.start = start; c.end = end; return c; }
// A tool run in a time range, with a seed for its random draws.
std::vector<Note> runIn (Session::NoteToolSettings s, const std::vector<Note>& notes, notetools::Context c, std::uint32_t seed = 1, double grid = .25, double clip = 16)
{ s.seed = seed; return notetools::run (s, notes, clip, grid, c); }
}

int runMidiToolTests (const juce::File& artifacts)
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

        {
            // Quantize: starts, ends or both move Amount of the way to the grid's nearest line.
            const std::vector<Note> loose { note (60, .1, .5), note (62, 1.9, .3), note (64, 3.05, .02) };
            const auto quantized = [&] (std::initializer_list<std::pair<int, double>> values, double current = .25)
            { return notetools::run (settings (Tool::quantize, values), loose, 4, current); };
            check (notetools::valid (notetools::defaults (Tool::quantize)) && notetools::defaults (Tool::quantize).values[0] == 0 && notetools::defaults (Tool::quantize).values[1] == 1
                       && notetools::defaults (Tool::quantize).values[2] == 0 && notetools::defaults (Tool::quantize).values[3] == 100,
                   "Quantize starts on the current grid, quantizing starts by 100%");
            expect (quantized ({}), { note (60, 0, .5), note (62, 2, .3), note (64, 3, .02) }, "by default starts move to the current grid, each note keeping its length");
            expect (quantized ({ { 0, 1 } }), { note (60, 0, .5), note (62, 2, .3), note (64, 3, .02) }, "a 1/4 grid");
            expect (quantized ({ { 3, 50 } }), { note (60, .05, .5), note (62, 1.95, .3), note (64, 3.025, .02) }, "Amount moves them part of the way");
            expect (quantized ({ { 1, 0 }, { 2, 1 } }), { note (60, .1, .4), note (62, 1.9, .35), note (64, 3.05, .2) },
                    "ends alone stretch the notes, one ending at its start going to the next line");
            expect (quantized ({ { 2, 1 } }), { note (60, 0, .5), note (62, 2, .25), note (64, 3, .25) }, "starts and ends both, keeping at least a step");
            expect (quantized ({ { 0, 3 } }), { note (60, 0, .5), note (62, 2, .3), note (64, 3, .02) }, "a 1/8 triplet grid");
            const auto triplet = notetools::run (settings (Tool::quantize, { { 0, 3 } }), { note (60, .3, .5) }, 4);
            check (triplet.size() == 1 && near (triplet[0].startBeat, 1.0 / 3), "a 1/8 triplet grid has lines a third of a beat apart");
            const auto both = notetools::run (settings (Tool::quantize, { { 0, 7 } }), { note (60, .26, .1), note (62, .45, .1), note (64, .7, .1) }, 4);
            check (both.size() == 3 && near (both[0].startBeat, .25) && near (both[1].startBeat, .5) && near (both[2].startBeat, 2.0 / 3),
                   "1/16 + 1/16T takes the nearer line of either grid");
            const auto coarse = notetools::run (settings (Tool::quantize, {}), { note (60, .3, .5) }, 4, 1);
            check (coarse.size() == 1 && near (coarse[0].startBeat, 0), "Current Grid follows the editor's grid");
            Note marked = note (60, .1, .5); marked.id = "kept"; marked.chance = .5f;
            const auto keeps = notetools::run (settings (Tool::quantize, {}), { marked }, 4);
            check (keeps.size() == 1 && keeps[0].id == "kept" && keeps[0].chance == .5f, "a quantized note keeps its identity and properties");
        }

        {
            // The reference's random draws (static reading): libc++'s minstd_rand and uniform_int_distribution.
            notetools::ReferenceRandom random (12345);
            std::vector<long long> draws;
            for (int i = 0; i < 5; ++i) draws.push_back (random.uniform (1, 100));
            check (notetools::ReferenceRandom (1).next() == 48271 && draws == std::vector<long long> { 87, 21, 83, 86, 94 },
                   "the random draws are the reference's: minstd from seed 1, and 1 to 100 five times from seed 12345 as the model draws");
            const auto patterns = notetools::rhythmPatterns (8, 4);
            check (patterns.size() == 35 && notetools::rhythmPatterns (16, 4).size() == 455 && notetools::rhythmPatterns (16, 8).size() == 6435
                       && notetools::rhythmPatterns (5, 3).size() == 6 && patterns[0] == 0b1111 && patterns[14] == 0b01010101,
                   "Rhythm's patterns are each mask whose first step is a hit, in order: the default, the 14th of 8 steps with 4 hits, every other step");
        }
        {
            // Shape's presets, as the model computed them (to four places).
            const auto levelsAre = [] (int preset, std::initializer_list<double> expected)
            {
                const auto levels = notetools::shapeLevels (preset);
                size_t i = 0;
                for (const auto e : expected) if (std::abs (levels[i++] - e) > 6e-5) return false;
                return i == 21;
            };
            check (levelsAre (1, { .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5 })
                       && levelsAre (2, { 0, .05, .1, .15, .2, .25, .3, .35, .4, .45, .5, .55, .6, .65, .7, .75, .8, .85, .9, .95, 1 })
                       && levelsAre (4, { 0, .1, .2, .3, .4, .5, .6, .7, .8, .9, 1, .9, .8, .7, .6, .5, .4, .3, .2, .1, 0 })
                       && levelsAre (5, { 1, .9, .8, .7, .6, .5, .4, .3, .2, .1, 0, .1, .2, .3, .4, .5, .6, .7, .8, .9, 1 })
                       && levelsAre (6, { 0, .1495, .296, .4365, .568, .6875, .792, .8785, .944, .9855, 1, .9855, .944, .8785, .792, .6875, .568, .4365, .296, .1495, 0 })
                       && levelsAre (7, { 1, .8505, .704, .5635, .432, .3125, .208, .1215, .056, .0145, 0, .0145, .056, .1215, .208, .3125, .432, .5635, .704, .8505, 1 })
                       && levelsAre (8, { 0, .1733, .3367, .48, .5933, .6667, .6867, .66, .6067, .5467, .5, .4533, .3933, .34, .3133, .3333, .4067, .52, .6633, .8267, 1 })
                       && levelsAre (9, { 1, .8267, .6633, .52, .4067, .3333, .3133, .34, .3933, .4533, .5, .5467, .6067, .66, .6867, .6667, .5933, .48, .3367, .1733, 0 }),
                   "Shape's presets are the reference's lines and curves");
        }
        {
            // Rhythm, against the research model's output to the last bit.
            const auto rhythm = [&] (std::initializer_list<std::pair<int, double>> values, std::uint32_t seed, notetools::Context c = range (0, 4), double grid = .25)
            { return runIn (settings (Tool::rhythm, values), {}, c, seed, grid); };
            expectExactly (rhythm ({ { 1, 0 } }, 1), { { 0, .5, 127, 36 }, { .5, .5, 100, 36 }, { 1, .5, 100, 36 }, { 1.5, .5, 100, 36 } },
                           "Rhythm's first pattern of 8 eighths fills a bar's first half, its first note accented, as the model's");
            expectExactly (rhythm ({ { 0, 16 }, { 1, 7 }, { 2, 5 }, { 3, 9 }, { 5, .5 }, { 6, 2 }, { 10, 3 }, { 11, 1 } }, 987654),
                           { { .5, .125, 100, 36 }, { .625, .125, 127, 36 }, { .75, .125, 100, 36 }, { .875, .125, 100, 36 }, { 1.25, .25, 127, 36 },
                             { 1.5, .25, 100, 36 }, { 2, .125, 100, 36 }, { 2.125, .125, 127, 36 } },
                           "16 sixteenths with 5 hits, split by chance, shifted two steps, accenting every third note from the second, as the model's");
            expectExactly (rhythm ({}, 1), { { 0, .5, 127, 36 }, { 1, .5, 100, 36 }, { 2, .5, 100, 36 }, { 3, .5, 100, 36 } },
                           "by default every other eighth of a bar plays C1, accenting every fourth note");
            const auto muted = rhythm ({ { 8, 0 }, { 10, 0 } }, 1);
            check (muted.size() == 4 && std::all_of (muted.begin(), muted.end(), [] (const Note& n) { return ! n.active && juce::roundToInt (n.velocity * 127) == 1; }),
                   "a velocity of 0 gives muted notes of velocity 1, with no accents at an Accent Frequency of 0");
            const auto fit = rhythm ({ { 3, 1 }, { 4, 2 } }, 1);
            check (fit.size() == 8 && fit.back().startBeat == 3.5 && fit.back().lengthBeats == .25, "Fit fits Steps times Repetitions steps into the range");
            auto wide = settings (Tool::rhythm, { { 0, 16 }, { 10, 9 }, { 11, 7 } });
            check (notetools::range (wide, 0, .25, range (0, 2)) == std::pair { 1.0, 4.0 } && notetools::range (wide, 1, .25, range (0, 2)) == std::pair { 0.0, 0.0 }
                       && notetools::range (wide, 10, .25, range (0, 2)) == std::pair { 0.0, 4.0 },
                   "two beats of eighths leave room for 4 steps, one pattern of 4 hits and accents up to every fourth");
            const auto narrowed = notetools::limited (wide, .25, range (0, 2));
            check (narrowed.values[0] == 4 && narrowed.values[2] == 4 && narrowed.values[1] == 0 && narrowed.values[10] == 4 && narrowed.values[11] == 3
                       && notetools::limited (wide, .25, range (0, 16)).values[0] == 16,
                   "the steps, hits, pattern and accents narrow to fit, and widen again with the range");
        }
        {
            // Shape, against the model.
            const auto up = settings (Tool::shape, {});
            expectExactly (runIn (up, {}, range (0, 4), 5),
                           { { 0, .25, 64, 60 }, { .25, .25, 64, 62 }, { .5, .25, 64, 63 }, { .75, .25, 64, 65 }, { 1, .25, 64, 66 }, { 1.25, .25, 64, 68 },
                             { 1.5, .25, 64, 70 }, { 1.75, .25, 64, 71 }, { 2, .25, 64, 73 }, { 2.25, .25, 64, 74 }, { 2.5, .25, 64, 76 }, { 2.75, .25, 64, 78 },
                             { 3, .25, 64, 79 }, { 3.25, .25, 64, 81 }, { 3.5, .25, 64, 82 }, { 3.75, .25, 64, 84 } },
                           "Shape's Up climbs from C3 to C5 on the grid's sixteenths, as the model's");
            expectExactly (runIn (settings (Tool::shape, { { 2, .5 }, { 3, .75 }, { 6, 1 } }), {}, range (0, 4), 5),
                           { { 0, .25, 64, 63 }, { .25, .75, 64, 62 }, { 1, 1.25, 64, 68 }, { 2.75, .25, 64, 70 }, { 3, .25, 64, 73 }, { 3.75, .25, 64, 74 } },
                           "with Tie, Density and Jitter its notes tie, rest and stray from the shape, as the model's");
            check (same (runIn (up, {}, range (0, 4), 9), runIn (up, {}, range (0, 4), 9)) && ! same (runIn (settings (Tool::shape, { { 6, 1 } }), {}, range (0, 4), 9),
                                                                                                      runIn (settings (Tool::shape, { { 6, 1 } }), {}, range (0, 4), 10)),
                   "a seed repeats its draws, and another jitters differently");
            auto scaled = range (0, 4); scaled.scale = Session::ScaleSettings { true, 0, 9 };
            const auto pentatonic = runIn (up, {}, scaled, 5);
            check (! pentatonic.empty() && std::all_of (pentatonic.begin(), pentatonic.end(), [] (const Note& n) { return Session::inScale ({ true, 0, 9 }, n.pitch); }),
                   "with the clip's scale on it plays only the scale's keys");
            auto drawn = settings (Tool::shape, { { 0, 0 } });
            check (! notetools::valid (drawn), "a drawn shape needs its levels");
            drawn.levels.assign (21, 1.0f);
            const auto top = runIn (drawn, {}, range (0, 1), 5);
            check (notetools::valid (drawn) && top.size() == 4 && std::all_of (top.begin(), top.end(), [] (const Note& n) { return n.pitch == 84; }), "a drawn shape at the top plays the highest key");
            const std::vector<Note> kept { note (50, 0, 1) };
            auto none = settings (Tool::shape, { { 4, 61 }, { 5, 61 } });
            auto major = range (0, 4); major.scale = Session::ScaleSettings { true, 0, 0 };
            check (same (runIn (none, kept, major, 5), kept), "with no key of the scale in its range it passes the notes through");
            check (! notetools::valid (settings (Tool::shape, { { 4, 70 }, { 5, 60 } })) && ! notetools::valid ([] { auto s = notetools::defaults (Tool::strum); s.levels.assign (21, 0); return s; }()),
                   "the lowest key may not pass the highest, and only Shape draws levels");
        }
        {
            // Stacks, with Halftrack's own chord bank.
            auto stacks = notetools::defaults (Tool::stacks);
            check (notetools::valid (stacks) && stacks.chords.size() == 1 && notetools::stacksRules (12).size() == 16 && notetools::stacksRules (6) == notetools::stacksRules (7),
                   "Stacks starts with one chord; the bank has 16 rules a scale size, scales of other sizes taking the 7-note rules");
            expectExactly (runIn (stacks, {}, range (0, 4)), { { 0, 4, 64, 60 }, { 0, 4, 64, 64 }, { 0, 4, 64, 67 } }, "its first chord is C major at C3 across the range");
            stacks.chords = { { 0, 0, 3, 0, 1, 0 }, { 2, 4, 3, 1, .5, .25 }, { 8, 5, 3, -1, 1.875, -.875 } };
            auto cMajor = range (0, 8); cMajor.scale = Session::ScaleSettings { true, 0, 0 };
            constexpr double slot = 8.0 / 3;
            expectExactly (runIn (stacks, {}, cMajor),
                           { { 0, slot, 64, 60 }, { 0, slot, 64, 64 }, { 0, slot, 64, 67 }, { 3, 5, 64, 67 }, { 3, 5, 64, 69 }, { 3, 5, 64, 72 }, { 3, 5, 64, 76 },
                             { 10.0 / 3, 4.0 / 3, 64, 72 }, { 10.0 / 3, 4.0 / 3, 64, 74 }, { 10.0 / 3, 4.0 / 3, 64, 79 } },
                           "in C major, three chords share the range, each sized and placed in eighths: a triad, an inverted sus4 and an Am7 inverted down");
            check (notetools::stacksDegree (64, cMajor) == std::pair { 2, 3 } && notetools::stacksDegree (61, cMajor) == std::pair { 0, 3 }
                       && notetools::stacksRoot ({ 0, 2, 3 }, cMajor) == 64 && notetools::stacksRoot ({ 0, 2, 3 }, range (0, 4)) == 62,
                   "a root key is a degree and octave of the scale, snapping down to the scale");
            auto aMinor = range (0, 4); aMinor.scale = Session::ScaleSettings { true, 9, 1 };
            check (notetools::stacksDegree (72, aMinor) == std::pair { 2, 3 } && notetools::stacksRoot ({ 0, 2, 3 }, aMinor) == 72, "in A minor too");
            auto pads = range (0, 4); pads.drumKeys = std::bitset<128> {};
            for (int key = 36; key < 44; ++key) pads.drumKeys->set ((size_t) key);
            stacks.chords = { { 0, 0, 1, 2, 1, 0 } };
            expectExactly (runIn (stacks, {}, pads), { { 0, 4, 64, 36 }, { 0, 4, 64, 40 }, { 0, 4, 64, 43 } }, "on a Drum Rack a chord counts pads from its root's, without inversions");
            auto many = notetools::defaults (Tool::stacks);
            many.chords.assign (17, {});
            check (! notetools::valid (many) && ! notetools::valid ([] { auto s = notetools::defaults (Tool::stacks); s.chords.clear(); return s; }()), "Stacks holds one to 16 chords");
        }
        {
            // Time Warp, against the model: eight notes half a beat apart, warped across four beats.
            std::vector<Note> line;
            for (int i = 0; i < 8; ++i) { auto n = note (60, i * .5, .25); n.id = juce::String (i); line.push_back (n); }
            const auto warped = [&] (std::initializer_list<std::pair<int, double>> values) { return runIn (settings (Tool::timeWarp, values), line, range (0, 4)); };
            const auto at = [] (const std::vector<Note>& notes, std::initializer_list<std::pair<double, double>> expected)
            {
                if (notes.size() != expected.size()) return false;
                size_t i = 0;
                for (const auto& [start, length] : expected) { if (notes[i].startBeat != start || notes[i].lengthBeats != length || notes[i].id != juce::String ((int) i)) return false; ++i; }
                return true;
            };
            check (at (warped ({}), { { 0, .25 }, { .5, .25 }, { 1, .25 }, { 1.5, .25 }, { 2, .25 }, { 2.5, .25 }, { 3, .25 }, { 3.5, .25 } }), "at speed 1 Time Warp changes nothing");
            const auto faster = warped ({ { 7, 1 } });
            check (at (faster, { { 0, 0.42027035464535467 }, { 0.8162163357475858, 0.37162160755910756 }, { 1.5351351773226773, 0.32297286047286045 },
                                 { 2.156756784881785, 0.27432437354312356 }, { 2.6810811584249086, 0.22567562645687644 }, { 3.108108037795538, 0.17702713952713953 },
                                 { 3.4378379433066932, 0.12837839244089244 }, { 3.670270354645355, 0.07972964535464536 } }),
                   "speeding up to 10x, preserving the range, moves and shortens the notes as the model's, each keeping its identity (" + describeExactly (faster) + ")");
            const auto loose = warped ({ { 7, 1 }, { 10, 0 } });
            check (at (loose, { { 0, 0.24296875 }, { 0.471875, 0.21484375 }, { 0.8875, 0.18671875 }, { 1.246875, 0.15859375 }, { 1.55, 0.13046875 },
                                { 1.796875, 0.10234375 }, { 1.9875, 0.07421875 }, { 2.121875, 0.04609375 } }),
                   "without Preserve Time Range the notes gather early, as the model's (" + describeExactly (loose) + ")");
            const auto middle = warped ({ { 1, .75 }, { 4, .25 }, { 5, 1 }, { 7, .75 } });
            check (at (middle, { { 0, 0.13479020979020978 }, { 0.29459316724941725, 0.18481544497169497 }, { 0.689237065018315, 0.24507315601065602 },
                                 { 1.2248610764235763, 0.33602829462204464 }, { 1.942395104895105, 0.38150573384948383 }, { 2.6599291333666333, 0.2905508553946054 },
                                 { 3.195553404928405, 0.20982819264069263 }, { 3.5901970425407925, 0.15980295745920747 } }),
                   "a slow middle breakpoint between fast ends spreads the middle, as the model's (" + describeExactly (middle) + ")");
            const auto snapped = warped ({ { 7, 1 }, { 11, 1 } });
            check (std::all_of (snapped.begin(), snapped.end(), [] (const Note& n) { return std::abs (n.startBeat * 4 - std::round (n.startBeat * 4)) < 1e-12; }),
                   "Quantize puts the warped starts on the grid");
            const auto kept = warped ({ { 7, 1 }, { 9, 0 } });
            check (kept.size() == 8 && std::all_of (kept.begin(), kept.end(), [] (const Note& n) { return n.lengthBeats == .25; }), "without Include Note End the lengths stay");
            check (notetools::timeWarpSpeed (0) == 0.09999999999999999 && notetools::timeWarpSpeed (.5) == 1 && std::abs (notetools::timeWarpSpeed (1) - 10) < 1e-12
                       && std::abs (notetools::timeWarpValue (notetools::timeWarpSpeed (.3)) - .3) < 1e-12 && std::abs (notetools::timeWarpSpeed (notetools::timeWarpValue (4)) - 4) < 1e-12,
                   "a breakpoint's value maps to speeds of 0.1 to 10, and back");
            check (! notetools::valid (settings (Tool::timeWarp, { { 2, 0 }, { 8, 0 } })) && ! notetools::valid (settings (Tool::timeWarp, { { 0, .6 }, { 5, 1 } })),
                   "Time Warp needs a breakpoint on, and its breakpoints in time order");
            check (notetools::range (settings (Tool::timeWarp, { { 5, 1 }, { 3, .25 }, { 6, .75 } }), 3, .25, range (0, 4)) == std::pair { 0.0, .75 },
                   "a breakpoint moves between the breakpoints on either side");
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
        s.undo();
        {
            // Quantize moves the chosen notes themselves.
            juce::StringArray loose;
            ok (s.addMidiNotes (clip, { note (72, .1, .5), note (74, 1.9, .3) }, loose), "two loose notes");
            ok (s.setMidiNotesChance (clip, loose, .5f), "with a chance");
            ok (s.applyNoteTool (clip, loose, settings (Tool::quantize, {}), result, .5), "quantize them to a half-beat grid");
            auto moved = notesNow();
            std::erase_if (moved, [&] (const Note& n) { return ! loose.contains (n.id); });
            std::sort (moved.begin(), moved.end(), [] (const Note& a, const Note& b) { return a.startBeat < b.startBeat; });
            check (result == loose && moved.size() == 2 && near (moved[0].startBeat, 0) && near (moved[1].startBeat, 2) && near (moved[0].lengthBeats, .5)
                       && moved[0].chance == .5f && moved[1].chance == .5f,
                   "Quantize moves the notes on the given grid, keeping their identities and chance");
            check (s.undoDescription().containsIgnoreCase ("quantize"), "as one Quantize step");
            s.undo();
            auto back = notesNow();
            std::erase_if (back, [&] (const Note& n) { return ! loose.contains (n.id); });
            check (back.size() == 2 && std::any_of (back.begin(), back.end(), [] (const Note& n) { return near (n.startBeat, .1); }), "one undo puts them back");
            check (s.applyNoteTool (clip, loose, settings (Tool::quantize, {}), result, 0).failed(), "a grid of nothing is refused");
        }
        // Scales.
        const Session::ScaleSettings cMajor { true, 0, 0 }, aMinor { true, 9, 1 }, cPentatonic { true, 0, 9 };
        check (Session::inScale (cMajor, 60) && ! Session::inScale (cMajor, 61) && Session::inScale (cMajor, 71) && Session::inScale (aMinor, 69)
               && ! Session::inScale (aMinor, 70) && Session::inScale (aMinor, 72) && Session::inScale ({ false, 0, 0 }, 61), "scale membership follows root and mode; a disabled scale holds every pitch");
        check (Session::fitToScale (cMajor, 61) == 60 && Session::fitToScale (cMajor, 66) == 65 && Session::fitToScale (cMajor, 64) == 64
               && Session::fitToScale (cPentatonic, 65) == 64 && Session::fitToScale (cPentatonic, 66) == 67, "Fit to Scale takes the nearest scale tone, ties going down");
        check (Session::scaleIntervals (0) == 0xab5 && std::string (Session::scaleModeName (11)) == "Minor Blues" && Session::scaleIntervals (16) == 0, "the scale table is exact");
        expect (run (Tool::fitToScale, { { 0, 0 }, { 1, 0 } }, { note (61, 0, 1), note (66, 1, 1), note (70, 2, 1) }), { note (60, 0, 1), note (65, 1, 1), note (69, 2, 1) },
                "the Fit to Scale tool moves notes into C major");
        check (s.setClipScale (clip, { true, 12, 0 }).failed() && s.setClipScale (clip, { true, 0, Session::scaleModeCount }).failed()
               && s.setClipScale ("missing", {}).failed(), "roots past B, unknown modes and unknown clips are rejected");
        const auto scaleNow = [&] { const auto snapshot = s.snapshot(); return snapshot.tracks.back().clips[0].scale; };
        check (scaleNow() == Session::ScaleSettings {}, "clips start without a scale");
        ok (s.setClipScale (clip, aMinor), "give the clip A minor");
        check (scaleNow() == aMinor, "the clip reports its scale");
        s.undo(); check (scaleNow() == Session::ScaleSettings {}, "undo removes it"); s.redo();
        juce::String copy;
        ok (s.duplicateMidiClip (clip, copy), "duplicate the clip");
        bool kept = false; { const auto snapshot = s.snapshot(); for (const auto& c : snapshot.tracks.back().clips) if (c.id == copy) kept = c.scale == aMinor; }
        check (kept, "a duplicate keeps the scale");
        const auto root = artifacts.getChildFile ("note-tools-" + juce::Uuid().toString());
        ok (root.createDirectory(), "create a project folder");
        const auto project = root.getChildFile ("Scale.halftrack");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check (scaleNow() == aMinor, "the scale reopens");
        auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "parse the project");
        std::function<void (juce::XmlElement&)> corrupt = [&] (juce::XmlElement& e) { if (e.hasAttribute ("halftrackScaleMode")) e.setAttribute ("halftrackScaleMode", 99); for (auto* c : e.getChildIterator()) corrupt (*c); };
        corrupt (*xml);
        const auto broken = root.getChildFile ("Broken.halftrack"); check (xml->writeTo (broken), "write an unknown scale mode");
        const auto loaded = s.loadProject (broken);
        check (loaded.failed() && loaded.getErrorMessage().contains ("clip scale") && scaleNow() == aMinor, "an unknown scale rejects the load and keeps the open project");
        {
            // The project's current scale, as the reference's Set has one: the foreground clip's, taken by new MIDI clips.
            check (s.currentScale() == Session::ScaleSettings {}, "a project starts with no current scale");
            const Session::ScaleSettings dMinor { true, 2, 1 };
            s.setCurrentScale (dMinor);
            ok (s.addMidiTrack ("New clips"), "a MIDI track");
            const auto trackId = s.snapshot().tracks.back().id;
            ok (s.addMidiClip (trackId, 0, 4), "draw a clip");
            ok (s.addMidiClipToSlot (trackId, 0, 4), "and one in a slot");
            auto snapshot = s.snapshot();
            check (snapshot.tracks.back().clips[0].scale == dMinor && snapshot.tracks.back().slots[0].clip.scale == dMinor && ! s.canRedo(),
                   "new clips, drawn and in a slot, take D minor");
            s.undo();
            check (s.currentScale() == dMinor, "undo leaves the current scale");
            ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
            check (s.currentScale() == dMinor, "the current scale reopens with the project");
            auto saved = juce::XmlDocument::parse (project);
            auto* edit = saved != nullptr ? saved->getChildByName ("EDIT") : nullptr;
            check (edit != nullptr, "parse the saved project");
            edit->setAttribute ("halftrackCurrentScaleRoot", 12);
            const auto badScale = root.getChildFile ("Bad Scale.halftrack"); check (saved->writeTo (badScale), "write a root past B");
            check (s.loadProject (badScale).failed() && s.currentScale() == dMinor, "an invalid current scale rejects the load and keeps the open project");
            s.setCurrentScale ({ true, 12, 0 });
            check (s.currentScale() == dMinor, "an invalid current scale is ignored");
            ok (s.newProject(), "a new project");
            check (s.currentScale() == Session::ScaleSettings {}, "starts without one again");
        }
        {
            // Rhythm, Shape, Stacks and Time Warp in a clip: the time range, Rhythm's clearing, the clip's scale and a
            // Drum Rack's pads.
            ok (s.addMidiTrack ("Tools"), "add another MIDI track");
            const auto toolsTrack = s.snapshot().tracks.back().id;
            ok (s.addMidiClip (toolsTrack, 0, 8), "an eight-beat clip");
            const auto toolClip = s.snapshot().tracks.back().clips[0].id;
            const auto toolNotes = [&] { const auto snapshot = s.snapshot(); return snapshot.tracks.back().clips[0].notes; };
            juce::StringArray placed;
            ok (s.addMidiNotes (toolClip, { note (36, 1, .25), note (36, 5, .25), note (60, 0, 1) }, placed), "C1 inside and outside a bar, and C3");
            ok (s.applyNoteTool (toolClip, {}, notetools::defaults (Tool::rhythm), result, .25, std::pair { 0.0, 4.0 }), "generate Rhythm's default in the first bar");
            auto now = toolNotes();
            const auto count = [&] (int pitch, double from, double to)
            { return std::count_if (now.begin(), now.end(), [&] (const Note& n) { return n.pitch == pitch && n.startBeat >= from && n.startBeat < to; }); };
            check (result.size() == 4 && count (36, 0, 4) == 4 && count (36, 4, 8) == 1 && count (60, 0, 8) == 1 && s.undoDescription().containsIgnoreCase ("rhythm"),
                   "Rhythm clears the notes of its key starting in the range and adds its own, as one step");
            const auto accented = std::find_if (now.begin(), now.end(), [] (const Note& n) { return n.pitch == 36 && n.startBeat == 0; });
            check (accented != now.end() && juce::roundToInt (accented->velocity * 127) == 127 && accented->releaseVelocity == 127, "its accented note has velocity and release velocity 127");
            s.undo();
            check (toolNotes().size() == 3, "one undo restores the notes");
            check (s.noteToolContext (toolClip)->start == 0 && s.noteToolContext (toolClip)->end == 8 && ! s.noteToolContext (toolClip, std::pair { 9.0, 12.0 }),
                   "the range is the clip's loop brace when nothing is selected, and a range past the clip holds nothing");
            const auto c3 = placed[2];
            ok (s.applyNoteTool (toolClip, { c3 }, notetools::defaults (Tool::stacks), result), "Stacks over the chosen C3, across the clip's loop");
            now = toolNotes();
            check (result.size() == 3 && now.size() == 5 && count (60, 0, 8) == 1 && count (64, 0, 8) == 1 && count (67, 0, 8) == 1
                       && std::all_of (now.begin(), now.end(), [&] (const Note& n) { return n.pitch < 60 || (n.startBeat == 0 && n.lengthBeats == 8 && n.releaseVelocity == 64); }),
                   "a generator replaces the chosen notes, its chord lasting the clip's eight beats");
            s.undo();
            ok (s.setClipScale (toolClip, { true, 9, 1 }), "give the clip A minor");
            ok (s.applyNoteTool (toolClip, {}, notetools::defaults (Tool::stacks), result, .25, std::pair { 2.0, 6.0 }), "Stacks in a selection, with the scale on");
            now = toolNotes();
            check (count (69, 2, 3) == 1 && count (72, 2, 3) == 1 && count (76, 2, 3) == 1, "its chord is A minor's triad on A3, in the selection");
            s.undo(); s.undo();
            ok (s.applyNoteTool (toolClip, placed, settings (Tool::timeWarp, { { 7, 1 } }), result, .25, std::pair { 0.0, 8.0 }), "Time Warp the three notes");
            now = toolNotes();
            const auto byId = [&] (const juce::String& id) { return *std::find_if (now.begin(), now.end(), [&] (const Note& n) { return n.id == id; }); };
            check (std::set<juce::String> (result.begin(), result.end()) == std::set<juce::String> (placed.begin(), placed.end()) && now.size() == 3
                       && byId (placed[0]).startBeat != 1 && byId (placed[2]).startBeat == 0 && std::abs (byId (placed[1]).startBeat + byId (placed[1]).lengthBeats - 5.25) < 1e-6
                       && s.undoDescription().containsIgnoreCase ("time warp"),
                   "Time Warp moves the notes themselves, keeping their identities, the latest ending where it did, as one step");
            s.undo();
            check (s.applyNoteTool (toolClip, {}, settings (Tool::timeWarp, {}), result).failed(), "Time Warp needs notes");
            check (s.applyNoteTool (toolClip, {}, notetools::defaults (Tool::rhythm), result, .25, std::pair { 4.0, 4.0 }).failed() && toolNotes().size() == 3,
                   "an empty time range is refused, changing nothing");
            ok (s.setTrackInstrument (toolsTrack, Session::EffectKind::drumRack), "give the track a Drum Rack");
            const auto rackId = s.snapshot().tracks.back().instrument.id;
            for (const auto key : { 38, 42 })
            {
                juce::String chainId;
                ok (s.addRackChain (rackId, "Pad", chainId), "add a pad");
                Session::RackChainSettings pad; pad.name = "Pad"; pad.receiveNote = key;
                ok (s.setRackChainSettings (chainId, pad), "on key " + juce::String (key));
            }
            const auto drums = s.noteToolContext (toolClip);
            check (drums && drums->drumKeys && drums->drumKeys->count() == 2 && drums->drumKeys->test (38) && drums->drumKeys->test (42), "a Drum Rack's pads are the keys the tools play");
            ok (s.applyNoteTool (toolClip, {}, settings (Tool::shape, { { 4, 30 }, { 5, 50 } }), result, .25, std::pair { 0.0, 4.0 }), "Shape over the pads");
            now = toolNotes();
            check (result.size() == 16 && std::all_of (now.begin(), now.end(), [&] (const Note& n) { return ! result.contains (n.id) || n.pitch == 38 || n.pitch == 42; }),
                   "Shape plays only the keys that have pads");
            s.undo();
        }
        std::cout << "Note tools passed." << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL note tools: " << e.what() << std::endl; return 1; }
}
}
