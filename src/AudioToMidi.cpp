#include "AudioToMidi.h"
#include "Tuner.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace halftrack::audiotomidi
{
namespace
{
constexpr double pi = 3.14159265358979323846;
struct Part { long long start = 0, end = 0; float peak = 0; };

// The parts between transients, the first from the audio's start when it sounds before the first transient.
// Transients closer than 50 ms count as one onset, and one where the sound grows by less than 40% (the beating of
// tones held together, say) as none.
std::vector<Part> partsOf (const float* x, long long count, double rate, std::vector<long long> transients)
{
    std::sort (transients.begin(), transients.end());
    std::vector<long long> starts;
    const auto first = transients.empty() ? count : std::min (count, transients.front());
    float before = 0;
    for (long long i = 0; i < first; ++i) before = std::max (before, std::abs (x[i]));
    if (before > 0.01f && first > (long long) (0.02 * rate)) starts.push_back (0);
    const auto gap = (long long) (0.05 * rate), span = (long long) (0.02 * rate);
    const auto rms = [&] (long long from, long long to)
    {
        double sum = 0; from = std::max (0LL, from); to = std::min (count, to);
        for (auto i = from; i < to; ++i) sum += (double) x[i] * x[i];
        return to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
    };
    for (const auto t : transients)
    {
        if (t < 0 || t >= count || (! starts.empty() && t - starts.back() < gap)) continue;
        const auto earlier = rms (t - span, t);
        if (earlier < 0.002 || rms (t, t + span) > 1.4 * earlier) starts.push_back (t);
    }
    std::vector<Part> parts;
    for (size_t i = 0; i < starts.size(); ++i)
    {
        Part p { starts[i], i + 1 < starts.size() ? starts[i + 1] : count };
        const auto attack = std::min (p.end, p.start + (long long) (0.05 * rate));
        for (auto k = p.start; k < attack; ++k) p.peak = std::max (p.peak, std::abs (x[k]));
        parts.push_back (p);
    }
    return parts;
}
// Louder onsets play louder: from a fifth for the quietest to full for the loudest.
float velocityOf (float peak, float loudest) { return loudest > 0 ? juce::jlimit (0.2f, 1.0f, 0.2f + 0.8f * peak / loudest) : 1.0f; }
// Where a part's sound ends: after the last 10 ms block reaching a thirtieth (-30 dB) of its attack's peak.
long long soundEnd (const float* x, const Part& p, double rate)
{
    const auto block = std::max<long long> (1, (long long) (0.01 * rate));
    auto end = std::min (p.end, p.start + block);
    for (auto k = p.start; k < p.end; k += block)
    {
        float m = 0;
        for (auto i = k; i < std::min (p.end, k + block); ++i) m = std::max (m, std::abs (x[i]));
        if (m > p.peak / 30) end = std::min (p.end, k + block);
    }
    return end;
}
// A stretch of a part's steadier middle, after its attack, at most `frames` long.
std::pair<long long, long long> windowOf (const Part& p, double rate, long long frames)
{
    const auto from = p.start + std::min ((long long) (0.03 * rate), (p.end - p.start) / 5);
    return { from, std::min (p.end, from + frames) };
}
// The magnitude spectrum of a Hann-windowed stretch, zero-padded to 2^order frames: bins 0 to 2^(order-1).
std::vector<float> spectrum (const float* x, long long from, long long to, int order)
{
    const int size = 1 << order;
    std::vector<float> data ((size_t) size * 2, 0.0f);
    const auto n = (int) std::min<long long> (size, to - from);
    for (int i = 0; i < n; ++i) data[(size_t) i] = x[from + i] * (float) (0.5 - 0.5 * std::cos (2 * pi * i / std::max (1, n - 1)));
    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (data.data());
    data.resize ((size_t) size / 2 + 1);
    return data;
}
double midiHz (double note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

std::vector<Note> melody (const float* x, double rate, const std::vector<Part>& parts, float loudest)
{
    std::vector<Note> notes;
    for (const auto& p : parts)
    {
        const auto [from, to] = windowOf (p, rate, tuner::analysisFrames);
        if (p.peak < 0.005f || to - from < (long long) (0.02 * rate)) continue;
        if (const auto reading = tuner::detect (x + from, (int) (to - from), rate, 440.0))
            notes.push_back ({ (double) p.start / rate, (double) (soundEnd (x, p, rate) - p.start) / rate, juce::jlimit (0, 127, reading->note), velocityOf (p.peak, loudest) });
    }
    return notes;
}

// Each part's pitches, strongest first: the pitch whose first eight harmonics, weighted down as they rise, sum
// highest, with its own fundamental sounding (a pitch an octave below the notes would otherwise collect their
// harmonics), whose harmonics are then taken out of the spectrum; at most six, each at least 15% of the first.
std::vector<Note> harmony (const float* x, double rate, const std::vector<Part>& parts, float loudest)
{
    constexpr int order = 14, lowest = 28, highest = 100, harmonics = 8, most = 6;
    const double binHz = rate / (1 << order);
    std::vector<Note> notes;
    for (const auto& p : parts)
    {
        const auto [from, to] = windowOf (p, rate, 1 << order);
        if (p.peak < 0.005f || to - from < (long long) (0.04 * rate)) continue;
        auto magnitude = spectrum (x, from, to, order);
        const auto at = [&] (double hz)
        {
            const auto bin = (int) std::round (hz / binHz);
            float m = 0;
            for (int k = bin - 1; k <= bin + 1; ++k) if (k > 0 && k < (int) magnitude.size()) m = std::max (m, magnitude[(size_t) k]);
            return (double) m;
        };
        double first = 0;
        std::vector<int> chosen;
        for (int round = 0; round < most; ++round)
        {
            int best = -1;
            double bestSalience = 0;
            for (int note = lowest; note <= highest; ++note)
            {
                const auto f0 = midiHz (note);
                double salience = 0, strongest = 0;
                for (int h = 1; h <= harmonics && f0 * h < rate / 2; ++h)
                {
                    const auto m = at (f0 * h);
                    salience += m / std::sqrt ((double) h);
                    if (h > 1) strongest = std::max (strongest, m);
                }
                if (at (f0) < 0.2 * strongest) continue;
                if (salience > bestSalience) { bestSalience = salience; best = note; }
            }
            if (best < 0 || bestSalience <= 0 || (round > 0 && bestSalience < 0.15 * first)) break;
            if (round == 0) first = bestSalience;
            chosen.push_back (best);
            for (int h = 1; h <= harmonics; ++h)
            {
                const auto bin = (int) std::round (midiHz (best) * h / binHz);
                for (int k = bin - 2; k <= bin + 2; ++k) if (k > 0 && k < (int) magnitude.size()) magnitude[(size_t) k] = 0;
            }
        }
        std::sort (chosen.begin(), chosen.end());
        const auto end = soundEnd (x, p, rate);
        for (const auto note : chosen) notes.push_back ({ (double) p.start / rate, (double) (end - p.start) / rate, note, velocityOf (p.peak, loudest) });
    }
    return notes;
}

// What each onset adds over its first 40 ms, beyond what the 40 ms before it held (an earlier hit's tail): a kick
// when most of it lies below 150 Hz, a hi-hat when over 40% lies above 6 kHz with almost none below 150 Hz, and
// otherwise a snare.
std::vector<Note> drums (const float* x, double rate, const std::vector<Part>& parts, float loudest)
{
    constexpr int order = 12;
    const double binHz = rate / (1 << order);
    const auto window = (long long) (0.04 * rate);
    std::vector<Note> notes;
    for (const auto& p : parts)
    {
        const auto to = std::min (p.end, p.start + window);
        if (p.peak < 0.01f || to - p.start < 32) continue;
        const auto added = spectrum (x, p.start, to, order), held = spectrum (x, std::max (0LL, p.start - (to - p.start)), p.start, order);
        double low = 0, high = 0, total = 0;
        for (size_t k = 1; k < added.size(); ++k)
        {
            const auto hz = (double) k * binHz, energy = std::max (0.0, (double) added[k] * added[k] - (double) held[k] * held[k]);
            total += energy;
            if (hz < 150) low += energy; else if (hz > 6000) high += energy;
        }
        if (total <= 0) continue;
        const int note = low / total > 0.5 ? kickNote : (high / total > 0.4 && low / total < 0.1) ? hihatNote : snareNote;
        notes.push_back ({ (double) p.start / rate, std::min (0.1, (double) (p.end - p.start) / rate), note, velocityOf (p.peak, loudest) });
    }
    return notes;
}
}

std::vector<Note> convert (const float* samples, int count, double rate, Kind kind, std::vector<long long> transients)
{
    if (samples == nullptr || count <= 0 || rate <= 0) return {};
    const auto parts = partsOf (samples, count, rate, std::move (transients));
    float loudest = 0;
    for (const auto& p : parts) loudest = std::max (loudest, p.peak);
    auto notes = kind == Kind::melody ? melody (samples, rate, parts, loudest) : kind == Kind::harmony ? harmony (samples, rate, parts, loudest) : drums (samples, rate, parts, loudest);
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    return notes;
}

juce::AudioBuffer<float> drumSound (int note, double rate)
{
    const auto seconds = note == kickNote ? 0.5 : note == snareNote ? 0.35 : 0.15;
    juce::AudioBuffer<float> out (1, std::max (1, (int) (seconds * rate)));
    juce::Random random (note); // The same sound each time.
    // A first-order high pass at a cutoff, kept between samples.
    struct HighPass { double a = 0, x = 0, y = 0; double operator() (double in) { y = a * (y + in - x); x = in; return y; } };
    const auto highPass = [rate] (double cutoff) { const auto rc = 1 / (2 * pi * cutoff); return HighPass { rc / (rc + 1 / rate) }; };
    auto noiseFilter = highPass (note == snareNote ? 1000.0 : 7000.0), second = highPass (7000.0);
    double phase = 0;
    float peak = 0;
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        const double t = i / rate, noise = random.nextFloat() * 2 - 1;
        double v = 0;
        if (note == kickNote)
        {
            phase += 2 * pi * (45 + 75 * std::exp (-t / 0.04)) / rate; // A falling sine with a click at its start.
            v = std::sin (phase) * std::exp (-t / 0.18) + (t < 0.002 ? 0.3 * noise * (1 - t / 0.002) : 0.0);
        }
        else if (note == snareNote)
        {
            phase += 2 * pi * 185 / rate; // A tone under bright noise.
            v = 0.5 * std::sin (phase) * std::exp (-t / 0.08) + 0.6 * noiseFilter (noise) * std::exp (-t / 0.12);
        }
        else v = second (noiseFilter (noise)) * std::exp (-t / 0.03); // High noise, short.
        out.setSample (0, i, (float) v);
        peak = std::max (peak, std::abs ((float) v));
    }
    if (peak > 0) out.applyGain (0.7f / peak);
    return out;
}
}
