#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
using Settings = Session::EffectSettings;
constexpr double pi = 3.14159265358979323846;
#define EQ_BAND(N, F) { "band" #N "Type", "Band " #N " mode", 0, 7, 1, 0, "", "Off|Bell|Low shelf|High shelf|Low cut|High cut|Notch|Band pass" }, \
    { "band" #N "Hz", "Frequency", 20, 20000, F, 1000, " Hz" }, \
    { "band" #N "GainDb", "Gain", -24, 24, 0, 0, " dB" }, \
    { "band" #N "Q", "Q", 0.1f, 18, 0.70710678f, 1, "" }
constexpr EffectParameter eqFields[] { EQ_BAND(1, 50), EQ_BAND(2, 120), EQ_BAND(3, 300), EQ_BAND(4, 750),
    EQ_BAND(5, 1800), EQ_BAND(6, 4500), EQ_BAND(7, 9000), EQ_BAND(8, 16000),
    { "outputDb", "Output", -24, 24, 0, 0, " dB" }, { "mix", "Dry / Wet", 0, 1, 1, 0, "" } };
#undef EQ_BAND
constexpr EffectParameter compressorFields[] {
    { "thresholdDb", "Threshold", -60, 0, -18, 0, " dB" }, { "ratio", "Ratio", 1, 20, 4, 4, ":1" },
    { "attackMs", "Attack", 0.01f, 1000, 10, 10, " ms" }, { "releaseMs", "Release", 1, 3000, 100, 100, " ms" },
    { "kneeDb", "Knee", 0, 24, 6, 0, " dB" }, { "makeupDb", "Makeup", -24, 24, 0, 0, " dB" },
    { "mix", "Dry / Wet", 0, 1, 1, 0, "" }, { "detector", "Detector", 0, 1, 0, 0, "", "Peak|RMS" } };
constexpr EffectParameter delayFields[] {
    { "leftMs", "Left time", 1, 2000, 250, 250, " ms" }, { "rightMs", "Right time", 1, 2000, 375, 250, " ms" },
    { "feedback", "Feedback", 0, 0.95f, 0.35f, 0, "" }, { "mix", "Dry / Wet", 0, 1, 0.3f, 0, "" },
    { "pingPong", "Routing", 0, 1, 0, 0, "", "Stereo|Ping pong" },
    { "highCutHz", "Feedback high cut", 20, 20000, 12000, 2000, " Hz" },
    { "freeze", "Freeze", 0, 1, 0, 0, "", "Off|On" }, { "link", "Link times", 0, 1, 0, 0, "", "Independent|Linked" },
    { "sync", "Time mode", 0, 1, 0, 0, "", "Milliseconds|Project tempo" },
    { "leftDivision", "Left division", 0, 10, 3, 0, "", "1/16|1/8 triplet|1/16 dotted|1/8|1/4 triplet|1/8 dotted|1/4|1/4 dotted|1/2|1/2 dotted|Whole note" },
    { "rightDivision", "Right division", 0, 10, 5, 0, "", "1/16|1/8 triplet|1/16 dotted|1/8|1/4 triplet|1/8 dotted|1/4|1/4 dotted|1/2|1/2 dotted|Whole note" } };
constexpr EffectParameter reverbFields[] {
    { "preDelayMs", "Pre-delay", 0, 250, 15, 50, " ms" }, { "decaySeconds", "Decay", 0.1f, 20, 2, 2, " s" },
    { "size", "Room size", 0.25f, 2, 1, 0, "" }, { "dampingHz", "Damping", 500, 20000, 6000, 4000, " Hz" },
    { "width", "Stereo width", 0, 2, 1, 0, "" }, { "mix", "Dry / Wet", 0, 1, 0.25f, 0, "" } };
constexpr EffectParameter crusherFields[] {
    { "bits", "Bit depth", 1, 24, 8, 0, " bits" },
    { "downsample", "Hold interval", 1, 64, 4, 8, " samples" },
    { "driveDb", "Input drive", 0, 36, 0, 0, " dB" },
    { "outputDb", "Output", -24, 24, 0, 0, " dB" },
    { "mix", "Dry / Wet", 0, 1, 1, 0, "" } };
constexpr EffectParameter gateFields[] {
    { "thresholdDb", "Threshold", -80, 0, -36, 0, " dB" },
    { "hysteresisDb", "Hysteresis", 0, 24, 3, 0, " dB" },
    { "attackMs", "Attack", 0, 1000, 1, 10, " ms" },
    { "holdMs", "Hold", 0, 1000, 10, 50, " ms" },
    { "releaseMs", "Release", 0, 3000, 50, 100, " ms" },
    { "floorDb", "Floor", -100, 0, -100, 0, " dB" },
    { "invert", "Mode", 0, 1, 0, 0, "", "Gate|Duck" },
    { "mix", "Dry / Wet", 0, 1, 1, 0, "" } };
constexpr EffectParameter limiterFields[] {
    { "driveDb", "Input gain", 0, 36, 0, 0, " dB" },
    { "ceilingDb", "Peak ceiling", -24, 0, -.3f, 0, " dB" },
    { "releaseMs", "Release", 1, 3000, 100, 100, " ms" } };
#define LFO_DIVISIONS "1/32|1/16|1/8|1/4|1/2|1 bar|2 bars|4 bars|8 bars"
#define LFO_SHAPES "Sine|Triangle|Saw up|Saw down|Square|Random"
constexpr EffectParameter saturatorFields[] {
    { "driveDb", "Drive", -36, 36, 0, 0, " dB" },
    { "curve", "Curve", 0, 5, 0, 0, "", "Analog clip|Soft sine|Medium curve|Hard curve|Sinoid fold|Digital clip" },
    { "outputDb", "Output", -36, 36, 0, 0, " dB" },
    { "dcFilter", "DC filter", 0, 1, 0, 0, "", "Off|On" },
    { "softClip", "Output clip", 0, 1, 0, 0, "", "Off|Soft" },
    { "mix", "Dry / Wet", 0, 1, 1, 0, "" } };
constexpr EffectParameter overdriveFields[] {
    { "frequencyHz", "Filter frequency", 50, 20000, 1500, 1000, " Hz" },
    { "widthOctaves", "Bandwidth", .5f, 9, 6, 3, " oct" },
    { "drive", "Drive", 0, 1, .5f, 0, "%" },
    { "tone", "Tone", 0, 1, .5f, 0, "%" },
    { "dynamics", "Dynamics", 0, 1, .5f, 0, "%" },
    { "mix", "Dry / Wet", 0, 1, 1, 0, "" } };
constexpr EffectParameter autoFilterFields[] {
    { "filterType", "Filter", 0, 3, 0, 0, "", "Low pass|High pass|Band pass|Notch" },
    { "slope", "Slope", 0, 1, 0, 0, "", "12 dB/oct|24 dB/oct" },
    { "frequencyHz", "Frequency", 20, 20000, 2000, 1000, " Hz" },
    { "resonance", "Resonance", 0, 1, .2f, 0, "%" },
    { "lfoAmount", "LFO amount", 0, 1, 0, 0, "%" },
    { "lfoRateHz", "LFO rate", .01f, 40, 1, 1, " Hz" },
    { "lfoSync", "LFO timing", 0, 1, 0, 0, "", "Hertz|Project tempo" },
    { "lfoDivision", "LFO division", 0, 8, 3, 0, "", LFO_DIVISIONS },
    { "lfoShape", "LFO shape", 0, 5, 0, 0, "", LFO_SHAPES },
    { "lfoPhaseDeg", "Stereo phase", 0, 360, 0, 180, " deg" },
    { "envelopeAmount", "Envelope amount", -1, 1, 0, 0, "%" },
    { "attackMs", "Envelope attack", .1f, 100, 6, 10, " ms" },
    { "releaseMs", "Envelope release", 1, 2000, 200, 200, " ms" },
    { "mix", "Dry / Wet", 0, 1, 1, 0, "" } };
constexpr EffectParameter autoPanFields[] {
    { "amount", "Amount", 0, 1, .5f, 0, "%" },
    { "rateHz", "Rate", .01f, 40, 1, 1, " Hz" },
    { "sync", "Timing", 0, 1, 0, 0, "", "Hertz|Project tempo" },
    { "division", "Division", 0, 8, 3, 0, "", LFO_DIVISIONS },
    { "shape", "Shape", 0, 5, 0, 0, "", LFO_SHAPES },
    { "phaseDeg", "Stereo phase", 0, 360, 180, 180, " deg" },
    { "offsetDeg", "Offset", 0, 360, 0, 180, " deg" } };
constexpr EffectParameter chorusFields[] {
    { "delayMs", "Delay", 1, 30, 7, 7, " ms" },
    { "depthMs", "Depth", 0, 10, 2, 2, " ms" },
    { "rateHz", "Rate", .01f, 20, .8f, 1, " Hz" },
    { "voices", "Voices", 0, 2, 1, 0, "", "One|Two|Three" },
    { "spreadDeg", "Stereo spread", 0, 180, 90, 90, " deg" },
    { "feedback", "Feedback", -.95f, .95f, 0, 0, "" },
    { "mix", "Dry / Wet", 0, 1, .5f, 0, "" } };
constexpr EffectParameter phaserFields[] {
    { "stages", "Stages", 0, 5, 1, 0, "", "2|4|6|8|10|12" },
    { "centerHz", "Center", 50, 10000, 800, 800, " Hz" },
    { "depth", "Depth", 0, 1, .5f, 0, "%" },
    { "rateHz", "Rate", .01f, 20, .5f, 1, " Hz" },
    { "feedback", "Feedback", -.95f, .95f, .4f, 0, "" },
    { "spreadDeg", "Stereo spread", 0, 180, 90, 90, " deg" },
    { "mix", "Dry / Wet", 0, 1, .5f, 0, "" } };
constexpr EffectParameter flangerFields[] {
    { "delayMs", "Delay", .1f, 20, 2.5f, 3, " ms" },
    { "depthMs", "Depth", 0, 10, 2, 2, " ms" },
    { "rateHz", "Rate", .01f, 20, .3f, 1, " Hz" },
    { "feedback", "Feedback", -.95f, .95f, .5f, 0, "" },
    { "spreadDeg", "Stereo spread", 0, 180, 90, 90, " deg" },
    { "mix", "Dry / Wet", 0, 1, .5f, 0, "" } };
constexpr EffectParameter eqThreeFields[] {
    { "lowDb", "Low", -70, 6, 0, 0, " dB" },
    { "midDb", "Mid", -70, 6, 0, 0, " dB" },
    { "highDb", "High", -70, 6, 0, 0, " dB" },
    { "lowHz", "Low / mid", 50, 5000, 250, 500, " Hz" },
    { "highHz", "Mid / high", 200, 18000, 2500, 2500, " Hz" },
    { "slope", "Slope", 0, 1, 0, 0, "", "24 dB/oct|48 dB/oct" },
    { "lowOn", "Low band", 0, 1, 1, 0, "", "Off|On" },
    { "midOn", "Mid band", 0, 1, 1, 0, "", "Off|On" },
    { "highOn", "High band", 0, 1, 1, 0, "", "Off|On" } };
constexpr EffectParameter glueFields[] {
    { "thresholdDb", "Threshold", -40, 0, -10, 0, " dB" },
    { "ratio", "Ratio", 0, 2, 0, 0, "", "2:1|4:1|10:1" },
    { "attack", "Attack", 0, 6, 3, 0, "", "0.01 ms|0.1 ms|0.3 ms|1 ms|3 ms|10 ms|30 ms" },
    { "release", "Release", 0, 6, 2, 0, "", "0.1 s|0.2 s|0.4 s|0.6 s|0.8 s|1.2 s|Auto" },
    { "makeupDb", "Makeup", 0, 20, 0, 0, " dB" },
    { "rangeDb", "Range", 0, 70, 70, 0, " dB" },
    { "softClip", "Soft clip", 0, 1, 0, 0, "", "Off|On" },
    { "mix", "Dry / Wet", 0, 1, 1, 0, "" } };
constexpr EffectParameter shifterFields[] {
    { "shiftHz", "Frequency", -5000, 5000, 100, 0, " Hz" },
    { "mode", "Mode", 0, 1, 0, 0, "", "Shift|Ring" },
    { "wide", "Stereo", 0, 1, 0, 0, "", "Same|Opposite" },
    { "mix", "Dry / Wet", 0, 1, 1, 0, "" } };
#undef LFO_DIVISIONS
#undef LFO_SHAPES
constexpr std::array<double, 11> delayBeats { .25, 1.0/3, .375, .5, 2.0/3, .75, 1, 1.5, 2, 3, 4 };
constexpr std::array<double, 8> roomSeconds { .0297, .0371, .0411, .0437, .0531, .0617, .0713, .0799 };
double finiteSample (double v) noexcept { return std::isfinite (v) ? std::clamp (v, -1.0e12, 1.0e12) : 0; }
}
const char* effectName (Kind kind) noexcept
{
    switch (kind) { case Kind::equalizer: return "Eight-band EQ"; case Kind::compressor: return "Compressor";
        case Kind::delay: return "Stereo Delay"; case Kind::reverb: return "Room Reverb"; case Kind::bitcrusher: return "Bitcrusher"; case Kind::gate: return "Noise Gate"; case Kind::limiter: return "Peak Limiter";
        case Kind::saturator: return "Saturator"; case Kind::overdrive: return "Overdrive"; case Kind::autoFilter: return "Auto Filter";
        case Kind::autoPan: return "Auto Pan"; case Kind::chorus: return "Chorus"; case Kind::phaser: return "Phaser"; case Kind::flanger: return "Flanger";
        case Kind::eqThree: return "Three-band EQ"; case Kind::glueCompressor: return "Bus Compressor"; case Kind::frequencyShifter: return "Frequency Shifter"; }
    return "Native effect";
}
std::span<const EffectParameter> effectParameters (Kind kind) noexcept
{
    switch (kind) { case Kind::equalizer: return eqFields; case Kind::compressor: return compressorFields;
        case Kind::delay: return delayFields; case Kind::reverb: return reverbFields; case Kind::bitcrusher: return crusherFields; case Kind::gate: return gateFields; case Kind::limiter: return limiterFields;
        case Kind::saturator: return saturatorFields; case Kind::overdrive: return overdriveFields; case Kind::autoFilter: return autoFilterFields;
        case Kind::autoPan: return autoPanFields; case Kind::chorus: return chorusFields; case Kind::phaser: return phaserFields; case Kind::flanger: return flangerFields;
        case Kind::eqThree: return eqThreeFields; case Kind::glueCompressor: return glueFields; case Kind::frequencyShifter: return shifterFields; }
    return {};
}
const char* effectCategory (Kind kind) noexcept
{
    switch (kind)
    {
        case Kind::compressor: case Kind::gate: case Kind::limiter: case Kind::glueCompressor: return "Dynamics";
        case Kind::equalizer: case Kind::eqThree: case Kind::autoFilter: return "EQ & Filters";
        case Kind::delay: case Kind::reverb: return "Delay & Reverb";
        case Kind::bitcrusher: case Kind::saturator: case Kind::overdrive: return "Distortion";
        case Kind::autoPan: case Kind::chorus: case Kind::phaser: case Kind::flanger: case Kind::frequencyShifter: return "Modulation";
    }
    return "Effects";
}
bool effectUsesTempo (Kind kind) noexcept { return kind == Kind::delay || kind == Kind::autoFilter || kind == Kind::autoPan; }
double effectLatencySeconds (Kind kind, double rate) noexcept
{ return kind == Kind::limiter && rate > 0 ? std::ceil (rate * .001) / rate : 0.0; }
double effectTailSeconds (Kind kind, double rate) noexcept
{
    switch (kind)
    {
        case Kind::delay: return 1800.0;
        case Kind::reverb: return 20.25;
        case Kind::equalizer: return 10.0;
        case Kind::bitcrusher: return .008;
        case Kind::limiter: return effectLatencySeconds (kind, rate);
        case Kind::saturator: case Kind::overdrive: case Kind::autoFilter: return .5;
        case Kind::chorus: case Kind::flanger: return 3.0;
        case Kind::phaser: return 1.0;
        case Kind::eqThree: case Kind::frequencyShifter: return .1;
        case Kind::compressor: case Kind::gate: case Kind::autoPan: case Kind::glueCompressor: return 0.0;
    }
    return 0.0;
}
int effectParameterIndex (Kind kind, const char* key) noexcept
{
    const auto fields = effectParameters (kind);
    for (size_t i = 0; i < fields.size(); ++i) if (std::string_view (fields[i].key) == key) return static_cast<int> (i);
    return -1;
}
Settings defaultEffectSettings (Kind kind) noexcept
{
    Settings s; s.kind = kind; const auto fields = effectParameters (kind);
    for (size_t i = 0; i < fields.size(); ++i) s.values[i] = fields[i].initial;
    return s;
}
bool validEffectSettings (const Settings& s) noexcept
{
    const auto fields = effectParameters (s.kind);
    if (fields.empty() || ! std::isfinite (s.tempoBpm) || s.tempoBpm < 20 || s.tempoBpm > 999) return false;
    for (size_t i = 0; i < s.values.size(); ++i)
    {
        const auto v = s.values[i];
        if (! std::isfinite (v)) return false;
        if (i >= fields.size()) { if (v != 0) return false; continue; }
        if (v < fields[i].minimum || v > fields[i].maximum || (fields[i].choices && std::floor (v) != v)) return false;
    }
    return true;
}
NativeEffectDSP::NativeEffectDSP() : settings (defaultEffectSettings (Kind::equalizer)) {}
void NativeEffectDSP::prepare (double sampleRate, Kind kind)
{
    rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 384000.0) : 48000;
    if (settings.kind != kind) settings = defaultEffectSettings (kind);
    const auto delaySize = kind == Kind::delay ? static_cast<size_t> (std::ceil (rate * 12)) + 4
        : kind == Kind::reverb ? static_cast<size_t> (std::ceil (rate * .25)) + 4
        : kind == Kind::limiter ? static_cast<size_t> (std::ceil (rate * .001)) : 0;
    for (auto& buffer : delays) buffer.assign (delaySize, 0);
    for (size_t i = 0; i < room.size(); ++i)
        room[i].assign (kind == Kind::reverb ? static_cast<size_t> (std::ceil (rate * roomSeconds[i] * 2)) + 1 : 0, 0);
    limiterPeaks.assign (kind == Kind::limiter ? delaySize + 1 : 0, 0);
    limiterPeakFrames.assign (limiterPeaks.size(), 0);
    modulatedDelay.prepare (rate, kind == Kind::chorus ? 40 : kind == Kind::flanger ? 30 : 0);
    saturator.prepare (rate); overdrive.prepare (rate); autoFilter.prepare (rate); autoPan.prepare (rate);
    phaser.prepare (rate); eqThree.prepare (rate); glue.prepare (rate); shifter.prepare (rate);
    processed = false; reset(); configure (settings);
}
void NativeEffectDSP::reset() noexcept
{
    for (auto& channel : eq) for (auto& band : channel) band.z1 = band.z2 = 0;
    for (auto& buffer : delays) std::fill (buffer.begin(), buffer.end(), 0);
    for (auto& buffer : room) std::fill (buffer.begin(), buffer.end(), 0);
    crusherHeld = {}; crusherCountdown = 0;
    gateEnvelope = 0; gateHoldRemaining = 0; gateOpen = false;
    limiterFrame = 0; limiterHead = limiterSize = 0; limiterGain = 1;
    roomCursor = {}; roomTone = {}; delayTone = {}; cursor = 0; envelope = 0; processed = false; ramp = 0;
    saturator.reset(); overdrive.reset(); autoFilter.reset(); autoPan.reset(); modulatedDelay.reset();
    phaser.reset(); eqThree.reset(); glue.reset(); shifter.reset();
    outputGain = gainTarget; wet = wetTarget; delayLeft = delayTargetLeft; delayRight = delayTargetRight;
}
std::array<double, 5> NativeEffectDSP::eqCoefficients (int type, double frequency, double gain, double q, double sampleRate) noexcept
{
    if (type == 0 || ((type == 1 || type == 2 || type == 3) && gain == 0)) return { 1, 0, 0, 0, 0 };
    const auto omega = 2 * pi * std::clamp (frequency, 5.0, sampleRate * .45) / sampleRate;
    const auto c = std::cos (omega), alpha = std::sin (omega) / (2 * std::clamp (q, .1, 18.0));
    const auto a = std::pow (10.0, gain / 40.0), root = 2 * std::sqrt (a) * alpha;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
        case 1: b0 = 1 + alpha * a; b1 = -2 * c; b2 = 1 - alpha * a; a0 = 1 + alpha / a; a1 = -2 * c; a2 = 1 - alpha / a; break;
        case 2: b0 = a * ((a+1)-(a-1)*c+root); b1 = 2*a*((a-1)-(a+1)*c); b2 = a*((a+1)-(a-1)*c-root);
                a0 = (a+1)+(a-1)*c+root; a1 = -2*((a-1)+(a+1)*c); a2 = (a+1)+(a-1)*c-root; break;
        case 3: b0 = a*((a+1)+(a-1)*c+root); b1 = -2*a*((a-1)+(a+1)*c); b2 = a*((a+1)+(a-1)*c-root);
                a0 = (a+1)-(a-1)*c+root; a1 = 2*((a-1)-(a+1)*c); a2 = (a+1)-(a-1)*c-root; break;
        case 4: b0 = (1+c)/2; b1 = -(1+c); b2 = b0; a0 = 1+alpha; a1 = -2*c; a2 = 1-alpha; break;
        case 5: b0 = (1-c)/2; b1 = 1-c; b2 = b0; a0 = 1+alpha; a1 = -2*c; a2 = 1-alpha; break;
        case 6: b0 = 1; b1 = -2*c; b2 = 1; a0 = 1+alpha; a1 = -2*c; a2 = 1-alpha; break;
        case 7: b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1+alpha; a1 = -2*c; a2 = 1-alpha; break;
        default: break;
    }
    return { b0/a0, b1/a0, b2/a0, a1/a0, a2/a0 };
}
double NativeEffectDSP::Filter::tick (double input) noexcept
{
    const auto out = finiteSample (c[0] * input + z1);
    z1 = finiteSample (c[1] * input - c[3] * out + z2); z2 = finiteSample (c[2] * input - c[4] * out);
    return out;
}
void NativeEffectDSP::configure (const Settings& next) noexcept
{
    if (next.kind != settings.kind || ! validEffectSettings (next)) return;
    const auto previous = settings; settings = next; const auto& v = settings.values;
    gainTarget = 1;
    if (settings.kind == Kind::equalizer)
    {
        for (size_t b = 0; b < 8; ++b) for (auto& channel : eq)
        {
            channel[b].c = eqCoefficients (static_cast<int> (v[4*b]), v[4*b+1], v[4*b+2], v[4*b+3], rate);
            if (previous.values[4*b] != v[4*b]) channel[b].z1 = channel[b].z2 = 0;
        }
        gainTarget = std::pow (10.0, v[32]/20.0); wetTarget = v[33];
    }
    if (settings.kind == Kind::compressor)
    {
        if (v[7] != previous.values[7]) envelope = 0;
        attack = std::exp (-1.0 / (rate * v[2] * .001)); release = std::exp (-1.0 / (rate * v[3] * .001));
        gainTarget = std::pow (10.0, v[5]/20.0); wetTarget = v[6];
    }
    if (settings.kind == Kind::delay)
    {
        delayTargetLeft = v[8] != 0 ? delayBeats[static_cast<size_t>(v[9])] * rate * 60 / settings.tempoBpm : v[0] * rate * .001;
        delayTargetRight = v[7] != 0 ? delayTargetLeft : v[8] != 0 ? delayBeats[static_cast<size_t>(v[10])] * rate * 60 / settings.tempoBpm : v[1] * rate * .001;
        damping = std::exp (-2 * pi * std::min (static_cast<double> (v[5]), rate * .45) / rate); wetTarget = v[3];
    }
    if (settings.kind == Kind::reverb)
    {
        delayTargetLeft = delayTargetRight = v[0] * rate * .001;
        damping = std::exp (-2 * pi * std::min (static_cast<double> (v[3]), rate * .45) / rate); wetTarget = v[5];
        for (size_t i = 0; i < room.size(); ++i)
        {
            roomLength[i] = std::max (1, static_cast<int> (std::lround (rate * roomSeconds[i] * v[2])));
            roomFeedback[i] = std::pow (10.0, -3.0 * roomLength[i] / (rate * v[1]));
            if (roomCursor[i] >= roomLength[i]) roomCursor[i] = 0;
        }
    }
    if (settings.kind == Kind::bitcrusher)
    {
        crusherLevels = std::exp2 (v[0] - 1.0);
        crusherDrive = std::pow (10.0, v[2] / 20.0);
        gainTarget = std::pow (10.0, v[3] / 20.0); wetTarget = v[4];
    }
    if (settings.kind == Kind::gate)
    {
        gateThreshold = std::pow (10.0, v[0] / 20.0);
        gateCloseThreshold = std::pow (10.0, (v[0] - v[1]) / 20.0);
        gateFloor = v[5] <= -100 ? 0 : std::pow (10.0, v[5] / 20.0);
        gateAttackStep = 1.0 / std::max (1.0, std::ceil (rate * v[2] * .001));
        gateReleaseStep = 1.0 / std::max (1.0, std::ceil (rate * v[4] * .001));
        gateHoldSamples = static_cast<int> (std::ceil (rate * v[3] * .001));
        gateHoldRemaining = std::min (gateHoldRemaining, gateHoldSamples); wetTarget = v[7];
    }
    if (settings.kind == Kind::limiter)
    {
        limiterDrive = std::pow (10.0, v[0] / 20.0);
        limiterCeiling = std::pow (10.0, v[1] / 20.0);
        limiterRelease = std::exp (-1.0 / (rate * v[2] * .001)); wetTarget = 1;
    }
    const bool immediate = ! processed;
    switch (settings.kind)
    {
        case Kind::saturator: saturator.configure (v.data(), immediate); wetTarget = v[5]; break;
        case Kind::overdrive: overdrive.configure (v.data(), immediate); wetTarget = v[5]; break;
        case Kind::autoFilter: autoFilter.configure (v.data(), settings.tempoBpm, immediate); wetTarget = v[13]; break;
        case Kind::autoPan: autoPan.configure (v.data(), settings.tempoBpm, immediate); wetTarget = 1; break;
        case Kind::chorus: modulatedDelay.configure (v[0], v[1], v[2], v[5], v[4], static_cast<int> (v[3]) + 1, immediate); wetTarget = v[6]; break;
        case Kind::phaser: phaser.configure (v.data(), immediate); wetTarget = v[6]; break;
        case Kind::flanger: modulatedDelay.configure (v[0], v[1], v[2], v[3], v[4], 1, immediate); wetTarget = v[5]; break;
        case Kind::eqThree: eqThree.configure (v.data(), immediate); wetTarget = 1; break;
        case Kind::glueCompressor: glue.configure (v.data(), immediate); wetTarget = v[7]; break;
        case Kind::frequencyShifter: shifter.configure (v.data(), immediate); wetTarget = v[3]; break;
        default: break;
    }
    if (! processed) { wet = wetTarget; outputGain = gainTarget; delayLeft = delayTargetLeft; delayRight = delayTargetRight; ramp = 0; }
    else ramp = std::max (1, static_cast<int> (rate * .005));
}
double NativeEffectDSP::delayRead (int channel, double frames) const noexcept
{
    const auto& data = delays[static_cast<size_t> (channel)]; if (data.empty()) return 0;
    frames = std::clamp (frames, 1.0, static_cast<double> (data.size()-2));
    const auto whole = static_cast<size_t> (frames); const auto fraction = frames - whole;
    const auto a = (cursor + data.size() - whole) % data.size(), b = (a + data.size() - 1) % data.size();
    return data[a] * (1-fraction) + data[b] * fraction;
}
void NativeEffectDSP::processSamples (float* left, float* right, int count) noexcept
{
    if (! left || count <= 0) return;
    processed = true; const auto& v = settings.values;
    for (int frame = 0; frame < count; ++frame)
    {
        if (ramp > 0)
        {
            wet += (wetTarget-wet)/ramp; outputGain += (gainTarget-outputGain)/ramp;
            delayLeft += (delayTargetLeft-delayLeft)/ramp; delayRight += (delayTargetRight-delayRight)/ramp; --ramp;
        }
        const double l = finiteSample (left[frame]), r = right ? finiteSample (right[frame]) : l;
        double outL = l, outR = r;
        switch (settings.kind)
        {
            case Kind::equalizer:
                for (size_t b = 0; b < 8; ++b) { outL = eq[0][b].tick (outL); outR = eq[1][b].tick (outR); }
                break;
            case Kind::compressor:
            {
                const auto peak = std::max (std::abs (l), std::abs (r));
                const auto detected = v[7] != 0 ? peak * peak : peak;
                const auto coefficient = detected > envelope ? attack : release;
                envelope = coefficient * envelope + (1-coefficient) * detected;
                const auto level = v[7] != 0 ? std::sqrt (envelope) : envelope;
                const auto over = 20 * std::log10 (std::max (level, 1.0e-20)) - v[0];
                const auto slope = 1.0/v[1] - 1.0;
                double reduction = over > 0 ? slope * over : 0;
                if (v[4] > 0 && std::abs (over) < v[4]/2) reduction = slope * std::pow (over + v[4]/2, 2) / (2*v[4]);
                const auto gain = std::pow (10.0, reduction/20.0); outL *= gain; outR *= gain;
                break;
            }
            case Kind::delay:
            {
                if (delays[0].empty()) break;
                outL = delayRead (0, delayLeft); outR = delayRead (1, delayRight);
                const bool frozen = v[6] != 0, ping = v[4] != 0;
                delayTone[0] = damping * delayTone[0] + (1-damping)*outL;
                delayTone[1] = damping * delayTone[1] + (1-damping)*outR;
                const auto feedback = frozen ? 1.0 : v[2];
                const auto feedbackL = frozen ? outL : delayTone[0], feedbackR = frozen ? outR : delayTone[1];
                delays[0][cursor] = finiteSample ((frozen ? 0 : ping ? .5*(l+r) : l) + feedback*(ping ? feedbackR : feedbackL));
                delays[1][cursor] = finiteSample ((frozen || ping ? 0 : r) + feedback*(ping ? feedbackL : feedbackR));
                cursor = (cursor+1) % delays[0].size(); break;
            }
            case Kind::limiter:
            {
                if (delays[0].empty()) break;
                const auto drivenL = l * limiterDrive, drivenR = r * limiterDrive;
                const auto peak = std::max (std::abs (drivenL), std::abs (drivenR));
                const auto capacity = limiterPeaks.size();
                // A preallocated monotonic deque includes the delayed sample and
                // all lookahead frames. Each entry is inserted/removed once.
                while (limiterSize > 0 && limiterFrame - limiterPeakFrames[limiterHead] > delays[0].size())
                { limiterHead = (limiterHead + 1) % capacity; --limiterSize; }
                while (limiterSize > 0 && limiterPeaks[(limiterHead + limiterSize - 1) % capacity] <= peak)
                    --limiterSize;
                const auto back = (limiterHead + limiterSize) % capacity;
                limiterPeaks[back] = peak; limiterPeakFrames[back] = limiterFrame; ++limiterSize;
                const auto maximum = limiterPeaks[limiterHead];
                const auto bound = maximum > limiterCeiling ? limiterCeiling / maximum : 1.0;
                limiterGain = std::min (bound, 1.0 + limiterRelease * (limiterGain - 1.0));
                outL = delays[0][cursor] * limiterGain; outR = delays[1][cursor] * limiterGain;
                delays[0][cursor] = drivenL; delays[1][cursor] = drivenR;
                cursor = (cursor + 1) % delays[0].size(); ++limiterFrame;
                break;
            }
            case Kind::gate:
            {
                const auto detected = std::max (std::abs (l), std::abs (r));
                if (detected >= gateThreshold || (gateOpen && detected >= gateCloseThreshold))
                { gateOpen = true; gateHoldRemaining = gateHoldSamples; }
                else if (gateOpen)
                {
                    if (gateHoldRemaining > 0) --gateHoldRemaining;
                    else gateOpen = false;
                }
                gateEnvelope = gateOpen ? std::min (1.0, gateEnvelope + gateAttackStep)
                    : std::max (0.0, gateEnvelope - gateReleaseStep);
                const auto opening = v[6] != 0 ? 1.0 - gateEnvelope : gateEnvelope;
                const auto gain = gateFloor + (1.0 - gateFloor) * opening;
                outL *= gain; outR *= gain;
                break;
            }
            case Kind::bitcrusher:
            {
                if (crusherCountdown <= 0)
                {
                    // Signed PCM-style nearest quantization with saturation.
                    const auto quantize = [this] (double sample) noexcept
                    {
                        const auto code = std::round (std::clamp (sample * crusherDrive, -1.0, 1.0) * crusherLevels);
                        return std::clamp (code, -crusherLevels, crusherLevels - 1.0) / crusherLevels;
                    };
                    crusherHeld = { quantize (l), quantize (r) };
                    crusherCountdown += v[1];
                }
                --crusherCountdown;
                outL = crusherHeld[0]; outR = crusherHeld[1];
                break;
            }
            case Kind::reverb:
            {
                if (delays[0].empty() || room[0].empty()) break;
                const auto preL = delayLeft < 1 ? l : delayRead (0, delayLeft), preR = delayRight < 1 ? r : delayRead (1, delayRight);
                delays[0][cursor] = l; delays[1][cursor] = r; cursor = (cursor+1)%delays[0].size();
                std::array<double, 8> taps {}, feedback {};
                for (size_t i = 0; i < 8; ++i)
                {
                    taps[i] = room[i][static_cast<size_t> (roomCursor[i])];
                    roomTone[i] = damping*roomTone[i] + (1-damping)*taps[i]; feedback[i] = roomTone[i]*roomFeedback[i];
                }
                // An orthonormal Householder feedback matrix preserves energy before decay.
                double sum = 0; for (auto value : feedback) sum += value;
                outL = outR = 0;
                for (size_t i = 0; i < 8; ++i)
                {
                    room[i][static_cast<size_t> (roomCursor[i])] = finiteSample ((i%2 ? preR : preL)*.25 + feedback[i] - .25*sum);
                    roomCursor[i] = (roomCursor[i]+1)%roomLength[i];
                    outL += taps[i]*(i%2 ? -.25 : .25); outR += taps[i]*((i/2)%2 ? -.25 : .25);
                }
                const auto mid = .5*(outL+outR), side = .5*(outL-outR)*v[4]; outL = mid+side; outR = mid-side;
                break;
            }
            case Kind::saturator: saturator.tick (outL, outR); break;
            case Kind::overdrive: overdrive.tick (outL, outR); break;
            case Kind::autoFilter: autoFilter.tick (outL, outR); break;
            case Kind::autoPan: autoPan.tick (outL, outR); break;
            case Kind::chorus: case Kind::flanger: modulatedDelay.tick (outL, outR); break;
            case Kind::phaser: phaser.tick (outL, outR); break;
            case Kind::eqThree: eqThree.tick (outL, outR); break;
            case Kind::glueCompressor: glue.tick (outL, outR); break;
            case Kind::frequencyShifter: shifter.tick (outL, outR); break;
        }
        left[frame] = static_cast<float> (finiteSample (l*(1-wet) + wet*outL*outputGain));
        if (right) right[frame] = static_cast<float> (finiteSample (r*(1-wet) + wet*outR*outputGain));
    }
}
void NativeEffectDSP::setTimelinePosition (double seconds) noexcept
{
    switch (settings.kind)
    {
        case Kind::autoFilter: autoFilter.syncTo (seconds); break;
        case Kind::autoPan: autoPan.syncTo (seconds); break;
        case Kind::chorus: case Kind::flanger: modulatedDelay.syncTo (seconds); break;
        case Kind::phaser: phaser.syncTo (seconds); break;
        case Kind::frequencyShifter: shifter.syncTo (seconds); break;
        default: break;
    }
}
bool EffectParameterQueue::push (const Settings& value) noexcept
{
    const auto w = writeIndex.load (std::memory_order_relaxed), next = (w+1)%capacity;
    if (next == readIndex.load (std::memory_order_acquire)) return false;
    packets[w] = value; writeIndex.store (next, std::memory_order_release); return true;
}
bool EffectParameterQueue::pop (Settings& value) noexcept
{
    const auto r = readIndex.load (std::memory_order_relaxed);
    if (r == writeIndex.load (std::memory_order_acquire)) return false;
    value = packets[r]; readIndex.store ((r+1)%capacity, std::memory_order_release); return true;
}
void EffectParameterQueue::resetWhileStopped() noexcept
{ readIndex.store (0, std::memory_order_relaxed); writeIndex.store (0, std::memory_order_relaxed); }
NativeEffectPlugin::NativeEffectPlugin (tracktion::PluginCreationInfo info)
    : Plugin (info), kind (static_cast<Kind> (static_cast<int> (state.getProperty ("effectKind", 0))))
{ publish(); startTimerHz (30); }
NativeEffectPlugin::~NativeEffectPlugin() { stopTimer(); }
Settings NativeEffectPlugin::readSettings() const
{
    auto s = defaultEffectSettings (kind); const auto fields = effectParameters (kind);
    if (effectUsesTempo (kind)) s.tempoBpm = static_cast<float> (edit.tempoSequence.getTempo (0)->getBpm());
    for (size_t i = 0; i < fields.size(); ++i) s.values[i] = state.getProperty (fields[i].key, fields[i].initial);
    return validEffectSettings (s) ? s : defaultEffectSettings (kind);
}
void NativeEffectPlugin::writeSettings (const Settings& s)
{
    if (s.kind != kind || ! validEffectSettings (s)) return;
    writing = true; const auto fields = effectParameters (kind);
    for (size_t i = 0; i < fields.size(); ++i) state.setProperty (fields[i].key, s.values[i], getUndoManager());
    writing = false; publish();
}
juce::Result NativeEffectPlugin::validateState (const juce::ValueTree& tree)
{
    if (tree.hasType (tracktion::IDs::PLUGIN) && tree.getProperty (tracktion::IDs::type).toString() == xmlTypeName)
    {
        const auto parse = [&tree] (const char* key, double fallback, double& output)
        {
            if (! tree.hasProperty (key)) { output = fallback; return true; }
            const auto value = tree[key].toString().trim().toStdString(); char* end = nullptr;
            output = std::strtod (value.c_str(), &end);
            return ! value.empty() && end == value.c_str()+value.size() && std::isfinite (output);
        };
        double type = 0;
        if (! parse ("effectKind", 0, type) || type < 0 || type >= Session::effectKindCount || std::floor (type) != type)
            return juce::Result::fail ("The project contains an invalid native effect type.");
        for (const auto& f : effectParameters (static_cast<Kind> (static_cast<int> (type))))
        {
            double v = 0;
            if (! parse (f.key, f.initial, v) || v < f.minimum || v > f.maximum || (f.choices && std::floor (v) != v))
                return juce::Result::fail ("The project contains invalid native effect settings.");
        }
    }
    for (const auto& child : tree) if (auto result = validateState (child); result.failed()) return result;
    return juce::Result::ok();
}
void NativeEffectPlugin::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{ Plugin::valueTreePropertyChanged (tree, property); if (tree == state && ! writing) publish(); }
void NativeEffectPlugin::publish() { pendingSettings = readSettings(); pending = ! parameters.push (pendingSettings); }
void NativeEffectPlugin::timerCallback()
{
    if (effectUsesTempo (kind) && static_cast<float> (edit.tempoSequence.getTempo (0)->getBpm()) != pendingSettings.tempoBpm)
        publish();
    else if (pending) pending = ! parameters.push (pendingSettings);
}
void NativeEffectPlugin::prepareForOfflineRender() { JUCE_ASSERT_MESSAGE_THREAD parameters.resetWhileStopped(); publish(); }
void NativeEffectPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{ sampleRate = info.sampleRate; processor.prepare (sampleRate, kind); }
void NativeEffectPlugin::reset() { processor.reset(); }
void NativeEffectPlugin::applyToBuffer (const tracktion::PluginRenderContext& context)
{
    const juce::ScopedNoDenormals noDenormals; Settings next; bool changed = false;
    for (int i = 0; i < 64 && parameters.pop (next); ++i) changed = true;
    if (changed) processor.configure (next);
    if (! context.destBuffer || context.bufferNumSamples <= 0 || context.destBuffer->getNumChannels() == 0) return;
    auto* left = context.destBuffer->getWritePointer (0, context.bufferStartSample);
    auto* right = context.destBuffer->getNumChannels() > 1 ? context.destBuffer->getWritePointer (1, context.bufferStartSample) : nullptr;
    if (context.isPlaying || context.isRendering) processor.setTimelinePosition (context.editTime.getStart().inSeconds());
    processor.processSamples (left, right, context.bufferNumSamples);
}
}
