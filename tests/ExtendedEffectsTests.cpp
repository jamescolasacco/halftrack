#include "NativeEffects.h"
#include "Tuner.h"
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
bool near (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

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
// Pedal: the EQ through the drive stage's near-linear small-signal path, then the curves'
// harmonics. A peak reaches its gain at its centre and a shelf half its gain at its corner.
void pedalTests()
{
    const auto relative = [] (std::initializer_list<std::pair<const char*, float>> values, double f)
    {
        const auto level = [&] (std::initializer_list<std::pair<const char*, float>> v)
        { return amplitudeAt (process (sine (f, 1e-4, 48000), with (Kind::pedal, v)), 0, f, 24000, 48000); };
        return db (level (values) / level ({}));
    };
    check (std::abs (relative ({ { "bassDb", 12 } }, 100) - 12) < .05 && std::abs (relative ({ { "bassDb", -12 } }, 100) + 12) < .05, "Bass peaks at exactly +/-12 dB at 100 Hz");
    bool mids = true;
    for (const auto [choice, hz] : { std::pair { 0, 500.0 }, { 1, 1000.0 }, { 2, 2000.0 } })
        mids = mids && std::abs (relative ({ { "midDb", 12 }, { "midFrequency", (float) choice } }, hz) - 12) < .05;
    check (mids, "Mid peaks at exactly +12 dB at 500 Hz, 1 kHz and 2 kHz");
    check (std::abs (relative ({ { "trebleDb", 12 } }, 3300) - 6) < .05 && std::abs (relative ({ { "trebleDb", 12 } }, 100)) < .1, "Treble is a 3.3 kHz shelf: half its boost at 3.3 kHz, none at 100 Hz");
    check (std::abs (relative ({ { "sub", 1 } }, 250) - 3) < .05 && relative ({ { "sub", 1 } }, 25) > 5.5 && std::abs (relative ({ { "sub", 1 } }, 5000)) < .05,
           "Sub is a 6 dB shelf below 250 Hz");
    check (relative ({ { "midDb", 12 } }, 1500) / 12 < relative ({ { "midDb", 4 } }, 1500) / 4, "a bigger boost is more resonant");
    check (relative ({ { "midDb", 6 }, { "midFrequency", 0 } }, 700) / 6 < relative ({ { "midDb", 6 }, { "midFrequency", 2 } }, 2800) / 6, "the 500 Hz mid is narrower than the 2 kHz one");
    // Harmonics of a 1 kHz tone at half scale.
    const auto harmonics = [] (int type, float gain)
    {
        const auto out = process (sine (1000, .5, 48000), with (Kind::pedal, { { "pedalType", (float) type }, { "pedalGain", gain } }));
        const auto fundamental = amplitudeAt (out, 0, 1000, 24000, 48000);
        return std::pair { db (amplitudeAt (out, 0, 2000, 24000, 48000) / fundamental), db (amplitudeAt (out, 0, 3000, 24000, 48000) / fundamental) };
    };
    const auto overdrive = harmonics (0, .5f), distortion = harmonics (1, .5f), fuzz = harmonics (2, .5f);
    check (distortion.first < -80 && distortion.second > -30, "Distortion is symmetric: odd harmonics only");
    check (overdrive.first > -40 && fuzz.first > overdrive.first, "Overdrive adds even harmonics and Fuzz more of them");
    bool rising = true;
    for (int type = 0; type < 3; ++type) rising = rising && harmonics (type, 1).second > harmonics (type, 0).second;
    check (rising, "more Gain means more distortion for every type");
    {
        const auto in = sine (440, .5, 4800, 48000, .3);
        const auto dry = process (in, with (Kind::pedal, { { "mix", 0 } }));
        bool same = true;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < in.getNumSamples(); ++n) same = same && dry.getSample (c, n) == in.getSample (c, n);
        const auto loud = process (in, with (Kind::pedal, { { "outputDb", 0 } })), quiet = process (in, with (Kind::pedal, { { "outputDb", -6 } }));
        double worst = 0;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < in.getNumSamples(); ++n) worst = std::max (worst, std::abs (quiet.getSample (c, n) - std::pow (10.0, -6 / 20.0) * loud.getSample (c, n)));
        check (same && worst < 1e-6, "fully dry passes audio unchanged and Output scales exactly");
    }
}
// Beat Repeat with the transport running at 120 BPM and 48 kHz: a beat is 24000 samples, a 1/16
// slice 6000. The input is a unique sequence, so every output sample names the input it repeats.
juce::AudioBuffer<float> processPlaying (const juce::AudioBuffer<float>& source, const Settings& s, int block = 512)
{
    auto out = source; NativeEffectDSP dsp; dsp.prepare (48000, s.kind); dsp.configure (s);
    for (int start = 0; start < out.getNumSamples(); start += block)
    {
        dsp.setTimelinePosition (start / 48000.0);
        dsp.processSamples (out.getWritePointer (0, start), out.getWritePointer (1, start), std::min (block, out.getNumSamples() - start));
    }
    return out;
}
void beatRepeatTests()
{
    constexpr int bar = 96000, beat = 24000, slice = 6000, frames = 2 * bar;
    juce::AudioBuffer<float> in (2, frames);
    for (int n = 0; n < frames; ++n) { const auto x = (float) (((n * 7919LL) % 10007) / 10007.0 - .5) * .5f; in.setSample (0, n, x); in.setSample (1, n, -x); }
    // Interval 1 Bar, Offset 8/16 (beat 3), Gate 4/16 (one beat) and a 1/16 grid repeat slice 0 of
    // each bar's third beat four times.
    const auto timed = [] (float mode, std::initializer_list<std::pair<const char*, float>> more = {})
    {
        auto s = with (Kind::beatRepeat, { { "interval", 5 }, { "offset", 8 }, { "gate", 4 }, { "grid", 4 }, { "mixMode", mode } });
        for (const auto& [key, value] : more) s.values[(size_t) effectParameterIndex (Kind::beatRepeat, key)] = value;
        return s;
    };
    const auto expect = [&] (int mode, int n, double gain = 1, double decay = 0) -> double
    {
        const auto x = (double) in.getSample (0, n);
        const int at = n % bar - 2 * beat;
        if (at < 0 || at >= beat) return mode == 2 ? 0 : x;
        const int index = at / slice;
        const auto repeated = (double) in.getSample (0, n - at + at % slice) * (index == 0 ? 1 : gain * std::pow (1 - decay, index));
        if (mode == 0) return x + (index > 0 ? repeated : 0);
        return index == 0 ? x : repeated;
    };
    const auto matches = [&] (const juce::AudioBuffer<float>& out, int mode, double gain = 1, double decay = 0)
    {
        double worst = 0;
        for (int n = 0; n < frames; ++n)
            worst = std::max ({ worst, std::abs (out.getSample (0, n) - expect (mode, n, gain, decay)), std::abs (out.getSample (1, n) + expect (mode, n, gain, decay)) });
        return worst;
    };
    check (matches (processPlaying (in, timed (2)), 2) < 1e-6, "Gate passes only the third beat of each bar: its first sixteenth, four times");
    check (matches (processPlaying (in, timed (1)), 1) < 1e-6, "Insert keeps the input outside the repeats and mutes it inside");
    check (matches (processPlaying (in, timed (0)), 0) < 1e-6, "Mix adds the repeats after the captured slice");
    check (matches (processPlaying (in, timed (0, { { "decay", .5f } })), 0, 1, .5) < 1e-6, "Decay halves each repeat after the last");
    check (matches (processPlaying (in, timed (1, { { "volumeDb", -6 } })), 1, std::pow (10.0, -6 / 20.0)) < 1e-6, "Volume scales the repeats");
    check (matches (processPlaying (in, timed (0, { { "chance", 0 } })), 2) > .1 && matches (processPlaying (in, timed (1, { { "chance", 0 } })), 1) > .01
           && [&] { const auto dry = processPlaying (in, timed (1, { { "chance", 0 } })); double worst = 0;
                    for (int n = 0; n < frames; ++n) worst = std::max (worst, (double) std::abs (dry.getSample (0, n) - in.getSample (0, n))); return worst < 1e-7; } (),
           "Chance 0 never repeats");
    check (matches (processPlaying (in, timed (2), 333), 2) < 1e-6, "the repeats land on the same samples with any block size");
    {
        // A half-chance pass repeats some bars and not others, and the same render repeats its choices.
        const auto quarter = with (Kind::beatRepeat, { { "interval", 3 }, { "gate", 1 }, { "grid", 4 }, { "mixMode", 2 }, { "chance", .5f } });
        const auto a = processPlaying (in, quarter), b = processPlaying (in, quarter);
        int repeated = 0;
        for (int q = 0; q < frames / beat; ++q) if (a.getMagnitude (0, q * beat, slice) > 0) ++repeated;
        bool same = true; for (int n = 0; n < frames; ++n) same = same && a.getSample (0, n) == b.getSample (0, n);
        check (repeated > 0 && repeated < frames / beat && same, "Chance 50% repeats some beats and not others, the same way in each render");
    }
    {
        const auto held = processPlaying (in, with (Kind::beatRepeat, { { "repeat", 1 }, { "grid", 4 }, { "mixMode", 2 } }));
        double worst = 0;
        for (int n = 0; n < frames; ++n) worst = std::max (worst, (double) std::abs (held.getSample (0, n) - in.getSample (0, n % slice)));
        check (worst < 1e-6, "the Repeat switch repeats the first slice for as long as it is on");
    }
    {
        const auto low = processPlaying (in, timed (2, { { "pitch", -12 } }));
        double worst = 0;
        for (int n = 0; n < frames; ++n)
        {
            const int at = n % bar - 2 * beat;
            if (at < slice || at >= beat) continue;
            const auto read = (at % slice) * .5;
            const int start = n - at, whole = (int) read;
            const auto a = in.getSample (0, start + whole), b = in.getSample (0, start + whole + 1);
            worst = std::max (worst, std::abs (low.getSample (0, n) - (a + (b - a) * (read - whole))));
        }
        check (worst < 1e-6, "Pitch -12 plays the repeats an octave down, at half speed through the slice");
    }
    {
        // Stopped, Beat Repeat passes the input in Mix and Insert, and nothing in Gate.
        const auto mixed = process (in, timed (0)), gated = process (in, timed (2));
        double worst = 0;
        for (int n = 0; n < frames; ++n) worst = std::max (worst, (double) std::abs (mixed.getSample (0, n) - in.getSample (0, n)));
        check (worst < 1e-7 && gated.getMagnitude (0, frames) == 0, "stopped, it passes the input in Mix and nothing in Gate");
    }
}
// Filter Delay: impulses show each line's routing, timing, feedback, pan and level exactly; tones
// show the filters, which sit inside the feedback loop.
void filterDelayTests()
{
    // Plain lines: filters, sync and feedback off, Dry off; times of 12000, 18000 and 24000 samples.
    const auto plain = [] (std::initializer_list<std::pair<const char*, float>> more)
    {
        auto s = with (Kind::filterDelay, { { "line1Filter", 0 }, { "line2Filter", 0 }, { "line3Filter", 0 }, { "line1Sync", 0 }, { "line2Sync", 0 }, { "line3Sync", 0 },
                                            { "line1Feedback", 0 }, { "line2Feedback", 0 }, { "line3Feedback", 0 }, { "line1On", 0 }, { "line3On", 0 }, { "dryDb", -70 } });
        for (const auto& [key, value] : more) s.values[(size_t) effectParameterIndex (Kind::filterDelay, key)] = value;
        return s;
    };
    const auto impulse = [] (float left, float right) { juce::AudioBuffer<float> x (2, 48000); x.clear(); x.setSample (0, 0, left); x.setSample (1, 0, right); return x; };
    const auto at = [] (const juce::AudioBuffer<float>& x, int channel, int n) { return (double) x.getSample (channel, n); };
    {
        const auto left = process (impulse (1, 0), plain ({ { "line1On", 1 }, { "line2On", 1 }, { "line3On", 1 } }));
        check (near (at (left, 0, 12000), 1, 1e-6) && near (at (left, 1, 12000), 0, 1e-9) && near (at (left, 0, 18000), .5, 1e-6) && near (at (left, 1, 18000), .5, 1e-6)
               && near (at (left, 1, 24000), 0, 1e-9) && left.getMagnitude (0, 1, 11998) == 0, "line 1 delays the left input to the left, line 2 their mean to both sides");
        const auto right = process (impulse (0, 1), plain ({ { "line3On", 1 } }));
        check (near (at (right, 1, 24000), 1, 1e-6) && near (at (right, 0, 24000), 0, 1e-9), "line 3 delays the right input to the right");
    }
    {
        const auto echoes = process (impulse (1, 0), plain ({ { "line1On", 1 }, { "line1Feedback", .5f } }));
        check (near (at (echoes, 0, 12000), 1, 1e-6) && near (at (echoes, 0, 24000), .5, 1e-6) && near (at (echoes, 0, 36000), .25, 1e-6), "feedback 50% halves each echo");
        const auto panned = process (impulse (1, 0), plain ({ { "line1On", 1 }, { "line1Pan", 1 } })), centred = process (impulse (1, 0), plain ({ { "line1On", 1 }, { "line1Pan", 0 } }));
        check (near (at (panned, 1, 12000), 1, 1e-6) && near (at (panned, 0, 12000), 0, 1e-9) && near (at (centred, 0, 12000), 1, 1e-6) && near (at (centred, 1, 12000), 1, 1e-6),
               "Pan sends line 1 to the right, or the centre to both");
        const auto louder = process (impulse (1, 0), plain ({ { "line1On", 1 }, { "line1Db", 6 } }));
        check (near (at (louder, 0, 12000), std::pow (10.0, 6 / 20.0), 1e-5), "Volume raises a line by up to 6 dB");
        const auto synced = process (impulse (1, 0), plain ({ { "line1On", 1 }, { "line1Sync", 1 }, { "line1Division", 3 } }));
        auto swung = plain ({ { "line1On", 1 }, { "line1Sync", 1 }, { "line1Division", 3 }, { "line1Swing", .25f } });
        const auto late = process (impulse (1, 0), swung);
        check (near (at (synced, 0, 24000), 1, 1e-6) && near (at (late, 0, 30000), 1, 1e-6), "a synced line waits 4/16 at 120 BPM, a quarter longer with +25% swing");
        const auto wet = process (impulse (1, .5f), plain ({ { "dryDb", 0 } })), half = process (impulse (1, .5f), plain ({ { "dryDb", -6 } }));
        check (near (at (wet, 0, 0), 1, 1e-6) && near (at (wet, 1, 0), .5, 1e-6) && near (at (half, 0, 0), std::pow (10.0, -6 / 20.0), 1e-6), "Dry sets the direct signal's level");
    }
    {
        // The filters: a 1 kHz band four octaves wide passes its centre and rejects tones far outside it.
        const auto echoLevel = [&] (double hz)
        {
            const auto out = process (sine (hz, .2, 48000), plain ({ { "line1On", 1 }, { "line1Filter", 1 }, { "line1Hz", 1000 }, { "line1Width", 4 } }));
            return db (amplitudeAt (out, 0, hz, 24000, 48000) / .2);
        };
        check (echoLevel (1000) > -1 && echoLevel (8000) < -10 && echoLevel (100) < -10, "the filters pass the band's centre and reject tones three octaves away");
        // Inside the feedback loop, each echo is filtered again: with full feedback an echo outside the band shrinks.
        const auto loop = process (impulse (1, 0), plain ({ { "line1On", 1 }, { "line1Filter", 1 }, { "line1Hz", 2000 }, { "line1Width", 1 }, { "line1Feedback", 1 } }));
        const auto first = loop.getMagnitude (0, 12000, 12000), second = loop.getMagnitude (0, 24000, 12000);
        check (first > 0 && second < first * .9, "every echo passes the filters again");
    }
    check (process (sine (440, .3, 48000), defaultEffectSettings (Kind::filterDelay)).getMagnitude (0, 24000, 24000) > .3, "the default lines echo the input");
}
// Erosion: a sine-modulated delay is phase modulation, so a tone's sidebands follow Bessel
// functions of the modulation index 2·pi·f·(depth/2).
double bessel (int order, double x)
{
    double sum = 0, factorial = 1;
    for (int k = 0; k < 40; ++k)
    {
        if (k > 0) factorial *= k;
        double other = 1; for (int i = 2; i <= k + order; ++i) other *= i;
        sum += (k % 2 == 0 ? 1 : -1) * std::pow (x / 2, 2 * k + order) / (factorial * other);
    }
    return sum;
}
void erosionTests()
{
    {
        const auto in = sine (1000, .5, 4800, 48000, .3);
        const auto out = process (in, with (Kind::erosion, { { "amount", 0 } }));
        bool same = true;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < in.getNumSamples(); ++n) same = same && out.getSample (c, n) == in.getSample (c, n);
        check (same, "Erosion with no Amount passes audio unchanged");
    }
    // A 2 kHz tone through a full-depth (0.5 ms) delay swung by a 900 Hz sine: index pi. With 900 Hz
    // the sidebands folded below 0 Hz land between the measured ones.
    const auto modulated = process (sine (2000, .5, 48000), with (Kind::erosion, { { "amount", 1 }, { "frequencyHz", 900 } }));
    const auto beta = 2 * pi * 2000 * effects::Erosion::maximumDepthSeconds / 2;
    const auto level = [&] (const juce::AudioBuffer<float>& x, double hz) { return amplitudeAt (x, 0, hz, 24000, 48000); };
    bool sidebands = true;
    for (const auto [hz, order] : { std::pair { 2000.0, 0 }, { 2900.0, 1 }, { 1100.0, 1 }, { 3800.0, 2 }, { 200.0, 2 } })
        sidebands = sidebands && std::abs (level (modulated, hz) - .5 * std::abs (bessel (order, beta))) < .05 * .5 * std::abs (bessel (order, beta)) + 1e-3;
    check (sidebands, "the sine modulation places Bessel sidebands at 2 kHz +/- multiples of 900 Hz");
    const auto noisy = process (sine (2000, .5, 48000), with (Kind::erosion, { { "amount", 1 }, { "frequencyHz", 900 }, { "noiseBlend", 1 } }));
    check (level (noisy, 2900) < .3 * level (modulated, 2900) && noisy.getRMSLevel (0, 24000, 24000) > .3, "noise modulation spreads the sidebands instead");
    {
        const auto mono = process (sine (2000, .5, 48000), with (Kind::erosion, { { "amount", 1 }, { "noiseBlend", .5f } }));
        const auto wide = process (sine (2000, .5, 48000), with (Kind::erosion, { { "amount", 1 }, { "noiseBlend", .5f }, { "stereoWidth", 1 } }));
        double sameMono = 0, sameWide = 0;
        for (int n = 0; n < 48000; ++n)
        { sameMono = std::max (sameMono, (double) std::abs (mono.getSample (0, n) - mono.getSample (1, n))); sameWide = std::max (sameWide, (double) std::abs (wide.getSample (0, n) - wide.getSample (1, n))); }
        check (sameMono == 0 && sameWide > .05, "Stereo Width 0% modulates both channels alike, 100% independently");
    }
}
// Resonators: impulses ring at each resonator's pitch, measured with the Tuner's detector.
void resonatorTests()
{
    const auto only = [] (int resonator, std::initializer_list<std::pair<const char*, float>> more)
    {
        auto s = with (Kind::resonators, { { "filterType", 2 }, { "filterHz", 30 }, { "color", 1 }, { "mix", 1 }, { "gainDb", 0 }, { "r1Note", 57 } });
        for (int k = 1; k <= 5; ++k) s.values[(size_t) effectParameterIndex (Kind::resonators, ("r" + std::to_string (k) + "On").c_str())] = k == resonator ? 1.0f : 0.0f;
        for (const auto& [key, value] : more) s.values[(size_t) effectParameterIndex (Kind::resonators, key)] = value;
        return s;
    };
    const auto impulse = [] (float left, float right, int frames = 48000) { juce::AudioBuffer<float> x (2, frames); x.clear(); x.setSample (0, 0, left); x.setSample (1, 0, right); return x; };
    const auto pitch = [] (const juce::AudioBuffer<float>& x, int channel = 0) -> double
    {
        std::vector<float> frames ((size_t) tuner::analysisFrames);
        for (int n = 0; n < tuner::analysisFrames; ++n) frames[(size_t) n] = x.getSample (channel, 4800 + n);
        const auto r = tuner::detect (frames.data(), (int) frames.size(), 48000, 440);
        return r ? r->frequency : 0;
    };
    const auto cents = [] (double hz, double target) { return hz > 0 ? 1200 * std::log2 (hz / target) : 1e9; };
    check (std::abs (cents (pitch (process (impulse (1, 1), only (1, {}))), 220)) < 3, "Resonator I rings at its Note, A3");
    check (std::abs (cents (pitch (process (impulse (1, 1), only (1, { { "color", 0 } }))), 220)) < 5, "a dark Color keeps the pitch true");
    check (std::abs (cents (pitch (process (impulse (1, 1), only (1, { { "r1Fine", 50 } }))), 220 * std::pow (2.0, 50 / 1200.0))) < 3, "Fine detunes by cents");
    check (std::abs (cents (pitch (process (impulse (1, 0), only (2, { { "r2Pitch", 12 } }))), 440)) < 3, "Resonator II rings its Pitch above I, an octave");
    {
        // Mode B: half-period negative feedback, odd harmonics only.
        const auto harmonic = [&] (bool modeB, double multiple)
        {
            const auto x = process (impulse (1, 1, 96000), only (1, { { "mode", modeB ? 1.0f : 0.0f }, { "decaySeconds", 10 } }));
            return amplitudeAt (x, 0, 220 * multiple, 24000, 72000);
        };
        check (std::abs (cents (pitch (process (impulse (1, 1), only (1, { { "mode", 1 } }))), 220)) < 3, "Mode B rings at the same pitch");
        check (harmonic (true, 2) < .05 * harmonic (true, 3) && harmonic (false, 2) > .3 * harmonic (false, 3), "Mode B has odd harmonics only, Mode A all of them");
    }
    {
        // Decay: the fundamental falls 60 dB over the Decay time with Const, faster for higher notes without it.
        const auto drop = [&] (std::initializer_list<std::pair<const char*, float>> more, double seconds)
        {
            auto settings = only (1, more);
            const auto hz = effects::Resonators::frequencyOf (settings.values.data(), 0);
            const auto x = process (impulse (1, 1, 96000), settings);
            return db (amplitudeAt (x, 0, hz, (int) ((.05 + seconds) * 48000), (int) ((.15 + seconds) * 48000)) / amplitudeAt (x, 0, hz, 2400, 7200));
        };
        check (std::abs (drop ({ { "constant", 1 }, { "decaySeconds", .5f } }, .5) + 60) < 3, "with Const, a 0.5 s Decay falls 60 dB in 0.5 s");
        check (std::abs (drop ({ { "constant", 1 }, { "decaySeconds", .5f }, { "r1Note", 69 } }, .5) + 60) < 3, "at any note");
        check (drop ({ { "constant", 0 }, { "decaySeconds", .5f }, { "r1Note", 69 } }, .5) < -100, "without Const, a higher note dies sooner");
    }
    {
        const auto fromLeft = process (impulse (1, 0), only (3, {})), fromRight = process (impulse (0, 1), only (3, {}));
        check (fromLeft.getMagnitude (0, 48000) == 0 && fromLeft.getMagnitude (1, 0, 48000) == 0 && fromRight.getMagnitude (1, 0, 48000) > .1 && fromRight.getMagnitude (0, 0, 48000) == 0,
               "Resonator III takes and plays only the right channel");
        const auto mono = process (impulse (1, 0), only (2, { { "width", 0 } }));
        double worst = 0; for (int n = 0; n < 48000; ++n) worst = std::max (worst, (double) std::abs (mono.getSample (0, n) - mono.getSample (1, n)));
        check (worst == 0 && mono.getMagnitude (1, 0, 48000) > 0, "Width 0 sums the resonances to mono");
    }
    {
        const auto in = sine (440, .5, 4800, 48000, .3);
        const auto dry = process (in, with (Kind::resonators, { { "mix", 0 } }));
        bool same = true; for (int c = 0; c < 2; ++c) for (int n = 0; n < in.getNumSamples(); ++n) same = same && dry.getSample (c, n) == in.getSample (c, n);
        check (same, "fully dry passes audio unchanged");
    }
}
// Vinyl Distortion: a band-passed tone at its band's centre passes at unity, so squaring and
// cubing give exact harmonics: Tracing k·A²/2 at 2f, Pinch k·A³/4 at 3f.
void vinylTests()
{
    const auto settings = [&] (std::initializer_list<std::pair<const char*, float>> more)
    {
        auto s = with (Kind::vinylDistortion, { { "crackleDb", -70 }, { "tracingDrive", 0 }, { "pinchDrive", 0 }, { "hardness", 1 } });
        for (const auto& [key, value] : more) s.values[(size_t) effectParameterIndex (Kind::vinylDistortion, key)] = value;
        return s;
    };
    {
        const auto in = sine (1000, .3, 9600, 48000, .5);
        const auto out = process (in, settings ({}));
        bool same = true; for (int c = 0; c < 2; ++c) for (int n = 0; n < in.getNumSamples(); ++n) same = same && out.getSample (c, n) == in.getSample (c, n);
        check (same, "with no drive and no crackle Vinyl Distortion passes audio unchanged");
    }
    const auto level = [] (const juce::AudioBuffer<float>& x, int channel, double hz) { return amplitudeAt (x, channel, hz, 24000, 48000); };
    {
        const auto traced = process (sine (1000, .3, 48000), settings ({ { "tracingDrive", .3f }, { "tracingHz", 1000 } }));
        const auto k = 8 * .3 * .5;
        check (std::abs (level (traced, 0, 2000) - k * .09 / 2) < .03 * k * .09 / 2 && level (traced, 0, 3000) < 1e-4, "Tracing adds an exact second harmonic and no third");
        const auto pinched = process (sine (3000, .3, 48000), settings ({ { "pinchDrive", .2f }, { "pinchHz", 3000 } }));
        const auto p = 16 * .2 * .5;
        check (std::abs (level (pinched, 0, 9000) - p * .027 / 4) < .03 * p * .027 / 4 && level (pinched, 0, 6000) < 1e-4, "Pinch adds an exact third harmonic and no second");
        double opposite = 0, same = 0;
        const auto mono = process (sine (3000, .3, 48000), settings ({ { "pinchDrive", .2f }, { "pinchHz", 3000 }, { "pinchStereo", 0 } }));
        const auto source = sine (3000, .3, 48000);
        for (int n = 0; n < 48000; ++n)
        {
            opposite = std::max (opposite, (double) std::abs ((pinched.getSample (0, n) - source.getSample (0, n)) + (pinched.getSample (1, n) - source.getSample (1, n))));
            same = std::max (same, (double) std::abs (mono.getSample (0, n) - mono.getSample (1, n)));
        }
        check (opposite < 1e-6 && same == 0, "stereo Pinch distorts the right channel in opposite phase, mono alike");
        const auto hard = process (sine (1000, .9, 48000), settings ({ { "tracingDrive", 1 }, { "tracingHz", 1000 }, { "globalDrive", 1 } }));
        const auto soft = process (sine (1000, .9, 48000), settings ({ { "tracingDrive", 1 }, { "tracingHz", 1000 }, { "globalDrive", 1 }, { "hardness", 0 } }));
        check (level (soft, 0, 2000) < .8 * level (hard, 0, 2000), "Soft rounds off heavy distortion");
        const auto off = process (sine (1000, .3, 48000), settings ({ { "tracingDrive", 1 }, { "pinchDrive", 1 }, { "globalDrive", 0 } }));
        check (level (off, 0, 2000) < 1e-6 && level (off, 0, 3000) < 1e-6, "Global Drive 0 turns both off");
    }
    {
        // Crackle: clicks at Density per second, the same in every render.
        juce::AudioBuffer<float> silence (2, 10 * 48000); silence.clear();
        const auto crackle = settings ({ { "crackleDb", 0 }, { "crackleDensity", 20 } });
        const auto a = processPlaying (silence, crackle), b = processPlaying (silence, crackle);
        int clicks = 0, since = 1000;
        for (int n = 0; n < a.getNumSamples(); ++n) { if (std::abs (a.getSample (0, n)) > .45f && since > 240) { ++clicks; since = 0; } else ++since; }
        bool same = true; for (int n = 0; n < a.getNumSamples(); ++n) same = same && a.getSample (0, n) == b.getSample (0, n);
        check (clicks > 150 && clicks < 250 && same, "Crackle clicks about 20 times a second, the same way each render (" + juce::String (clicks) + " in 10 s)");
        check (processPlaying (silence, settings ({ { "crackleDb", -70 }, { "crackleDensity", 20 } })).getMagnitude (0, 10 * 48000) == 0, "at -70 dB the crackle is off");
    }
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
    phaserTests(); eqThreeTests(); channelEqTests(); pedalTests(); beatRepeatTests(); filterDelayTests(); erosionTests(); resonatorTests(); vinylTests(); glueTests(); shifterTests();
}
}
