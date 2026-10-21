#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>

// Independent oracles for the extended native device set. Expected values are
// derived from closed forms (memoryless curves, analytic filter magnitudes,
// envelope recurrences) rather than by calling the processors under test.
namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
using Settings = Session::EffectSettings;
constexpr double pi = 3.14159265358979323846;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS native effects: " << text << std::endl; }
juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& source, const Settings& s, double rate = 48000)
{
    auto out = source; NativeEffectDSP dsp; dsp.prepare (rate, s.kind); dsp.configure (s);
    dsp.processSamples (out.getWritePointer (0), out.getWritePointer (1), out.getNumSamples()); return out;
}
Settings with (Kind kind, std::initializer_list<std::pair<const char*, float>> values)
{
    auto s = defaultEffectSettings (kind);
    for (const auto& [key, value] : values)
    {
        const auto index = effectParameterIndex (kind, key);
        if (index < 0) throw std::runtime_error (std::string ("unknown test parameter ") + key);
        s.values[(size_t) index] = value;
    }
    if (! validEffectSettings (s)) throw std::runtime_error ("invalid test settings");
    return s;
}
juce::AudioBuffer<float> sine (double frequency, double amplitude, int frames, double rate = 48000, double rightScale = 1)
{
    juce::AudioBuffer<float> a (2, frames);
    for (int n = 0; n < frames; ++n)
    {
        const auto v = amplitude * std::sin (2 * pi * frequency * n / rate);
        a.setSample (0, n, (float) v); a.setSample (1, n, (float) (v * rightScale));
    }
    return a;
}
// Single-bin DFT amplitude over [start, end), using an integer number of cycles.
double amplitudeAt (const juce::AudioBuffer<float>& a, int channel, double frequency, int start, int end, double rate = 48000)
{
    std::complex<double> sum;
    for (int n = start; n < end; ++n) sum += (double) a.getSample (channel, n) * std::polar (1.0, -2 * pi * frequency * n / rate);
    return 2 * std::abs (sum) / (end - start);
}
double db (double v) { return 20 * std::log10 (std::max (v, 1e-20)); }

double curve (int type, double x)
{
    if (type == 0) return std::tanh (x);
    if (type == 1) return std::abs (x) >= 1 ? (x < 0 ? -1 : 1) : std::sin (x * pi / 2);
    if (type == 2) return x / std::sqrt (1 + x * x);
    if (type == 3) return std::abs (x) >= 1 ? (x < 0 ? -1 : 1) : x * (3 - x * x) / 2;
    if (type == 4) return std::sin (x * pi / 2);
    return std::max (-1.0, std::min (1.0, x));
}
void saturatorTests()
{
    juce::AudioBuffer<float> ramp (2, 4001);
    for (int n = 0; n < ramp.getNumSamples(); ++n) { ramp.setSample (0, n, (float) ((n - 2000) / 1600.0)); ramp.setSample (1, n, (float) ((2000 - n) / 3200.0)); }
    for (int type = 0; type < 6; ++type) for (float drive : { -12.0f, 0.0f, 12.0f, 24.0f }) for (float output : { -6.0f, 0.0f })
    {
        const auto out = process (ramp, with (Kind::saturator, { { "curve", (float) type }, { "driveDb", drive }, { "outputDb", output } }));
        double error = 0;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < ramp.getNumSamples(); ++n)
            error = std::max (error, std::abs (out.getSample (c, n) - std::pow (10.0, output / 20.0) * curve (type, ramp.getSample (c, n) * std::pow (10.0, drive / 20.0))));
        check (error < 2e-7, "memoryless curve " + juce::String (type) + " drive " + juce::String (drive) + " output " + juce::String (output));
    }
    auto clipped = process (ramp, with (Kind::saturator, { { "curve", 5 }, { "driveDb", 36 }, { "outputDb", 36 }, { "softClip", 1 } }));
    check (clipped.getMagnitude (0, clipped.getNumSamples()) <= 1, "soft output clip bounds +72 dB of gain at full scale");
    auto gentle = process (ramp, with (Kind::saturator, { { "curve", 5 }, { "softClip", 1 } }));
    bool linear = true;
    for (int n = 0; n < ramp.getNumSamples(); ++n) if (std::abs (ramp.getSample (0, n)) <= .5f) linear = linear && gentle.getSample (0, n) == ramp.getSample (0, n);
    check (linear, "soft clip is exact below its half-scale knee");
    juce::AudioBuffer<float> offset (2, 96000);
    for (int n = 0; n < offset.getNumSamples(); ++n) { offset.setSample (0, n, .3f); offset.setSample (1, n, (float) (.3 + .1 * std::sin (2 * pi * 440 * n / 48000))); }
    const auto filtered = process (offset, with (Kind::saturator, { { "dcFilter", 1 } }));
    check (std::abs (filtered.getSample (0, 95999)) < 1e-3, "DC filter removes a constant shaped offset");
    check (std::abs (amplitudeAt (filtered, 1, 440, 48000, 96000) - amplitudeAt (process (offset, with (Kind::saturator, {})), 1, 440, 48000, 96000)) < 1e-3,
           "DC filter leaves an audio-band component unchanged");
    const auto wet = process (ramp, with (Kind::saturator, { { "driveDb", 12 } })), half = process (ramp, with (Kind::saturator, { { "driveDb", 12 }, { "mix", .5f } }));
    double mixError = 0;
    for (int n = 0; n < ramp.getNumSamples(); ++n) mixError = std::max (mixError, std::abs (half.getSample (0, n) - .5 * (wet.getSample (0, n) + ramp.getSample (0, n))));
    check (mixError < 2e-7, "half mix is the exact dry/wet average");
}

