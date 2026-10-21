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

// Beat Repeat after the reference's. At every Interval (plus Offset) of the transport, with the
// given Chance, it captures one Grid slice of the input and repeats it for Gate sixteenths, each
// repeat Decay quieter and, with Pitch and Pitch Decay, resampled lower. The Repeat switch holds
// a repeat for as long as it is on. Mix adds the repeats to the input, Insert mutes the input
// while repeating and Gate passes only the repeats. Variation picks nearby grid sizes. Timing is
// taken from the transport position at each block, so renders and playback agree; the random
// choices are seeded at reset, so each render repeats them and live passes vary.
class BeatRepeat
{
public:
    static constexpr std::array<double, 8> intervalBeats { .125, .25, .5, 1, 2, 4, 8, 16 }; // 1/32 to 4 bars of 4/4.
    static constexpr std::array<double, 8> gridBeats { 1, 2.0 / 3, .5, 1.0 / 3, .25, 1.0 / 6, .125, 1.0 / 12 }; // 1/4, 1/4T ... 1/32T.
    static constexpr double longestSliceSeconds = 3; // A quarter note at 20 BPM.
    void prepare (double rate) noexcept; // Allocates the capture buffer, as all preparation may.
    void configure (const float* values, double bpm) noexcept;
    void reset() noexcept;
    // Called at a block start while the transport plays or renders; without it a block is stopped.
    void syncTo (double seconds) noexcept;
    void endBlock() noexcept { synced = false; }
    void tick (double& left, double& right) noexcept;
private:
    double nextUniform() noexcept;
    int pickGrid() noexcept;
    void start (double beat, bool held) noexcept;
    double rate = 48000, bpm = 120, beat = 0, beatStep = 0, nextTrigger = 0;
    std::array<float, 17> v {};
    bool synced = false, active = false, held = false;
    double episodeEnd = 0;
    int64_t frame = 0, sliceIndex = 0;
    double sliceFrames = 0, sliceStart = 0, capturedFrames = 0;
    uint64_t random = 0;
    std::array<std::vector<float>, 2> capture;
    std::array<std::array<Biquad, 2>, 2> filter; // High-pass and low-pass per channel.
};

// Filter Delay after the reference's: three delay lines fed by the left input, the mean of both
// channels and the right input. Each line's linked high-pass and low-pass filters sit in front
// of the line inside its feedback loop, so every echo passes them again. A line is synced to
// sixteenths (with a swing percentage) or set in milliseconds, and pans to its source channel
// unless panned elsewhere. Controls per line, in order: on, filter on, frequency, bandwidth,
// sync, time ms, division, swing, feedback, pan, volume; then Dry.
class FilterDelay
{
public:
    static constexpr int controlsPerLine = 11;
    static constexpr std::array<int, 8> divisions { 1, 2, 3, 4, 5, 6, 8, 16 }; // Sixteenths.
    static constexpr double longestSeconds = 16; // 16 sixteenths at 20 BPM with +33% swing.
    // A line's delay in seconds.
    static double delaySeconds (const float* line, double bpm) noexcept
    {
        if (line[4] == 0) return line[5] * .001;
        return divisions[(size_t) std::clamp ((int) line[6], 0, 7)] * .25 * 60 / std::clamp (bpm, 20.0, 999.0) * (1 + line[7]);
    }
    void prepare (double rate) noexcept; // Allocates the delay lines.
    void configure (const float* values, double bpm) noexcept;
    void reset() noexcept;
    void tick (double& left, double& right) noexcept;
private:
    struct Line
    {
        std::vector<float> buffer;
        size_t write = 0;
        double delay = 1, feedback = 0, left = 0, right = 0, last = 0;
        bool on = false, filtered = false;
        Biquad highPass, lowPass;
    };
    double rate = 48000, dry = 1;
    std::array<Line, 3> lines;
};

// Erosion after the reference's: a very short delay modulated by a sine at Freq, by noise
// band-filtered around Freq (Filter Width octaves wide), or a blend. Amount sets the modulation
// depth, up to half a millisecond peak to peak; at 0 the input passes unchanged. Stereo Width
// moves the right channel's modulator from the left's (mono) to its own quadrature sine and
// independent noise (stereo). While the transport plays, the sine and the noise follow the
// transport position, so renders and playback agree; stopped, they run on from where they were.
class Erosion
{
public:
    static constexpr double maximumDepthSeconds = .0005;
    void prepare (double rate) noexcept;
    void configure (const float* values) noexcept; // noise blend, frequency, amount, filter width, stereo width.
    void reset() noexcept;
    void syncTo (double seconds) noexcept;
    void tick (double& left, double& right) noexcept;
private:
    double noise (int channel) noexcept;
    double rate = 48000, phase = 0, increment = 0, blend = 0, depth = 0, width = 0, frequency = 4000;
    int64_t sample = 0; // The noise is a function of the sample position.
    std::array<std::array<Biquad, 2>, 2> band; // High- and low-pass per channel.
    std::array<std::array<float, 64>, 2> history {};
    size_t write = 0;
};
}

