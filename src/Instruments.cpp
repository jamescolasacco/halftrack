#include "Instruments.h"
#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace halftrack::instruments
{
namespace
{
using Kind = Session::EffectKind;
constexpr double pi = 3.14159265358979323846;
double midiHz (double note) noexcept { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }
double decibels (double db) noexcept { return std::pow (10.0, db / 20.0); }
// Per-sample coefficient that falls by 60 dB over the given time.
double sixtyDb (double ms, double rate) noexcept { return ms <= 0 ? 0.0 : std::exp (-6.907755278982137 / (ms * .001 * rate)); }
uint32_t xorshift (uint32_t& s) noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
double noiseSample (uint32_t& s) noexcept { return static_cast<double> (xorshift (s) >> 8) / 8388608.0 - 1.0; }
int rounded (float v) noexcept { return static_cast<int> (std::lround (v)); }
double wrap (double phase) noexcept { return phase >= 1 ? phase - std::floor (phase) : phase; }
// Shared state-variable filter modes: 0 off, 1 LP12, 2 LP24, 3 HP12, 4 BP12, 5 notch.
double filterSample (std::array<effects::StateVariable, 2>& stages, int type, double x, double g, double k) noexcept
{
    switch (type)
    {
        case 1: return stages[0].tick (x, g, k, 0);
        case 2: return stages[1].tick (stages[0].tick (x, g, k, 0), g, std::sqrt (2.0), 0);
        case 3: return stages[0].tick (x, g, k, 1);
        case 4: return stages[0].tick (x, g, k, 2);
        case 5: return stages[0].tick (x, g, k, 3);
        default: return x;
    }
}

struct Tables
{
    std::vector<float> data; // [table][frame][level][sample]
    const float* frame (int table, int frame, int level) const noexcept
    {
        using namespace wavetable;
        return data.data() + ((static_cast<size_t> (table) * frames + static_cast<size_t> (frame)) * levels + static_cast<size_t> (level)) * size;
    }
};
Tables& sharedTables() { static Tables tables; return tables; }
std::once_flag tablesBuilt;
void buildTables()
{
    using namespace wavetable;
    constexpr int harmonics = size / 2;
    auto& tables = sharedTables(); tables.data.assign (static_cast<size_t> (tableCount) * frames * levels * size, 0.0f);
    std::vector<double> sine (size);
    for (int n = 0; n < size; ++n) sine[static_cast<size_t> (n)] = std::sin (2 * pi * n / size);
    std::vector<double> sinCoefficients (harmonics + 1), cosCoefficients (harmonics + 1), shape (size), naive (4096), denseSine (4096);
    for (size_t n = 0; n < denseSine.size(); ++n) denseSine[n] = std::sin (2 * pi * static_cast<double> (n) / static_cast<double> (denseSine.size()));
    for (int table = 0; table < tableCount; ++table)
        for (int frame = 0; frame < frames; ++frame)
        {
            const double position = frame / static_cast<double> (frames - 1);
            std::fill (sinCoefficients.begin(), sinCoefficients.end(), 0.0); std::fill (cosCoefficients.begin(), cosCoefficients.end(), 0.0);
            if (table == 0)
            {
                // Sine -> triangle -> saw -> square, interpolating Fourier amplitudes.
                const auto key = [] (int shapeIndex, int k)
                {
                    switch (shapeIndex)
                    {
                        case 0: return k == 1 ? 1.0 : 0.0;
                        case 1: return k % 2 ? 8 / (pi * pi * k * k) * ((k / 2) % 2 ? -1.0 : 1.0) : 0.0;
                        case 2: return 2 / (pi * k) * (k % 2 ? 1.0 : -1.0);
                        default: return k % 2 ? 4 / (pi * k) : 0.0;
                    }
                };
                const auto scaled = position * 3; const int lower = std::min (2, static_cast<int> (scaled)); const auto fraction = scaled - lower;
                for (int k = 1; k <= harmonics; ++k) sinCoefficients[static_cast<size_t> (k)] = key (lower, k) * (1 - fraction) + key (lower + 1, k) * fraction;
            }
            else if (table == 1)
            {
                // Additive drawbars: each frame adds two more harmonics at 1/sqrt(k).
                const int top = 1 + frame * 2;
                for (int k = 1; k <= std::min (top, harmonics); ++k) sinCoefficients[static_cast<size_t> (k)] = 1 / std::sqrt (static_cast<double> (k));
            }
            else if (table == 2)
            {
                // Zero-mean pulse whose duty narrows from 50% to 5%.
                const auto duty = .5 - position * .45;
                // (4 / pi k) sin(pi k d) cos(2 pi k t - pi k d), expanded into sine and cosine terms.
                for (int k = 1; k <= harmonics; ++k)
                {
                    const auto a = 4 / (pi * k) * std::sin (pi * k * duty);
                    sinCoefficients[static_cast<size_t> (k)] = a * std::sin (pi * k * duty); cosCoefficients[static_cast<size_t> (k)] = a * std::cos (pi * k * duty);
                }
            }
            else
            {
                // Hard-synchronized saw, slave ratio 1..8, analysed from a dense naive cycle.
                const auto ratio = 1 + position * 7; const auto dense = naive.size();
                for (size_t n = 0; n < dense; ++n) { const auto slave = std::fmod (static_cast<double> (n) * ratio / static_cast<double> (dense), 1.0); naive[n] = 2 * slave - 1; }
                for (int k = 1; k <= harmonics; ++k)
                {
                    double s = 0, c = 0;
                    for (size_t n = 0; n < dense; ++n)
                    {
                        const auto index = (static_cast<size_t> (k) * n) % dense;
                        s += naive[n] * denseSine[index]; c += naive[n] * denseSine[(index + dense / 4) % dense];
                    }
                    sinCoefficients[static_cast<size_t> (k)] = 2 * s / static_cast<double> (dense); cosCoefficients[static_cast<size_t> (k)] = 2 * c / static_cast<double> (dense);
                }
            }
            double peak = 0;
            for (int level = 0; level < levels; ++level)
            {
                const int top = std::max (1, harmonics >> level);
                std::fill (shape.begin(), shape.end(), 0.0);
                for (int k = 1; k <= top; ++k)
                {
                    const auto s = sinCoefficients[static_cast<size_t> (k)], c = cosCoefficients[static_cast<size_t> (k)];
                    if (s == 0 && c == 0) continue;
                    for (int n = 0; n < size; ++n)
                    {
                        const auto index = static_cast<size_t> ((static_cast<long long> (k) * n) % size);
                        shape[static_cast<size_t> (n)] += s * sine[index] + c * sine[(index + size / 4) % size];
                    }
                }
                if (level == 0) for (auto v : shape) peak = std::max (peak, std::abs (v));
                auto* out = const_cast<float*> (tables.frame (table, frame, level));
                for (int n = 0; n < size; ++n) out[n] = static_cast<float> (shape[static_cast<size_t> (n)] / std::max (peak, 1.0e-9));
            }
        }
}
}

const std::array<Algorithm, 11> fmAlgorithms {{
    { { 0b0010, 0b0100, 0b1000, 0 }, 0b0001 },      // D > C > B > A
    { { 0b0010, 0b1100, 0, 0 }, 0b0001 },           // C + D > B > A
    { { 0b0110, 0, 0b1000, 0 }, 0b0001 },           // B + (D > C) > A
    { { 0b1110, 0, 0, 0 }, 0b0001 },                // B + C + D > A
    { { 0b0010, 0, 0b1000, 0 }, 0b0101 },           // two stacks: B > A, D > C
    { { 0b1000, 0b1000, 0b1000, 0 }, 0b0111 },      // D > A, B, C
    { { 0b0010, 0b0100, 0, 0 }, 0b1001 },           // C > B > A, plus D
    { { 0, 0b0100, 0b1000, 0 }, 0b0011 },           // D > C > B, plus A
    { { 0b0010, 0, 0, 0 }, 0b1101 },                // B > A, plus C and D
    { { 0b0110, 0b1000, 0b1000, 0 }, 0b0001 },      // D > (B, C) > A
    { { 0, 0, 0, 0 }, 0b1111 } }};                  // additive

void prepareWavetables() { std::call_once (tablesBuilt, buildTables); }
const float* wavetableFrame (int table, int frame, int level) noexcept
{
    using namespace wavetable;
    return sharedTables().frame (std::clamp (table, 0, tableCount - 1), std::clamp (frame, 0, frames - 1), std::clamp (level, 0, levels - 1));
}

void Envelope::configure (double attackMs, double decayMs, double sustain, double releaseMs, double rate) noexcept
{
    attackStep = attackMs <= 0 ? 1.0 : 1.0 / (attackMs * .001 * rate);
    decayCoefficient = sixtyDb (decayMs, rate); releaseCoefficient = sixtyDb (releaseMs, rate);
    sustainLevel = std::clamp (sustain, 0.0, 1.0);
}
void Envelope::start() noexcept { stage = Stage::attack; } // Restarts from the current level without a click.
double Envelope::tick() noexcept
{
    switch (stage)
    {
        case Stage::idle: return 0;
        case Stage::attack:
            level += attackStep;
            if (level >= 1) { level = 1; stage = Stage::decay; }
            return level;
        case Stage::decay:
            level = sustainLevel + (level - sustainLevel) * decayCoefficient;
            if (std::abs (level - sustainLevel) < 1.0e-6) { level = sustainLevel; stage = Stage::sustain; }
            return level;
        case Stage::sustain: level = sustainLevel; return level;
        case Stage::release:
            level *= releaseCoefficient;
            if (level < 1.0e-5) { level = 0; stage = Stage::idle; }
            return level;
    }
    return 0;
}

double polyBlep (double t, double dt) noexcept
{
    if (dt <= 0) return 0;
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0;
}
double oscillator (int shape, double phase, double dt, double width) noexcept
{
    switch (shape)
    {
        case 0: return 2 * phase - 1 - polyBlep (phase, dt);
        case 1:
        {
            const auto w = std::clamp (width, .01, .99);
            auto shifted = phase - w; if (shifted < 0) shifted += 1;
            return (phase < w ? 1.0 : -1.0) + polyBlep (phase, dt) - polyBlep (shifted, dt) - (2 * w - 1);
        }
        case 2: return phase < .5 ? 4 * phase - 1 : 3 - 4 * phase;
        default: return std::sin (2 * pi * phase);
    }
}

void InstrumentDSP::prepare (double sampleRate, Kind nextKind)
{
    rate = std::isfinite (sampleRate) && sampleRate > 0 ? sampleRate : 48000; kind = nextKind;
    if (kind == Kind::wavetableSynth) prepareWavetables();
    settings = defaultEffectSettings (kind); reset(); configure (settings);
}
int InstrumentDSP::voiceLimit() const noexcept
{
    const auto& v = settings.values;
    switch (kind)
    {
        case Kind::fmSynth: return std::array<int, 4> { 4, 8, 16, 32 } [static_cast<size_t> (v[fm::voices])];
        case Kind::drumKit: return 8;
        case Kind::sampler: return std::array<int, 5> { 1, 4, 8, 16, 32 } [static_cast<size_t> (v[sampler::voices])];
        case Kind::drumSampler: return maximumVoices;
        case Kind::wavetableSynth: return std::array<int, 5> { 1, 1, 8, 16, 32 } [static_cast<size_t> (v[wavetable::voiceMode])];
        default: return std::array<int, 5> { 1, 1, 8, 16, 32 } [static_cast<size_t> (v[poly::voiceMode])];
    }
}
bool InstrumentDSP::monophonic() const noexcept
{
    return (kind == Kind::polySynth && settings.values[poly::voiceMode] < 2) || (kind == Kind::wavetableSynth && settings.values[wavetable::voiceMode] < 2)
        || (kind == Kind::sampler && settings.values[sampler::voices] == 0);
}
bool InstrumentDSP::legatoMode() const noexcept
{
    return (kind == Kind::polySynth && settings.values[poly::voiceMode] == 1) || (kind == Kind::wavetableSynth && settings.values[wavetable::voiceMode] == 1);
}
void InstrumentDSP::configure (const Session::EffectSettings& next) noexcept
{
    if (next.kind != kind || ! validEffectSettings (next)) return;
    settings = next; const auto& v = settings.values;
    double glideMs = 0, volume = 0;
    switch (kind)
    {
        case Kind::polySynth:
            glideMs = v[poly::glide]; volume = v[poly::volume];
            lfo.setRate (v[poly::lfoRate], rate);
            for (auto& voice : voices)
            {
                voice.amp.configure (v[poly::ampAttack], v[poly::ampDecay], v[poly::ampSustain], v[poly::ampRelease], rate);
                voice.filter.configure (v[poly::filterAttack], v[poly::filterDecay], v[poly::filterSustain], v[poly::filterRelease], rate);
            }
            break;
        case Kind::wavetableSynth:
            glideMs = v[wavetable::glide]; volume = v[wavetable::volume];
            lfo.setRate (v[wavetable::lfoRate], rate);
            for (auto& voice : voices)
            {
                voice.amp.configure (v[wavetable::ampAttack], v[wavetable::ampDecay], v[wavetable::ampSustain], v[wavetable::ampRelease], rate);
                voice.filter.configure (v[wavetable::filterAttack], v[wavetable::filterDecay], v[wavetable::filterSustain], v[wavetable::filterRelease], rate);
            }
            break;
        case Kind::fmSynth:
            volume = v[fm::volume];
            for (auto& voice : voices)
                for (size_t o = 0; o < 4; ++o)
                {
                    const auto base = o * fm::operatorStride;
                    voice.operators[o].configure (v[base + 3], v[base + 4], v[base + 5], v[base + 6], rate);
                }
            break;
        case Kind::sampler:
            volume = v[sampler::volume];
            for (auto& voice : voices)
            {
                // One-Shot plays at full level between its fade in and fade out.
                const bool oneShot = v[sampler::mode] == 1;
                voice.amp.configure (v[sampler::ampAttack], v[sampler::ampDecay], oneShot ? 1.0 : v[sampler::ampSustain], v[sampler::ampRelease], rate);
            }
            break;
        case Kind::drumSampler:
            // Pads play their samples at full level; a choke or a shortened length fades them out.
            volume = v[drumSampler::volume];
            for (auto& voice : voices) voice.amp.configure (0, 1, 1, drumSampler::cutFadeMs, rate);
            break;
        default: volume = v[drums::volume]; break;
    }
    glideCoefficient = glideMs > 0 ? std::exp (-1.0 / (glideMs * .001 * rate)) : 0.0;
    // The sampler plays its source at unity; synthesized sources leave headroom.
    gain = decibels (volume) * (kind == Kind::sampler || kind == Kind::drumSampler ? 1.0 : .25);
}
void InstrumentDSP::reset() noexcept
{
    for (auto& v : voices) { v = Voice {}; }
    configure (settings);
    sustainPedal = {}; wheel = {}; keyCount = 0; clock = 0; lfo.reset(); lfoSeed = 0x6d2b79f5u;
}
int InstrumentDSP::activeVoices() const noexcept
{ int count = 0; for (const auto& v : voices) count += v.active; return count; }
double InstrumentDSP::bend (int channel) const noexcept
{
    const auto range = kind == Kind::polySynth ? settings.values[poly::bendRange] : kind == Kind::fmSynth ? settings.values[fm::bendRange]
        : kind == Kind::wavetableSynth ? settings.values[wavetable::bendRange] : kind == Kind::sampler ? settings.values[sampler::bendRange] : 0.0f;
    return wheel[static_cast<size_t> (std::clamp (channel, 1, 16) - 1)] * range;
}
InstrumentDSP::Voice* InstrumentDSP::allocate() noexcept
{
    const int limit = voiceLimit();
    Voice* best = nullptr;
    for (int i = 0; i < limit; ++i) if (! voices[static_cast<size_t> (i)].active) return &voices[static_cast<size_t> (i)];
    // Steal the oldest released voice, otherwise the oldest voice.
    for (int i = 0; i < limit; ++i)
    {
        auto& v = voices[static_cast<size_t> (i)];
        if (! v.held && ! v.sustained && (best == nullptr || v.age < best->age)) best = &v;
    }
    if (best) return best;
    for (int i = 0; i < limit; ++i) if (best == nullptr || voices[static_cast<size_t> (i)].age < best->age) best = &voices[static_cast<size_t> (i)];
    return best;
}
void InstrumentDSP::start (Voice& v, int channel, int note, double velocity, bool retrigger) noexcept
{
    const bool wasActive = v.active;
    v.active = true; v.held = true; v.sustained = false; v.channel = channel; v.note = note; v.velocity = velocity;
    v.age = clock++; v.targetPitch = note;
    if (! wasActive || glideCoefficient == 0) v.pitch = note;
    if (! wasActive || kind == Kind::drumKit)
    {
        v.phases = {}; v.feedback = {}; for (auto& f : v.filters) f.clear(); for (auto& f : v.rightFilters) f.clear();
        v.noise = 0x9e3779b9u ^ (static_cast<uint32_t> (note) * 2654435761u) ^ (static_cast<uint32_t> (channel) << 24) ^ static_cast<uint32_t> (v.age);
        if (v.noise == 0) v.noise = 1;
    }
    v.time = 0; v.choke = 1; v.drumState = v.drumState2 = 0; v.fade = 1; v.fadeStep = 0;
    if (kind == Kind::sampler && (retrigger || ! wasActive)) v.position = sampleRegion().first;
    if (retrigger) { v.amp.start(); v.filter.start(); for (auto& op : v.operators) op.start(); }
    if (kind == Kind::drumKit)
    {
        v.drumFilter.clear();
        const auto pad = static_cast<size_t> (std::find (drums::notes.begin(), drums::notes.end(), note) - drums::notes.begin());
        const auto& p = settings.values; const auto ratio = std::exp2 (p[pad * drums::padStride] / 12.0), tone = static_cast<double> (p[pad * drums::padStride + 2]);
        switch (pad)
        {
            case 1: v.drumFilter.c = effects::highPassSection (1500 * ratio, .707, rate); break;
            case 2: v.drumFilter.c = effects::bandPassOctaves (1100 * ratio * (.7 + .6 * tone), 1, rate); break;
            case 3: case 4: v.drumFilter.c = effects::highPassSection (std::min (rate * .4, 6500 * (.8 + .5 * tone)), .707, rate); break;
            case 7: v.drumFilter.c = effects::highPassSection (std::min (rate * .4, 4500 * (.7 + .6 * tone)), .707, rate); break;
            default: break;
        }
    }
}
void InstrumentDSP::release (Voice& v) noexcept
{
    v.held = false; v.sustained = false;
    v.amp.release(); v.filter.release(); for (auto& op : v.operators) op.release();
}
void InstrumentDSP::noteOn (int channel, int note, float velocity) noexcept
{
    if (channel < 1 || channel > 16 || note < 0 || note > 127 || ! std::isfinite (velocity)) return;
    const auto level = std::clamp (static_cast<double> (velocity), 0.0, 1.0);
    if (level <= 0) { noteOff (channel, note); return; }
    if (kind == Kind::drumSampler)
    {
        const int pad = note - drumSampler::firstNote;
        if (pad < 0 || pad >= Session::drumPadCount || padSamples[static_cast<size_t> (pad)] == nullptr) return;
        const auto& v = settings.values;
        // A pad in a choke group silences the other pads of that group.
        if (const auto group = rounded (v[static_cast<size_t> (pad) * drumSampler::padStride + drumSampler::choke]); group > 0)
            for (auto& other : voices)
                if (other.active && other.pad >= 0 && other.pad != pad
                    && rounded (v[static_cast<size_t> (other.pad) * drumSampler::padStride + drumSampler::choke]) == group) release (other);
        if (auto* voice = allocate())
        {
            voice->active = false; voice->pad = pad;
            start (*voice, channel, note, level, true); voice->position = 0;
        }
        return;
    }
    if (kind == Kind::drumKit)
    {
        const auto found = std::find (drums::notes.begin(), drums::notes.end(), note);
        if (found == drums::notes.end()) return;
        const auto pad = static_cast<size_t> (found - drums::notes.begin());
        if (pad == 3 && voices[4].active) voices[4].drumState2 = 1; // Closed hat chokes the open hat.
        auto& v = voices[pad]; v.active = false; start (v, channel, note, level, true); return;
    }
    if (monophonic())
    {
        int kept = 0;
        for (int i = 0; i < keyCount; ++i)
            if (! (keyStack[static_cast<size_t> (i)] == note && keyChannels[static_cast<size_t> (i)] == channel))
            { keyStack[static_cast<size_t> (kept)] = keyStack[static_cast<size_t> (i)]; keyChannels[static_cast<size_t> (kept)] = keyChannels[static_cast<size_t> (i)]; keyVelocities[static_cast<size_t> (kept)] = keyVelocities[static_cast<size_t> (i)]; ++kept; }
        keyCount = kept;
        if (keyCount == static_cast<int> (keyStack.size()))
        {
            std::copy (keyStack.begin() + 1, keyStack.end(), keyStack.begin()); std::copy (keyChannels.begin() + 1, keyChannels.end(), keyChannels.begin());
            std::copy (keyVelocities.begin() + 1, keyVelocities.end(), keyVelocities.begin()); --keyCount;
        }
        keyStack[static_cast<size_t> (keyCount)] = note; keyChannels[static_cast<size_t> (keyCount)] = channel; keyVelocities[static_cast<size_t> (keyCount++)] = level;
        auto& v = voices[0];
        const bool legato = legatoMode() && v.active && v.held;
        if (legato) { v.targetPitch = note; v.note = note; v.channel = channel; }
        else start (v, channel, note, level, true);
        return;
    }
    for (auto& v : voices)
        if (v.active && v.held && v.channel == channel && v.note == note) { start (v, channel, note, level, true); return; }
    if (auto* v = allocate()) { v->active = false; start (*v, channel, note, level, true); } // A stolen voice restarts cleanly.
}
void InstrumentDSP::noteOff (int channel, int note) noexcept
{
    if (channel < 1 || channel > 16 || kind == Kind::drumKit || kind == Kind::drumSampler) return;
    // A triggered one-shot plays to its end regardless of the key.
    if (kind == Kind::sampler && settings.values[sampler::mode] == 1 && settings.values[sampler::trigger] == 0) return;
    if (monophonic())
    {
        int kept = 0;
        for (int i = 0; i < keyCount; ++i)
            if (! (keyStack[static_cast<size_t> (i)] == note && keyChannels[static_cast<size_t> (i)] == channel))
            { keyStack[static_cast<size_t> (kept)] = keyStack[static_cast<size_t> (i)]; keyChannels[static_cast<size_t> (kept)] = keyChannels[static_cast<size_t> (i)]; keyVelocities[static_cast<size_t> (kept)] = keyVelocities[static_cast<size_t> (i)]; ++kept; }
        keyCount = kept;
        auto& v = voices[0];
        if (! v.active || ! v.held || v.note != note || v.channel != channel) return;
        if (keyCount > 0)
        {
            const auto previous = keyStack[static_cast<size_t> (keyCount - 1)];
            if (legatoMode()) { v.targetPitch = previous; v.note = previous; v.channel = keyChannels[static_cast<size_t> (keyCount - 1)]; }
            else start (v, keyChannels[static_cast<size_t> (keyCount - 1)], previous, keyVelocities[static_cast<size_t> (keyCount - 1)], true);
            return;
        }
        if (sustainPedal[static_cast<size_t> (channel - 1)]) { v.held = false; v.sustained = true; } else release (v);
        return;
    }
    for (auto& v : voices)
        if (v.active && v.held && v.channel == channel && v.note == note)
        {
            if (sustainPedal[static_cast<size_t> (channel - 1)]) { v.held = false; v.sustained = true; }
            else release (v);
        }
}
void InstrumentDSP::controlChange (int channel, int controller, int value) noexcept
{
    if (channel < 1 || channel > 16) return;
    const auto c = static_cast<size_t> (channel - 1);
    if (controller == 64 || controller == 121)
    {
        const bool down = controller == 64 && value >= 64;
        if (sustainPedal[c] && ! down)
            for (auto& v : voices) if (v.active && v.sustained && v.channel == channel) release (v);
        sustainPedal[c] = down;
        if (controller == 121) wheel[c] = 0;
    }
    else if (controller == 120) { for (auto& v : voices) if (v.channel == channel) v = Voice {}; configure (settings); if (monophonic()) keyCount = 0; }
    else if (controller == 123)
    {
        if (monophonic()) keyCount = 0;
        for (auto& v : voices)
            if (v.active && v.held && v.channel == channel && kind != Kind::drumKit)
            { if (sustainPedal[c]) { v.held = false; v.sustained = true; } else release (v); }
    }
}
void InstrumentDSP::pitchWheel (int channel, int value) noexcept
{
    if (channel < 1 || channel > 16 || value < 0 || value > 16383) return;
    wheel[static_cast<size_t> (channel - 1)] = value >= 8192 ? (value - 8192) / 8191.0 : (value - 8192) / 8192.0;
}

void InstrumentDSP::renderPoly (Voice& v, double& left, double& right) noexcept
{
    using namespace poly;
    const auto& p = settings.values;
    v.pitch = v.targetPitch + (v.pitch - v.targetPitch) * glideCoefficient;
    const auto modulation = std::clamp (lfo.value (0, rounded (p[lfoShape])), -1.0, 1.0);
    const auto pitch = v.pitch + bend (v.channel) + p[lfoPitch] * modulation;
    const auto amp = v.amp.tick(), envelope = v.filter.tick();
    const int unisonVoices = rounded (p[unison]) + 1;
    double sumL = 0, sumR = 0;
    for (int u = 0; u < unisonVoices; ++u)
    {
        const auto spread = unisonVoices == 1 ? 0.0 : 2.0 * u / (unisonVoices - 1) - 1;
        const auto gainL = unisonVoices == 1 ? 1.0 : std::sqrt (.5 * (1 - .7 * spread)), gainR = unisonVoices == 1 ? 1.0 : std::sqrt (.5 * (1 + .7 * spread));
        for (size_t osc = 0; osc < 2; ++osc)
        {
            const auto base = osc == 0 ? osc1Shape : osc2Shape;
            const auto level = static_cast<double> (p[base + 4]);
            auto& phase = v.phases[static_cast<size_t> (u) * 2 + osc];
            const auto note = pitch + 12 * p[base + 1] + p[base + 2] + (p[base + 3] + spread * p[unisonDetune]) / 100.0;
            const auto dt = std::min (midiHz (note) / rate, .49);
            if (level > 0) { const auto s = level * oscillator (rounded (p[base]), phase, dt, p[base + 5]); sumL += s * gainL; sumR += s * gainR; }
            phase = wrap (phase + dt);
        }
    }
    const auto norm = 1 / std::sqrt (static_cast<double> (unisonVoices));
    sumL *= norm; sumR *= norm;
    if (p[noiseLevel] > 0) { const auto n = p[noiseLevel] * noiseSample (v.noise); sumL += n; sumR += n; }
    const int type = rounded (p[filterType]);
    if (type != 0)
    {
        const auto octaves = p[filterEnvelope] * 5 * envelope + p[keyTracking] * (v.note - 60) / 12.0 + p[lfoCutoff] * modulation + p[velocityCutoff] * 2 * (v.velocity - .5);
        const auto frequency = std::clamp (p[cutoff] * std::exp2 (octaves), 20.0, rate * .49);
        const auto g = std::tan (pi * frequency / rate), k = 1 / effects::AutoFilter::qFromResonance (p[resonance]);
        if (unisonVoices == 1) { sumL = filterSample (v.filters, type, sumL, g, k); sumR = sumL; }
        else { sumL = filterSample (v.filters, type, sumL, g, k); sumR = filterSample (v.rightFilters, type, sumR, g, k); }
    }
    const auto level = amp * ((1 - p[velocityAmp]) + p[velocityAmp] * v.velocity) * (1 - p[lfoAmp] * (1 - modulation) * .5) * gain;
    left += sumL * level; right += sumR * level;
}

void InstrumentDSP::renderWavetable (Voice& v, double& left, double& right) noexcept
{
    using namespace wavetable;
    const auto& p = settings.values;
    v.pitch = v.targetPitch + (v.pitch - v.targetPitch) * glideCoefficient;
    const auto modulation = lfo.value (0, 0);
    const auto pitch = v.pitch + bend (v.channel) + p[lfoPitch] * modulation;
    const auto amp = v.amp.tick(), envelope = v.filter.tick();
    const int unisonVoices = rounded (p[unison]) + 1;
    double sumL = 0, sumR = 0;
    for (int u = 0; u < unisonVoices; ++u)
    {
        const auto spread = unisonVoices == 1 ? 0.0 : 2.0 * u / (unisonVoices - 1) - 1;
        for (size_t osc = 0; osc < 2; ++osc)
        {
            const auto base = osc * oscillatorStride;
            const auto level = static_cast<double> (p[base + 4]);
            auto& phase = v.phases[static_cast<size_t> (u) * 2 + osc];
            const auto dt = std::min (midiHz (pitch + 12 * p[base + 2] + (p[base + 3] + spread * p[unisonDetune]) / 100.0) / rate, .49);
            if (level > 0)
            {
                const auto position = std::clamp (p[base + 1] + p[lfoPosition] * modulation, 0.0, 1.0) * (frames - 1);
                const int frame = std::min (frames - 2, static_cast<int> (position)); const auto blend = position - frame;
                // Choose the band-limited level whose highest harmonic stays below Nyquist.
                const auto allowed = .5 / dt;
                int mip = 0; while (mip < levels - 1 && (size / 2 >> mip) > allowed) ++mip;
                const auto* a = wavetableFrame (rounded (p[base]), frame, mip); const auto* b = wavetableFrame (rounded (p[base]), frame + 1, mip);
                const auto x = phase * size; const int i0 = static_cast<int> (x) % size, i1 = (i0 + 1) % size; const auto f = x - std::floor (x);
                const auto sampleA = a[i0] + (a[i1] - a[i0]) * f, sampleB = b[i0] + (b[i1] - b[i0]) * f;
                const auto s = level * (sampleA + (sampleB - sampleA) * blend);
                const auto width = p[base + 5] * spread;
                sumL += s * (unisonVoices == 1 ? 1.0 : std::sqrt (.5 * (1 - width))); sumR += s * (unisonVoices == 1 ? 1.0 : std::sqrt (.5 * (1 + width)));
            }
            phase = wrap (phase + dt);
        }
    }
    const auto norm = 1 / std::sqrt (static_cast<double> (unisonVoices));
    sumL *= norm; sumR *= norm;
    const int type = rounded (p[filterType]);
    if (type != 0)
    {
        const auto frequency = std::clamp (p[cutoff] * std::exp2 (p[filterEnvelope] * 5 * envelope), 20.0, rate * .49);
        const auto g = std::tan (pi * frequency / rate), k = 1 / effects::AutoFilter::qFromResonance (p[resonance]);
        sumL = filterSample (v.filters, type, sumL, g, k); sumR = filterSample (v.rightFilters, type, sumR, g, k);
    }
    const auto level = amp * ((1 - p[velocityAmp]) + p[velocityAmp] * v.velocity) * gain;
    left += sumL * level; right += sumR * level;
}

void InstrumentDSP::renderFm (Voice& v, double& left, double& right) noexcept
{
    const auto& p = settings.values;
    const auto& algorithm = fmAlgorithms[static_cast<size_t> (p[fm::algorithm])];
    const auto frequency = midiHz (v.note + bend (v.channel));
    const auto velocityScale = (1 - p[fm::velocityLevel]) + p[fm::velocityLevel] * v.velocity;
    std::array<double, 4> out {};
    for (int o = 3; o >= 0; --o)
    {
        const auto base = static_cast<size_t> (o) * fm::operatorStride;
        const auto env = v.operators[static_cast<size_t> (o)].tick();
        const auto coarse = p[base];
        const auto ratio = (coarse == 0 ? .5 : static_cast<double> (coarse)) + p[base + 1];
        double modulation = 0;
        for (int m = 0; m < 4; ++m) if ((algorithm.modulators[static_cast<size_t> (o)] >> m) & 1) modulation += out[static_cast<size_t> (m)];
        if (o == 3) modulation += p[fm::feedback] * .5 * (v.feedback[0] + v.feedback[1]);
        auto& phase = v.phases[static_cast<size_t> (o)];
        out[static_cast<size_t> (o)] = env * p[base + 2] * velocityScale * std::sin (2 * pi * (phase + modulation));
        phase = wrap (phase + std::min (frequency * ratio / rate, .49));
    }
    v.feedback[1] = v.feedback[0]; v.feedback[0] = out[3];
    double sum = 0; int carriers = 0;
    for (int o = 0; o < 4; ++o) if ((algorithm.carriers >> o) & 1) { sum += out[static_cast<size_t> (o)]; ++carriers; }
    sum /= std::max (1, carriers);
    const int type = rounded (p[fm::filterType]);
    if (type != 0)
    {
        const auto g = std::tan (pi * std::clamp (static_cast<double> (p[fm::cutoff]), 20.0, rate * .49) / rate), k = 1 / effects::AutoFilter::qFromResonance (p[fm::resonance]);
        sum = filterSample (v.filters, type == 1 ? 1 : 3, sum, g, k);
    }
    left += sum * gain; right += sum * gain;
}

void InstrumentDSP::renderDrum (Voice& v, double& left, double& right) noexcept
{
    const auto pad = static_cast<size_t> (std::find (drums::notes.begin(), drums::notes.end(), v.note) - drums::notes.begin());
    if (pad >= drums::notes.size()) { v.active = false; return; }
    const auto& p = settings.values; const auto base = pad * drums::padStride;
    const auto ratio = std::exp2 (p[base] / 12.0), decay = static_cast<double> (p[base + 1]), tone = static_cast<double> (p[base + 2]);
    const auto t = v.time / rate; v.time += 1;
    const auto noise = noiseSample (v.noise);
    static constexpr std::array<double, 6> metal { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    const auto metallic = [&] (double scale)
    {
        double sum = 0;
        for (size_t i = 0; i < metal.size(); ++i)
        {
            auto& phase = v.phases[i]; sum += phase < .5 ? 1.0 : -1.0;
            phase = wrap (phase + std::min (metal[i] * scale * ratio / rate, .49));
        }
        return sum / 6;
    };
    double sample = 0, envelope = 0;
    switch (pad)
    {
        case 0:
        {
            envelope = std::exp (-t / (.4 * decay));
            sample = std::sin (2 * pi * v.phases[0]) * envelope + tone * noise * std::exp (-t / .003) * .5;
            v.phases[0] = wrap (v.phases[0] + 50 * ratio * (1 + 4 * std::exp (-t / .03)) / rate);
            break;
        }
        case 1:
        {
            const auto body = (.6 * std::sin (2 * pi * v.phases[0]) + .4 * std::sin (2 * pi * v.phases[1])) * std::exp (-t / (.12 * decay));
            v.phases[0] = wrap (v.phases[0] + 185 * ratio / rate); v.phases[1] = wrap (v.phases[1] + 330 * ratio / rate);
            envelope = std::exp (-t / (.18 * decay));
            sample = (1 - tone) * body + tone * v.drumFilter.tick (noise) * envelope;
            envelope = std::max (envelope, std::exp (-t / (.12 * decay)));
            break;
        }
        case 2:
        {
            envelope = t < .03 ? std::exp (-std::fmod (t, .01) / .003) : std::exp (-(t - .03) / (.25 * decay));
            sample = v.drumFilter.tick (noise) * envelope * 1.5;
            if (t < .03) envelope = 1;
            break;
        }
        case 3: case 4:
        {
            envelope = std::exp (-t / ((pad == 3 ? .05 : .35) * decay));
            if (pad == 4 && v.drumState2 != 0) v.choke = std::max (0.0, v.choke - 1 / (.002 * rate));
            envelope *= v.choke;
            sample = v.drumFilter.tick (metallic (2) + .2 * noise) * envelope;
            break;
        }
        case 5: case 6:
        {
            envelope = std::exp (-t / (.35 * decay));
            sample = std::sin (2 * pi * v.phases[0]) * envelope + tone * noise * std::exp (-t / .002) * .3;
            v.phases[0] = wrap (v.phases[0] + (pad == 5 ? 100 : 160) * ratio * (1 + .5 * std::exp (-t / .05)) / rate);
            break;
        }
        default:
        {
            envelope = std::exp (-t / (1.2 * decay));
            sample = v.drumFilter.tick (metallic (2.94) + .3 * noise) * envelope;
            break;
        }
    }
    if (envelope < 1.0e-4 && t > .005) { v.active = false; return; }
    const auto level = p[base + 3] * v.velocity * gain * 2;
    left += sample * level; right += sample * level;
}

void InstrumentDSP::renderVoice (Voice& v, double& left, double& right) noexcept
{
    switch (kind)
    {
        case Kind::fmSynth:
        {
            renderFm (v, left, right);
            bool sounding = false;
            const auto& algorithm = fmAlgorithms[static_cast<size_t> (settings.values[fm::algorithm])];
            for (int o = 0; o < 4; ++o)
                if ((algorithm.carriers >> o) & 1)
                {
                    const auto& env = v.operators[static_cast<size_t> (o)];
                    sounding = sounding || (env.active() && ! (env.stage == Envelope::Stage::sustain && env.sustainLevel <= 0));
                }
            if (! sounding) v.active = false;
            return;
        }
        case Kind::drumKit: renderDrum (v, left, right); return;
        case Kind::wavetableSynth: renderWavetable (v, left, right); break;
        case Kind::sampler: renderSampler (v, left, right); if (! v.active) return; break;
        case Kind::drumSampler: renderDrumSampler (v, left, right); if (! v.active) return; break;
        default: renderPoly (v, left, right); break;
    }
    if (! v.amp.active() || (v.amp.stage == Envelope::Stage::sustain && v.amp.sustainLevel <= 0)) v.active = false;
}
double InstrumentDSP::readSample (const SampleData& s, bool right, double position) noexcept
{
    const auto* data = right ? s.right.data() : s.left.data();
    const int frames = s.frames();
    const auto index = static_cast<int> (std::floor (position));
    const auto t = position - index;
    const auto at = [data, frames] (int i) { return static_cast<double> (data[std::clamp (i, 0, frames - 1)]); };
    const auto x0 = at (index - 1), x1 = at (index), x2 = at (index + 1), x3 = at (index + 2);
    if (t == 0) return x1;
    const auto c1 = .5 * (x2 - x0), c2 = x0 - 2.5 * x1 + 2 * x2 - .5 * x3, c3 = .5 * (x3 - x0) + 1.5 * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}
void InstrumentDSP::setPadSample (int pad, const SampleData* next) noexcept
{
    if (pad < 0 || pad >= Session::drumPadCount || padSamples[static_cast<size_t> (pad)] == next) return;
    padSamples[static_cast<size_t> (pad)] = next;
    for (auto& v : voices) if (v.active && v.pad == pad) v = Voice {};
}
void InstrumentDSP::renderDrumSampler (Voice& v, double& left, double& right) noexcept
{
    using namespace drumSampler;
    const auto* s = v.pad >= 0 ? padSamples[static_cast<size_t> (v.pad)] : nullptr;
    if (s == nullptr || v.position >= s->frames()) { v.active = false; return; }
    const auto& p = settings.values;
    const auto base = static_cast<size_t> (v.pad) * padStride;
    // A shortened pad starts its fade so that it ends at the cut point.
    if (p[base + length] < 1 && v.amp.stage != Envelope::Stage::release
        && v.position >= p[base + length] * s->frames() - cutFadeMs * .001 * s->sampleRate) v.amp.release();
    const auto sumL = readSample (*s, false, v.position), sumR = readSample (*s, true, v.position);
    v.position += std::exp2 (p[base + tune] / 12.0) * s->sampleRate / rate;
    const auto level = v.amp.tick() * p[base + drumSampler::level] * v.velocity * gain;
    left += sumL * level * std::min (1.0, 1.0 - p[base + pan]); right += sumR * level * std::min (1.0, 1.0 + p[base + pan]);
}
void InstrumentDSP::setSample (const SampleData* next) noexcept
{
    if (next == sample) return;
    sample = next;
    for (auto& v : voices) if (v.active) v = Voice {};
    configure (settings);
}
// Playback region [first, second) in source frames, and the loop restart frame.
std::pair<double, double> InstrumentDSP::sampleRegion() const noexcept
{
    const auto frames = sample != nullptr ? static_cast<double> (sample->frames()) : 0.0;
    const auto& p = settings.values;
    const auto a = std::floor (std::min (p[sampler::start], p[sampler::end]) * frames), b = std::round (std::max (p[sampler::start], p[sampler::end]) * frames);
    return { std::min (a, std::max (0.0, frames - 1)), std::max (std::min (a, std::max (0.0, frames - 1)) + 1, std::min (b, frames)) };
}
void InstrumentDSP::renderSampler (Voice& v, double& left, double& right) noexcept
{
    using namespace sampler;
    if (sample == nullptr || sample->frames() < 1) { v.active = false; return; }
    const auto& p = settings.values;
    const auto [first, last] = sampleRegion();
    const bool oneShot = p[mode] == 1, looping = ! oneShot && p[loop] == 1;
    if (v.position >= last)
    {
        if (! looping) { v.active = false; return; }
        const auto restart = first + std::floor (p[loopStart] * (last - first));
        const auto length = std::max (1.0, last - std::min (restart, last - 1));
        v.position = last - length + std::fmod (v.position - last, length);
    }
    // Four-point Hermite interpolation; whole-frame positions read the source exactly.
    const int frames = sample->frames();
    const auto index = static_cast<int> (std::floor (v.position));
    const auto t = v.position - index;
    const auto at = [frames] (const float* data, int i) { return static_cast<double> (data[std::clamp (i, 0, frames - 1)]); };
    const auto hermite = [&] (const float* data)
    {
        const auto x0 = at (data, index - 1), x1 = at (data, index), x2 = at (data, index + 1), x3 = at (data, index + 2);
        if (t == 0) return x1;
        const auto c1 = .5 * (x2 - x0), c2 = x0 - 2.5 * x1 + 2 * x2 - .5 * x3, c3 = .5 * (x3 - x0) + 1.5 * (x1 - x2);
        return ((c3 * t + c2) * t + c1) * t + x1;
    };
    auto sumL = hermite (sample->left.data()), sumR = hermite (sample->right.data());
    const auto pitch = v.pitch + p[transpose] + p[detune] / 100.0 + bend (v.channel) - p[rootNote];
    v.position += std::exp2 (pitch / 12.0) * sample->sampleRate / rate;
    const int type = rounded (p[filterType]);
    if (type != 0)
    {
        const auto frequency = std::clamp (static_cast<double> (p[cutoff]), 20.0, rate * .49);
        const auto g = std::tan (pi * frequency / rate), k = 1 / effects::AutoFilter::qFromResonance (p[resonance]);
        sumL = filterSample (v.filters, type, sumL, g, k); sumR = filterSample (v.rightFilters, type, sumR, g, k);
    }
    const auto level = v.amp.tick() * ((1 - p[velocityAmp]) + p[velocityAmp] * v.velocity) * gain;
    left += sumL * level * std::min (1.0, 1.0 - p[pan]); right += sumR * level * std::min (1.0, 1.0 + p[pan]);
}
void InstrumentDSP::chokeAll() noexcept
{
    const auto step = 1.0 / std::max (1.0, std::ceil (rate * .005));
    for (auto& v : voices) if (v.active && v.fadeStep == 0) v.fadeStep = step;
    if (monophonic()) keyCount = 0;
}
void InstrumentDSP::render (float* left, float* right, int samples) noexcept
{
    for (int i = 0; i < samples; ++i)
    {
        double l = 0, r = 0;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (v.fadeStep == 0) { renderVoice (v, l, r); continue; }
            // A choked voice keeps playing under a linear fade, then stops.
            double vl = 0, vr = 0; renderVoice (v, vl, vr);
            v.fade = std::max (0.0, v.fade - v.fadeStep);
            l += vl * v.fade; r += vr * v.fade;
            if (v.fade <= 0) v = Voice {};
        }
        lfo.advance();
        if (left) left[i] += static_cast<float> (effects::finite (l));
        if (right) right[i] += static_cast<float> (effects::finite (r));
    }
}
}