double harmonicRatio (const juce::AudioBuffer<float>& a, double fundamental, int first, int last)
{
    double harmonics = 0;
    for (int h = first; h <= last; ++h) harmonics += std::pow (amplitudeAt (a, 0, fundamental * h, 24000, 48000), 2);
    return std::sqrt (harmonics) / amplitudeAt (a, 0, fundamental, 24000, 48000);
}
void overdriveTests()
{
    const auto loud = sine (500, .9, 48000), moderate = sine (500, .2, 48000);
    const auto light = process (moderate, with (Kind::overdrive, { { "drive", .1f }, { "dynamics", 0 }, { "tone", 1 } }));
    const auto heavy = process (moderate, with (Kind::overdrive, { { "drive", .9f }, { "dynamics", 0 }, { "tone", 1 } }));
    const auto full = process (loud, with (Kind::overdrive, { { "drive", 1 }, { "dynamics", 0 }, { "tone", 1 } }));
    check (harmonicRatio (heavy, 500, 2, 9) > 3 * harmonicRatio (light, 500, 2, 9), "more drive produces substantially more harmonic distortion");
    check (amplitudeAt (heavy, 0, 1000, 24000, 48000) / amplitudeAt (heavy, 0, 500, 24000, 48000) > 1e-3, "biased curve produces even harmonics");
    check (full.getMagnitude (0, full.getNumSamples()) < 1.5f, "saturated output stays bounded for a near full-scale input");
    const auto quiet = sine (1000, .01, 48000), offBand = sine (100, .01, 48000);
    const auto narrow = with (Kind::overdrive, { { "frequencyHz", 1000 }, { "widthOctaves", .5f }, { "drive", 0 }, { "dynamics", 0 }, { "tone", 1 } });
    const auto centre = amplitudeAt (process (quiet, narrow), 0, 1000, 24000, 48000), outside = amplitudeAt (process (offBand, narrow), 0, 100, 24000, 48000);
    check (std::abs (db (centre / .01)) < .1, "unity-peak input band passes its centre frequency");
    check (db (outside / .01) < -20, "narrow input band rejects a distant frequency");
    juce::AudioBuffer<float> bright (2, 48000);
    juce::Random random (17);
    for (int n = 0; n < bright.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) bright.setSample (c, n, random.nextFloat() * .2f - .1f);
    const auto dark = process (bright, with (Kind::overdrive, { { "tone", 0 } })), open = process (bright, with (Kind::overdrive, { { "tone", 1 } }));
    check (amplitudeAt (dark, 0, 9000, 0, 48000) < .5 * amplitudeAt (open, 0, 9000, 0, 48000), "lower tone removes high-frequency content");
    const auto restored = process (quiet, with (Kind::overdrive, { { "frequencyHz", 1000 }, { "drive", 1 }, { "dynamics", 1 }, { "tone", 1 } }));
    const auto flattened = process (quiet, with (Kind::overdrive, { { "frequencyHz", 1000 }, { "drive", 1 }, { "dynamics", 0 }, { "tone", 1 } }));
    check (amplitudeAt (restored, 0, 1000, 24000, 48000) < .5 * amplitudeAt (flattened, 0, 1000, 24000, 48000), "full dynamics restores a quiet input's level instead of flattening it");
}

