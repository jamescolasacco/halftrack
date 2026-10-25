#include "NoteTools.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>

// Rhythm, Shape, Stacks and Time Warp follow the reference's arithmetic step by step (static reading; see the MIDI
// tools contract), fused multiply-adds included: CMakeLists builds this file without floating-point contraction, so
// only the std::fma calls fuse.
namespace halftrack::notetools
{
namespace
{
using Tool = Session::NoteTool;
using Note = Session::MidiNoteInfo;
using Values = std::array<double, 16>;
constexpr double minimumLength = 1.0 / 960, epsilon = 1e-9;
constexpr const char* grids = "1/32|1/16|1/8|1/4|1/2";
// The reference's beat divisions for Rhythm's Step Duration and Shape's Rate: the editor's grid, the time range shared
// out (Fit), then whole notes to 1/128 notes in beats.
constexpr const char* beatDivisions = "Grid|Fit|1|1/2|1/3|1/4|1/6|1/8|1/12|1/16|1/24|1/32|1/48|1/64|1/96|1/128";
constexpr double divisionBeats[] { 0, 0, 4, 2, 4.0 / 3, 1, 2.0 / 3, .5, 1.0 / 3, .25, 1.0 / 6, .125, 1.0 / 12, .0625, 1.0 / 24, .03125 };
constexpr const char* onOff = "Off|On";
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
constexpr Field quantizeFields[] {
    { "Grid", 0, 8, 0, true, "", "Current Grid|1/4|1/8|1/8T|1/8 + 1/8T|1/16|1/16T|1/16 + 1/16T|1/32" }, { "Start", 0, 1, 1, true, "", "Off|On" },
    { "End", 0, 1, 0, true, "", "Off|On" }, { "Amount", 0, 100, 100, true, "%" } };
constexpr Field seedFields[] {
    { "Notes", 1, 256, 16, true, "" }, { "Low pitch", 0, 127, 48, true, "" }, { "High pitch", 0, 127, 72, true, "" },
    { "Shortest", 1.0 / 16, 4, .25, false, " beats" }, { "Longest", 1.0 / 16, 4, 1, false, " beats" }, { "Grid", 0, 4, 1, true, "", grids },
    { "Seed", 0, 9999, 1, true, "" }, { "Velocity", .01, 1, .8, false, "" } };
// The reference's Rhythm, Shape and Time Warp parameters, with their defaults (static reading). Pattern's and the
// accents' ranges narrow with the others (see range()); the largest Pattern is the last of 16 steps' 6435 patterns of 8.
constexpr Field rhythmFields[] {
    { "Steps", 1, 16, 8, true, "" }, { "Pattern", 0, 6434, 14, true, "" }, { "Density", 1, 16, 4, true, "" },
    { "Step Duration", 0, 15, 7, true, "", beatDivisions }, { "Repetitions", 1, 8, 1, true, "" }, { "Split", 0, 1, 0, false, "", nullptr, Display::percent },
    { "Shift", -8, 8, 0, true, "" }, { "Pitch", 0, 127, 36, true, "", nullptr, Display::note }, { "Velocity", 0, 127, 100, true, "" },
    { "Accent", 0, 127, 127, true, "" }, { "Accent Frequency", 0, 1000000, 4, true, "" }, { "Accent Offset", 0, 999999, 0, true, "" } };
constexpr Field shapeFields[] {
    { "Shape", 0, 9, 2, true, "", "User|Flat|Up|Down|Up & Down|Down & Up|Arc Up|Arc Down|Bounce Up|Bounce Down" },
    { "Rate", 0, 15, 0, true, "", beatDivisions }, { "Tie", 0, 1, 0, false, "", nullptr, Display::percent },
    { "Density", 0, 1, 1, false, "", nullptr, Display::percent }, { "Minimum Pitch", 0, 127, 60, true, "", nullptr, Display::note },
    { "Maximum Pitch", 0, 127, 84, true, "", nullptr, Display::note }, { "Jitter", 0, 1, 0, false, "", nullptr, Display::percent } };
constexpr Field timeWarpFields[] {
    { "Time 1", 0, 1, 0, false, "", nullptr, Display::percent, true }, { "Speed 1", 0, 1, .5, false, "", nullptr, Display::speed, true },
    { "Breakpoint 1", 0, 1, 1, true, "", onOff, Display::plain, true },
    { "Time 2", 0, 1, .5, false, "", nullptr, Display::percent, true }, { "Speed 2", 0, 1, .5, false, "", nullptr, Display::speed, true },
    { "Breakpoint 2", 0, 1, 0, true, "", onOff, Display::plain, true },
    { "Time 3", 0, 1, 1, false, "", nullptr, Display::percent, true }, { "Speed 3", 0, 1, .5, false, "", nullptr, Display::speed, true },
    { "Breakpoint 3", 0, 1, 1, true, "", onOff, Display::plain, true },
    { "Include Note End", 0, 1, 1, true, "", onOff }, { "Preserve Time Range", 0, 1, 1, true, "", onOff }, { "Quantize", 0, 1, 0, true, "", onOff } };
// Halftrack's chord bank holds 16 rules of 3 or 4 notes for each size of scale.
constexpr int stacksRuleCount = 16, longestStacksRule = 4;

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
std::vector<Note> strum (const Values& v, const std::vector<Note>& notes, double clip)
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
// Quantize, as the reference's tool: note starts, ends or both move Amount of the way to the nearest
// grid line, of either grid for the combined ones (as Record Quantization's list has them). A note
// whose start alone moves keeps its length; a quantized end that would reach the start goes to the
// next line after it. Each note keeps its identity and properties.
std::vector<Note> quantize (const Values& v, const std::vector<Note>& notes, double clip, double current)
{
    // Each choice's straight and triplet steps; 0 is none.
    constexpr std::pair<double, double> grids[] { { 0, 0 }, { 1, 0 }, { .5, 0 }, { 0, 1.0 / 3 }, { .5, 1.0 / 3 }, { .25, 0 }, { 0, 1.0 / 6 }, { .25, 1.0 / 6 }, { .125, 0 } };
    const auto [straight, triplet] = v[0] == 0 ? std::pair { current, 0.0 } : grids[(size_t) v[0]];
    const bool starts = v[1] > 0, ends = v[2] > 0;
    const auto amount = v[3] / 100;
    const auto nearest = [&] (double beat)
    {
        const auto a = straight > 0 ? std::round (beat / straight) * straight : beat, b = triplet > 0 ? std::round (beat / triplet) * triplet : beat;
        return straight <= 0 ? b : triplet <= 0 ? a : std::abs (a - beat) <= std::abs (b - beat) ? a : b;
    };
    const auto after = [&] (double beat)
    {
        const auto next = [beat] (double step) { return step > 0 ? (std::floor (beat / step + epsilon) + 1) * step : std::numeric_limits<double>::max(); };
        return std::min (next (straight), next (triplet));
    };
    std::vector<Note> result;
    for (auto n : notes)
    {
        const auto end = n.startBeat + n.lengthBeats;
        auto start = starts ? n.startBeat + (nearest (n.startBeat) - n.startBeat) * amount : n.startBeat;
        auto finish = end + (start - n.startBeat);
        if (ends)
        {
            auto target = nearest (end);
            if (target <= start + epsilon) target = after (start);
            finish = end + (target - end) * amount;
        }
        start = std::clamp (start, 0.0, std::max (0.0, clip - minimumLength));
        finish = std::clamp (finish, start + minimumLength, std::max (start + minimumLength, clip));
        n.startBeat = start; n.lengthBeats = finish - start;
        result.push_back (n);
    }
    return result;
}
std::vector<Note> chop (const Values& v, const std::vector<Note>& notes, double clip)
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
std::vector<Note> span (const Values& v, const std::vector<Note>& notes, double clip)
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
std::vector<Note> recombine (const Values& v, const std::vector<Note>& notes, double clip)
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
std::vector<Note> velocityShaper (const Values& v, const std::vector<Note>& notes, double clip)
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
std::vector<Note> arpeggiate (const Values& v, const std::vector<Note>& notes, double clip)
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
std::vector<Note> ornament (const Values& v, const std::vector<Note>& notes, double clip)
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
std::vector<Note> connect (const Values& v, const std::vector<Note>& notes, double clip)
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
std::vector<Note> euclidean (const Values& v, double clip)
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
std::vector<Note> seed (const Values& v, double clip)
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

// ---- The reference's arithmetic (static reading) ----
// Its beat time rounding: to the nearest 3843840th of a beat, halves away from zero, the half added by a fused
// multiply-add. Its four largest values (as markers) and infinity pass unrounded.
double tick (double x) noexcept
{
    constexpr double ticks = 3843840.0, unit = 1.0 / ticks, largest = std::numeric_limits<double>::max();
    if (std::isnan (x) || x == std::numeric_limits<double>::infinity() || x == largest || x == std::nextafter (largest, 0.0) || x == -std::nextafter (largest, 0.0)) return x;
    const auto sign = x >= 0 ? 1.0 : -1.0;
    return sign * (std::floor (std::abs (std::fma (sign * unit, 0.5, x)) * ticks) / ticks);
}
double roundAway (double x) noexcept { return x >= 0 ? std::floor (x + 0.5) : -std::floor (-x + 0.5); }
double single (double x) noexcept { return (double) (float) x; } // Its float parameters, widened again.
double division (double grid, double length, int option, long long count) noexcept
{ return option == 0 ? grid : option == 1 ? length / (double) count : divisionBeats[std::clamp (option, 2, 15)]; }
long long choose (int n, int k) noexcept
{
    if (k < 0 || k > n) return 0;
    long long result = 1;
    for (int i = 1; i <= k; ++i) result = result * (n - k + i) / i;
    return result;
}
bool accepted (const Context& c, int key) noexcept
{
    return (! c.drumKeys || c.drumKeys->test ((size_t) key)) && (! c.scale || ! c.scale->enabled || Session::inScale (*c.scale, key));
}

// Rhythm's values within the ranges the time range and grid leave, as the reference narrows its parameters: Steps to
// those that fit (16 with Fit), Density to Steps, Pattern to those there are, Accent Frequency to the hits and Accent
// Offset below it.
struct RhythmShape
{
    int steps = 1, mostSteps = 1, density = 1, pattern = 0, period = 0, offset = 0;
    long long patterns = 1, total = 1, mostPeriod = 0;
    double step = 1;
};
RhythmShape rhythmShape (const Values& v, double grid, double length) noexcept
{
    RhythmShape r;
    const auto option = (int) v[3];
    const bool fit = option == 1;
    const auto plain = fit ? 0.0 : tick (division (grid, length, option, std::numeric_limits<int>::min()));
    r.mostSteps = fit ? 16 : (int) std::clamp (std::floor (length / plain), 1.0, 16.0);
    r.steps = std::clamp ((int) v[0], 1, r.mostSteps);
    r.density = std::clamp ((int) v[2], 1, r.steps);
    r.patterns = choose (r.steps - 1, r.density - 1);
    r.pattern = (int) std::clamp ((long long) v[1], 0LL, r.patterns - 1);
    const long long count = (long long) r.steps * (long long) v[4];
    r.step = fit ? tick (division (grid, length, option, count)) : plain;
    r.total = fit ? count : std::max (1LL, (long long) std::floor (length / r.step));
    r.mostPeriod = (long long) std::floor ((double) r.total / (double) r.steps * (double) r.density);
    r.period = (int) std::clamp ((long long) v[10], 0LL, r.mostPeriod);
    r.offset = (int) std::clamp ((long long) v[11], 0LL, (long long) std::max (r.period, 1) - 1);
    return r;
}
// Rhythm: one key on a grid of steps filling the time range, a pattern repeating every Steps steps, steps split in two
// by chance, the pattern shifted, and accents every Accent Frequency notes from Accent Offset.
std::vector<Note> rhythm (const Values& v, double grid, const Context& c, std::uint32_t seed)
{
    const auto r = rhythmShape (v, grid, c.end - c.start);
    const auto split = (float) v[5];
    const auto velocity = single (v[8]), accent = single (v[9]);
    const int pitch = (int) v[7], shift = (int) v[6], steps = r.steps;
    const auto mask = rhythmPatterns (steps, r.density)[(size_t) r.pattern];
    const auto hits = [&] (long long step) { return ((mask >> (step % steps)) & 1u) != 0; };
    // Steps before the range, enough to shift from, count the split draws and notes the pattern had before it.
    const long long preroll = std::max ((long long) steps * (long long) std::ceil (8.0 / steps), (long long) steps);
    const auto draws = ((r.total + 2 * preroll) / steps) * r.density;
    ReferenceRandom random (seed);
    const auto half = r.step * 0.5;
    std::vector<char> splits ((size_t) std::max (0LL, draws));
    for (auto& s : splits) { const auto draw = random.uniform (1, 100); s = (double) (split * 100.0f) >= (double) draw && half > 1.0 / 3843840.0; }
    const auto splitAt = [&] (long long hit) { return hit >= 0 && hit < (long long) splits.size() && splits[(size_t) hit] != 0; };
    const auto first = preroll - shift;
    long long hit = 0, notesBefore = 0, notesPreroll = 0;
    for (long long i = 0; i < first; ++i) if (hits (i)) notesBefore += splitAt (hit++) ? 2 : 1;
    for (long long i = 0, h = 0; i < preroll; ++i) if (hits (i)) notesPreroll += splitAt (h++) ? 2 : 1;
    auto counter = notesBefore - notesPreroll;
    std::vector<Note> out;
    const auto emit = [&] (double start, double end)
    {
        auto place = r.period != 0 ? counter % r.period : 0;
        if (place < 0) place += r.period;
        const auto value = r.period != 0 && place == r.offset ? accent : velocity;
        ++counter;
        const auto played = value < 1 ? 1.0 : std::min (value, 127.0);
        Note note { {}, pitch, start, tick (tick (std::min (c.end, end)) - start), (float) (played / 127), value != 0 };
        note.releaseVelocity = juce::roundToInt (played);
        out.push_back (note);
    };
    auto t = tick (c.start);
    for (auto i = first; i < first + r.total; ++i)
    {
        const auto next = tick (r.step + t);
        if (hits (i))
        {
            if (! splitAt (hit)) emit (t, next);
            else { const auto middle = tick (half + t); emit (t, middle); emit (middle, next); }
            ++hit;
        }
        t = next;
    }
    return out;
}

// Shape: a random rhythm of notes, ties and rests on Rate's steps, its keys following the contour across the notes.
std::vector<Note> shapeNotes (const Session::NoteToolSettings& s, double grid, const Context& c, const std::vector<Note>& input)
{
    const auto& v = s.values;
    std::vector<int> keys;
    for (int key = (int) v[4]; key <= (int) v[5]; ++key) if (accepted (c, key)) keys.push_back (key);
    if (c.start >= c.end || keys.empty()) return input;
    std::array<float, 21> levels = shapeLevels ((int) v[0]);
    if (v[0] == 0) std::copy (s.levels.begin(), s.levels.end(), levels.begin());
    ReferenceRandom random (s.seed);
    const auto tie = single (v[2]), density = single (v[3]), jitter = single (v[6]);
    const auto length = c.end - c.start;
    const auto step = division (grid, length, (int) v[1], (long long) keys.size());
    const auto total = (long long) roundAway (length / step);
    const auto count = std::max (1LL, (long long) ((double) total * density));
    const auto groups = std::max (1LL, (long long) roundAway ((double) count * (1.0 - tie)));
    // The notes in groups of tied steps: the last step of each group is chosen at random, the very last kept.
    std::vector<char> ends ((size_t) count, 0);
    for (long long i = 0; i < groups; ++i) ends[(size_t) (count - 1 - i)] = 1;
    random.shuffle (ends, 0, (size_t) count - 1);
    std::vector<long long> parts;
    for (long long i = 0, previous = 0; i < count; ++i) if (ends[(size_t) i]) { parts.push_back (i + 1 - previous); previous = i + 1; }
    // With a step's rest for each step left over, shuffled after the first note.
    for (auto rest = total - count; rest > 0; --rest) parts.push_back (0);
    random.shuffle (parts, 1, parts.size());
    std::vector<std::pair<double, double>> events;
    long long position = 0;
    for (const auto part : parts)
    {
        if (part != 0) events.push_back ({ tick (std::fma ((double) position, step, c.start)), tick ((double) part * step) });
        position += std::max (part, 1LL);
    }
    // The contour runs from the middle of the first note to the middle of the last.
    auto from = c.start, to = c.end;
    if (events.size() >= 2) { from = c.start + events.front().second * 0.5; to = c.end - events.back().second * 0.5; }
    const auto last = (double) (levels.size() - 1), widest = (double) (keys.size() - 1);
    std::vector<Note> out;
    for (const auto& [start, duration] : events)
    {
        const auto x = ((start + duration * 0.5) - from) / (to - from);
        auto p = std::fma (x, last, std::fma (-x, 0.0, 0.0));
        p = p < 0.0 ? 0.0 : std::min (last, p);
        const auto low = std::floor (p), high = std::ceil (p);
        double level = levels[(size_t) low];
        if (low != high)
        {
            const auto t = (p - low) / (high - low);
            level = std::fma (t, (double) levels[(size_t) high], std::fma (-t, (double) levels[(size_t) low], (double) levels[(size_t) low]));
        }
        // Jitter pulls the key from the contour's toward a random one, at most halfway.
        const auto drawn = random.uniform (0, (long long) keys.size() - 1);
        const auto base = (long long) roundAway (std::fma (level, widest, std::fma (-level, 0.0, 0.0)));
        const auto index = std::fma ((double) (drawn - base) * 0.5, jitter, (double) base);
        Note note { {}, keys[(size_t) std::clamp ((long long) index, 0LL, (long long) keys.size() - 1)], start, duration, 64 / 127.0f, true };
        note.releaseVelocity = 64;
        out.push_back (note);
    }
    return out;
}

// Stacks: its chords one after another, each in an equal share of the time range, sized and placed in eighths of it.
Session::StacksChord limitedChord (Session::StacksChord chord, const Context& c)
{
    const auto notes = (int) scaleSteps (c.scale).size();
    const auto& rules = stacksRules (notes);
    chord.rule = std::clamp (chord.rule, 0, (int) rules.size() - 1);
    chord.degree = std::clamp (chord.degree, 0, notes - 1);
    const auto most = (int) rules[(size_t) chord.rule].size() - 1;
    chord.inversion = std::clamp (chord.inversion, -most, most);
    return chord;
}
std::vector<Note> stacks (const std::vector<Session::StacksChord>& chords, const Context& c)
{
    std::vector<Note> out;
    if (chords.empty()) return out;
    const auto slot = (c.end - c.start) / (double) chords.size();
    auto at = c.start;
    for (const auto& chord : chords)
    {
        const auto start = tick (std::fma (slot, single (chord.offset), at)), duration = tick (slot * single (chord.duration));
        for (const auto key : stacksPitches (chord, c))
        {
            Note note { {}, key, start, duration, 64 / 127.0f, true };
            note.releaseVelocity = 64;
            out.push_back (note);
        }
        at = slot + at;
    }
    return out;
}

// Time Warp: each note's start (and end) moved through the integral of a density, the inverse of the breakpoints'
// speed, interpolated linearly in time across the range and held beyond it.
struct WarpPoint { double x, density, integral; };
std::vector<WarpPoint> warpCurve (double start, double end, const std::vector<std::pair<double, double>>& points, double low, double high)
{
    std::vector<WarpPoint> curve;
    const auto add = [&] (double x, double value)
    {
        const auto density = 1.0 / timeWarpSpeed (value);
        const auto [x0, d0, f0] = curve.empty() ? WarpPoint { start, density, start } : curve.back();
        const auto dx = x - x0;
        const auto slope = dx == 0.0 ? 0.0 : (density - d0) / dx;
        const auto b = std::fma (-slope, x, density);
        curve.push_back ({ x, density, f0 + (dx * 0.5) * std::fma (slope, x0 + x, b + b) });
    };
    const bool unit = start == 0.0 && end == 1.0;
    const auto map = [&] (double t) { return unit ? t : std::fma (t, end, std::fma (-t, start, start)); };
    add (low, points.front().second);
    add (map (points.front().first), points.front().second);
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto [t0, v0] = points[i];
        const auto [t1, v1] = points[i + 1];
        // Where the speeds cross 1, the curve passes through it.
        if (t0 != t1 && ((v0 > 0.5 && v1 < 0.5) || (v0 < 0.5 && v1 > 0.5)))
        {
            const auto m = (v1 - v0) / (t1 - t0), c = std::fma (-m, t1, v1);
            add (map ((0.5 - c) / m), 0.5);
        }
        add (map (t1), v1);
    }
    add (high, points.back().second);
    return curve;
}
double warp (const std::vector<WarpPoint>& curve, double t)
{
    auto at = std::find_if (curve.begin(), curve.end(), [t] (const WarpPoint& p) { return t <= p.x; });
    if (at == curve.begin()) ++at;
    if (at == curve.end()) --at;
    const auto& [x1, d1, f1] = *at;
    const auto& [x0, d0, f0] = *(at - 1);
    const auto dx = x1 - x0;
    const auto slope = dx == 0.0 ? 0.0 : (d1 - d0) / dx;
    const auto b = std::fma (-slope, x1, d1);
    return f0 + ((t - x0) * 0.5) * std::fma (slope, t + x0, b + b);
}
std::vector<Note> timeWarp (const Values& v, std::vector<Note> notes, double grid, const Context& c)
{
    std::vector<std::pair<double, double>> points;
    for (size_t k = 0; k < 3; ++k) if (v[3 * k + 2] > 0) points.push_back ({ single (v[3 * k]), single (v[3 * k + 1]) });
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.startBeat < b.startBeat || (a.startBeat == b.startBeat && a.pitch < b.pitch); });
    if (! (c.start < c.end) || points.empty() || notes.empty()) return notes;
    auto latest = c.end;
    for (const auto& n : notes) latest = std::max (latest, tick (n.startBeat + n.lengthBeats));
    const auto curve = warpCurve (c.start, c.end, points, std::min (notes.front().startBeat, c.start) - 1.0, latest + 1.0);
    const bool ends = v[9] > 0, preserve = v[10] > 0, quantized = v[11] > 0;
    const auto original = notes;
    for (auto& n : notes)
    {
        const auto start = n.startBeat;
        n.startBeat = tick (warp (curve, start));
        if (ends) n.lengthBeats = tick (tick (warp (curve, tick (start + n.lengthBeats))) - n.startBeat);
    }
    // Preserve Time Range scales the result about the range's start so the last note ends where it did.
    if (preserve)
    {
        const auto& before = original.back();
        const auto& after = notes.back();
        if (const auto ratio = (tick (before.startBeat + before.lengthBeats) - c.start) / (tick (after.startBeat + after.lengthBeats) - c.start); std::isfinite (ratio))
            for (auto& n : notes)
            {
                n.startBeat = tick (std::fma (n.startBeat - c.start, ratio, c.start));
                if (ends) n.lengthBeats = tick (ratio * n.lengthBeats);
            }
    }
    if (quantized && grid != 0.0) for (auto& n : notes) n.startBeat = tick (roundAway (n.startBeat / grid) * grid);
    return notes;
}

