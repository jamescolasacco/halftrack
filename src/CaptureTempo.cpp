#include "CaptureTempo.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

// The arithmetic follows the reference's capture library step by step (static reading; the contract names its parts),
// so that its choices between near tempos, grid positions and loop starts come out the same.
namespace halftrack::capturetempo
{
namespace
{
constexpr double tick = 1.0 / 3843840.0;
double msPerBeat (double bpm) noexcept { return (1.0 / bpm) * 60000.0; }
int twelfth (int t) noexcept { return ((t % 12) + 12) % 12; }
bool allowed (int t) noexcept
{
    switch (twelfth (t)) { case 0: case 2: case 3: case 4: case 6: case 8: case 9: case 10: return true; default: return false; }
}
// A position's metrical level and weight: beat, half beat, triplet eighth, sixteenth, triplet sixteenth.
int level (int t) noexcept
{
    switch (twelfth (t)) { case 0: return 1; case 6: return 2; case 4: case 8: return 3; case 3: case 9: return 4; default: return 5; }
}
double classWeight (int t) noexcept
{
    switch (twelfth (t)) { case 0: return 1.0; case 6: return 0.45; case 4: case 8: return 0.16; case 3: case 9: return 0.18; default: return 0.08; }
}
int nextPosition (int t) noexcept { do ++t; while (! allowed (t)); return t; }
int previousPosition (int t) noexcept { do --t; while (! allowed (t)); return t; }
double onGrid (double beats) noexcept { return beatPosition (beats) / 12.0; }

// How much a note counts: louder, lower and longer notes more.
struct Weights { std::vector<int> pitches; double maximumVelocity = 0, durationScale = 0; };
Weights weightsOf (const std::vector<Note>& notes)
{
    Weights w;
    std::set<int> pitches;
    double longest = 200.0;
    for (const auto& n : notes)
    {
        pitches.insert (n.pitch);
        w.maximumVelocity = std::max (w.maximumVelocity, n.velocity);
        longest = std::max (longest, n.durationMs);
    }
    w.pitches.assign (pitches.begin(), pitches.end());
    w.durationScale = 0.4 / std::min (longest, 1000.0);
    return w;
}
double pitchFactor (const Weights& w, int pitch) noexcept
{
    const auto index = (double) (std::lower_bound (w.pitches.begin(), w.pitches.end(), pitch) - w.pitches.begin());
    return 1.0 + index * (w.pitches.size() == 1 ? 0.0 : -0.4 / (double) (w.pitches.size() - 1));
}
double velocityFactor (const Weights& w, const Note& n) noexcept { return n.velocity / (w.maximumVelocity > 0 ? w.maximumVelocity : 127.0); }
double noteWeight (const Note& n, const Weights& w) noexcept
{
    const double d = std::min (std::max (n.durationMs, 200.0), 1000.0);
    return velocityFactor (w, n) * pitchFactor (w, n.pitch) * (1.0 + (d - 200.0) * w.durationScale);
}

// The first note of the last phrase: after the last silence of more than 1.7 s, or after a clearly longest gap
// inside what follows it.
size_t findEndSection (const std::vector<Note>& notes)
{
    size_t lastUser = 0;
    for (size_t i = 0; i < notes.size(); ++i) if (notes[i].user) lastUser = i;
    std::vector<Note> sounding (notes.begin(), notes.begin() + (std::ptrdiff_t) lastUser + 1);
    std::stable_sort (sounding.begin(), sounding.end(), [] (const Note& a, const Note& b) { return a.ms < b.ms; });
    std::vector<std::pair<double, double>> regions;
    for (const auto& n : sounding)
    {
        const double a = n.ms, b = n.ms + n.durationMs;
        if (! regions.empty() && a <= regions.back().second) regions.back().second = std::max (regions.back().second, b);
        else regions.push_back ({ a, b });
    }
    constexpr double high = 1700.0, low = 900.0, shortest = 1000.0, longest = 4000.0;
    size_t k = 0;
    for (size_t j = regions.size() - 1; j > 0; --j)
        if (regions[j].first - regions[j - 1].second > high) { k = j; break; }
    std::vector<std::pair<double, double>> gaps; // Each gap and where it ends.
    for (size_t j = k + 1; j < regions.size(); ++j) gaps.push_back ({ regions[j].first - regions[j - 1].second, regions[j].first });
    std::stable_sort (gaps.begin(), gaps.end(), [] (const auto& a, const auto& b) { return -a.first < -b.first; });
    double start = regions[k].first;
    if (! gaps.empty())
    {
        const double length = regions.back().second - gaps[0].second;
        const double u = (length - longest) / (shortest - longest);
        double threshold = low - u * low + u * high;
        threshold = threshold < low ? low : (threshold > high ? high : threshold);
        if (gaps[0].first > threshold && (gaps.size() < 2 || gaps[0].first > gaps[1].first * 2.0)) start = gaps[0].second;
    }
    for (size_t i = 0; i < notes.size(); ++i) if (notes[i].ms >= start) return i;
    return notes.size();
}

// The tempos each note's distance from the notes before it could mean, for the units of time it could span.
constexpr double units[] { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0 };
constexpr double unitWeights[] { 0.4, 1.0, 0.2, 1.0, 0.25, 0.6 };
using Interpretations = std::vector<std::vector<std::pair<int, double>>>;
Interpretations interpretationsOf (const std::vector<Note>& notes, double low, double high)
{
    const double window = units[5] * msPerBeat (low);
    Interpretations out;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        std::vector<std::pair<int, double>> list;
        for (size_t j = i; j-- > 0;)
        {
            if (notes[j].ms <= notes[i].ms - window) break;
            const double interval = notes[i].ms - notes[j].ms;
            if (interval <= 0.0) continue;
            for (int k = 0; k < 6; ++k)
                if (const double t = units[k] / (interval / 60000.0); low <= t && t <= high) list.push_back ({ k, t });
        }
        out.push_back (std::move (list));
    }
    return out;
}
struct Cluster { double tempo, weight; };
Cluster merged (double tempo, double weight, double otherTempo, double otherWeight) noexcept
{
    const double f = weight / (otherWeight + weight);
    const double ms = msPerBeat (otherTempo) * (1.0 - f) + msPerBeat (tempo) * f;
    return { 1.0 / (ms / 60000.0), weight + otherWeight };
}
std::vector<Cluster> clustersOf (const std::vector<Note>& notes, const Interpretations& interpretations, const Weights& w)
{
    std::vector<Cluster> clusters;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        for (auto& c : clusters) c.weight *= 0.95;
        for (const auto& [unit, tempo] : interpretations[i])
        {
            const double weight = velocityFactor (w, notes[i]) * pitchFactor (w, notes[i].pitch) * unitWeights[unit];
            bool joined = false;
            for (auto& c : clusters)
                if (std::abs (msPerBeat (tempo) - msPerBeat (c.tempo)) < 50.0) { c = merged (c.tempo, c.weight, tempo, weight); joined = true; break; }
            if (! joined) clusters.push_back ({ tempo, weight });
        }
        if (clusters.size() >= 2)
            for (size_t a = 0; a < clusters.size(); ++a)
                for (size_t b = a + 1; b < clusters.size(); ++b)
                    if (std::abs (msPerBeat (clusters[a].tempo) - msPerBeat (clusters[b].tempo)) < 50.0)
                    {
                        clusters[b] = merged (clusters[b].tempo, clusters[b].weight, clusters[a].tempo, clusters[a].weight);
                        clusters[a] = { -1.0, -1.0 };
                    }
        std::erase_if (clusters, [] (const Cluster& c) { return ! (c.tempo > 0.0 && ! (c.weight < 0.15)); });
    }
    return clusters;
}
std::vector<Cluster> heaviest (std::vector<Cluster> clusters, size_t k)
{
    if (clusters.size() < k) return clusters;
    std::stable_sort (clusters.begin(), clusters.end(), [] (const Cluster& a, const Cluster& b) { return -a.weight < -b.weight; });
    clusters.resize (k);
    return clusters;
}
// The candidate tempo, moved to the upper median of the last 2.1 bars' close interpretations when there are enough.
double refined (double tempo, double bar, const std::vector<Note>& notes, const Interpretations& interpretations)
{
    const double threshold = notes.back().ms - 2.1 * bar * msPerBeat (tempo);
    size_t count = 0;
    for (size_t i = notes.size(); i-- > 0 && notes[i].ms >= threshold;) ++count;
    std::vector<double> values;
    for (size_t i = notes.size() - count; i < notes.size(); ++i)
        for (const auto& [unit, t] : interpretations[i])
        {
            if (unit == 4) continue;
            if (const double ms = msPerBeat (t); 0.85 * msPerBeat (tempo) <= ms && ms <= 1.15 * msPerBeat (tempo)) values.push_back (t);
        }
    if (values.size() > 8) { std::sort (values.begin(), values.end()); return values[values.size() / 2]; }
    return tempo;
}

