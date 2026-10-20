#pragma once
#include "EffectProcessors.h"
#include "Session.h"
#include <array>
#include <cstdint>

// Original native instruments. prepare() may allocate (shared wavetables are
// built once on the message thread); configure/note/render calls are bounded and
// allocation-free so the device plugin can call them from the audio callback.
namespace halftrack::instruments
{
namespace poly
{
enum : size_t { osc1Shape, osc1Octave, osc1Semitone, osc1Detune, osc1Level, osc1Width,
    osc2Shape, osc2Octave, osc2Semitone, osc2Detune, osc2Level, osc2Width, noiseLevel,
    filterType, cutoff, resonance, filterEnvelope, keyTracking, filterAttack, filterDecay, filterSustain, filterRelease,
    ampAttack, ampDecay, ampSustain, ampRelease, lfoShape, lfoRate, lfoPitch, lfoCutoff, lfoAmp,
    glide, voiceMode, unison, unisonDetune, velocityAmp, velocityCutoff, bendRange, volume, count };
}
namespace fm
{
// Operator o (A..D) uses indices o * 7 + { coarse, fine, level, attack, decay, sustain, release }.
enum : size_t { operatorStride = 7, algorithm = 28, feedback, filterType, cutoff, resonance, velocityLevel, bendRange, volume, voices, count };
}
namespace drums
{
// Pad p uses indices p * 4 + { tune, decay, tone, level }; pads play GM notes 36, 38, 39, 42, 46, 45, 48 and 49.
enum : size_t { padStride = 4, volume = 32, count };
constexpr std::array<int, 8> notes { 36, 38, 39, 42, 46, 45, 48, 49 };
}
namespace wavetable
{
// Oscillator o uses indices o * 6 + { table, position, octave, detune, level, spread }.
enum : size_t { oscillatorStride = 6, filterType = 12, cutoff, resonance, filterEnvelope, filterAttack, filterDecay, filterSustain, filterRelease,
    ampAttack, ampDecay, ampSustain, ampRelease, lfoRate, lfoPosition, lfoPitch, unison, unisonDetune, glide, voiceMode, velocityAmp, bendRange, volume, count };
constexpr int tableCount = 4, frames = 16, size = 1024, levels = 10;
}

struct Envelope
{
    enum class Stage { idle, attack, decay, sustain, release };
    Stage stage = Stage::idle;
    double level = 0, attackStep = 1, decayCoefficient = 0, releaseCoefficient = 0, sustainLevel = 1;
    // Linear attack; decay and release reach -60 dB relative after their times.
    void configure (double attackMs, double decayMs, double sustain, double releaseMs, double rate) noexcept;
    void start() noexcept;
    void release() noexcept { if (stage != Stage::idle) stage = Stage::release; }
    void clear() noexcept { stage = Stage::idle; level = 0; }
    double tick() noexcept;
    bool active() const noexcept { return stage != Stage::idle; }
};

double polyBlep (double phase, double increment) noexcept;
// Shapes: 0 saw, 1 pulse, 2 triangle, 3 sine. Band-limited where discontinuous.
double oscillator (int shape, double phase, double increment, double width) noexcept;
// Phase-modulation operator wiring: bit m of modulators[a][o] means operator m modulates o.
struct Algorithm { std::array<uint8_t, 4> modulators; uint8_t carriers; };
extern const std::array<Algorithm, 11> fmAlgorithms;
const float* wavetableFrame (int table, int frame, int level) noexcept; // Shared, built by prepareWavetables().
void prepareWavetables();

class InstrumentDSP
{
public:
    static constexpr int maximumVoices = 32;
    void prepare (double sampleRate, Session::EffectKind);
    void configure (const Session::EffectSettings&) noexcept;
    void reset() noexcept;
    void noteOn (int channel, int note, float velocity) noexcept; // Channels 1-16.
    void noteOff (int channel, int note) noexcept;
    void controlChange (int channel, int controller, int value) noexcept;
    void pitchWheel (int channel, int value) noexcept;
    void render (float* left, float* right, int samples) noexcept;
    int activeVoices() const noexcept;
private:
    struct Voice
    {
        bool active = false, held = false, sustained = false;
        int channel = 0, note = 0;
        uint64_t age = 0;
        double velocity = 0, pitch = 60, targetPitch = 60;
        Envelope amp, filter;
        std::array<Envelope, 4> operators;
        std::array<double, 8> phases {};
        std::array<double, 4> feedback {};
        std::array<effects::StateVariable, 2> filters {};
        std::array<effects::StateVariable, 2> rightFilters {};
        uint32_t noise = 1;
        double time = 0, choke = 1, drumState = 0, drumState2 = 0;
        effects::Biquad drumFilter;
    };
    void start (Voice&, int channel, int note, double velocity, bool retrigger) noexcept;
    void release (Voice&) noexcept;
    Voice* allocate() noexcept;
    void renderVoice (Voice&, double& left, double& right) noexcept;
    void renderPoly (Voice&, double& left, double& right) noexcept;
    void renderFm (Voice&, double& left, double& right) noexcept;
    void renderDrum (Voice&, double& left, double& right) noexcept;
    void renderWavetable (Voice&, double& left, double& right) noexcept;
    double bend (int channel) const noexcept;
    int voiceLimit() const noexcept;
    bool monophonic() const noexcept;
    Session::EffectKind kind = Session::EffectKind::polySynth;
    Session::EffectSettings settings;
    double rate = 48000, glideCoefficient = 0, gain = 1;
    uint64_t clock = 0;
    uint32_t lfoSeed = 0x6d2b79f5u;
    effects::Lfo lfo;
    std::array<Voice, maximumVoices> voices;
    std::array<bool, 16> sustainPedal {};
    std::array<double, 16> wheel {};
    // Monophonic key stack: last-note priority with return to earlier held keys.
    std::array<int, 16> keyStack {};
    std::array<int, 16> keyChannels {};
    std::array<double, 16> keyVelocities {};
    int keyCount = 0;
};
}
