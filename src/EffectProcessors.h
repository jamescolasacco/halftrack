#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Original per-sample processors for the extended native device set. Every
// processor allocates only in prepare(); configure/reset/tick are bounded and
// allocation-free so NativeEffectDSP can call them from the audio callback.
namespace halftrack::effects
{
constexpr double pi = 3.14159265358979323846;
inline double finite (double v) noexcept { return std::isfinite (v) ? std::clamp (v, -1.0e12, 1.0e12) : 0; }
// Note values in beats shared by tempo-synchronized modulation controls.
constexpr std::array<double, 9> lfoDivisionBeats { .125, .25, .5, 1, 2, 4, 8, 16, 32 };
double lfoRateHz (bool synchronized, double hertz, int division, double bpm) noexcept;

struct Ramp
{
    double value = 0, target = 0;
    int remaining = 0;
    void set (double next, bool immediate, int steps) noexcept;
    double tick() noexcept;
};

struct Biquad
{
    std::array<double, 5> c { 1, 0, 0, 0, 0 };
    double z1 = 0, z2 = 0;
    double tick (double) noexcept;
    void clear() noexcept { z1 = z2 = 0; }
};
// Bilinear sections normalized as { b0, b1, b2, a1, a2 }.
std::array<double, 5> lowPassSection (double frequency, double q, double rate) noexcept;
std::array<double, 5> highPassSection (double frequency, double q, double rate) noexcept;
std::array<double, 5> allPassSection (double frequency, double q, double rate) noexcept;
std::array<double, 5> bandPassOctaves (double frequency, double octaves, double rate) noexcept;
// Peak and shelf sections; 0 dB gives the identity section.
std::array<double, 5> peakSection (double frequency, double gainDb, double q, double rate) noexcept;
std::array<double, 5> lowShelfSection (double frequency, double gainDb, double q, double rate) noexcept;
std::array<double, 5> highShelfSection (double frequency, double gainDb, double q, double rate) noexcept;

// Shapes: sine, triangle, rising saw, falling saw, square, held random.
struct Lfo
{
    uint64_t cycles = 0;
    double phase = 0, increment = 0;
    void setRate (double hertz, double rate) noexcept;
    void reset() noexcept { cycles = 0; phase = 0; }
    // Transport lock: phase becomes seconds * rate cycles, keeping renders and playback identical.
    void syncTo (double seconds, double sampleRate) noexcept;
    double value (double offsetCycles, int shape) const noexcept;
    void advance() noexcept;
};

// Zero-delay-feedback state-variable filter (trapezoidal integration).
struct StateVariable
{
    double ic1 = 0, ic2 = 0;
    // mode 0 low pass, 1 high pass, 2 unity-peak band pass, 3 notch.
    double tick (double input, double g, double k, int mode) noexcept;
    void clear() noexcept { ic1 = ic2 = 0; }
};

class Saturator
{
public:
    static double shape (int curve, double x) noexcept;
    static double softClip (double x) noexcept;
    void prepare (double rate) noexcept;
    void configure (const float* values, bool immediate) noexcept;
    void reset() noexcept;
    void tick (double& left, double& right) noexcept;
private:
    double rate = 48000, dcPole = 0;
    int curve = 0;
    bool dc = false, clip = false;
    Ramp drive, output;
    std::array<double, 2> dcInput {}, dcOutput {};
};

class Overdrive
{
public:
    void prepare (double rate) noexcept;
    void configure (const float* values, bool immediate) noexcept;
    void reset() noexcept;
    void tick (double& left, double& right) noexcept;
private:
    double rate = 48000, drive = 1, bias = 0, dynamics = 0, tone = 0, release = 0, dcPole = 0;
    std::array<Biquad, 2> band;
    std::array<double, 2> toneState {}, dcInput {}, dcOutput {}, inputLevel {}, shapedLevel {};
};

class AutoFilter
{
public:
    void prepare (double rate) noexcept;
    void configure (const float* values, float bpm, bool immediate) noexcept;
    void reset() noexcept;
    void syncTo (double seconds) noexcept;
    void tick (double& left, double& right) noexcept;
    static double qFromResonance (double resonance) noexcept { return .5 * std::pow (40.0, std::clamp (resonance, 0.0, 1.0)); }
private:
    double rate = 48000, k = 1, lfoAmount = 0, phaseOffset = 0, envelopeAmount = 0;
    double attack = 0, release = 0, envelope = 0;
    int mode = 0, shape = 0;
    bool steep = false;
    Ramp logFrequency;
    Lfo lfo;
    std::array<std::array<StateVariable, 2>, 2> filters;
};

class AutoPan
{
public:
    void prepare (double rate) noexcept;
    void configure (const float* values, float bpm, bool immediate) noexcept;
    void reset() noexcept;
    void syncTo (double seconds) noexcept;
    void tick (double& left, double& right) noexcept;
    static double channelGain (double amount, double lfoValue) noexcept { return 1.0 - amount * (1.0 - lfoValue) * .5; }
private:
    double rate = 48000, phase = 0, offset = 0;
    int shape = 0;
    Ramp amount;
    Lfo lfo;
};

// Modulated delay voices shared by chorus and flanger.
class ModulatedDelay
{
public:
    void prepare (double rate, double maximumMs);
    void configure (double delayMs, double depthMs, double rateHz, double feedback, double spreadDegrees, int voices, bool immediate) noexcept;
    void reset() noexcept;
    void syncTo (double seconds) noexcept;
    void tick (double& left, double& right) noexcept;
private:
    double tap (size_t channel, double frames) const noexcept;
    double rate = 48000, spread = 0;
    int voices = 1;
    Ramp delay, depth, feedback;
    Lfo lfo;
    std::array<std::vector<double>, 2> buffers;
    size_t cursor = 0;
};

class Phaser
{
public:
    static constexpr int maximumStages = 12;
    void prepare (double rate) noexcept;
    void configure (const float* values, bool immediate) noexcept;
    void reset() noexcept;
    void syncTo (double seconds) noexcept;
    void tick (double& left, double& right) noexcept;
    static double allPassCoefficient (double frequency, double rate) noexcept;
private:
    double rate = 48000, depth = 0, spread = 0;
    int stages = 4;
    Ramp logCenter, feedback;
    Lfo lfo;
    std::array<std::array<double, maximumStages>, 2> state {};
    std::array<double, 2> last {};
};

class ThreeBandEq
{
public:
    void prepare (double rate) noexcept;
    void configure (const float* values, bool immediate) noexcept;
    void reset() noexcept;
    void tick (double& left, double& right) noexcept;
private:
    struct Chain
    {
        std::array<Biquad, 4> sections;
        int count = 2;
        double tick (double) noexcept;
        void clear() noexcept { for (auto& s : sections) s.clear(); }
    };
    double rate = 48000;
    Ramp low, mid, high;
    // Per channel: low/high split at the first crossover, mid/high split at the
    // second and a matching all-pass that keeps the low band phase-aligned.
    std::array<Chain, 2> lowLow, lowHigh, highLow, highHigh, align;
};

class GlueCompressor
{
public:
    static constexpr std::array<double, 3> ratios { 2, 4, 10 };
    static constexpr std::array<double, 7> attackMs { .01, .1, .3, 1, 3, 10, 30 };
    static constexpr std::array<double, 6> releaseSeconds { .1, .2, .4, .6, .8, 1.2 };
    void prepare (double rate) noexcept;
    void configure (const float* values, bool immediate) noexcept;
    void reset() noexcept;
    void tick (double& left, double& right) noexcept;
private:
    double rate = 48000, threshold = 0, slope = 0, range = 70, attack = 0, release = 0, fastRelease = 0, slowRelease = 0;
    double envelope = 0, fast = 0, slow = 0;
    bool automatic = false, clip = false;
    Ramp makeup;
};

class FrequencyShifter
{
public:
    void prepare (double rate) noexcept;
    void configure (const float* values, bool immediate) noexcept;
    void reset() noexcept;
    void syncTo (double seconds) noexcept;
    void tick (double& left, double& right) noexcept;
private:
    struct Quadrature
    {
        std::array<std::array<double, 4>, 4> a {}, b {};
        double delayed = 0;
        void process (double input, double& inPhase, double& quadrature) noexcept;
        void clear() noexcept { a = {}; b = {}; delayed = 0; }
    };
    double rate = 48000, phase = 0;
    bool ring = false, opposite = false;
    Ramp shift;
    std::array<Quadrature, 2> networks;
};

// Channel EQ after the reference's: a switchable 80 Hz high-pass, a 100 Hz low shelf, a sweepable
// mid peak, and a high shelf that, when cutting, also lowers a low-pass from 20 kHz to 8 kHz.
// The corner frequencies and slopes the reference does not document are Halftrack's choices.
class ChannelEq
{
public:
    static constexpr double highPassHz = 80, lowShelfHz = 100, highShelfHz = 8000, midQ = .7, butterworthQ = .70710678118654752;
    // The low-pass cutoff for a High cut: 20 kHz at 0 dB down to 8 kHz at -15 dB, evenly in octaves.
    static double lowPassHz (double highDb) noexcept { return 20000 * std::pow (.4, std::clamp (-highDb, 0.0, 15.0) / 15); }
    void prepare (double rate) noexcept;
    void configure (const float* values) noexcept; // highPass, lowDb, midDb, midHz, highDb.
    void reset() noexcept;
    void tick (double& left, double& right) noexcept;
private:
    double rate = 48000;
    bool highPass = false, lowPass = false;
    std::array<std::array<Biquad, 5>, 2> sections; // High-pass, low shelf, mid peak, high shelf, low-pass.
};

// Pedal after the reference's: an Overdrive, Distortion or Fuzz stage driven by Gain, then a
// three-band EQ (a 100 Hz bass peak, a switchable 500 Hz / 1 kHz / 2 kHz mid that is narrower at
// lower settings, and a 3.3 kHz treble shelf) whose resonance rises with boost, an optional Sub
// shelf below 250 Hz, and a DC blocker. The curves and drive range are Halftrack's.
class Pedal
{
public:
    static constexpr double bassHz = 100, trebleHz = 3300, subHz = 250, subDb = 6, dcHz = 10;
    static constexpr std::array<double, 3> midHz { 500, 1000, 2000 }, midQ { 1.4, 1.0, .7 };
    // Gain 0-1 drives the stage from +6 dB to +42 dB; even Gain 0 distorts a loud input.
    static double driveFor (double gain) noexcept { return std::pow (10.0, (6 + 36 * std::clamp (gain, 0.0, 1.0)) / 20); }
    // Overdrive is a warm asymmetric soft clip, Distortion a tight symmetric one, Fuzz a hard,
    // strongly asymmetric one.
    static double shape (int type, double x) noexcept;
    // Boosting a band raises its resonance; cutting keeps it broad.
    static double resonance (double baseQ, double gainDb) noexcept { return baseQ * (1 + .04 * std::max (0.0, gainDb)); }
    void prepare (double rate) noexcept;
    void configure (const float* values) noexcept; // type, gain, output, bass, mid, mid frequency, treble, sub, mix.
    void reset() noexcept;
    void tick (double& left, double& right) noexcept;
private:
    double rate = 48000, drive = 1, dcPole = 0;
    int type = 0;
    bool sub = false;
    std::array<std::array<Biquad, 4>, 2> eq; // Bass, mid, treble, sub.
    std::array<double, 2> dcInput {}, dcOutput {};
};
}