// The tempo path: a beam search placing each note on the grid, keeping or changing the tempo.
struct Marker { double beats, ms; };
struct Node { double beats, ms, tempo, score; int previous; bool alive = true; };
struct Path { std::vector<Marker> markers; double score = 0; std::optional<Marker> start; };
double positionWeight (double bar, int t, bool firstChord) noexcept
{
    const double p = t / 12.0;
    const double k = roundTo (p / bar, 1.0);
    double w = classWeight (t);
    if (beatPosition (bar * k) == t)
    {
        w *= 1.25;
        if (firstChord)
        {
            const double u = (k - 5.0) / 5.0;
            const double f = u + (2.6 - u * 2.6);
            w *= f < 1.0 ? 1.0 : std::min (f, 2.6);
        }
    }
    return w;
}
double tempoChangeLikelihood (double tempo, double previous, double candidate) noexcept
{
    const double d = (msPerBeat (previous) - msPerBeat (tempo)) / ((msPerBeat (candidate) / msPerBeat (120.0)) * 14.14213562373095);
    return std::exp (d * d * -0.5);
}
double onsetLikelihood (double expected, double ms, double candidate) noexcept
{
    const double d = std::abs (ms - expected) / (2.0 * (msPerBeat (candidate) / msPerBeat (120.0)) * -14.14213562373095) + 1.0;
    return std::max (d, 0.0);
}
void expandNode (std::vector<Node>& fresh, double candidate, double bar, const std::vector<Node>& nodes, double ms, double w, int index, bool firstChord)
{
    const auto& node = nodes[(size_t) index];
    if (! (node.ms < ms)) return;
    const double expected = node.beats + (node.tempo / 60.0) * (ms - node.ms) / 1000.0;
    const int last = beatPosition (expected + 0.5);
    for (int t = beatPosition (expected - 0.5); t <= last; t = nextPosition (t))
    {
        const double p = t / 12.0;
        if (! (p > node.beats)) continue;
        const double pw = positionWeight (bar, t, firstChord) * w;
        if (level (t) == 1 || level (t) == 2)
        {
            const double tempo = (p - node.beats) / ((ms - node.ms) / 60000.0);
            const double f = tempoChangeLikelihood (tempo, node.tempo, candidate);
            if (std::abs (msPerBeat (tempo) - msPerBeat (candidate)) < msPerBeat (candidate) * 0.18 && f > 0.005)
                fresh.push_back ({ p, ms, tempo, node.score + f * pw, index });
        }
        const double predicted = node.ms + (p - node.beats) / (node.tempo / 60.0) * 1000.0;
        if (const double g = onsetLikelihood (predicted, ms, candidate); g > 0.0)
            fresh.push_back ({ p, predicted, node.tempo, node.score + g * pw, index });
    }
}
void mergeNodes (std::vector<Node>& fresh, double candidate)
{
    const double r = msPerBeat (candidate) / msPerBeat (120.0);
    const double msTolerance = 16.0 * r, periodTolerance = 10.0 * r;
    for (size_t i = 0; i < fresh.size(); ++i)
    {
        auto& a = fresh[i];
        if (! a.alive) continue;
        const double period = msPerBeat (a.tempo);
        for (size_t j = i + 1; j < fresh.size(); ++j)
        {
            auto& b = fresh[j];
            if (! b.alive) continue;
            if (a.beats == b.beats && std::abs (a.ms - b.ms) < msTolerance && std::abs (period - msPerBeat (b.tempo)) < periodTolerance)
            {
                (a.score < b.score ? a : b).alive = false;
                if (! a.alive) break;
            }
        }
    }
    std::erase_if (fresh, [] (const Node& n) { return ! n.alive; });
}
// Marks all but the best `keep` living nodes dead (ties at the threshold live).
void prune (std::vector<Node>& nodes, size_t keep)
{
    std::vector<double> scores;
    for (const auto& n : nodes) if (n.alive) scores.push_back (n.score);
    std::sort (scores.begin(), scores.end());
    if (keep < scores.size())
    {
        const double threshold = scores[scores.size() - keep];
        for (auto& n : nodes) if (n.alive && n.score < threshold) n.alive = false;
    }
}
std::vector<Node> expand (const std::vector<Node>& nodes, double candidate, double bar, double ms, double w, bool firstChord)
{
    std::vector<Node> fresh;
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].alive) expandNode (fresh, candidate, bar, nodes, ms, w, (int) i, firstChord);
    mergeNodes (fresh, candidate);
    if (fresh.size() >= 17) { prune (fresh, 16); std::erase_if (fresh, [] (const Node& n) { return ! n.alive; }); }
    return fresh;
}
// The best node (the first of the highest score, dead or alive) traced back to where its search started.
Path backtrack (const std::vector<Node>& nodes)
{
    if (nodes.empty()) return {};
    size_t best = 0;
    for (size_t i = 1; i < nodes.size(); ++i) if (nodes[best].score < nodes[i].score) best = i;
    Path path;
    for (int i = (int) best;; i = nodes[(size_t) i].previous)
    {
        path.markers.push_back ({ nodes[(size_t) i].beats, nodes[(size_t) i].ms });
        if (nodes[(size_t) i].previous < 0) break;
    }
    path.score = nodes[best].score;
    path.start = path.markers.back();
    return path;
}
size_t firstChordEnd (const std::vector<Note>& notes, size_t begin, size_t end) noexcept
{
    for (size_t i = begin; i < end; ++i) if (std::abs (notes[i].ms - notes[begin].ms) > 40.0) return i;
    return end;
}
void grow (std::vector<Node>& nodes, std::vector<Node> fresh)
{
    nodes.insert (nodes.end(), fresh.begin(), fresh.end());
    if (nodes.size() >= 49) prune (nodes, 40);
}
Path forwardSearch (double candidate, double bar, const std::vector<Note>& notes, size_t begin, const Weights& w)
{
    std::vector<Node> nodes;
    for (size_t i = begin, end = firstChordEnd (notes, begin, notes.size()); i < end; ++i) nodes.push_back ({ 0.0, notes[i].ms, candidate, noteWeight (notes[i], w), -1 });
    for (size_t i = begin; i < notes.size(); ++i) grow (nodes, expand (nodes, candidate, bar, notes[i].ms, noteWeight (notes[i], w), false));
    auto path = backtrack (nodes);
    std::reverse (path.markers.begin(), path.markers.end());
    return path;
}
// The same search backwards in time, over notes [begin, end), from the given starting markers and scores.
Path backwardSearch (double candidate, double bar, const std::vector<std::pair<Marker, double>>& from, const std::vector<Note>& notes, size_t begin, size_t end, const Weights& w)
{
    std::vector<Node> nodes;
    if (begin != end)
    {
        for (const auto& [marker, score] : from) nodes.push_back ({ -marker.beats, -marker.ms, candidate, score, -1 });
        const auto chordEnd = firstChordEnd (notes, begin, end);
        for (size_t j = end; j > begin; --j)
            grow (nodes, expand (nodes, candidate, bar, -notes[j - 1].ms, noteWeight (notes[j - 1], w), j <= chordEnd && j > begin));
    }
    auto path = backtrack (nodes);
    for (auto& m : path.markers) m = { -m.beats, -m.ms };
    if (path.start) path.start = Marker { -path.start->beats, -path.start->ms };
    return path;
}
void chordNodes (std::vector<std::pair<Marker, double>>& out, const std::vector<Note>& notes, double beats, double offset, double window, const Weights& w)
{
    const double last = notes.back().ms;
    for (size_t i = notes.size(); i-- > 0;)
    {
        if (std::abs (notes[i].ms - last) > window) break;
        out.push_back ({ { beats, notes[i].ms }, noteWeight (notes[i], w) - offset });
    }
}
Path constantPath (double tempo, double startMs) { return { { { 0.0, startMs }, { 1.0, startMs + msPerBeat (tempo) } }, 0.0, Marker { 0.0, startMs } }; }
bool valid (const std::vector<Marker>& m) noexcept
{
    if (m.size() < 2) return false;
    for (size_t i = 0; i + 1 < m.size(); ++i) if (! (m[i].beats <= m[i + 1].beats) || ! (m[i].ms <= m[i + 1].ms)) return false;
    return true;
}
std::vector<Marker> withEarlier (const Path& earlier, const std::vector<Marker>& markers)
{
    std::vector<Marker> full;
    if (! earlier.markers.empty()) full.assign (earlier.markers.begin(), earlier.markers.end() - 1);
    full.insert (full.end(), markers.begin(), markers.end());
    return full;
}
Path findPath (double candidate, double bar, const std::vector<Note>& notes, size_t endSection, const Weights& w)
{
    if (notes.size() < 2) return constantPath (candidate, notes[0].ms);
    Path main;
    if (notes.back().ms - notes[endSection].ms >= (bar * 8.0 + 0.1) * msPerBeat (candidate))
    {
        std::vector<std::pair<Marker, double>> from;
        chordNodes (from, notes, 0.0, 0.0, 40.0, w);
        chordNodes (from, notes, -1.0, 0.15, msPerBeat (candidate) * 0.95, w);
        chordNodes (from, notes, -0.5, 0.3, msPerBeat (candidate) * 0.45, w);
        main = backwardSearch (candidate, bar, from, notes, endSection, notes.size(), w);
    }
    else main = forwardSearch (candidate, bar, notes, endSection, w);
    if (main.markers.empty()) return constantPath (candidate, notes[0].ms);
    auto full = withEarlier (backwardSearch (candidate, bar, { { main.markers[0], main.score } }, notes, 0, endSection, w), main.markers);
    if (! valid (full)) return constantPath (candidate, notes[0].ms);
    return { std::move (full), main.score, main.start };
}
// A last phrase of one note: its length taken as a power of two bars, when it can be.
std::pair<std::vector<Marker>, double> singleNotePath (double candidate, double bar, const std::vector<Note>& notes, size_t endSection, const Weights& w)
{
    const auto& n = notes[endSection];
    const double t = n.ms, d = n.durationMs;
    std::vector<Marker> markers;
    double tempo = candidate;
    if (bar * msPerBeat (164.0) <= d && d < bar * 8.0 * msPerBeat (78.0))
    {
        const double r = d / (bar * msPerBeat (120.0));
        const double k = r > 1.0 ? std::ldexp (1.0, (int) std::trunc (std::log2 (r))) : 1.0;
        markers = { { 0.0, t }, { bar * k, t + d } };
        tempo = (bar * k) / (d / 60000.0);
    }
    else markers = constantPath (candidate, t).markers;
    auto full = withEarlier (backwardSearch (tempo, bar, { { markers[0], 0.0 } }, notes, 0, endSection, w), markers);
    if (! valid (full)) full = markers;
    return { std::move (full), roundTo (tempo, 0.1) };
}

