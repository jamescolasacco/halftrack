#pragma once

#include "Session.h"
#include <span>

// MIDI note tools after the reference's transformation and generative tools. Each tool is a
// pure function from notes (and the clip length) to notes; Session::applyNoteTool applies it.
namespace halftrack::notetools
{
// How a field's value reads: as a number, a key's name (60 is C3), a speed (a Time Warp breakpoint's value), or a
// percentage of 0 to 1.
enum class Display { plain, note, speed, percent };
struct Field
{
    const char* label;
    double minimum, maximum, initial;
    bool integer;
    const char* unit;
    const char* choices = nullptr; // "A|B|C": the value is the choice index.
    Display display = Display::plain;
    bool ownEditor = false; // Shown by the tool's own editor (Time Warp's breakpoints) rather than as a row.
};
std::span<const Field> fields (Session::NoteTool) noexcept;
const char* name (Session::NoteTool) noexcept;
bool generates (Session::NoteTool) noexcept; // Generators fill the clip instead of reshaping notes.
// Tools that move the notes they are given, which keep their identities and properties, rather than replacing them.
bool movesNotes (Session::NoteTool) noexcept;
// Tools that work in a time range (see Context): Rhythm, Shape, Stacks and Time Warp.
bool usesTimeRange (Session::NoteTool) noexcept;
Session::NoteToolSettings defaults (Session::NoteTool);
bool valid (const Session::NoteToolSettings&) noexcept;
// Beats for a grid choice: 1/32, 1/16, 1/8, 1/4 or 1/2 note.
double gridBeats (double choice) noexcept;
// Where Rhythm, Shape, Stacks and Time Warp work (see Session::noteToolContext).
using Context = Session::NoteToolContext;
// The notes the tool produces. Transformations reshape `notes`; generators ignore them, except that Shape passes
// them through unchanged when it has no keys to play. Results lie inside [0, clipLengthBeats], are at least a 960th
// of a beat long and are ordered by start. Quantize and Time Warp keep each note's identity and properties, and
// Current Grid (Grid for Rhythm and Shape) is `currentGridBeats`.
std::vector<Session::MidiNoteInfo> run (const Session::NoteToolSettings&, const std::vector<Session::MidiNoteInfo>& notes, double clipLengthBeats,
                                        double currentGridBeats = 0.25, const Context& = {});
// A field's range with the other values, the time range and grid, as the reference narrows them: Rhythm's Steps (by
// how many steps fit), Pattern, Density, Accent Frequency and Offset, and a Time Warp breakpoint's time (between its
// active neighbours). Other fields keep their table's range.
std::pair<double, double> range (const Session::NoteToolSettings&, size_t field, double currentGridBeats, const Context&) noexcept;
// The values a tool plays with: each brought into its range, as the reference clamps them when a range narrows, and
// Stacks' chords into the chord bank and scale.
Session::NoteToolSettings limited (Session::NoteToolSettings, double currentGridBeats, const Context&);

// The reference's random draws (static reading): libc++'s minstd_rand, uniform_int_distribution and shuffle,
// made here so every platform draws the same.
class ReferenceRandom
{
public:
    explicit ReferenceRandom (std::uint32_t seed) noexcept;
    std::uint32_t next() noexcept;
    // A whole number from low to high, as uniform_int_distribution<int>, or <long> when wide.
    long long uniform (long long low, long long high, bool wide = false) noexcept;
    // As std::shuffle over values[from, to).
    template <typename Values> void shuffle (Values& values, size_t from, size_t to) noexcept
    {
        if (to <= from || to - from < 2) return;
        for (auto first = from, last = to - 1, span = to - from - 1; first < last; ++first, --span)
            if (const auto i = (size_t) uniform (0, (long long) span, true); i != 0)
            { auto held = static_cast<typename Values::value_type> (values[first]); values[first] = values[first + i]; values[first + i] = held; }
    }
private:
    unsigned long long bits (int width, int digits) noexcept;
    std::uint32_t state;
};
// Rhythm's patterns for a number of steps and hits: every mask with that many hits whose first step is one, in
// ascending order (bit n is step n).
std::vector<std::uint32_t> rhythmPatterns (int steps, int density);
// Shape's 21 levels for a preset, numbered as in its menu: 1 Flat, 2 Up, 3 Down, 4 Up & Down, 5 Down & Up, 6 Arc Up,
// 7 Arc Down, 8 Bounce Up and 9 Bounce Down. User (0), a drawn shape, has none; it gives Flat.
std::array<float, 21> shapeLevels (int preset) noexcept;
// Halftrack's chord bank for Stacks: rules of scale degrees from the root, for scales of 5, 7 and 12 notes; others
// use the 7-note rules, as the reference falls back to them.
const std::vector<std::vector<int>>& stacksRules (int scaleNotes);
// The semitones above its root of each note of a scale, or the 12 of the chromatic scale without one.
std::vector<int> scaleSteps (const std::optional<Session::ScaleSettings>&);
// The key a Stacks chord's root plays: its degree of the scale (of the chromatic scale without one) in its octave,
// where octave 3 holds C3. On a Drum Rack the degree counts semitones, as the reference's does.
int stacksRoot (const Session::StacksChord&, const Context&);
// A key as a Stacks chord's root degree and octave: the nearest key the scale (or Drum Rack) has, lower on ties,
// within octaves -2 to 8.
std::pair<int, int> stacksDegree (int pitch, const Context&);
// The keys a chord plays, in the rule's order; keys outside 0-127 are left out of the result.
std::vector<int> stacksPitches (const Session::StacksChord&, const Context&);
// A Time Warp breakpoint's speed for its value of 0 to 1: 0.1 to 1 below the middle and 1 to 10 above it.
double timeWarpSpeed (double value) noexcept;
double timeWarpValue (double speed) noexcept;
}