double svfMagnitude (int mode, double frequency, double cutoff, double k, double rate)
{
    const auto omega = std::tan (pi * frequency / rate) / std::tan (pi * cutoff / rate);
    const std::complex<double> d (1 - omega * omega, k * omega);
    const std::complex<double> n = mode == 0 ? std::complex<double> (1, 0) : mode == 1 ? std::complex<double> (-omega * omega, 0)
        : mode == 2 ? std::complex<double> (0, k * omega) : std::complex<double> (1 - omega * omega, 0);
    return std::abs (n / d);
}
void autoFilterTests()
{
    for (int mode = 0; mode < 4; ++mode) for (int steep = 0; steep < 2; ++steep) for (float cutoff : { 500.0f, 2000.0f }) for (float resonance : { 0.0f, .5f })
    {
        const auto settings = with (Kind::autoFilter, { { "filterType", (float) mode }, { "slope", (float) steep }, { "frequencyHz", cutoff }, { "resonance", resonance } });
        const auto k = 1 / (.5 * std::pow (40.0, (double) resonance));
        double worst = 0;
        for (double f : { 100.0, 500.0, 1000.0, 2000.0, 8000.0 })
        {
            const auto out = process (sine (f, .1, 48000), settings);
            auto expected = svfMagnitude (mode, f, cutoff, k, 48000);
            if (steep) expected *= svfMagnitude (mode, f, cutoff, mode < 2 ? std::sqrt (2.0) : k, 48000);
            worst = std::max (worst, std::abs (amplitudeAt (out, 0, f, 24000, 48000) / .1 - expected));
        }
        check (worst < 2e-4, "analytic state-variable magnitude, mode " + juce::String (mode) + " slope " + juce::String (steep)
               + " cutoff " + juce::String (cutoff) + " resonance " + juce::String (resonance));
    }
    check (std::abs (effects::lfoRateHz (true, 1, 3, 120) - 2) < 1e-12 && std::abs (effects::lfoRateHz (true, 1, 5, 90) - .375) < 1e-12
           && effects::lfoRateHz (false, 3.5, 3, 120) == 3.5, "tempo divisions map exactly to LFO rates");
    const auto tone = sine (3000, .1, 96000);
    const auto still = process (tone, with (Kind::autoFilter, { { "frequencyHz", 1000 } }));
    const auto moving = process (tone, with (Kind::autoFilter, { { "frequencyHz", 1000 }, { "lfoAmount", 1 }, { "lfoRateHz", 2 } }));
    double minimum = 1, maximum = 0;
    for (int window = 0; window < 8; ++window)
    {
        const auto a = amplitudeAt (moving, 0, 3000, 48000 + window * 3200, 48000 + (window + 1) * 3200);
        minimum = std::min (minimum, a); maximum = std::max (maximum, a);
    }
    check (maximum > 4 * minimum && std::abs (amplitudeAt (still, 0, 3000, 48000, 51200) - amplitudeAt (still, 0, 3000, 92800, 96000)) < 1e-6,
           "LFO sweeps the cutoff over time while a static filter is stationary");
    const auto stereo = process (tone, with (Kind::autoFilter, { { "frequencyHz", 1000 }, { "lfoAmount", 1 }, { "lfoRateHz", 2 }, { "lfoPhaseDeg", 180 } }));
    check (std::abs (amplitudeAt (stereo, 0, 3000, 54400, 57600) - amplitudeAt (stereo, 1, 3000, 54400, 57600)) > .01, "stereo phase offsets right-channel modulation");
    const auto quiet = sine (3000, .01, 48000), loud = sine (3000, .9, 48000);
    const auto follow = with (Kind::autoFilter, { { "frequencyHz", 300 }, { "envelopeAmount", 1 } });
    check (amplitudeAt (process (loud, follow), 0, 3000, 24000, 48000) / .9 > 10 * amplitudeAt (process (quiet, follow), 0, 3000, 24000, 48000) / .01,
           "positive envelope amount opens the filter for a louder input");
}