// Conversions through a tempo map, linear between markers and carried on past both ends.
double msToBeats (const std::vector<Marker>& m, double t) noexcept
{
    if (m.size() <= 1) return 0.0;
    size_t i = m.size() - 1;
    for (size_t k = 0; k + 1 < m.size(); ++k) if (m[k].ms > t) { i = k; break; }
    const size_t j = i == 0 ? i + 1 : i - 1;
    return m[i].beats + ((t - m[i].ms) / 1000.0) * ((m[i].beats - m[j].beats) / ((m[i].ms - m[j].ms) / 1000.0));
}
double beatsToMs (const std::vector<Marker>& m, double b) noexcept
{
    if (m.size() <= 1) return 0.0;
    size_t i = m.size() - 1;
    for (size_t k = 0; k + 1 < m.size(); ++k) if (m[k].beats > b) { i = k; break; }
    const size_t j = i == 0 ? i + 1 : i - 1;
    return m[i].ms + ((b - m[i].beats) / ((m[i].beats - m[j].beats) / ((m[i].ms - m[j].ms) / 1000.0))) * 1000.0;
}
std::vector<Note> converted (const std::vector<Note>& notes, const std::vector<Marker>& m)
{
    std::vector<Note> out;
    for (auto n : notes)
    {
        n.beat = engineRound (msToBeats (m, n.ms));
        n.durationBeats = std::max (tick, engineRound (msToBeats (m, n.ms + n.durationMs) - n.beat));
        out.push_back (n);
    }
    return out;
}

