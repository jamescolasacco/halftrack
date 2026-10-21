#include "NoteTools.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace halftrack::notetools
{
namespace
{
using Tool = Session::NoteTool;
using Note = Session::MidiNoteInfo;
constexpr double minimumLength = 1.0 / 960, epsilon = 1e-9;
constexpr const char* grids = "1/32|1/16|1/8|1/4|1/2";
constexpr Field strumFields[] {
    { "Low", -1, 1, 0, false, " beats" }, { "High", -1, 1, .125, false, " beats" }, { "Tension", -1, 1, 0, false, "" } };
constexpr Field chopFields[] { { "Parts", 2, 16, 4, true, "" }, { "Gap", 0, .9, 0, false, "" } };
constexpr Field spanFields[] { { "Mode", 0, 2, 0, true, "", "Legato|Tenuto|Staccato" }, { "Amount", .05, 1, .5, false, "" } };
constexpr Field recombineFields[] {
    { "Property", 0, 2, 0, true, "", "Pitch|Velocity|Length" }, { "Mode", 0, 2, 0, true, "", "Rotate|Mirror|Shuffle" },
    { "Steps", -16, 16, 1, true, "" }, { "Seed", 0, 9999, 1, true, "" } };
constexpr Field velocityFields[] { { "Start", 1, 127, 30, true, "" }, { "End", 1, 127, 127, true, "" }, { "Curve", -1, 1, 0, false, "" } };
constexpr Field arpeggiateFields[] {
    { "Style", 0, 5, 0, true, "", "Up|Down|Up & Down|Down & Up|Converge|Random" }, { "Rate", 0, 4, 1, true, "", grids },
    { "Gate", .1, 1, .8, false, "" }, { "Steps", 1, 4, 1, true, "" }, { "Distance", -24, 24, 12, true, " st" }, { "Seed", 0, 9999, 1, true, "" } };
constexpr Field ornamentFields[] { { "Offset", 1.0 / 64, .25, 1.0 / 16, false, " beats" }, { "Pitch", -12, 12, 0, true, " st" }, { "Velocity", .1, 1, .5, false, "" } };
constexpr Field connectFields[] { { "Rate", 0, 4, 1, true, "", grids }, { "Velocity", .1, 1, .8, false, "" }, { "Gate", .1, 1, .9, false, "" } };
constexpr Field euclideanFields[] {
    { "Steps", 2, 32, 16, true, "" }, { "Pulses", 1, 32, 5, true, "" }, { "Rotation", 0, 31, 0, true, "" }, { "Pitch", 0, 127, 36, true, "" },
    { "Rate", 0, 4, 1, true, "", grids }, { "Gate", .1, 1, .5, false, "" }, { "Velocity", .01, 1, .8, false, "" } };
constexpr Field fitFields[] {
    { "Root", 0, 11, 0, true, "", "C|C#|D|D#|E|F|F#|G|G#|A|A#|B" },
    { "Scale", 0, Session::scaleModeCount - 1, 0, true, "", "Major|Minor|Dorian|Phrygian|Lydian|Mixolydian|Locrian|Harmonic Minor|Melodic Minor|Major Pentatonic|Minor Pentatonic|Minor Blues|Whole Tone|Half-whole Diminished|Whole-half Diminished|Phrygian Dominant" } };
constexpr Field seedFields[] {
    { "Notes", 1, 256, 16, true, "" }, { "Low pitch", 0, 127, 48, true, "" }, { "High pitch", 0, 127, 72, true, "" },
    { "Shortest", 1.0 / 16, 4, .25, false, " beats" }, { "Longest", 1.0 / 16, 4, 1, false, " beats" }, { "Grid", 0, 4, 1, true, "", grids },
    { "Seed", 0, 9999, 1, true, "" }, { "Velocity", .01, 1, .8, false, "" } };

// Shapes 0-1 by a tension from -1 to 1: positive bends late, negative bends early.
double shape (double t, double tension)
{
    t = std::clamp (t, 0.0, 1.0);
    return tension >= 0 ? std::pow (t, 1 + 3 * tension) : 1 - std::pow (1 - t, 1 - 3 * tension);
}
// Chords are notes sharing a start beat, in start order.
std::vector<std::vector<Note>> chords (std::vector<Note> notes)
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.startBeat < b.startBeat - epsilon || (std::abs (a.startBeat - b.startBeat) <= epsilon && a.pitch < b.pitch); });
    std::vector<std::vector<Note>> result;
    for (const auto& n : notes)
        if (! result.empty() && std::abs (result.back().front().startBeat - n.startBeat) <= epsilon) result.back().push_back (n);
        else result.push_back ({ n });
    return result;
}
std::vector<Note> ordered (std::vector<Note> notes)
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.startBeat < b.startBeat - epsilon || (std::abs (a.startBeat - b.startBeat) <= epsilon && a.pitch < b.pitch); });
    return notes;
}
Note make (int pitch, double start, double length, float velocity, double clip)
{
    start = std::clamp (start, 0.0, std::max (0.0, clip - minimumLength));
    return { {}, std::clamp (pitch, 0, 127), start, std::clamp (length, minimumLength, clip - start), std::clamp (velocity, 1.0f / 127, 1.0f), true };
}
std::vector<Note> strum (const std::array<double, 8>& v, const std::vector<Note>& notes, double clip)
{
    std::vector<Note> result;
    for (const auto& chord : chords (notes))
        for (size_t i = 0; i < chord.size(); ++i)
        {
            const auto& n = chord[i];
            const auto offset = v[0] + (v[1] - v[0]) * shape (chord.size() > 1 ? (double) i / (double) (chord.size() - 1) : 0.0, v[2]);
            // The note moves and keeps its end, so a strummed chord still releases together.
            const auto start = std::max (0.0, n.startBeat + offset), end = std::max (n.startBeat + n.lengthBeats, start + minimumLength);
            result.push_back (make (n.pitch, start, end - start, n.velocity, clip));
        }
    return result;
}
std::vector<Note> chop (const std::array<double, 8>& v, const std::vector<Note>& notes, double clip)
{
    std::vector<Note> result;
    const int parts = (int) v[0];
    for (const auto& n : ordered (notes))
    {
        const auto part = n.lengthBeats / parts;
        for (int k = 0; k < parts; ++k) result.push_back (make (n.pitch, n.startBeat + k * part, std::max (minimumLength, part * (1 - v[1])), n.velocity, clip));
    }
    return result;
}
std::vector<Note> span (const std::array<double, 8>& v, const std::vector<Note>& notes, double clip)
{
    const auto sorted = ordered (notes);
    double last = 0; for (const auto& n : sorted) last = std::max (last, n.startBeat + n.lengthBeats);
    std::vector<Note> result;
    for (const auto& n : sorted)
    {
        double next = last;
        for (const auto& other : sorted) if (other.startBeat > n.startBeat + epsilon) { next = other.startBeat; break; }
        const auto room = std::max (minimumLength, next - n.startBeat);
        const int mode = (int) v[0];
        const auto length = mode == 0 ? room : mode == 1 ? room * v[1] : n.lengthBeats * v[1];
        result.push_back (make (n.pitch, n.startBeat, length, n.velocity, clip));
    }
    return result;
}
std::vector<Note> recombine (const std::array<double, 8>& v, const std::vector<Note>& notes, double clip)
{
    auto result = ordered (notes);
    const auto n = result.size();
    std::vector<size_t> source (n);
    for (size_t i = 0; i < n; ++i) source[i] = i;
    const int mode = (int) v[1];
    if (mode == 0 && n > 0)
    {
        const auto steps = (long long) v[2];
        for (size_t i = 0; i < n; ++i) source[i] = (size_t) ((((long long) i - steps) % (long long) n + (long long) n) % (long long) n);
    }
    else if (mode == 1) std::reverse (source.begin(), source.end());
    else
    {
        juce::Random random ((juce::int64) v[3]);
        for (size_t i = n; i > 1; --i) std::swap (source[i - 1], source[(size_t) random.nextInt ((int) i)]);
    }
    const auto original = result;
    for (size_t i = 0; i < n; ++i)
    {
        const auto& from = original[source[i]];
        if ((int) v[0] == 0) result[i].pitch = from.pitch;
        else if ((int) v[0] == 1) result[i].velocity = from.velocity;
        else result[i].lengthBeats = from.lengthBeats;
        result[i] = make (result[i].pitch, result[i].startBeat, result[i].lengthBeats, result[i].velocity, clip);
    }
    return result;
}
std::vector<Note> velocityShaper (const std::array<double, 8>& v, const std::vector<Note>& notes, double clip)
{
    auto result = ordered (notes);
    if (result.empty()) return result;
    const auto first = result.front().startBeat, last = result.back().startBeat;
    for (auto& n : result)
    {
        const auto t = last > first ? (n.startBeat - first) / (last - first) : 0.0;
        n = make (n.pitch, n.startBeat, n.lengthBeats, (float) std::round (v[0] + (v[1] - v[0]) * shape (t, v[2])) / 127.0f, clip);
    }
    return result;
}
std::vector<Note> arpeggiate (const std::array<double, 8>& v, const std::vector<Note>& notes, double clip)
{
    std::vector<Note> result;
    const auto rate = gridBeats (v[1]);
    juce::Random random ((juce::int64) v[5]);
    for (const auto& chord : chords (notes))
    {
        double end = chord.front().startBeat;
        for (const auto& n : chord) end = std::max (end, n.startBeat + n.lengthBeats);
        // Chord tones, repeated a distance apart for each further step.
        std::vector<Note> tones;
        for (int step = 0; step < (int) v[3]; ++step)
            for (auto n : chord) { n.pitch += step * (int) v[4]; if (n.pitch >= 0 && n.pitch <= 127) tones.push_back (n); }
        std::stable_sort (tones.begin(), tones.end(), [] (const Note& a, const Note& b) { return a.pitch < b.pitch; });
        tones.erase (std::unique (tones.begin(), tones.end(), [] (const Note& a, const Note& b) { return a.pitch == b.pitch; }), tones.end());
        if (tones.empty()) continue;
        std::vector<Note> order;
        const int style = (int) v[0];
        if (style == 0 || style == 5) order = tones;
        else if (style == 1) order.assign (tones.rbegin(), tones.rend());
        else if (style == 2 || style == 3)
        {
            // Up & Down turns at the ends without repeating them.
            std::vector<Note> up = tones, down (tones.rbegin(), tones.rend());
            if (style == 3) std::swap (up, down);
            order = up;
            if (down.size() > 2) order.insert (order.end(), down.begin() + 1, down.end() - 1);
        }
        else
        {
            // Converge alternates the outermost remaining tones toward the middle.
            size_t low = 0, high = tones.size() - 1;
            while (low < high) { order.push_back (tones[low++]); order.push_back (tones[high--]); }
            if (low == high) order.push_back (tones[low]);
        }
        int index = 0;
        for (double at = chord.front().startBeat; at < end - epsilon; at += rate, ++index)
        {
            const auto& tone = style == 5 ? order[(size_t) random.nextInt ((int) order.size())] : order[(size_t) index % order.size()];
            result.push_back (make (tone.pitch, at, std::min (rate * v[2], end - at), tone.velocity, clip));
        }
    }
    return result;
}
std::vector<Note> ornament (const std::array<double, 8>& v, const std::vector<Note>& notes, double clip)
{
    std::vector<Note> result;
    for (const auto& n : ordered (notes))
    {
        // A grace note just before each note; notes too close to the clip start keep none.
        if (n.startBeat - v[0] >= -epsilon) result.push_back (make (n.pitch + (int) v[1], std::max (0.0, n.startBeat - v[0]), v[0], n.velocity * (float) v[2], clip));
        result.push_back (n);
    }
    return ordered (result);
}
std::vector<Note> connect (const std::array<double, 8>& v, const std::vector<Note>& notes, double clip)
{
    const auto sorted = ordered (notes);
    const auto rate = gridBeats (v[0]);
    std::vector<Note> result = sorted;
    for (size_t i = 0; i + 1 < sorted.size(); ++i)
    {
        const auto& a = sorted[i];
        const auto& b = sorted[i + 1];
        const auto span = b.startBeat - a.startBeat;
        // Passing notes on the rate grid between the end of one note and the start of the next.
        for (double at = a.startBeat + a.lengthBeats; at + rate * v[2] <= b.startBeat + epsilon; at += rate)
        {
            const auto t = span > 0 ? (at - a.startBeat) / span : 0.0;
            result.push_back (make ((int) std::lround (a.pitch + (b.pitch - a.pitch) * t), at, rate * v[2], (a.velocity + b.velocity) * .5f * (float) v[1], clip));
        }
    }
    return ordered (result);
}
std::vector<Note> euclidean (const std::array<double, 8>& v, double clip)
{
    const int steps = (int) v[0], pulses = std::min ((int) v[1], steps), rotation = (int) v[2];
    const auto rate = gridBeats (v[4]);
    std::vector<Note> result;
    for (long long k = 0; k * rate < clip - epsilon; ++k)
    {
        // Evenly spread pulses: step i sounds when the running count of pulses steps up there.
        const auto i = (int) ((k + rotation) % steps);
        if ((i * pulses) % steps < pulses)
            result.push_back (make ((int) v[3], (double) k * rate, std::min (rate * v[5], clip - (double) k * rate), (float) v[6], clip));
    }
    return result;
}
std::vector<Note> seed (const std::array<double, 8>& v, double clip)
{
    juce::Random random ((juce::int64) v[6]);
    const auto grid = gridBeats (v[5]);
    const int low = (int) std::min (v[1], v[2]), high = (int) std::max (v[1], v[2]);
    const auto shortest = std::min (v[3], v[4]), longest = std::max (v[3], v[4]);
    const auto slots = std::max (1LL, (long long) std::floor ((clip - minimumLength) / grid) + 1);
    std::vector<Note> result;
    for (int i = 0; i < (int) v[0]; ++i)
    {
        const auto start = (double) random.nextInt ((int) std::min<long long> (slots, 1 << 30)) * grid;
        const auto length = std::max (minimumLength, std::round ((shortest + random.nextDouble() * (longest - shortest)) / grid) * grid);
        result.push_back (make (low + random.nextInt (high - low + 1), start, std::min (length, clip - start), (float) v[7], clip));
    }
    return ordered (result);
}
}