double shapeValue (int shape, double p)
{
    p -= std::floor (p);
    switch (shape) { case 0: return std::sin (2 * pi * p); case 1: return p < .25 ? 4 * p : p < .75 ? 2 - 4 * p : 4 * p - 4;
        case 2: return 2 * p - 1; case 3: return 1 - 2 * p; default: return p < .5 ? 1 : -1; }
}
void autoPanTests()
{
    juce::AudioBuffer<float> constant (2, 48000);
    for (int n = 0; n < constant.getNumSamples(); ++n) { constant.setSample (0, n, .5f); constant.setSample (1, n, -.25f); }
    for (int shape = 0; shape < 5; ++shape) for (float amount : { .25f, 1.0f }) for (float phase : { 0.0f, 90.0f, 180.0f })
    {
        const auto out = process (constant, with (Kind::autoPan, { { "shape", (float) shape }, { "amount", amount }, { "rateHz", 3 }, { "phaseDeg", phase }, { "offsetDeg", 45 } }));
        double error = 0;
        for (int n = 0; n < out.getNumSamples(); ++n)
        {
            const auto p = n * 3.0 / 48000 + .125;
            const auto gl = 1 - amount * (1 - shapeValue (shape, p)) / 2, gr = 1 - amount * (1 - shapeValue (shape, p + phase / 360.0)) / 2;
            // Saw/square discontinuities depend on accumulated phase rounding; skip frames at an edge.
            const auto edge = [] (double x) { const auto f = x * 2 - std::floor (x * 2); return f < 1e-6 || f > 1 - 1e-6; };
            if (shape >= 2 && (edge (p) || edge (p + phase / 360.0))) continue;
            error = std::max ({ error, std::abs (out.getSample (0, n) - .5 * gl), std::abs (out.getSample (1, n) + .25 * gr) });
        }
        check (error < 1e-6, "closed-form gain, shape " + juce::String (shape) + " amount " + juce::String (amount) + " phase " + juce::String (phase));
    }
    const auto random = process (constant, with (Kind::autoPan, { { "shape", 5 }, { "amount", 1 }, { "rateHz", 4 }, { "phaseDeg", 0 } }));
    bool held = true, varied = false;
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        const auto first = random.getSample (0, cycle * 12000 + 10);
        for (int n = cycle * 12000 + 10; n < (cycle + 1) * 12000 - 10; ++n) held = held && random.getSample (0, n) == first;
        if (cycle > 0) varied = varied || first != random.getSample (0, (cycle - 1) * 12000 + 10);
        held = held && first >= 0 && first <= .5f;
    }
    check (held && varied, "random shape holds one bounded gain per cycle and changes between cycles");
    auto synced = with (Kind::autoPan, { { "sync", 1 }, { "division", 3 }, { "amount", 1 }, { "phaseDeg", 0 } }); synced.tempoBpm = 60;
    const auto beat = process (constant, synced);
    check (std::abs (beat.getSample (0, 12000) - .5) < 1e-6 && std::abs (beat.getSample (0, 36000)) < 1e-6, "quarter-note synchronization completes one cycle per beat at 60 BPM");
}