// Halftrack's clips hold notes inside [0, clip], at least a 960th of a beat long: generated notes are cut to the clip,
// and any left shorter are dropped; moved notes are kept inside it.
std::vector<Note> inClip (std::vector<Note> notes, double clip)
{
    for (auto& n : notes)
    {
        if (n.startBeat < 0) { n.lengthBeats = n.startBeat + n.lengthBeats; n.startBeat = 0; }
        if (n.startBeat + n.lengthBeats > clip) n.lengthBeats = clip - n.startBeat;
    }
    std::erase_if (notes, [] (const Note& n) { return ! (n.lengthBeats >= minimumLength) || n.pitch < 0 || n.pitch > 127; });
    return notes;
}
std::vector<Note> keptInClip (std::vector<Note> notes, double clip)
{
    for (auto& n : notes)
    {
        n.startBeat = std::clamp (n.startBeat, 0.0, std::max (0.0, clip - minimumLength));
        if (! (n.lengthBeats >= minimumLength)) n.lengthBeats = minimumLength;
        if (n.startBeat + n.lengthBeats > clip) n.lengthBeats = std::max (minimumLength, clip - n.startBeat);
    }
    return notes;
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
        case Tool::quantize: return quantizeFields;
        case Tool::rhythm: return rhythmFields;
        case Tool::shape: return shapeFields;
        case Tool::stacks: return {};
        case Tool::timeWarp: return timeWarpFields;
    }
    return {};
}
const char* name (Tool tool) noexcept
{
    constexpr const char* names[] { "Strum", "Chop", "Span", "Recombine", "Velocity Shaper", "Arpeggiate", "Ornament", "Connect", "Euclidean", "Seed", "Fit to Scale", "Quantize",
                                    "Rhythm", "Shape", "Stacks", "Time Warp" };
    const auto index = (int) tool;
    return index >= 0 && index < Session::noteToolCount ? names[index] : "";
}
bool generates (Tool tool) noexcept
{ return tool == Tool::euclidean || tool == Tool::seed || tool == Tool::rhythm || tool == Tool::shape || tool == Tool::stacks; }
bool movesNotes (Tool tool) noexcept { return tool == Tool::quantize || tool == Tool::timeWarp; }
bool usesTimeRange (Tool tool) noexcept { return tool == Tool::rhythm || tool == Tool::shape || tool == Tool::stacks || tool == Tool::timeWarp; }
double gridBeats (double choice) noexcept { return .125 * std::pow (2.0, std::clamp (std::round (choice), 0.0, 4.0)); }
Session::NoteToolSettings defaults (Tool tool)
{
    Session::NoteToolSettings s; s.tool = tool;
    const auto f = fields (tool);
    for (size_t i = 0; i < f.size(); ++i) s.values[i] = f[i].initial;
    if (tool == Tool::stacks) s.chords.push_back ({}); // As the reference's, one chord: the first rule at C3.
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
    if (s.tool != Tool::stacks && ! s.chords.empty()) return false;
    if (! (s.tool == Tool::shape && s.values[0] == 0) && ! s.levels.empty()) return false;
    switch (s.tool)
    {
        case Tool::shape:
            // A drawn shape (User) has its 21 levels; the pitch range runs upward.
            if (s.values[4] > s.values[5]) return false;
            if (s.values[0] == 0 && (s.levels.size() != 21 || ! std::all_of (s.levels.begin(), s.levels.end(), [] (float l) { return l >= 0 && l <= 1; }))) return false;
            break;
        case Tool::stacks:
            if (s.chords.empty() || s.chords.size() > (size_t) Session::maximumStacksChords) return false;
            for (const auto& c : s.chords)
                if (c.rule < 0 || c.rule >= stacksRuleCount || c.degree < 0 || c.degree > 11 || c.octave < -2 || c.octave > 8 || std::abs (c.inversion) >= longestStacksRule
                    || ! (c.duration >= .125 && c.duration <= 1.875) || ! (c.offset >= -.875 && c.offset <= .875)) return false;
            break;
        case Tool::timeWarp:
        {
            // One to three breakpoints, in time order.
            double previous = 0;
            bool any = false;
            for (size_t k = 0; k < 3; ++k)
                if (s.values[3 * k + 2] > 0) { if (s.values[3 * k] < previous) return false; previous = s.values[3 * k]; any = true; }
            if (! any) return false;
            break;
        }
        default: break;
    }
    return true;
}
std::vector<Note> run (const Session::NoteToolSettings& s, const std::vector<Note>& notes, double clip, double current, const Context& context)
{
    if (! valid (s) || ! std::isfinite (clip) || clip < minimumLength || ! std::isfinite (current) || current < minimumLength || current > 16) return {};
    if (usesTimeRange (s.tool) && ! (std::isfinite (context.start) && std::isfinite (context.end) && context.start < context.end)) return {};
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
        case Tool::quantize: result = quantize (v, notes, clip, current); break;
        case Tool::rhythm: result = inClip (rhythm (v, current, context, s.seed), clip); break;
        case Tool::shape: result = inClip (shapeNotes (s, current, context, notes), clip); break;
        case Tool::stacks:
        {
            std::vector<Session::StacksChord> chords;
            for (const auto& chord : s.chords) chords.push_back (limitedChord (chord, context));
            result = inClip (stacks (chords, context), clip);
            break;
        }
        case Tool::timeWarp: result = keptInClip (timeWarp (v, notes, current, context), clip); break;
    }
    return ordered (result);
}
std::pair<double, double> range (const Session::NoteToolSettings& s, size_t field, double current, const Context& c) noexcept
{
    const auto f = fields (s.tool);
    if (field >= f.size()) return { 0, 0 };
    if (s.tool == Tool::rhythm && (field <= 2 || field >= 10))
    {
        const auto r = rhythmShape (s.values, current, c.end - c.start);
        if (field == 0) return { 1, r.mostSteps };
        if (field == 1) return { 0, (double) r.patterns - 1 };
        if (field == 2) return { 1, r.steps };
        if (field == 10) return { 0, (double) r.mostPeriod };
        return { 0, std::max (r.period, 1) - 1.0 };
    }
    if (s.tool == Tool::timeWarp && field < 9 && field % 3 == 0)
    {
        // A breakpoint's time lies between the active breakpoints before and after it.
        double low = 0, high = 1;
        for (size_t k = 0; k < 3; ++k)
            if (k != field / 3 && s.values[3 * k + 2] > 0) { if (k < field / 3) low = std::max (low, s.values[3 * k]); else high = std::min (high, s.values[3 * k]); }
        return { low, std::max (low, high) };
    }
    return { f[field].minimum, f[field].maximum };
}
Session::NoteToolSettings limited (Session::NoteToolSettings s, double current, const Context& c)
{
    if (s.tool == Tool::rhythm)
    {
        const auto r = rhythmShape (s.values, current, c.end - c.start);
        s.values[0] = r.steps; s.values[1] = r.pattern; s.values[2] = r.density; s.values[10] = r.period; s.values[11] = r.offset;
    }
    else if (s.tool == Tool::stacks) for (auto& chord : s.chords) chord = limitedChord (chord, c);
    else if (s.tool == Tool::timeWarp)
        for (size_t k = 0; k < 3; ++k) { const auto [low, high] = range (s, 3 * k, current, c); s.values[3 * k] = std::clamp (s.values[3 * k], low, high); }
    return s;
}