std::span<const Field> fields (Tool tool) noexcept
{
    switch (tool)
    {
        case Tool::strum: return strumFields;
        case Tool::chop: return chopFields;
        case Tool::span: return spanFields;
        case Tool::recombine: return recombineFields;
        case Tool::velocityShaper: return velocityFields;
        case Tool::arpeggiate: return arpeggiateFields;
        case Tool::ornament: return ornamentFields;
        case Tool::connect: return connectFields;
        case Tool::euclidean: return euclideanFields;
        case Tool::seed: return seedFields;
        case Tool::fitToScale: return fitFields;
    }
    return {};
}
const char* name (Tool tool) noexcept
{
    constexpr const char* names[] { "Strum", "Chop", "Span", "Recombine", "Velocity Shaper", "Arpeggiate", "Ornament", "Connect", "Euclidean", "Seed", "Fit to Scale" };
    const auto index = (int) tool;
    return index >= 0 && index < Session::noteToolCount ? names[index] : "";
}
bool generates (Tool tool) noexcept { return tool == Tool::euclidean || tool == Tool::seed; }
double gridBeats (double choice) noexcept { return .125 * std::pow (2.0, std::clamp (std::round (choice), 0.0, 4.0)); }
Session::NoteToolSettings defaults (Tool tool) noexcept
{
    Session::NoteToolSettings s; s.tool = tool;
    const auto f = fields (tool);
    for (size_t i = 0; i < f.size(); ++i) s.values[i] = f[i].initial;
    return s;
}
bool valid (const Session::NoteToolSettings& s) noexcept
{
    if ((int) s.tool < 0 || (int) s.tool >= Session::noteToolCount) return false;
    const auto f = fields (s.tool);
    for (size_t i = 0; i < s.values.size(); ++i)
    {
        const auto v = s.values[i];
        if (i >= f.size()) { if (v != 0) return false; continue; }
        if (! std::isfinite (v) || v < f[i].minimum || v > f[i].maximum || ((f[i].integer || f[i].choices) && v != std::floor (v))) return false;
    }
    return true;
}
std::vector<Note> run (const Session::NoteToolSettings& s, const std::vector<Note>& notes, double clip)
{
    if (! valid (s) || ! std::isfinite (clip) || clip < minimumLength) return {};
    const auto& v = s.values;
    std::vector<Note> result;
    switch (s.tool)
    {
        case Tool::strum: result = strum (v, notes, clip); break;
        case Tool::chop: result = chop (v, notes, clip); break;
        case Tool::span: result = span (v, notes, clip); break;
        case Tool::recombine: result = recombine (v, notes, clip); break;
        case Tool::velocityShaper: result = velocityShaper (v, notes, clip); break;
        case Tool::arpeggiate: result = arpeggiate (v, notes, clip); break;
        case Tool::ornament: result = ornament (v, notes, clip); break;
        case Tool::connect: result = connect (v, notes, clip); break;
        case Tool::euclidean: result = euclidean (v, clip); break;
        case Tool::seed: result = seed (v, clip); break;
        case Tool::fitToScale:
            // Out-of-scale notes move to the nearest scale tone; ties move down.
            for (auto n : notes) { n.pitch = Session::fitToScale ({ true, (int) v[0], (int) v[1] }, n.pitch); result.push_back (make (n.pitch, n.startBeat, n.lengthBeats, n.velocity, clip)); }
            break;
    }
    return ordered (result);
}
}
