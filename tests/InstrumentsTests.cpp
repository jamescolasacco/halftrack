#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <tuple>
#include <iostream>
#include <stdexcept>
#include <vector>

// Native instrument contract tests. Expected signals come from closed forms
// (oscillator, envelope and filter recurrences, Bessel sideband amplitudes)
// or from the documented behaviour, never from calling the voice code under test.
namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
using Settings = Session::EffectSettings;
using instruments::InstrumentDSP;
constexpr double pi = 3.14159265358979323846, rate = 48000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS instruments: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
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
// A bare sine voice: no filter, instant attack, full sustain, fixed level.
Settings pureSine (Kind kind = Kind::polySynth)
{
    if (kind == Kind::fmSynth)
        return with (kind, { { "opAAttack", 0 }, { "opASustain", 1 }, { "opBLevel", 0 }, { "velocityLevel", 0 } }); // Algorithm 1: A is the only carrier.
    if (kind == Kind::wavetableSynth)
        return with (kind, { { "filterType", 0 }, { "ampAttack", 0 }, { "ampSustain", 1 }, { "velocityAmp", 0 } });
    return with (kind, { { "osc1Shape", 3 }, { "osc2Level", 0 }, { "filterType", 0 }, { "ampAttack", 0 }, { "ampSustain", 1 }, { "velocityAmp", 0 } });
}
struct Event { int sample; int type; int a, b; }; // type 0 on, 1 off, 2 controller, 3 wheel
juce::AudioBuffer<float> play (const Settings& s, const std::vector<Event>& events, int frames, int block = 256)
{
    InstrumentDSP dsp; dsp.prepare (rate, s.kind); dsp.configure (s);
    juce::AudioBuffer<float> out (2, frames); out.clear();
    size_t next = 0;
    for (int start = 0; start < frames; start += block)
    {
        int position = start; const int end = std::min (frames, start + block);
        while (next < events.size() && events[next].sample < end)
        {
            const auto& e = events[next++];
            dsp.render (out.getWritePointer (0, position), out.getWritePointer (1, position), e.sample - position); position = e.sample;
            if (e.type == 0) dsp.noteOn (1, e.a, e.b / 127.0f);
            else if (e.type == 1) dsp.noteOff (1, e.a);
            else if (e.type == 2) dsp.controlChange (1, e.a, e.b);
            else dsp.pitchWheel (1, e.a);
        }
        dsp.render (out.getWritePointer (0, position), out.getWritePointer (1, position), end - position);
    }
    return out;
}
double amplitudeAt (const juce::AudioBuffer<float>& a, int channel, double frequency, int start, int end)
{
    std::complex<double> sum;
    for (int n = start; n < end; ++n) sum += (double) a.getSample (channel, n) * std::polar (1.0, -2 * pi * frequency * n / rate);
    return 2 * std::abs (sum) / (end - start);
}
double hz (double note) { return 440 * std::pow (2.0, (note - 69) / 12); }
bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return false;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) if (a.getSample (c, n) != b.getSample (c, n)) return false;
    return true;
}
double maxError (const juce::AudioBuffer<float>& a, const std::function<double (int, int)>& expected, int start, int end)
{
    double worst = 0;
    for (int c = 0; c < 2; ++c) for (int n = start; n < end; ++n) worst = std::max (worst, std::abs (a.getSample (c, n) - expected (c, n)));
    return worst;
}
double energy (const juce::AudioBuffer<float>& a, int start, int end)
{ double e = 0; for (int n = start; n < std::min (end, a.getNumSamples()); ++n) e += a.getSample (0, n) * (double) a.getSample (0, n); return e; }
double besselJ (int order, double x)
{
    double sum = 0, term = std::pow (x / 2, order) / std::tgamma (order + 1.0);
    for (int m = 0; m < 30; ++m) { sum += term; term *= -(x * x / 4) / ((m + 1.0) * (m + 1.0 + order)); }
    return sum;
}