void modulatedDelayTests()
{
    for (const auto kind : { Kind::chorus, Kind::flanger })
    {
        juce::AudioBuffer<float> impulse (2, 4800); impulse.clear(); impulse.setSample (0, 0, 1); impulse.setSample (1, 0, -.5f);
        const auto delay = kind == Kind::chorus ? 7.0f : 2.5f; const int frames = (int) std::lround (delay * 48);
        const auto still = process (impulse, with (kind, { { "delayMs", delay }, { "depthMs", 0 }, { "feedback", 0 }, { "mix", 1 } }));
        bool exact = true;
        for (int n = 0; n < impulse.getNumSamples(); ++n)
            exact = exact && std::abs (still.getSample (0, n) - (n == frames ? 1.0f : 0.0f)) < 1e-7f && std::abs (still.getSample (1, n) - (n == frames ? -.5f : 0.0f)) < 1e-7f;
        check (exact, juce::String (effectName (kind)) + " without depth is an exact stereo delay");
        const auto echoes = process (impulse, with (kind, { { "delayMs", delay }, { "depthMs", 0 }, { "feedback", -.5f }, { "mix", 1 } }));
        bool train = true;
        for (int k = 1; k <= 5; ++k) train = train && std::abs (echoes.getSample (0, k * frames) - std::pow (-.5, k - 1)) < 1e-6;
        check (train, juce::String (effectName (kind)) + " feedback produces a geometric echo train");
        juce::AudioBuffer<float> clicks (2, 96000); clicks.clear();
        for (int i = 0; i < 16; ++i) clicks.setSample (0, i * 6000, 1);
        const auto swept = process (clicks, with (kind, { { "delayMs", delay }, { "depthMs", 2 }, { "rateHz", .5f }, { "feedback", 0 }, { "mix", 1 } }));
        int earliest = 1 << 30, latest = 0;
        for (int i = 0; i < 16; ++i)
        {
            int position = 0; float peak = 0;
            for (int n = i * 6000; n < i * 6000 + 1000; ++n) if (std::abs (swept.getSample (0, n)) > peak) { peak = std::abs (swept.getSample (0, n)); position = n - i * 6000; }
            earliest = std::min (earliest, position); latest = std::max (latest, position);
            check (position >= frames - 1 && position <= frames + 97, "modulated delay remains within its configured depth");
        }
        check (latest - earliest > 48, juce::String (effectName (kind)) + " depth moves the delay over time");
    }
    juce::AudioBuffer<float> impulse (2, 960); impulse.clear(); impulse.setSample (0, 0, 1);
    const auto one = process (impulse, with (Kind::chorus, { { "voices", 0 }, { "depthMs", 0 }, { "mix", 1 } }));
    const auto three = process (impulse, with (Kind::chorus, { { "voices", 2 }, { "depthMs", 0 }, { "mix", 1 } }));
    double voiceError = 0; for (int n = 0; n < 960; ++n) voiceError = std::max (voiceError, (double) std::abs (one.getSample (0, n) - three.getSample (0, n)));
    check (voiceError < 1e-7, "unmodulated voices average to the same single delay");
}

void phaserTests()
{
    for (float centre : { 400.0f, 1000.0f, 3000.0f })
    {
        const auto notch = process (sine (centre, .25, 48000), with (Kind::phaser, { { "stages", 0 }, { "centerHz", centre }, { "depth", 0 }, { "feedback", 0 }, { "mix", .5f } }));
        check (amplitudeAt (notch, 0, centre, 24000, 48000) < 1e-4, "two all-pass stages cancel the centre frequency at " + juce::String (centre) + " Hz");
    }
    for (int stages = 0; stages < 6; ++stages) for (double f : { 200.0, 2000.0, 9000.0 })
    {
        const auto wet = process (sine (f, .25, 48000), with (Kind::phaser, { { "stages", (float) stages }, { "depth", 0 }, { "feedback", 0 }, { "mix", 1 } }));
        check (std::abs (amplitudeAt (wet, 0, f, 24000, 48000) - .25) < 1e-5, "all-pass cascade preserves magnitude, stages " + juce::String (2 * (stages + 1)) + " at " + juce::String (f));
    }
    const auto tone = sine (1500, .25, 96000);
    const auto swept = process (tone, with (Kind::phaser, { { "depth", 1 }, { "rateHz", 1 }, { "feedback", 0 }, { "mix", .5f } }));
    double minimum = 1, maximum = 0;
    for (int window = 0; window < 16; ++window)
    { const auto a = amplitudeAt (swept, 0, 1500, 48000 + window * 3000, 51000 + window * 3000); minimum = std::min (minimum, a); maximum = std::max (maximum, a); }
    check (maximum > 3 * minimum, "LFO depth moves the phaser notches across a fixed tone");
}

