#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// The reference's level scales, from a static reading of its code (2026-10-14): 101 levels in
// decibels, descending from an upper to a lower bound, built in single precision as it builds them,
// and the amplitudes they give, ascending. A position from 0 to 1 interpolates linearly between
// adjacent amplitudes. Scale 3, from -70 to +24 dB, is the one its clip gain uses, and its launch
// velocity scales a clip's gain along it. Halftrack's own implementation; the reference's math
// library may round its powers of ten differently in the last place.
namespace halftrack::levelscale
{
struct Scale
{
    std::array<float, 101> amplitude {}; // Ascending: index 0 is the lower bound's.
};

// Levels this close to zero are stored as zero, as the reference's builder does.
inline float snap (float value) { return std::abs (value) < 0.00009999999747378752f ? 0.0f : value; }

inline Scale make (int lower, int upper, float decrement, float increment)
{
    std::array<float, 101> level {};
    auto value = static_cast<float> (upper);
    // Sixty fixed steps down from the upper bound, then steps growing by the increment.
    for (int i = 0; i < 60; ++i) { level[(size_t) i] = value; value = snap (value - decrement); }
    level[60] = value;
    auto step = decrement;
    for (int i = 61; i <= 96; ++i) { step = step + increment; value = snap (value - step); level[(size_t) i] = value; }
    // The last four entries are corrected to land on the lower bound.
    const auto s = step + increment, floor = static_cast<float> (lower), q = level[96] - s;
    const auto c = std::fma (3.0f, s, floor - q) / 6.0f;
    level[97] = snap (q);
    level[98] = snap (level[97] - (s - c));
    level[99] = snap (level[98] - (s - 2.0f * c));
    level[100] = floor;
    Scale scale;
    for (size_t i = 0; i < 101; ++i) scale.amplitude[100 - i] = std::pow (10.0f, level[i] * 0.05000000074505806f);
    return scale;
}

// Scale 3: -70 to +24 dB, the reference's clip gain.
inline const Scale& clipGain()
{
    static const auto scale = make (-70, 24, 0.4000000059604645f, 0.03999999910593033f);
    return scale;
}

// The amplitude at a position from 0 to 1.
inline float amplitudeAt (const Scale& scale, double position)
{
    if (position <= 0) return scale.amplitude[0];
    if (position >= 1) return scale.amplitude[100];
    const auto index = static_cast<int> (position * 100.0);
    auto fraction = position * 100.0 - index;
    fraction = fraction <= 7.62939453125e-06 ? 0.0 : fraction >= 0.9999923706054688 ? 1.0 : fraction;
    return static_cast<float> (fraction * scale.amplitude[(size_t) index + 1] + (1.0 - fraction) * scale.amplitude[(size_t) index]);
}

// The position of an amplitude: the inverse of amplitudeAt along the same straight segments.
inline double positionOf (const Scale& scale, float amplitude)
{
    if (amplitude <= scale.amplitude[0]) return 0;
    if (amplitude >= scale.amplitude[100]) return 1;
    const auto above = std::upper_bound (scale.amplitude.begin(), scale.amplitude.end(), amplitude);
    const auto index = static_cast<size_t> (above - scale.amplitude.begin()) - 1;
    const double low = scale.amplitude[index], high = scale.amplitude[index + 1];
    return (static_cast<double> (index) + (high > low ? (amplitude - low) / (high - low) : 0.0)) / 100.0;
}

// The reference's launch velocity (static reading of its code): a launch's velocity, from 0 to 1
// (a note's velocity over 127, otherwise 1), blends with the clip's amount into a factor that
// scales a clip's gain position along its scale, or a MIDI clip's note velocities.
inline float launchFactor (float velocity, float amount) { return velocity * amount + (1.0f - amount); }
// The gain to multiply an audio clip's sound by at launch, for a clip gain in amplitude.
// A clip at or below the scale's bottom is left as it is.
inline float launchGain (float clipGain, float factor)
{
    const auto& scale = levelscale::clipGain();
    if (clipGain <= scale.amplitude[0] || factor >= 1.0f) return 1.0f;
    return amplitudeAt (scale, positionOf (scale, clipGain) * factor) / clipGain;
}
}