void polyTests()
{
    const auto sine = play (pureSine(), { { 0, 0, 69, 127 } }, 4800);
    check (maxError (sine, [] (int, int n) { return .25 * std::sin (2 * pi * 440 * n / rate); }, 0, 4800) < 2e-6, "sine voice equals a quarter-scale closed-form 440 Hz sine");
    for (const auto& [key, value, expected] : std::vector<std::tuple<const char*, float, double>> { { "osc1Octave", 1, 880 }, { "osc1Semitone", 7, hz (76) }, { "osc1Detune", 50, hz (69.5) } })
    {
        auto s = pureSine(); s.values[(size_t) effectParameterIndex (Kind::polySynth, key)] = value;
        const auto out = play (s, { { 0, 0, 69, 127 } }, 48000); // One second keeps 12.9 Hz neighbours resolvable.
        check (std::abs (amplitudeAt (out, 0, expected, 0, 48000) - .25) < 2e-3 && amplitudeAt (out, 0, 440, 0, 48000) < .01, juce::String (key) + " moves the oscillator to the exact pitch");
    }
    const auto saw = play (with (Kind::polySynth, { { "osc2Level", 0 }, { "filterType", 0 }, { "ampAttack", 0 }, { "ampSustain", 1 }, { "velocityAmp", 0 } }), { { 0, 0, 57, 127 } }, 48000);
    bool harmonics = true;
    for (int k = 1; k <= 6; ++k) harmonics = harmonics && std::abs (amplitudeAt (saw, 0, 220.0 * k, 24000, 48000) / (.25 * 2 / (pi * k)) - 1) < .03;
    check (harmonics, "band-limited saw harmonics follow 2/(pi k)");
    const auto chord = play (pureSine(), { { 0, 0, 60, 127 }, { 0, 0, 64, 127 }, { 0, 0, 67, 127 } }, 4800);
    check (maxError (chord, [] (int, int n) { double v = 0; for (int note : { 60, 64, 67 }) v += .25 * std::sin (2 * pi * hz (note) * n / rate); return v; }, 0, 4800) < 5e-6,
           "polyphonic voices sum independently");
    {
        auto s = pureSine(); s.values[(size_t) effectParameterIndex (Kind::polySynth, "voiceMode")] = 2;
        InstrumentDSP dsp; dsp.prepare (rate, Kind::polySynth); dsp.configure (s);
        for (int n = 0; n < 9; ++n) dsp.noteOn (1, 60 + n, 1);
        check (dsp.activeVoices() == 8, "eight-voice mode steals rather than exceeding its limit");
    }
    const auto attack = with (Kind::polySynth, { { "osc1Shape", 3 }, { "osc2Level", 0 }, { "filterType", 0 }, { "ampAttack", 10 }, { "ampSustain", 1 }, { "ampRelease", 100 }, { "velocityAmp", 0 } });
    const auto shaped = play (attack, { { 0, 0, 69, 127 }, { 9600, 1, 69, 0 } }, 24000);
    const auto release = std::exp (-6.907755278982137 / (.1 * rate));
    check (maxError (shaped, [&] (int, int n)
    {
        const double level = n < 9600 ? std::min (1.0, (n + 1) / 480.0) : std::pow (release, n - 9600 + 1);
        return level * .25 * std::sin (2 * pi * 440 * n / rate);
    }, 0, 16000) < 2e-6, "linear attack and -60 dB release follow their closed-form levels");
    check (shaped.getMagnitude (0, 9600 + 4800, 4800) < .25 * .0011, "release reaches -60 dB after its time");
    const auto filtered = play (with (Kind::polySynth, { { "osc1Shape", 3 }, { "osc2Level", 0 }, { "filterType", 1 }, { "cutoffHz", 1000 }, { "resonance", 0 },
                                                         { "filterEnvelope", 0 }, { "keyTracking", 0 }, { "ampAttack", 0 }, { "ampSustain", 1 }, { "velocityAmp", 0 } }), { { 0, 0, 81, 127 } }, 48000);
    const auto omega = std::tan (pi * 880 / rate) / std::tan (pi * 1000 / rate);
    check (std::abs (amplitudeAt (filtered, 0, 880, 24000, 48000) - .25 / std::abs (std::complex<double> (1 - omega * omega, 2 * omega))) < 1e-4, "voice filter matches the analytic state-variable response");
    const auto glide = play (with (Kind::polySynth, { { "osc1Shape", 3 }, { "osc2Level", 0 }, { "filterType", 0 }, { "ampAttack", 50 }, { "ampSustain", 1 }, { "velocityAmp", 0 },
                                                      { "voiceMode", 1 }, { "glideMs", 100 } }), { { 0, 0, 60, 127 }, { 24000, 0, 72, 127 } }, 96000);
    check (amplitudeAt (glide, 0, hz (72), 72000, 96000) > .24 && amplitudeAt (glide, 0, hz (60), 72000, 96000) < .01
           && glide.getMagnitude (0, 24000, 480) > .24, "legato glides to the new key without restarting its attack");
    const auto mono = play (with (Kind::polySynth, { { "osc1Shape", 3 }, { "osc2Level", 0 }, { "filterType", 0 }, { "ampAttack", 0 }, { "ampSustain", 1 }, { "velocityAmp", 0 }, { "voiceMode", 0 } }),
                            { { 0, 0, 60, 127 }, { 4800, 0, 64, 127 }, { 24000, 1, 64, 0 } }, 48000);
    check (amplitudeAt (mono, 0, hz (64), 4800, 24000) > .24 && amplitudeAt (mono, 0, hz (60), 28800, 48000) > .24, "mono mode returns to the earlier held key");
    const auto pedal = play (pureSine(), { { 0, 2, 64, 127 }, { 0, 0, 69, 127 }, { 4800, 1, 69, 0 }, { 24000, 2, 64, 0 } }, 48000);
    check (pedal.getMagnitude (0, 20000, 2400) > .24 && pedal.getMagnitude (0, 40000, 2400) < 1e-4, "sustain pedal holds a released key until the pedal lifts");
    const auto bent = play (pureSine(), { { 0, 3, 16383, 0 }, { 0, 0, 69, 127 } }, 24000);
    check (std::abs (amplitudeAt (bent, 0, hz (71), 0, 24000) - .25) < 2e-3, "maximum wheel bends by the two-semitone default range");
    const auto tremolo = play (with (Kind::polySynth, { { "osc1Shape", 3 }, { "osc2Level", 0 }, { "filterType", 0 }, { "ampAttack", 0 }, { "ampSustain", 1 }, { "velocityAmp", 0 },
                                                        { "lfoAmp", 1 }, { "lfoRateHz", 5 } }), { { 0, 0, 69, 127 } }, 9600);
    check (maxError (tremolo, [] (int, int n) { return .25 * std::sin (2 * pi * 440 * n / rate) * (1 + std::sin (2 * pi * 5 * n / rate)) / 2; }, 0, 9600) < 3e-6, "LFO tremolo follows its closed form");
    const auto wide = play (with (Kind::polySynth, { { "osc1Shape", 3 }, { "osc2Level", 0 }, { "filterType", 0 }, { "unison", 1 }, { "unisonDetune", 20 }, { "ampAttack", 0 }, { "ampSustain", 1 } }),
                            { { 0, 0, 69, 127 } }, 48000);
    double stereo = 0; for (int n = 0; n < 48000; ++n) stereo = std::max (stereo, (double) std::abs (wide.getSample (0, n) - wide.getSample (1, n)));
    check (amplitudeAt (wide, 0, hz (68.8), 0, 48000) > .05 && amplitudeAt (wide, 0, hz (69.2), 0, 48000) > .05 && stereo > .01, "unison detunes and spreads its voices");
    const std::vector<Event> phrase { { 0, 0, 60, 100 }, { 700, 0, 64, 90 }, { 3000, 1, 60, 0 }, { 4100, 3, 12000, 0 }, { 6000, 1, 64, 0 } };
    check (identical (play (defaultEffectSettings (Kind::polySynth), phrase, 12000, 1), play (defaultEffectSettings (Kind::polySynth), phrase, 12000, 509)), "render is independent of block size");
}