// Channel EQ: closed-form properties of its sections. A Butterworth section is 3.01 dB down at its
// corner, a peak reaches its full gain at its centre, and a shelf is at half its gain at its corner.
void channelEqTests()
{
    const auto gainAt = [] (const Settings& s, double f, int channel = 0)
    { return db (amplitudeAt (process (sine (f, .2, 48000), s), channel, f, 24000, 48000) / .2); };
    {
        const auto in = sine (440, .5, 4800, 48000, .25);
        const auto out = process (in, defaultEffectSettings (Kind::channelEq));
        bool same = true;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < in.getNumSamples(); ++n) same = same && out.getSample (c, n) == in.getSample (c, n);
        check (same, "Channel EQ at its defaults passes audio through unchanged");
    }
    const auto hp = with (Kind::channelEq, { { "highPass", 1 } });
    check (std::abs (gainAt (hp, 80) + 3.0103) < .05 && std::abs (gainAt (hp, 1000)) < .05 && gainAt (hp, 20) < -23,
           "HP 80 Hz is a second-order Butterworth high-pass at 80 Hz");
    check (std::abs (gainAt (with (Kind::channelEq, { { "lowDb", 15 } }), 100) - 7.5) < .05 && std::abs (gainAt (with (Kind::channelEq, { { "lowDb", -15 } }), 100) + 7.5) < .05
           && gainAt (with (Kind::channelEq, { { "lowDb", 15 } }), 20) > 14 && std::abs (gainAt (with (Kind::channelEq, { { "lowDb", 15 } }), 8000)) < .1,
           "Low is a 100 Hz shelf: half its gain at 100 Hz, nearly all of it at 20 Hz, none at 8 kHz");
    for (const float centre : { 120.0f, 1000.0f, 2500.0f, 7500.0f })
        check (std::abs (gainAt (with (Kind::channelEq, { { "midDb", 12 }, { "midHz", centre } }), centre) - 12) < .05
               && std::abs (gainAt (with (Kind::channelEq, { { "midDb", -12 }, { "midHz", centre } }), centre) + 12) < .05,
               "Mid peaks at exactly +/-12 dB at " + juce::String (centre) + " Hz");
    check (std::abs (gainAt (with (Kind::channelEq, { { "highDb", 15 } }), 8000) - 7.5) < .05 && std::abs (gainAt (with (Kind::channelEq, { { "highDb", 15 } }), 100)) < .05,
           "a High boost is an 8 kHz shelf alone");
    check (std::abs (effects::ChannelEq::lowPassHz (0) - 20000) < 1e-9 && std::abs (effects::ChannelEq::lowPassHz (-15) - 8000) < 1e-9
           && std::abs (effects::ChannelEq::lowPassHz (-7.5) - std::sqrt (20000.0 * 8000)) < 1e-6 && effects::ChannelEq::lowPassHz (15) == 20000,
           "a High cut moves the low-pass from 20 kHz to 8 kHz evenly in octaves");
    check (std::abs (gainAt (with (Kind::channelEq, { { "highDb", -15 } }), 8000) - (-7.5 - 3.0103)) < .05,
           "at -15 dB the shelf's half cut and the 8 kHz low-pass's 3 dB add at 8 kHz");
    {
        const auto in = sine (1000, .2, 9600, 48000, .5);
        const auto flat = process (in, with (Kind::channelEq, { { "midDb", 6 } })), quiet = process (in, with (Kind::channelEq, { { "midDb", 6 }, { "outputDb", -6 } }));
        double worst = 0;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < in.getNumSamples(); ++n) worst = std::max (worst, std::abs (quiet.getSample (c, n) - std::pow (10.0, -6 / 20.0) * flat.getSample (c, n)));
        check (worst < 1e-6 && std::abs (amplitudeAt (flat, 1, 1000, 4800, 9600) / amplitudeAt (flat, 0, 1000, 4800, 9600) - .5) < 1e-6,
               "Output scales exactly, and both channels are filtered alike");
    }
    auto bad = defaultEffectSettings (Kind::channelEq); bad.values[3] = 100;
    auto loud = defaultEffectSettings (Kind::channelEq); loud.values[2] = 13;
    check (! validEffectSettings (bad) && ! validEffectSettings (loud) && std::string (effectCategory (Kind::channelEq)) == "EQ & Filters" && ! Session::isRackKind (Kind::channelEq),
           "Channel EQ rejects a mid frequency below 120 Hz and a mid gain past 12 dB");
}
void eqThreeTests()
{
    for (int steep = 0; steep < 2; ++steep)
    {
        double worst = 0;
        for (double f : { 40.0, 250.0, 1000.0, 2500.0, 12000.0 })
            worst = std::max (worst, std::abs (amplitudeAt (process (sine (f, .2, 48000), with (Kind::eqThree, { { "slope", (float) steep } })), 0, f, 24000, 48000) - .2));
        check (worst < 2e-6, "unity bands recombine to an all-pass with flat magnitude, slope " + juce::String (steep));
        const auto low = process (sine (25, .2, 96000), with (Kind::eqThree, { { "slope", (float) steep }, { "lowDb", -12 } }));
        const auto mid = process (sine (1000, .2, 48000), with (Kind::eqThree, { { "slope", (float) steep }, { "midDb", 6 } }));
        const auto high = process (sine (16000, .2, 48000), with (Kind::eqThree, { { "slope", (float) steep }, { "highDb", -24 } }));
        check (std::abs (db (amplitudeAt (low, 0, 25, 48000, 96000) / .2) + 12) < .05, "low gain applies below the first crossover");
        check (std::abs (db (amplitudeAt (mid, 0, 1000, 24000, 48000) / .2) - 6) < .25, "mid gain applies between crossovers");
        check (std::abs (db (amplitudeAt (high, 0, 16000, 24000, 48000) / .2) + 24) < .25, "high gain applies above the second crossover");
        const auto killed = process (sine (25, .2, 96000), with (Kind::eqThree, { { "slope", (float) steep }, { "lowOn", 0 } }));
        check (db (amplitudeAt (killed, 0, 25, 48000, 96000) / .2) < -40, "disabled low band removes its content");
    }
    const auto crossed = process (sine (1000, .2, 48000), with (Kind::eqThree, { { "lowHz", 3000 }, { "highHz", 500 } }));
    check (std::abs (amplitudeAt (crossed, 0, 1000, 24000, 48000) - .2) < 2e-6, "inverted crossovers collapse safely to one split");
}