// The tempo map simplified to markers on bar lines (or on the best note between them).
size_t firstAtOrAfter (const std::vector<Marker>& out, double b) noexcept
{
    size_t i = 0;
    while (i < out.size() && ! (out[i].beats >= b)) ++i;
    return i;
}
bool canInsert (const std::vector<Marker>& out, double b) noexcept
{
    const auto i = firstAtOrAfter (out, b);
    if (i < out.size() && ! (out[i].beats >= b + 1.0)) return false;
    if (i > 0 && ! (out[i - 1].beats <= b - 1.0)) return false;
    return true;
}
void insertMarker (std::vector<Marker>& out, Marker marker) { out.insert (out.begin() + (std::ptrdiff_t) firstAtOrAfter (out, marker.beats), marker); }
double rank (const Note& n, double bar) noexcept
{
    const double whole = roundTo (n.beat, 1.0), half = roundTo (n.beat, 0.5);
    if (std::abs (n.beat - whole) < 0.0001)
    {
        double d = std::fmod (std::fmod (bar + std::fmod (whole, bar), bar), 2.0);
        d = std::fmod (d + 2.0, 2.0);
        return d == 0.0 ? 1.0 : 0.5;
    }
    return std::abs (n.beat - half) < 0.0001 ? 0.25 : 0.0;
}
template <typename Keep>
void addBestNote (std::vector<Marker>& out, double bar, const std::vector<Note>& notes, size_t begin, size_t end, Keep keep)
{
    const Note* best = nullptr;
    for (size_t i = begin; i < end; ++i)
        if (keep (notes[i]) && (best == nullptr || rank (notes[i], bar) > rank (*best, bar))) best = &notes[i];
    if (best == nullptr) return;
    if (std::abs (best->beat - roundTo (best->beat, 1.0)) >= 0.0001 && std::abs (best->beat - roundTo (best->beat, 0.5)) >= 0.0001) return;
    if (canInsert (out, best->beat)) insertMarker (out, { best->beat, best->ms });
}
std::vector<Marker> simplified (const std::vector<Marker>& path, std::optional<Marker> start, double bar, const std::vector<Note>& notes)
{
    std::vector<Marker> out;
    if (start) out.push_back (*start);
    double first = notes.front().beat, last = notes.back().beat;
    if (bar != 0.0)
    {
        const double f = bar * roundTo (first / bar, 1.0);
        first = f > first ? f - bar : f;
        const double l = bar * roundTo (last / bar, 1.0);
        last = l < last ? l + bar : l;
    }
    const auto all = [] (const Note&) { return true; };
    size_t current = 0;
    for (double b = first; b <= last; b += bar)
    {
        const auto count = out.size();
        size_t near = current;
        while (near < notes.size() && b - 0.085 > notes[near].beat) ++near;
        size_t past = near;
        while (past < notes.size() && ! (b + 0.085 < notes[past].beat)) ++past;
        size_t nextBar = past;
        while (nextBar < notes.size() && b + bar - 0.085 > notes[nextBar].beat) ++nextBar;
        if (near == past)
        {
            double previous = first;
            for (size_t k = out.size(); k-- > 0;) if (out[k].beats < b) { previous = out[k].beats; break; }
            if (b - previous > bar) addBestNote (out, bar, notes, current, past, all);
        }
        else if (canInsert (out, b)) insertMarker (out, { b, notes[near].ms });
        current = count < out.size() ? nextBar : past;
    }
    if (out.empty() || ! (out[0].beats < first + bar - 0.085))
        addBestNote (out, bar, notes, 0, notes.size(), [&] (const Note& n) { return n.beat < first + bar - 0.085; });
    if (std::none_of (out.begin(), out.end(), [&] (const Marker& m) { return m.beats > last - bar + 0.085; }))
        addBestNote (out, bar, notes, 0, notes.size(), [&] (const Note& n) { return n.beat > last - bar + 0.085; });
    const double end = last + (last == first ? bar : 0.0);
    if (out.empty()) out = { { first, beatsToMs (path, first) }, { end, beatsToMs (path, end) } };
    else if (out.size() == 1)
    {
        const double t = out[0].beats > (first + end) * 0.5 ? first : end;
        insertMarker (out, { t, beatsToMs (path, t) });
    }
    else
    {
        if (out[0].beats - first > 2.0 * bar) out.insert (out.begin(), { first, beatsToMs (path, first) });
        if (end - out.back().beats > 2.0 * bar) out.push_back ({ end, beatsToMs (path, end) });
    }
    return out;
}
// How much the last four bars repeat at one and at two bars' distance: 1 to 2.
double repetitionScore (double bar, const std::vector<Note>& notes)
{
    const double threshold = notes.back().beat - 4.0 * bar;
    std::map<int, std::set<int>> at;
    for (const auto& n : notes) if (n.beat >= threshold) at[beatPosition (n.beat)].insert (n.pitch);
    double best = 0.0;
    if (at.empty()) return 1.0;
    const int first = at.begin()->first, last = at.rbegin()->first;
    static const std::set<int> none;
    const auto pitchesAt = [&] (int t) -> const std::set<int>& { const auto found = at.find (t); return found == at.end() ? none : found->second; };
    for (const double lagBars : { 1.0, 2.0 })
    {
        const double lag = lagBars * bar;
        double s = 0.0;
        if (beatPosition (first / 12.0 + lag) <= last)
        {
            long total = 0, smaller = 0, shared = 0;
            for (int t = first;;)
            {
                const auto& a = pitchesAt (t);
                const auto& c = pitchesAt (beatPosition (t / 12.0 + lag));
                total += (long) std::max (a.size(), c.size());
                smaller += (long) std::min (a.size(), c.size());
                for (int p : a) shared += (long) c.count (p);
                t = nextPosition (t);
                if (! (beatPosition (t / 12.0 + lag) <= last)) break;
            }
            s = total == 0 ? 0.0 : (double) (smaller + shared) * 0.5 / (double) total;
        }
        const double coverage = std::min (std::floor ((last / 12.0 - first / 12.0) + 1.0 - lag) / (bar + bar), 1.0);
        best = std::max (best, s * coverage);
    }
    return 1.0 + best;
}

