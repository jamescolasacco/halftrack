#include "MetronomeSounds.h"
#include <cmath>
#include <vector>

namespace halftrack::metronome
{
namespace
{
// A partial of a tick: a multiple of the fundamental, its level and its decay time constant.
struct Partial { double ratio, level, decaySeconds; };
struct Design
{
    double hz;
    float peak;
    double seconds;
    std::vector<Partial> partials;
    double noise, noiseDecaySeconds; // A struck noise burst at the onset.
};
Design design (Session::MetronomeSound sound, bool accent)
{
    switch (sound)
    {
        // A pure tone blip: 1.1 kHz, the accent 1.58 kHz, gone (-60 dB) within 20 ms.
        case Session::MetronomeSound::classic:
            return { accent ? 1580.0 : 1100.0, accent ? .31f : .224f, .04, { { 1, 1, .0029 } }, 0, 0 };
        // A bright, very short click with a little noise.
        case Session::MetronomeSound::click:
            return { accent ? 4000.0 : 3000.0, accent ? .5f : .45f, .02, { { 1, 1, .0012 }, { 2.3, .4, .0006 } }, .3, .0004 };
        // A woodblock: three inharmonic modes that ring for a few hundred milliseconds.
        case Session::MetronomeSound::wood:
            return { accent ? 1550.0 : 1070.0, accent ? .9f : .85f, .3, { { 1, 1, .03 }, { 2.57, .35, .012 }, { 4.17, .15, .006 } }, .2, .001 };
    }
    return { 1100, .224f, .04, { { 1, 1, .0029 } }, 0, 0 };
}
}

double tickFrequency (Session::MetronomeSound sound, bool accent) { return design (sound, accent).hz; }
float tickPeak (Session::MetronomeSound sound, bool accent) { return design (sound, accent).peak; }

juce::AudioBuffer<float> tick (Session::MetronomeSound sound, bool accent, double sampleRate)
{
    const auto d = design (sound, accent);
    const auto frames = juce::jmax (1, (int) std::ceil (d.seconds * sampleRate));
    juce::AudioBuffer<float> out (1, frames);
    juce::Random random (accent ? 7 : 3); // Fixed, so a tick is the same every time.
    constexpr double attackSeconds = .0003, twoPi = juce::MathConstants<double>::twoPi;
    double loudest = 0;
    for (int n = 0; n < frames; ++n)
    {
        const double t = n / sampleRate;
        double v = 0;
        for (const auto& p : d.partials) v += p.level * std::exp (-t / p.decaySeconds) * std::sin (twoPi * d.hz * p.ratio * t);
        if (d.noise > 0) v += d.noise * std::exp (-t / d.noiseDecaySeconds) * (random.nextDouble() * 2 - 1);
        v *= 1 - std::exp (-t / attackSeconds);
        // The last 2 ms fall to silence.
        const double tail = (frames - 1 - n) / sampleRate;
        if (tail < .002) v *= tail / .002;
        out.setSample (0, n, (float) v);
        loudest = std::max (loudest, std::abs (v));
    }
    if (loudest > 0) out.applyGain ((float) (d.peak / loudest));
    return out;
}
}