void glueTests()
{
    for (int ratio = 0; ratio < 3; ++ratio) for (int attack : { 2, 4, 6 })
    {
        const auto s = with (Kind::glueCompressor, { { "thresholdDb", -24 }, { "ratio", (float) ratio }, { "attack", (float) attack }, { "release", 2 } });
        juce::AudioBuffer<float> a (2, 24000); for (int n = 0; n < 24000; ++n) { a.setSample (0, n, .5f); a.setSample (1, n, -.25f); }
        const auto out = process (a, s); const double ms = std::array<double, 7> { .01, .1, .3, 1, 3, 10, 30 } [(size_t) attack];
        const double r = std::array<double, 3> { 2, 4, 10 } [(size_t) ratio];
        double error = 0;
        for (int n = 0; n < 24000; ++n)
        {
            const auto level = .5 * -std::expm1 (-(n + 1) / (48000 * ms * .001));
            const auto over = 20 * std::log10 (level) + 24;
            const auto gain = over > 0 ? std::pow (10.0, over * (1 / r - 1) / 20) : 1.0;
            error = std::max ({ error, std::abs (out.getSample (0, n) - .5 * gain), std::abs (out.getSample (1, n) + .25 * gain) });
        }
        check (error < 1e-7, "closed-form linked attack, ratio " + juce::String (r) + " attack " + juce::String (ms) + " ms");
    }
    juce::AudioBuffer<float> full (2, 48000); for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (full.getWritePointer (c), 1.0f, 48000);
    const auto ranged = process (full, with (Kind::glueCompressor, { { "thresholdDb", -40 }, { "ratio", 2 }, { "rangeDb", 6 } }));
    check (std::abs (db (ranged.getSample (0, 47999)) + 6) < 1e-4, "range limits the maximum gain reduction");
    const auto clipped = process (full, with (Kind::glueCompressor, { { "thresholdDb", 0 }, { "makeupDb", 20 }, { "softClip", 1 } }));
    check (clipped.getMagnitude (0, 48000) <= 1, "soft clip bounds makeup gain at full scale");
    juce::AudioBuffer<float> burst (2, 96000); burst.clear();
    for (int n = 0; n < 4800; ++n) for (int c = 0; c < 2; ++c) burst.setSample (c, n, 1);
    for (int n = 4800; n < 96000; ++n) for (int c = 0; c < 2; ++c) burst.setSample (c, n, .01f);
    const auto recovery = [&] (float release) { const auto o = process (burst, with (Kind::glueCompressor, { { "thresholdDb", -30 }, { "ratio", 2 }, { "release", release } })); return o.getSample (0, 4800 + 9600) / .01; };
    const auto quick = recovery (0), slow = recovery (5), automatic = recovery (6);
    check (quick > slow && automatic > slow && automatic < quick, "automatic release recovers between its fast and slow detectors");
}