bool near (double a, double b) noexcept { return std::abs (a - b) < b * 0.085; }
double prior (double tempo, double context, bool hasClips) noexcept
{
    if (hasClips) return std::abs (tempo - context) < context * 0.085 ? 1.4 : 1.0;
    const double sigma = context * 0.25 / 1.1774100225154747;
    const double z = (tempo - context) / sigma;
    return std::exp (z * z * -0.5) + 1.0;
}
struct Candidate { std::vector<Marker> markers; double tempo, score; };
// Every interpretation kept, by tempo: the candidate tempos refined, fitted, simplified and scored.
std::vector<Candidate> analyse (const std::vector<Note>& notes, double bar, const std::optional<Context>& context)
{
    const auto w = weightsOf (notes);
    const auto endSection = findEndSection (notes);
    const double fallback = context ? context->tempo : 120.0;
    if (notes.size() - endSection == 1)
    {
        auto [markers, tempo] = singleNotePath (fallback, bar, notes, endSection, w);
        return { { std::move (markers), tempo, 0.0 } };
    }
    double low = 78.0, high = 164.0;
    if (context)
    {
        double r = (context->tempo - 60.0) / 60.0;
        r = (1.5 - r * 1.5) + r * 1.333;
        r = r < 1.333 ? 1.333 : (r > 1.5 ? 1.5 : r);
        low = context->tempo * (r * 0.5); high = context->tempo * r;
    }
    const auto interpretations = interpretationsOf (notes, low, high);
    auto candidates = heaviest (clustersOf (notes, interpretations, w), 4);
    if (context)
    {
        const double t = context->tempo;
        if (context->mode == Mode::setHasClips && std::none_of (candidates.begin(), candidates.end(), [t] (const Cluster& c) { return near (c.tempo, t); }))
            candidates.push_back ({ t, 1.0 });
        else if (context->mode == Mode::keep)
        {
            std::erase_if (candidates, [t] (const Cluster& c) { return ! near (c.tempo, t); });
            if (candidates.empty()) candidates = { { t, 1.0 }, { t + t * 0.045, 1.0 }, { t - t * 0.045, 1.0 }, { t + t * 0.09, 1.0 }, { t - t * 0.09, 1.0 } };
        }
    }
    std::vector<Candidate> results;
    for (const auto& candidate : candidates)
    {
        const double tempo0 = refined (candidate.tempo, bar, notes, interpretations);
        const auto path = findPath (tempo0, bar, notes, endSection, w);
        const auto placed = converted (notes, path.markers);
        const auto markers = simplified (path.markers, path.start, bar, placed);
        if (! valid (markers)) continue;
        const double repetition = repetitionScore (bar, placed);
        const double phraseBeat = msToBeats (markers, notes[endSection].ms);
        const double lastBeat = markers.back().beats;
        const double target = std::max (lastBeat - 4.0 * bar, std::min (lastBeat - bar, phraseBeat));
        size_t from = markers.size() - 2;
        for (size_t k = markers.size() - 2; k-- > 0;)
            if (std::abs (markers[k].beats - target) < std::abs (markers[from].beats - target)) from = k;
        double tempo = roundTo ((lastBeat - markers[from].beats) / ((markers.back().ms - markers[from].ms) / 60000.0), 0.5);
        double score = repetition * path.score;
        if (context)
        {
            score = prior (tempo, context->tempo, context->mode == Mode::setHasClips) * repetition * path.score;
            if (context->mode == Mode::keep || (context->mode == Mode::setHasClips && near (tempo, context->tempo))) tempo = context->tempo;
        }
        results.push_back ({ markers, tempo, score });
    }
    std::stable_sort (results.begin(), results.end(), [] (const Candidate& a, const Candidate& b) { return -a.score < -b.score; });
    std::vector<Candidate> kept;
    for (const auto& r : results)
        if (std::all_of (kept.begin(), kept.end(), [&] (const Candidate& k) { return std::abs (r.tempo - k.tempo) >= 5.0; })) kept.push_back (r);
    if (kept.empty()) return { { constantPath (fallback, notes[0].ms).markers, fallback, 0.0 } };
    std::stable_sort (kept.begin(), kept.end(), [] (const Candidate& a, const Candidate& b) { return a.tempo < b.tempo; });
    return kept;
}