void fmTests()
{
    const auto carrier = play (pureSine (Kind::fmSynth), { { 0, 0, 69, 127 } }, 4800);
    check (maxError (carrier, [] (int, int n) { return .25 * std::sin (2 * pi * 440 * n / rate); }, 0, 4800) < 2e-6, "single additive operator is a closed-form sine");
    const auto bessel = play (with (Kind::fmSynth, { { "opACoarse", 4 }, { "opAAttack", 0 }, { "opASustain", 1 }, { "opBCoarse", 1 }, { "opBLevel", (float) (1 / (2 * pi)) },
                                                     { "opBAttack", 0 }, { "opBSustain", 1 }, { "velocityLevel", 0 } }), { { 0, 0, 45, 127 } }, 48000);
    bool sidebands = true;
    for (int n = 0; n <= 2; ++n)
        for (int side : { -1, 1 })
            sidebands = sidebands && std::abs (amplitudeAt (bessel, 0, 110.0 * (4 + side * n), 24000, 48000) - .25 * besselJ (n, 1)) < 5e-4;
    check (sidebands, "phase modulation index 1 produces Bessel J0/J1/J2 sideband amplitudes");
    const auto sub = play (with (Kind::fmSynth, { { "opACoarse", 0 }, { "opAAttack", 0 }, { "opASustain", 1 }, { "opBLevel", 0 }, { "velocityLevel", 0 } }), { { 0, 0, 69, 127 } }, 24000);
    check (std::abs (amplitudeAt (sub, 0, 220, 0, 24000) - .25) < 1e-3, "coarse ratio zero plays half the note frequency");
    const auto pure = play (with (Kind::fmSynth, { { "opALevel", 0 }, { "opBLevel", 0 }, { "opDLevel", 1 }, { "opDAttack", 0 }, { "opDSustain", 1 }, { "velocityLevel", 0 }, { "algorithm", 10 } }), { { 0, 0, 57, 127 } }, 48000);
    const auto fed = play (with (Kind::fmSynth, { { "opALevel", 0 }, { "opBLevel", 0 }, { "opDLevel", 1 }, { "opDAttack", 0 }, { "opDSustain", 1 }, { "velocityLevel", 0 }, { "algorithm", 10 }, { "feedback", .6f } }), { { 0, 0, 57, 127 } }, 48000);
    check (amplitudeAt (pure, 0, 440, 24000, 48000) < 1e-4 && amplitudeAt (fed, 0, 440, 24000, 48000) > 1e-3, "operator feedback adds harmonics");
    const auto soft = play (with (Kind::fmSynth, { { "opAAttack", 0 }, { "opASustain", 1 }, { "opBLevel", 0 }, { "velocityLevel", 1 } }), { { 0, 0, 69, 64 } }, 24000);
    check (std::abs (amplitudeAt (soft, 0, 440, 0, 24000) - .25 * 64 / 127) < 1e-3, "full velocity sensitivity scales level linearly");
    InstrumentDSP limited; limited.prepare (rate, Kind::fmSynth); limited.configure (with (Kind::fmSynth, { { "voices", 0 } }));
    for (int n = 0; n < 5; ++n) limited.noteOn (1, 60 + n, 1);
    check (limited.activeVoices() == 4, "four-voice FM limit holds");
    for (int algorithm = 0; algorithm < 11; ++algorithm)
    {
        const auto out = play (with (Kind::fmSynth, { { "algorithm", (float) algorithm }, { "opCLevel", .5f }, { "opDLevel", .5f } }), { { 0, 0, 60, 100 }, { 9600, 1, 60, 0 } }, 48000);
        check (out.getMagnitude (0, 9600) > .01 && out.getMagnitude (0, 44000, 4000) < 1e-3, "algorithm " + juce::String (algorithm + 1) + " sounds and releases");
    }
}