void shifterTests()
{
    const auto input = sine (1000, .25, 48000);
    for (float shift : { 200.0f, -300.0f, 1500.0f })
    {
        const auto out = process (input, with (Kind::frequencyShifter, { { "shiftHz", shift } }));
        const auto wanted = amplitudeAt (out, 0, 1000 + shift, 24000, 48000), image = amplitudeAt (out, 0, std::abs (1000 - shift), 24000, 48000);
        check (std::abs (wanted - .25) < .005 && db (image / wanted) < -40 && amplitudeAt (out, 0, 1000, 24000, 48000) < 1e-3,
               "single-sideband shift by " + juce::String (shift) + " Hz suppresses the image and carrier");
    }
    const auto opposite = process (input, with (Kind::frequencyShifter, { { "shiftHz", 200 }, { "wide", 1 } }));
    check (amplitudeAt (opposite, 0, 1200, 24000, 48000) > .24 && amplitudeAt (opposite, 1, 800, 24000, 48000) > .24, "opposite stereo shifts the right channel downward");
    const auto ring = process (input, with (Kind::frequencyShifter, { { "shiftHz", 200 }, { "mode", 1 } }));
    check (std::abs (amplitudeAt (ring, 0, 800, 24000, 48000) - .125) < 1e-4 && std::abs (amplitudeAt (ring, 0, 1200, 24000, 48000) - .125) < 1e-4
           && amplitudeAt (ring, 0, 1000, 24000, 48000) < 1e-4, "ring mode produces equal sum and difference sidebands");
    const auto still = process (input, with (Kind::frequencyShifter, { { "shiftHz", 0 } }));
    check (std::abs (amplitudeAt (still, 0, 1000, 24000, 48000) - .25) < 1e-4, "zero shift preserves the input magnitude");
}
}
void runExtendedEffectsTests()
{
    saturatorTests(); overdriveTests(); autoFilterTests(); autoPanTests(); modulatedDelayTests();
    phaserTests(); eqThreeTests(); channelEqTests(); glueTests(); shifterTests();
}
}