double floorBar (double x, double bar) noexcept { const double f = bar * roundTo (x / bar, 1.0); return f > x ? f - bar : f; }
double ceilBar (double x, double bar) noexcept { const double c = bar * roundTo (x / bar, 1.0); return c < x ? c + bar : c; }
// The loop: from the bar of the last phrase's first played note to the bar after it ends, one to eight bars; longer
// phrases loop their last 8, 4, 6, 5 or 7 bars, whichever start cuts no note.
std::pair<double, double> loopOf (const std::vector<Note>& notes, double bar)
{
    const auto endSection = findEndSection (notes);
    std::vector<const Note*> played;
    for (size_t i = endSection; i < notes.size(); ++i) if (notes[i].user) played.push_back (&notes[i]);
    double s = onGrid (played.front()->beat);
    const double lastStart = onGrid (played.back()->beat);
    const Note* longest = played.front();
    for (size_t i = 1; i < played.size(); ++i)
        if (longest->beat + longest->durationBeats < played[i]->beat + played[i]->durationBeats) longest = played[i];
    const double lastEnd = onGrid (longest->beat + longest->durationBeats);
    double e = lastStart;
    if (lastEnd > lastStart + 1.0 || (long long) std::ceil ((lastStart - s) / bar) % 2 == 1 || (long long) std::ceil ((lastEnd - s) / bar) % 2 == 0) e = lastEnd;
    s = floorBar (s, bar); e = ceilBar (e, bar);
    double start = s;
    if (e - s > 8.0 * bar)
    {
        const auto free = [&] (double c)
        {
            const int t = beatPosition (c);
            const double low = previousPosition (t) / 12.0, high = nextPosition (t) / 12.0;
            for (size_t i = endSection; i < notes.size(); ++i)
                if (notes[i].beat < high && onGrid (notes[i].beat + notes[i].durationBeats) > low) return false;
            return true;
        };
        std::optional<double> chosen;
        for (const double k : { 8.0, 4.0, 6.0, 5.0, 7.0 })
            if (const double c = e - k * bar; s <= c && free (c)) { chosen = c; break; }
        start = chosen ? *chosen : std::max (s, e - 8.0 * bar);
    }
    const double length = std::min (std::max (e - start, bar), 8.0 * bar);
    return { start, start + length };
}
// Per pitch, a note starting less than 0.085 beat before the loop's start or end moves onto it, ending by the next
// note of its pitch.
void snapToLoop (std::vector<Note>& notes, double loopStart, double loopEnd)
{
    std::map<int, std::vector<Note*>> byPitch;
    for (auto& n : notes) byPitch[n.pitch].push_back (&n);
    for (const double edge : { loopStart, loopEnd })
        for (auto& [pitch, list] : byPitch)
        {
            Note* before = nullptr;
            for (auto* n : list) if (n->beat <= edge) before = n;
            if (before == nullptr || ! (edge - before->beat < 0.085)) continue;
            before->beat = edge;
            for (auto* n : list)
                if (n->beat > edge) { before->durationBeats = std::min (before->durationBeats, engineRound (n->beat - edge)); break; }
        }
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat != b.beat ? a.beat < b.beat : a.pitch < b.pitch; });
}
}

