#include "Tuner.h"
#include <cmath>
#include <vector>

namespace halftrack::tuner
{
std::optional<Session::TunerReading> detect (const float* x, int count, double rate, double reference)
{
    if (x == nullptr || count < 256 || ! (rate > 0) || ! (reference > 0)) return std::nullopt;
    double energy = 0;
    for (int n = 0; n < count; ++n) energy += (double) x[n] * x[n];
    if (! std::isfinite (energy) || energy / count < 1e-8) return std::nullopt;
    const int window = count / 2;
    const int shortest = std::max (2, (int) std::floor (rate / maximumHz));
    const int longest = std::min (window - 2, (int) std::ceil (rate / minimumHz));
    if (longest <= shortest + 2) return std::nullopt;
    // The difference function, then its cumulative mean normalization.
    std::vector<double> d ((size_t) longest + 2, 0.0), normalized ((size_t) longest + 2, 1.0);
    for (int tau = 1; tau <= longest + 1; ++tau)
    {
        double sum = 0;
        for (int j = 0; j < window; ++j) { const double delta = (double) x[j] - x[j + tau]; sum += delta * delta; }
        d[(size_t) tau] = sum;
    }
    double running = 0;
    for (int tau = 1; tau <= longest + 1; ++tau)
    {
        running += d[(size_t) tau];
        normalized[(size_t) tau] = running > 0 ? d[(size_t) tau] * tau / running : 1;
    }
    // The first dip under the threshold, followed down to its minimum. The search starts below the
    // shortest period in range so a higher pitch is found, and rejected, rather than a subharmonic.
    constexpr double threshold = .1;
    int best = -1;
    for (int tau = 2; tau <= longest; ++tau)
        if (normalized[(size_t) tau] < threshold)
        {
            while (tau + 1 <= longest && normalized[(size_t) tau + 1] < normalized[(size_t) tau]) ++tau;
            best = tau; break;
        }
    if (best < 0) return std::nullopt;
    // A first estimate from the dip, refined by a parabola through its neighbours.
    const auto parabola = [] (double a, double b, double c) { const double curvature = a - 2 * b + c; return curvature > 1e-12 ? std::clamp (.5 * (a - c) / curvature, -.5, .5) : 0.0; };
    const double estimate = best + parabola (normalized[(size_t) best - 1], normalized[(size_t) best], normalized[(size_t) best + 1]);
    // Then refine at a multiple of the period near 400 samples, where a fraction of a sample is a far
    // smaller fraction of the period: the raw difference function's minimum within a quarter period
    // of that multiple.
    const int multiple = std::max (1, std::min ((longest - 1) / best, (int) std::lround (400.0 / estimate)));
    const int reach = std::max (1, (int) std::floor (estimate / 4));
    const int centre = (int) std::lround (estimate * multiple);
    int lag = std::clamp (centre, 1, longest);
    for (int t = std::max (1, centre - reach); t <= std::min (longest, centre + reach); ++t)
        if (d[(size_t) t] < d[(size_t) lag]) lag = t;
    const double period = (lag + parabola (d[(size_t) lag - 1], d[(size_t) lag], d[(size_t) lag + 1])) / multiple;
    const double frequency = rate / period;
    if (frequency < minimumHz || frequency > maximumHz) return std::nullopt;
    const double semitones = 12 * std::log2 (frequency / reference) + 69;
    const int note = (int) std::lround (semitones);
    return Session::TunerReading { frequency, note, (semitones - note) * 100 };
}

juce::String noteName (int note, bool flats)
{
    static const char* sharpNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flatNames[] { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const int pitchClass = ((note % 12) + 12) % 12;
    const int octave = (note - pitchClass) / 12 - 1;
    return juce::String (flats ? flatNames[pitchClass] : sharpNames[pitchClass]) + juce::String (octave);
}
}