void drumTests()
{
    const auto kit = defaultEffectSettings (Kind::drumKit);
    for (size_t pad = 0; pad < instruments::drums::notes.size(); ++pad)
    {
        const auto hit = play (kit, { { 0, 0, instruments::drums::notes[pad], 120 } }, 48000);
        check (hit.getMagnitude (0, 4800) > .01 && std::isfinite (hit.getRMSLevel (0, 0, 48000)), "pad " + juce::String (instruments::drums::notes[pad]) + " sounds");
    }
    check (play (kit, { { 0, 0, 60, 120 } }, 4800).getMagnitude (0, 4800) == 0, "an unmapped note is silent");
    const auto period = [] (const juce::AudioBuffer<float>& a, int start, int end)
    {
        std::vector<double> crossings;
        for (int n = start + 1; n < end; ++n) if (a.getSample (0, n - 1) < 0 && a.getSample (0, n) >= 0) crossings.push_back (n - a.getSample (0, n) / (a.getSample (0, n) - a.getSample (0, n - 1)));
        return crossings.size() < 2 ? 0.0 : (crossings.back() - crossings.front()) / (crossings.size() - 1);
    };
    const auto low = play (with (Kind::drumKit, { { "kickTone", 0 } }), { { 0, 0, 36, 120 } }, 24000), high = play (with (Kind::drumKit, { { "kickTone", 0 }, { "kickTune", 12 } }), { { 0, 0, 36, 120 } }, 24000);
    const auto ratio = period (low, 7200, 12000) / period (high, 7200, 12000);
    check (std::abs (ratio - 2) < .03 && std::abs (rate / period (low, 9600, 14400) - 50) < 1.5, "kick settles near 50 Hz and tune +12 doubles its pitch");
    const auto shortKick = play (kit, { { 0, 0, 36, 120 } }, 48000), longKick = play (with (Kind::drumKit, { { "kickDecay", 2 } }), { { 0, 0, 36, 120 } }, 48000);
    check (energy (longKick, 19200, 38400) > 3 * energy (shortKick, 19200, 38400), "longer decay sustains the body");
    const auto open = play (kit, { { 0, 0, 46, 120 } }, 24000), choked = play (kit, { { 0, 0, 46, 120 }, { 2400, 0, 42, 120 } }, 24000);
    check (energy (choked, 9600, 24000) < .01 * energy (open, 9600, 24000), "closed hat chokes a ringing open hat");
    check (play (with (Kind::drumKit, { { "snareLevel", 0 } }), { { 0, 0, 38, 120 } }, 4800).getMagnitude (0, 4800) == 0, "zero pad level is silent");
    const auto loud = play (kit, { { 0, 0, 38, 127 } }, 4800), quiet = play (kit, { { 0, 0, 38, 64 } }, 4800);
    check (std::abs (quiet.getMagnitude (0, 4800) / loud.getMagnitude (0, 4800) - 64.0 / 127) < 1e-3, "velocity scales a pad linearly");
    const std::vector<Event> pattern { { 0, 0, 36, 120 }, { 0, 0, 42, 90 }, { 6000, 0, 42, 90 }, { 12000, 0, 38, 110 }, { 12000, 0, 42, 90 }, { 18000, 0, 46, 90 } };
    check (identical (play (kit, pattern, 30000, 1), play (kit, pattern, 30000, 333)), "kit renders identically for any block partition");
}