ReferenceRandom::ReferenceRandom (std::uint32_t seed) noexcept : state (seed % 2147483647u) { if (state == 0) state = 1; }
std::uint32_t ReferenceRandom::next() noexcept { return state = (std::uint32_t) ((std::uint64_t) state * 48271u % 2147483647u); }
// libc++'s __independent_bits_engine: `width` random bits from minstd_rand's 2^31 - 2 values, rejecting draws past the
// largest whole number of `w0`-bit blocks, in a working type of `digits` bits.
unsigned long long ReferenceRandom::bits (int width, int digits) noexcept
{
    constexpr unsigned long long range = 2147483646ull;
    constexpr int usable = 30, engineDigits = 32;
    int n = width / usable + (width % usable != 0);
    int w0 = width / n;
    auto y0 = w0 < digits ? (range >> w0) << w0 : 0ull;
    if (range - y0 > y0 / (unsigned long long) n)
    {
        ++n;
        w0 = width / n;
        y0 = w0 < digits ? (range >> w0) << w0 : 0ull;
    }
    const int n0 = n - width % n;
    const auto y1 = w0 < digits - 1 ? (range >> (w0 + 1)) << (w0 + 1) : 0ull;
    const auto mask0 = w0 > 0 ? 0xffffffffull >> (engineDigits - w0) : 0ull;
    const auto mask1 = w0 < engineDigits - 1 ? 0xffffffffull >> (engineDigits - (w0 + 1)) : 0xffffffffull;
    const auto full = digits >= 64 ? ~0ull : (1ull << digits) - 1;
    unsigned long long result = 0, u = 0;
    for (int k = 0; k < n0; ++k)
    {
        do u = next() - 1ull; while (u >= y0);
        result = (w0 < digits ? (result << w0) & full : 0ull) + (u & mask0);
    }
    for (int k = n0; k < n; ++k)
    {
        do u = next() - 1ull; while (u >= y1);
        result = (w0 < digits - 1 ? (result << (w0 + 1)) & full : 0ull) + (u & mask1);
    }
    return result & full;
}
long long ReferenceRandom::uniform (long long low, long long high, bool wide) noexcept
{
    const int digits = wide ? 64 : 32;
    const auto all = wide ? ~0ull : 0xffffffffull;
    const auto span = ((unsigned long long) high - (unsigned long long) low + 1ull) & all;
    if (span == 1) return low;
    if (span == 0) return low + (long long) bits (digits, digits);
    auto width = (int) std::bit_width (span) - 1;
    if ((span & (all >> (digits - width))) != 0) ++width;
    for (;;)
        if (const auto u = bits (width, digits); u < span) return (long long) u + low;
}
// The reference keeps each mask whose rotations it has not seen, adding only plain left shifts to what it has seen
// (its rotations are not cut to Steps bits), which leaves exactly the masks whose first step is a hit.
std::vector<std::uint32_t> rhythmPatterns (int steps, int density)
{
    std::vector<std::uint32_t> result;
    if (steps < 1 || steps > 16) return result;
    for (std::uint32_t mask = 1; mask < (1u << steps); mask += 2)
        if (std::popcount (mask) == density) result.push_back (mask);
    return result;
}
std::array<float, 21> shapeLevels (int preset) noexcept
{
    constexpr int n = 21;
    std::array<float, n> out;
    out.fill (0.5f);
    if (preset >= 2 && preset <= 5)
    {
        // Lines from a to b: across the whole shape for Up and Down, there and back for the others.
        const bool falling = preset == 3 || preset == 5;
        const double a = falling ? 1.0 : 0.0, b = falling ? 0.0 : 1.0;
        if (preset <= 3)
        {
            for (int i = 0; i < n; ++i)
            {
                auto t = (double) i / (double) (n - 1);
                t = std::fma (t, b, std::fma (-t, a, a));
                out[(size_t) i] = (float) t;
            }
            return out;
        }
        const auto half = (double) (n - 1) * 0.5;
        size_t k = 0;
        for (int i = 0; half > (double) i; ++i)
        {
            auto t = (double) i / half;
            t = std::fma (t, b, std::fma (-t, a, a));
            out[k++] = (float) t;
        }
        for (auto x = std::ceil (half); x < (double) n; x += 1.0)
        {
            auto t = (x - half) / ((double) (n - 1) - half);
            t = std::fma (t, a, std::fma (-t, b, b));
            out[k++] = (float) t;
        }
        return out;
    }
    if (preset >= 6 && preset <= 9)
    {
        // Uniform Catmull-Rom curves through the reference's control points.
        static const std::vector<double> points[] { { -2, 0, 1, 0, -2 }, { 3, 1, 0, 1, 3 }, { -13.0 / 12, 0, 2.0 / 3, .5, 1.0 / 3, 1, 25.0 / 12 },
                                                    { 25.0 / 12, 1, 1.0 / 3, .5, 2.0 / 3, 0, -13.0 / 12 } };
        const auto& p = points[preset - 6];
        const auto m = (int) p.size();
        for (int i = 0; i < n; ++i)
        {
            const auto t = (double) i / (double) (n - 1);
            const auto u = std::fma (t, (double) m - 2.0, 1.0 - t);
            const auto k = (int) std::min (std::floor (u), (double) (m - 3));
            const auto f = u - k;
            const auto p0 = p[(size_t) k - 1], p1 = p[(size_t) k], p2 = p[(size_t) k + 1], p3 = p[(size_t) k + 2];
            const auto d4 = (p2 - p0) * 0.5;
            const auto d2 = std::fma (p3 - p0, 0.5, (p1 - p2) * 1.5);
            auto d1 = (d4 + (p0 - p1)) - d2;
            d1 = std::fma (f, d2, d1);
            d1 = std::fma (f, d1, d4);
            out[(size_t) i] = (float) std::fma (f, d1, p1);
        }
    }
    return out;
}
const std::vector<std::vector<int>>& stacksRules (int scaleNotes)
{
    // Halftrack's own bank, not the reference's: eight chords of three notes and eight of four for each size of scale,
    // in scale degrees above the root (semitones in the chromatic set).
    static const std::vector<std::vector<int>> chromatic {
        { 0, 4, 7 }, { 0, 3, 7 }, { 0, 3, 6 }, { 0, 4, 8 }, { 0, 2, 7 }, { 0, 5, 7 }, { 0, 7, 12 }, { 0, 5, 10 },
        { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 4, 7, 10 }, { 0, 3, 6, 10 }, { 0, 3, 6, 9 }, { 0, 4, 7, 9 }, { 0, 3, 7, 9 }, { 0, 3, 7, 11 } };
    static const std::vector<std::vector<int>> sevenNotes {
        { 0, 2, 4 }, { 0, 1, 4 }, { 0, 3, 4 }, { 0, 2, 5 }, { 0, 3, 6 }, { 0, 4, 7 }, { 0, 2, 7 }, { 0, 4, 9 },
        { 0, 2, 4, 6 }, { 0, 2, 4, 8 }, { 0, 2, 4, 5 }, { 0, 1, 4, 6 }, { 0, 3, 4, 6 }, { 0, 4, 6, 8 }, { 0, 3, 6, 9 }, { 0, 4, 7, 9 } };
    static const std::vector<std::vector<int>> fiveNotes {
        { 0, 2, 4 }, { 0, 1, 3 }, { 0, 2, 3 }, { 0, 1, 2 }, { 0, 3, 5 }, { 0, 2, 5 }, { 0, 3, 6 }, { 0, 1, 5 },
        { 0, 2, 3, 4 }, { 0, 1, 3, 4 }, { 0, 2, 4, 6 }, { 0, 1, 2, 3 }, { 0, 3, 5, 7 }, { 0, 2, 3, 5 }, { 0, 1, 3, 5 }, { 0, 2, 5, 7 } };
    return scaleNotes == 12 ? chromatic : scaleNotes == 5 ? fiveNotes : sevenNotes;
}
std::vector<int> scaleSteps (const std::optional<Session::ScaleSettings>& scale)
{
    std::vector<int> steps;
    const auto mask = scale && scale->enabled ? Session::scaleIntervals (scale->mode) : (uint16_t) 0xfff;
    for (int i = 0; i < 12; ++i) if ((mask >> i) & 1u) steps.push_back (i);
    return steps;
}
int stacksRoot (const Session::StacksChord& given, const Context& c)
{
    const auto chord = limitedChord (given, c);
    const bool scaled = c.scale && c.scale->enabled;
    const auto base = 12 * chord.octave + 24 + (scaled ? c.scale->root : 0);
    if (! scaled || c.drumKeys) return base + chord.degree;
    return base + scaleSteps (c.scale)[(size_t) chord.degree];
}
std::pair<int, int> stacksDegree (int pitch, const Context& c)
{
    // The nearest key there is, lower on ties.
    auto key = std::clamp (pitch, 0, 127);
    for (int distance = 0; distance < 128; ++distance)
        if (key - distance >= 0 && accepted (c, key - distance)) { key -= distance; break; }
        else if (key + distance <= 127 && accepted (c, key + distance)) { key += distance; break; }
    if (! c.scale || ! c.scale->enabled) return { key % 12, std::clamp (key / 12 - 2, -2, 8) };
    const auto steps = scaleSteps (c.scale);
    const auto above = ((key - c.scale->root) % 12 + 12) % 12;
    const auto degree = (int) (std::find (steps.begin(), steps.end(), above) - steps.begin());
    return { std::min (degree, (int) steps.size() - 1), std::clamp ((key - above - c.scale->root - 24) / 12, -2, 8) };
}
std::vector<int> stacksPitches (const Session::StacksChord& given, const Context& c)
{
    const auto chord = limitedChord (given, c);
    const auto steps = scaleSteps (c.scale);
    const auto n = (int) steps.size();
    const auto& rule = stacksRules (n)[(size_t) chord.rule];
    const bool scaled = c.scale && c.scale->enabled;
    const auto base = chord.octave * 12 + 24 + (scaled ? c.scale->root : 0);
    std::vector<int> pitches;
    if (c.drumKeys)
    {
        // On a Drum Rack, the chord counts its pads up from the root's pad, without inversions.
        std::vector<int> keys;
        for (int key = 0; key < 128; ++key) if (accepted (c, key)) keys.push_back (key);
        const auto found = std::find (keys.begin(), keys.end(), base + chord.degree);
        const auto from = (long long) (found - keys.begin());
        for (const auto d : rule)
            if (const auto i = from + d; i >= 0 && i < (long long) keys.size()) pitches.push_back (keys[(size_t) i]);
        return pitches;
    }
    // Inverted: the lowest notes go up an octave of the scale (or the highest down, below zero), then lead.
    auto degrees = rule;
    const auto count = (int) degrees.size();
    if (chord.inversion != 0)
    {
        const auto k = chord.inversion > 0 ? chord.inversion : count + chord.inversion;
        if (chord.inversion > 0) for (int i = 0; i < chord.inversion; ++i) degrees[(size_t) i] += n;
        else for (int i = count + chord.inversion; i < count; ++i) degrees[(size_t) i] -= n;
        if (k > 0 && k < count) std::rotate (degrees.begin(), degrees.begin() + k, degrees.end());
    }
    for (const auto d : degrees)
    {
        const auto x = d + chord.degree, q = x / n, r = x - q * n;
        const auto key = (r < 0 ? steps[(size_t) (r + n)] - 12 : steps[(size_t) r]) + base + q * 12;
        if (key >= 0 && key <= 127) pitches.push_back (key);
    }
    return pitches;
}
double timeWarpSpeed (double value) noexcept
{
    constexpr double tenth = 0x1.9999999999999p-4; // One step below 0.1, as the reference's constant.
    if (value > 0.5)
    {
        auto w = value + -0.5;
        w = w + w;
        return 1.0 / std::fma (w, tenth, 1.0 - w);
    }
    const auto u = value + value;
    return u + std::fma (-u, tenth, tenth);
}
double timeWarpValue (double speed) noexcept
{
    constexpr double tenth = 0x1.9999999999999p-4;
    speed = std::clamp (speed, tenth, 10.0);
    return speed <= 1 ? (speed - tenth) / (1 - tenth) * 0.5 : 0.5 + (1 - 1 / speed) / (1 - tenth) * 0.5;
}
}