double roundTo (double x, double quantum) noexcept
{
    if (quantum == 0.0) return x;
    quantum = std::abs (quantum);
    return quantum * (x >= 0 ? std::floor (x / quantum + 0.5) : std::ceil (x / quantum - 0.5));
}
double engineRound (double x) noexcept
{
    const double s = x < 0 ? -1.0 : 1.0;
    return s * std::trunc (std::abs (x + s * tick * 0.5) * 3843840.0) / 3843840.0;
}
int beatPosition (double x) noexcept
{
    int t = x >= 0 ? (int) std::floor (x * 12.0 + 0.5) : -(int) std::floor (-x * 12.0 + 0.5);
    if (! allowed (t)) t = (int) (roundTo (x, 0.5) * 12.0);
    return t;
}
std::vector<Note> trimHistory (std::vector<Note> notes, std::optional<double> pressMs)
{
    std::erase_if (notes, [&] (const Note& n) { return (pressMs && ! (n.ms < *pressMs)) || ! (std::abs (n.durationMs) >= 1e-6); });
    size_t k = 0;
    for (size_t i = notes.size(); i-- > 1;)
        if (notes[i].ms - (notes[i - 1].ms + notes[i - 1].durationMs) > 20000.0) { k = i; break; }
    const auto firstPlayed = std::find_if (notes.begin() + (std::ptrdiff_t) k, notes.end(), [] (const Note& n) { return n.user; });
    if (firstPlayed != notes.end())
    {
        const double from = firstPlayed->ms - 30000.0;
        while (k < notes.size() && notes[k].ms < from) ++k;
    }
    return { notes.begin() + (std::ptrdiff_t) k, notes.end() };
}
Capture capture (std::vector<Note> notes, double bar, std::optional<Context> context)
{
    Capture result;
    if (notes.empty() || ! (bar > 0)) return result;
    if (std::none_of (notes.begin(), notes.end(), [] (const Note& n) { return n.user; })) for (auto& n : notes) n.user = true;
    const auto candidates = analyse (notes, bar, context);
    const auto* best = &candidates.front();
    for (const auto& c : candidates) if (best->score < c.score) best = &c;
    auto placed = converted (notes, best->markers);
    const auto [loopStart, loopEnd] = loopOf (placed, bar);
    snapToLoop (placed, loopStart, loopEnd);
    const double offset = std::floor (placed.front().beat / bar) * bar;
    result.tempo = best->tempo;
    for (auto n : placed)
    {
        n.beat = engineRound (n.beat - offset);
        result.notes.push_back (n);
    }
    result.loopStart = loopStart - offset; result.loopEnd = loopEnd - offset;
    return result;
}
}