void wavetableTests()
{
    const auto sine = play (pureSine (Kind::wavetableSynth), { { 0, 0, 69, 127 } }, 9600);
    check (maxError (sine, [] (int, int n) { return .25 * std::sin (2 * pi * 440 * n / rate); }, 0, 9600) < 2e-4, "basic table position zero is a sine");
    const auto square = play (with (Kind::wavetableSynth, { { "filterType", 0 }, { "ampAttack", 0 }, { "ampSustain", 1 }, { "velocityAmp", 0 }, { "osc1Position", 1 } }), { { 0, 0, 45, 127 } }, 48000);
    const auto h1 = amplitudeAt (square, 0, 110, 24000, 48000);
    check (std::abs (amplitudeAt (square, 0, 330, 24000, 48000) / h1 - 1.0 / 3) < .01 && amplitudeAt (square, 0, 220, 24000, 48000) < 1e-3 * h1, "basic table position one is an odd-harmonic square");
    const auto top = play (with (Kind::wavetableSynth, { { "filterType", 0 }, { "ampAttack", 0 }, { "ampSustain", 1 }, { "osc1Position", .667f } }), { { 0, 0, 120, 127 } }, 48000);
    check (amplitudeAt (top, 0, 48000 - 3 * hz (120), 24000, 48000) < 1e-3 && amplitudeAt (top, 0, hz (120), 24000, 48000) > .05, "high notes use a band-limited level without aliasing");
    for (int table = 0; table < instruments::wavetable::tableCount; ++table)
        for (float position : { 0.0f, .3f, .7f, 1.0f })
        {
            const auto out = play (with (Kind::wavetableSynth, { { "osc1Table", (float) table }, { "osc1Position", position }, { "ampAttack", 0 }, { "ampSustain", 1 } }), { { 0, 0, 57, 127 } }, 9600);
            check (out.getMagnitude (0, 9600) > .01 && out.getMagnitude (0, 9600) < 1, "table " + juce::String (table) + " position " + juce::String (position) + " is audible and bounded");
        }
    const auto wide = play (with (Kind::wavetableSynth, { { "unison", 2 }, { "ampAttack", 0 }, { "ampSustain", 1 } }), { { 0, 0, 57, 127 } }, 9600);
    double stereo = 0; for (int n = 0; n < 9600; ++n) stereo = std::max (stereo, (double) std::abs (wide.getSample (0, n) - wide.getSample (1, n)));
    check (stereo > .01, "unison spread differs between channels");
}

juce::AudioBuffer<float> read (const juce::File& f)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e9;
    double worst = 0; for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (a.getSample (c, n) - b.getSample (c, n)));
    return worst;
}
void engineTests (const juce::File& root)
{
    Session s (false); ok (s.addMidiTrack ("Keys"), "add MIDI track"); const int track = (int) s.snapshot().tracks.size() - 1;
    const auto trackId = s.snapshot().tracks.back().id;
    ok (s.addMidiClip (trackId, 0, 4), "add clip");
    { const auto snapshot = s.snapshot(); ok (s.addMidiNote (snapshot.tracks.back().clips[0].id, 69, 0, 1, 1), "add A4"); }
    const auto builtIn = root.getChildFile ("built-in.wav"), native = root.getChildFile ("native.wav"), other = root.getChildFile ("other.wav");
    ok (s.exportWav (builtIn, 48000, 24, .5), "render built-in synth");
    check (s.setTrackInstrument (s.snapshot().tracks[0].id, Kind::polySynth).failed(), "audio tracks reject instruments");
    check (s.setTrackInstrument (trackId, Kind::chorus).failed(), "audio effects are not instruments");
    ok (s.setTrackInstrument (trackId, Kind::polySynth), "choose Poly Synth");
    auto snapshot = s.snapshot();
    check (snapshot.tracks.back().instrument.isNativeEffect && snapshot.tracks.back().instrument.effect.kind == Kind::polySynth && snapshot.tracks.back().plugins.empty(), "instrument is reported separately from chain devices");
    ok (s.setNativeEffectSettings (snapshot.tracks.back().instrument.id, pureSine()), "configure a sine voice");
    ok (s.exportWav (native, 48000, 24, .5), "render Poly Synth");
    const auto rendered = read (native);
    check (maxError (rendered, [] (int, int n) { return .25 * std::sin (2 * pi * 440 * n / rate); }, 0, 23900) < 2e-6, "engine render matches the closed-form sine sample for sample");
    const auto history = s.canUndo(); ok (s.setTrackInstrument (trackId, Kind::polySynth), "re-choosing the same instrument is a no-op");
    check (history == s.canUndo() && ! s.canRedo(), "no-op instrument choice leaves history unchanged");
    s.undo(); s.undo(); ok (s.exportWav (other, 48000, 24, .5), "render after undoing the instrument change");
    check (difference (read (other), read (builtIn)) == 0 && ! s.snapshot().tracks.back().instrument.isNativeEffect, "undo restores the built-in synth bit-identically");
    s.redo(); s.redo(); ok (s.exportWav (other, 48000, 24, .5), "render after redo"); check (difference (read (other), rendered) == 0, "redo restores the configured instrument");
    ok (s.addNativeEffect (track, Kind::pitch), "insert a MIDI effect"); ok (s.addNativeEffect (track, Kind::saturator), "insert an audio effect");
    snapshot = s.snapshot();
    ok (s.setNativeEffectSettings (snapshot.tracks.back().plugins[0].id, with (Kind::pitch, { { "semitones", -12 } })), "transpose down an octave");
    ok (s.setNativeEffectSettings (snapshot.tracks.back().plugins[1].id, with (Kind::saturator, { { "mix", 0 } })), "keep the audio path dry");
    ok (s.setTrackInstrument (trackId, Kind::fmSynth), "switch to FM Synth"); ok (s.setTrackInstrument (trackId, Kind::polySynth), "switch back to Poly Synth");
    snapshot = s.snapshot();
    check (snapshot.tracks.back().plugins.size() == 2 && snapshot.tracks.back().plugins[0].effect.kind == Kind::pitch && snapshot.tracks.back().plugins[1].effect.kind == Kind::saturator,
           "instrument changes keep MIDI effects before and audio devices after the instrument");
    ok (s.setNativeEffectSettings (snapshot.tracks.back().instrument.id, pureSine()), "configure the new instance");
    ok (s.exportWav (other, 48000, 24, .5), "render through the MIDI effect");
    check (maxError (read (other), [] (int, int n) { return .25 * std::sin (2 * pi * 220 * n / rate); }, 0, 23900) < 2e-6, "MIDI effects feed the native instrument");
    const auto project = root.getChildFile ("instrument.halftrack"); ok (s.saveProject (project), "save instrument project"); ok (s.loadProject (project), "reopen instrument project");
    snapshot = s.snapshot();
    check (snapshot.tracks.back().instrument.effect.kind == Kind::polySynth && snapshot.tracks.back().instrument.effect.values == pureSine().values, "instrument and settings reopen");
    ok (s.exportWav (native, 48000, 24, .5), "render reopened project"); check (difference (read (native), read (other)) == 0, "reopened instrument renders identically");
    juce::String copy; ok (s.duplicateTrack (snapshot.tracks.back().id, copy), "duplicate the instrument track");
    check (s.snapshot().tracks.back().instrument.effect.kind == Kind::polySynth, "duplicates copy the native instrument");
    ok (s.showInstrumentEditor (track), "instrument editor window opens"); s.closePluginEditors();
    check (s.showInstrumentEditor (0).failed(), "audio tracks have no native instrument editor");
    Session drums (false); ok (drums.addMidiTrack ("Drums"), "add drum track"); const auto drumId = drums.snapshot().tracks.back().id;
    ok (drums.setTrackInstrument (drumId, Kind::drumKit), "choose Drum Kit"); ok (drums.addMidiClip (drumId, 0, 4), "add pattern clip");
    { const auto snap = drums.snapshot(); for (int step = 0; step < 8; ++step) ok (drums.addMidiNote (snap.tracks.back().clips[0].id, step % 2 ? 42 : 36, step * .5, .25, .9f), "write drum step"); }
    const auto beat = root.getChildFile ("beat.wav"); ok (drums.exportWav (beat, 48000, 24, .5), "render drum pattern");
    const auto pattern = read (beat);
    check (pattern.getMagnitude (0, 0, 2400) > .05 && pattern.getMagnitude (0, 12000, 2400) > .01, "drum kit renders kicks and hats through the engine");
    for (int kind = 0; kind < Session::deviceKindCount; ++kind)
    {
        if (! effectIsInstrument (static_cast<Kind> (kind)) || Session::isRackKind (static_cast<Kind> (kind))) continue;
        ok (drums.setTrackInstrument (drumId, static_cast<Kind> (kind)), "choose " + juce::String (effectName (static_cast<Kind> (kind))));
        ok (drums.exportWav (beat, 48000, 24, .5), "render with every instrument");
        // Samplers without samples are silent by design; the sampler suite covers loaded playback.
        if (static_cast<Kind> (kind) == Kind::sampler || static_cast<Kind> (kind) == Kind::drumSampler)
            check (read (beat).getMagnitude (0, read (beat).getNumSamples()) == 0, juce::String (effectName (static_cast<Kind> (kind))) + " without samples is silent");
        else check (read (beat).getMagnitude (0, read (beat).getNumSamples()) > .01, juce::String (effectName (static_cast<Kind> (kind))) + " is audible through the engine");
    }
    ok (drums.setTrackInstrument (drumId, std::nullopt), "restore the built-in synth");
    check (! drums.snapshot().tracks.back().instrument.isNativeEffect, "built-in synth is restored");
}
}
int runInstrumentsTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("instruments-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        polyTests(); fmTests(); drumTests(); wavetableTests(); engineTests (root);
        std::cout << "Instruments passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL instruments: " << e.what() << std::endl; return 1; }
}
}
