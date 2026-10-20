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
#define STEP_RATES "1/32|1/16 triplet|1/16|1/8 triplet|1/8|1/4 triplet|1/4|1/2|1 bar"
constexpr EffectParameter arpeggiatorFields[] {
    { "style", "Style", 0, 7, 0, 0, "", "Up|Down|Up-down|Down-up|Converge|Diverge|As played|Random" },
    { "rate", "Rate", 0, 8, 2, 0, "", STEP_RATES },
    { "gate", "Gate", .05f, 2, .5f, 0, "%" },
    { "octaves", "Octaves", 0, 3, 0, 0, "", "1|2|3|4" },
    { "hold", "Hold", 0, 1, 0, 0, "", "Off|On" },
    { "velocityMode", "Velocity", 0, 1, 0, 0, "", "As played|Fixed" },
    { "fixedVelocity", "Fixed velocity", 1, 127, 100, 0, "", nullptr, true } };
#define CHORD_SHIFT(N, V) { "shift" #N, "Shift " #N, -36, 36, V, 0, " st", nullptr, true }
#define CHORD_VELOCITY(N) { "velocity" #N, "Velocity " #N, 0, 1, 1, 0, "%" }
constexpr EffectParameter chordFields[] { CHORD_SHIFT (1, 4), CHORD_SHIFT (2, 7), CHORD_SHIFT (3, 0), CHORD_SHIFT (4, 0), CHORD_SHIFT (5, 0), CHORD_SHIFT (6, 0),
    CHORD_VELOCITY (1), CHORD_VELOCITY (2), CHORD_VELOCITY (3), CHORD_VELOCITY (4), CHORD_VELOCITY (5), CHORD_VELOCITY (6) };
#undef CHORD_SHIFT
#undef CHORD_VELOCITY
constexpr EffectParameter scaleFields[] {
    { "root", "Root", 0, 11, 0, 0, "", "C|C#|D|D#|E|F|F#|G|G#|A|A#|B" },
    { "scaleType", "Scale", 0, 12, 0, 0, "", "Major|Minor|Dorian|Phrygian|Lydian|Mixolydian|Locrian|Harmonic minor|Melodic minor|Major pentatonic|Minor pentatonic|Blues|Chromatic" },
    { "transpose", "Transpose", -24, 24, 0, 0, " st", nullptr, true } };
constexpr EffectParameter pitchFields[] {
    { "semitones", "Pitch", -48, 48, 0, 0, " st", nullptr, true },
    { "lowest", "Lowest note", 0, 127, 0, 0, "", nullptr, true },
    { "highest", "Highest note", 0, 127, 127, 0, "", nullptr, true } };
constexpr EffectParameter velocityFields[] {
    { "mode", "Mode", 0, 2, 0, 0, "", "Clip|Gate|Fixed" },
    { "inLow", "Input lowest", 1, 127, 1, 0, "", nullptr, true },
    { "inHigh", "Input highest", 1, 127, 127, 0, "", nullptr, true },
    { "outLow", "Output lowest", 1, 127, 1, 0, "", nullptr, true },
    { "outHigh", "Output highest", 1, 127, 127, 0, "", nullptr, true },
    { "drive", "Drive", -1, 1, 0, 0, "%" },
    { "fixed", "Fixed value", 1, 127, 100, 0, "", nullptr, true },
    { "random", "Random", 0, 64, 0, 0, "" } };
constexpr EffectParameter randomFields[] {
    { "chance", "Chance", 0, 1, .5f, 0, "%" },
    { "choices", "Choices", 1, 24, 1, 0, "", nullptr, true },
    { "interval", "Interval", 1, 24, 12, 0, " st", nullptr, true },
    { "sign", "Direction", 0, 2, 0, 0, "", "Add|Subtract|Both" },
    { "mode", "Mode", 0, 1, 0, 0, "", "Random|Alternate" } };
constexpr EffectParameter noteLengthFields[] {
    { "trigger", "Trigger", 0, 1, 0, 0, "", "Note on|Note off" },
    { "timing", "Timing", 0, 1, 1, 0, "", "Milliseconds|Project tempo" },
    { "lengthMs", "Length", 1, 4000, 250, 250, " ms" },
    { "division", "Division", 0, 8, 4, 0, "", STEP_RATES },
    { "gate", "Gate", .01f, 2, 1, 0, "%" } };
#define OSC_SHAPES "Saw|Pulse|Triangle|Sine"
#define VOICE_MODES "Mono|Legato|8 voices|16 voices|32 voices"
#define FILTER_TYPES "Off|Low pass 12|Low pass 24|High pass 12|Band pass 12|Notch"
#define ENVELOPE(PREFIX, LABEL, A, D, S, R) { PREFIX "Attack", LABEL " attack", 0, 10000, A, 100, " ms" }, { PREFIX "Decay", LABEL " decay", 1, 10000, D, 300, " ms" }, \
    { PREFIX "Sustain", LABEL " sustain", 0, 1, S, 0, "%" }, { PREFIX "Release", LABEL " release", 1, 10000, R, 300, " ms" }
constexpr EffectParameter polySynthFields[] {
    { "osc1Shape", "Osc 1 shape", 0, 3, 0, 0, "", OSC_SHAPES }, { "osc1Octave", "Osc 1 octave", -3, 3, 0, 0, " oct", nullptr, true },
    { "osc1Semitone", "Osc 1 semitone", -12, 12, 0, 0, " st", nullptr, true }, { "osc1Detune", "Osc 1 detune", -50, 50, 0, 0, " ct" },
    { "osc1Level", "Osc 1 level", 0, 1, 1, 0, "%" }, { "osc1Width", "Osc 1 pulse width", .05f, .95f, .5f, 0, "%" },
    { "osc2Shape", "Osc 2 shape", 0, 3, 1, 0, "", OSC_SHAPES }, { "osc2Octave", "Osc 2 octave", -3, 3, 0, 0, " oct", nullptr, true },
    { "osc2Semitone", "Osc 2 semitone", -12, 12, 0, 0, " st", nullptr, true }, { "osc2Detune", "Osc 2 detune", -50, 50, 7, 0, " ct" },
    { "osc2Level", "Osc 2 level", 0, 1, .5f, 0, "%" }, { "osc2Width", "Osc 2 pulse width", .05f, .95f, .5f, 0, "%" },
    { "noiseLevel", "Noise", 0, 1, 0, 0, "%" },
    { "filterType", "Filter", 0, 5, 2, 0, "", FILTER_TYPES }, { "cutoffHz", "Cutoff", 20, 20000, 2000, 1000, " Hz" },
    { "resonance", "Resonance", 0, 1, .2f, 0, "%" }, { "filterEnvelope", "Filter envelope", -1, 1, .3f, 0, "%" },
    { "keyTracking", "Key tracking", 0, 1, .5f, 0, "%" },
    ENVELOPE ("filter", "Filter", 1, 400, .3f, 300), ENVELOPE ("amp", "Amp", 2, 300, .8f, 250),
    { "lfoShape", "LFO shape", 0, 5, 0, 0, "", "Sine|Triangle|Saw up|Saw down|Square|Random" }, { "lfoRateHz", "LFO rate", .01f, 40, 5, 1, " Hz" },
    { "lfoPitch", "LFO to pitch", 0, 12, 0, 1, " st" }, { "lfoCutoff", "LFO to cutoff", 0, 4, 0, 0, " oct" }, { "lfoAmp", "LFO to amp", 0, 1, 0, 0, "%" },
    { "glideMs", "Glide", 0, 2000, 0, 200, " ms" }, { "voiceMode", "Voices", 0, 4, 3, 0, "", VOICE_MODES },
    { "unison", "Unison", 0, 3, 0, 0, "", "1|2|3|4" }, { "unisonDetune", "Unison detune", 0, 50, 10, 0, " ct" },
    { "velocityAmp", "Velocity to amp", 0, 1, .7f, 0, "%" }, { "velocityCutoff", "Velocity to cutoff", 0, 1, 0, 0, "%" },
    { "bendRange", "Bend range", 0, 24, 2, 0, " st", nullptr, true }, { "volumeDb", "Volume", -36, 6, 0, 0, " dB" } };
#define OPERATOR(N, L, D, S) { "op" N "Coarse", "Op " N " coarse", 0, 48, 1, 0, "", nullptr, true }, { "op" N "Fine", "Op " N " fine", 0, 1, 0, 0, "" }, \
    { "op" N "Level", "Op " N " level", 0, 1, L, 0, "%" }, { "op" N "Attack", "Op " N " attack", 0, 10000, 1, 100, " ms" }, \
    { "op" N "Decay", "Op " N " decay", 1, 10000, D, 300, " ms" }, { "op" N "Sustain", "Op " N " sustain", 0, 1, S, 0, "%" }, \
    { "op" N "Release", "Op " N " release", 1, 10000, 300, 300, " ms" }
constexpr EffectParameter fmSynthFields[] {
    OPERATOR ("A", 1, 400, .6f), OPERATOR ("B", .3f, 600, .4f), OPERATOR ("C", 0, 600, .4f), OPERATOR ("D", 0, 600, .4f),
    { "algorithm", "Algorithm", 0, 10, 0, 0, "", "1: D>C>B>A|2: C+D>B>A|3: B+(D>C)>A|4: B+C+D>A|5: B>A, D>C|6: D>A,B,C|7: C>B>A, D|8: D>C>B, A|9: B>A, C, D|10: D>(B,C)>A|11: A+B+C+D" },
    { "feedback", "Feedback", 0, 1, 0, 0, "%" }, { "filterType", "Filter", 0, 2, 0, 0, "", "Off|Low pass|High pass" },
    { "cutoffHz", "Cutoff", 20, 20000, 8000, 1000, " Hz" }, { "resonance", "Resonance", 0, 1, .1f, 0, "%" },
    { "velocityLevel", "Velocity to level", 0, 1, .7f, 0, "%" }, { "bendRange", "Bend range", 0, 24, 2, 0, " st", nullptr, true },
    { "volumeDb", "Volume", -36, 6, 0, 0, " dB" }, { "voices", "Voices", 0, 3, 2, 0, "", "4|8|16|32" } };
#define PAD(KEY, LABEL) { KEY "Tune", LABEL " tune", -24, 24, 0, 0, " st" }, { KEY "Decay", LABEL " decay", .1f, 4, 1, 1, "x" }, \
    { KEY "Tone", LABEL " tone", 0, 1, .5f, 0, "%" }, { KEY "Level", LABEL " level", 0, 1, .8f, 0, "%" }
constexpr EffectParameter drumKitFields[] {
    PAD ("kick", "Kick"), PAD ("snare", "Snare"), PAD ("clap", "Clap"), PAD ("closedHat", "Closed hat"),
    PAD ("openHat", "Open hat"), PAD ("lowTom", "Low tom"), PAD ("highTom", "High tom"), PAD ("cymbal", "Cymbal"),
    { "volumeDb", "Volume", -36, 6, 0, 0, " dB" } };
#define WAVE_OSC(N, POSITION, DETUNE, LEVEL) { "osc" N "Table", "Osc " N " table", 0, 3, 0, 0, "", "Basic shapes|Harmonics|Pulse width|Sync" }, \
    { "osc" N "Position", "Osc " N " position", 0, 1, POSITION, 0, "%" }, { "osc" N "Octave", "Osc " N " octave", -3, 3, 0, 0, " oct", nullptr, true }, \
    { "osc" N "Detune", "Osc " N " detune", -50, 50, DETUNE, 0, " ct" }, { "osc" N "Level", "Osc " N " level", 0, 1, LEVEL, 0, "%" }, \
    { "osc" N "Spread", "Osc " N " stereo spread", 0, 1, .5f, 0, "%" }
constexpr EffectParameter wavetableFields[] {
    WAVE_OSC ("1", 0, 0, 1), WAVE_OSC ("2", .5f, 5, 0),
    { "filterType", "Filter", 0, 5, 1, 0, "", FILTER_TYPES }, { "cutoffHz", "Cutoff", 20, 20000, 5000, 1000, " Hz" },
    { "resonance", "Resonance", 0, 1, .15f, 0, "%" }, { "filterEnvelope", "Filter envelope", -1, 1, 0, 0, "%" },
    ENVELOPE ("filter", "Filter", 1, 400, .5f, 300), ENVELOPE ("amp", "Amp", 2, 300, .8f, 250),
    { "lfoRateHz", "LFO rate", .01f, 40, .5f, 1, " Hz" }, { "lfoPosition", "LFO to position", 0, 1, 0, 0, "%" },
    { "lfoPitch", "LFO to pitch", 0, 12, 0, 1, " st" }, { "unison", "Unison", 0, 3, 0, 0, "", "1|2|3|4" },
    { "unisonDetune", "Unison detune", 0, 50, 12, 0, " ct" }, { "glideMs", "Glide", 0, 2000, 0, 200, " ms" },
    { "voiceMode", "Voices", 0, 4, 3, 0, "", VOICE_MODES }, { "velocityAmp", "Velocity to amp", 0, 1, .7f, 0, "%" },
    { "bendRange", "Bend range", 0, 24, 2, 0, " st", nullptr, true }, { "volumeDb", "Volume", -36, 6, 0, 0, " dB" } };
constexpr EffectParameter samplerFields[] {
    { "mode", "Mode", 0, 1, 0, 0, "", "Classic|One-Shot" }, { "rootNote", "Root note", 0, 127, 60, 0, "", nullptr, true },
    { "transpose", "Transpose", -48, 48, 0, 0, " st", nullptr, true }, { "detune", "Detune", -50, 50, 0, 0, " ct" },
    { "start", "Start", 0, 1, 0, 0, "%" }, { "end", "End", 0, 1, 1, 0, "%" },
    { "loop", "Loop", 0, 1, 0, 0, "", "Off|On" }, { "loopStart", "Loop start", 0, 1, 0, 0, "%" },
    { "trigger", "One-shot trigger", 0, 1, 0, 0, "", "Trigger|Gate" },
    { "filterType", "Filter", 0, 5, 0, 0, "", FILTER_TYPES }, { "cutoffHz", "Cutoff", 20, 20000, 8000, 1000, " Hz" },
    { "resonance", "Resonance", 0, 1, .1f, 0, "%" }, ENVELOPE ("amp", "Amp", 0, 600, 1, 50),
    { "voices", "Voices", 0, 4, 2, 0, "", "Mono|4 voices|8 voices|16 voices|32 voices" },
    { "velocityAmp", "Velocity to amp", 0, 1, .35f, 0, "%" }, { "bendRange", "Bend range", 0, 24, 2, 0, " st", nullptr, true },
    { "pan", "Pan", -1, 1, 0, 0, "" }, { "volumeDb", "Volume", -36, 6, 0, 0, " dB" } };
static_assert (std::size (samplerFields) == instruments::sampler::count, "the sampler table follows its processor index enum");
#define DRUM_PAD(N) { "pad" N "Tune", "Pad " N " tune", -48, 48, 0, 0, " st" }, { "pad" N "Length", "Pad " N " length", .01f, 1, 1, 0, "%" }, \
    { "pad" N "Level", "Pad " N " level", 0, 1, 1, 0, "%" }, { "pad" N "Pan", "Pad " N " pan", -1, 1, 0, 0, "" }, \
    { "pad" N "Choke", "Pad " N " choke", 0, 4, 0, 0, "", "None|1|2|3|4" }
constexpr EffectParameter drumSamplerFields[] {
    DRUM_PAD ("1"), DRUM_PAD ("2"), DRUM_PAD ("3"), DRUM_PAD ("4"), DRUM_PAD ("5"), DRUM_PAD ("6"), DRUM_PAD ("7"), DRUM_PAD ("8"),
    DRUM_PAD ("9"), DRUM_PAD ("10"), DRUM_PAD ("11"), DRUM_PAD ("12"), DRUM_PAD ("13"), DRUM_PAD ("14"), DRUM_PAD ("15"), DRUM_PAD ("16"),
    { "volumeDb", "Volume", -36, 6, 0, 0, " dB" } };
#undef DRUM_PAD
static_assert (std::size (drumSamplerFields) == instruments::drumSampler::count && std::size (drumSamplerFields) <= std::tuple_size_v<decltype (Settings::values)>,
               "the drum sampler table follows its processor index enum and fits the packet");
static_assert (std::size (polySynthFields) <= std::tuple_size_v<decltype (Settings::values)> && std::size (fmSynthFields) <= std::tuple_size_v<decltype (Settings::values)>
               && std::size (drumKitFields) <= std::tuple_size_v<decltype (Settings::values)> && std::size (wavetableFields) <= std::tuple_size_v<decltype (Settings::values)>,
               "every device fits the shared packet");
static_assert (std::size (polySynthFields) == instruments::poly::count && std::size (fmSynthFields) == instruments::fm::count
               && std::size (drumKitFields) == instruments::drums::count && std::size (wavetableFields) == instruments::wavetable::count,
               "instrument tables must follow their processor index enums");
#undef OSC_SHAPES
#undef VOICE_MODES
#undef FILTER_TYPES
#undef ENVELOPE
#undef OPERATOR
#undef PAD
#undef WAVE_OSC
#undef STEP_RATES
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
        case Kind::eqThree: return "Three-band EQ"; case Kind::glueCompressor: return "Bus Compressor"; case Kind::frequencyShifter: return "Frequency Shifter";
        case Kind::arpeggiator: return "Arpeggiator"; case Kind::chord: return "Chord"; case Kind::scale: return "Scale"; case Kind::pitch: return "Pitch";
        case Kind::velocity: return "Velocity"; case Kind::random: return "Random"; case Kind::noteLength: return "Note Length";
        case Kind::polySynth: return "Poly Synth"; case Kind::fmSynth: return "FM Synth"; case Kind::drumKit: return "Drum Kit"; case Kind::wavetableSynth: return "Wavetable";
        case Kind::sampler: return "Sampler"; case Kind::drumSampler: return "Drum Sampler"; }
    return "Native effect";
}
std::span<const EffectParameter> effectParameters (Kind kind) noexcept
{
    switch (kind) { case Kind::equalizer: return eqFields; case Kind::compressor: return compressorFields;
        case Kind::delay: return delayFields; case Kind::reverb: return reverbFields; case Kind::bitcrusher: return crusherFields; case Kind::gate: return gateFields; case Kind::limiter: return limiterFields;
        case Kind::saturator: return saturatorFields; case Kind::overdrive: return overdriveFields; case Kind::autoFilter: return autoFilterFields;
        case Kind::autoPan: return autoPanFields; case Kind::chorus: return chorusFields; case Kind::phaser: return phaserFields; case Kind::flanger: return flangerFields;
        case Kind::eqThree: return eqThreeFields; case Kind::glueCompressor: return glueFields; case Kind::frequencyShifter: return shifterFields;
        case Kind::arpeggiator: return arpeggiatorFields; case Kind::chord: return chordFields; case Kind::scale: return scaleFields; case Kind::pitch: return pitchFields;
        case Kind::velocity: return velocityFields; case Kind::random: return randomFields; case Kind::noteLength: return noteLengthFields;
        case Kind::polySynth: return polySynthFields; case Kind::fmSynth: return fmSynthFields; case Kind::drumKit: return drumKitFields; case Kind::wavetableSynth: return wavetableFields;
        case Kind::sampler: return samplerFields; case Kind::drumSampler: return drumSamplerFields; }
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
        case Kind::arpeggiator: case Kind::chord: case Kind::scale: case Kind::pitch: case Kind::velocity: case Kind::random: case Kind::noteLength: return "MIDI Effects";
        case Kind::polySynth: case Kind::fmSynth: case Kind::drumKit: case Kind::wavetableSynth: case Kind::sampler: case Kind::drumSampler: return "Instruments";
    }
    return "Effects";
}
bool effectUsesTempo (Kind kind) noexcept
{ return kind == Kind::delay || kind == Kind::autoFilter || kind == Kind::autoPan || kind == Kind::arpeggiator || kind == Kind::noteLength; }
bool effectIsMidi (Kind kind) noexcept { return static_cast<int> (kind) >= static_cast<int> (Kind::arpeggiator) && static_cast<int> (kind) <= static_cast<int> (Kind::noteLength); }
bool effectIsInstrument (Kind kind) noexcept { return static_cast<int> (kind) >= static_cast<int> (Kind::polySynth) && static_cast<int> (kind) < Session::effectKindCount; }
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
        case Kind::arpeggiator: case Kind::chord: case Kind::scale: case Kind::pitch: case Kind::velocity: case Kind::random: case Kind::noteLength: return 0.0;
        case Kind::polySynth: case Kind::fmSynth: case Kind::drumKit: case Kind::wavetableSynth: case Kind::sampler: case Kind::drumSampler: return 0.0;
    }
    return 0.0;
}
float effectValueToProportion (Kind kind, int parameter, float value) noexcept
{
    const auto fields = effectParameters (kind);
    if (parameter < 0 || parameter >= (int) fields.size()) return 0;
    const auto& f = fields[(size_t) parameter];
    juce::NormalisableRange<float> range (f.minimum, f.maximum);
    if (f.midpoint > f.minimum && f.midpoint < f.maximum && f.midpoint != 0) range.setSkewForCentre (f.midpoint);
    return range.convertTo0to1 (std::clamp (value, f.minimum, f.maximum));
}
float effectProportionToValue (Kind kind, int parameter, float proportion) noexcept
{
    const auto fields = effectParameters (kind);
    if (parameter < 0 || parameter >= (int) fields.size()) return 0;
    const auto& f = fields[(size_t) parameter];
    juce::NormalisableRange<float> range (f.minimum, f.maximum);
    if (f.midpoint > f.minimum && f.midpoint < f.maximum && f.midpoint != 0) range.setSkewForCentre (f.midpoint);
    const auto value = std::clamp (range.convertFrom0to1 (std::clamp (proportion, 0.0f, 1.0f)), f.minimum, f.maximum);
    return f.integer || f.choices != nullptr ? std::round (value) : value;
}
bool effectShowsPercent (const EffectParameter& f) noexcept
{ return std::string_view (f.unit) == "%" || std::string_view (f.key) == "mix" || std::string_view (f.key) == "feedback"; }
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
        if (v < fields[i].minimum || v > fields[i].maximum || ((fields[i].choices || fields[i].integer) && std::floor (v) != v)) return false;
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
            default: break; // MIDI kinds have no audio path.
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
{
    // Shared tables are built here on the message thread, never during graph preparation.
    if (kind == Kind::wavetableSynth) instruments::prepareWavetables();
    for (int slot = 0; slot < sampleSlotCount (kind); ++slot) loadSample (slot);
    for (const auto& field : effectParameters (kind)) controls.push_back (addParam (field.key, field.label, { 0.0f, 1.0f }));
    audioSettings = readSettings();
    publish(); refreshAutomation(); startTimerHz (30);
}
NativeEffectPlugin::~NativeEffectPlugin() { stopTimer(); }
juce::String samplerSampleProblem (const juce::File& file)
{
    if (! file.existsAsFile()) return "Missing sample: " + file.getFullPathName();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0 || reader->numChannels <= 0)
        return "Not a readable audio sample: " + file.getFileName();
    if (reader->lengthInSamples > static_cast<juce::int64> (reader->sampleRate * maximumSampleSeconds))
        return "Samples longer than ten minutes are not supported: " + file.getFileName();
    return {};
}
int sampleSlotCount (Kind kind) noexcept { return kind == Kind::sampler ? 1 : kind == Kind::drumSampler ? Session::drumPadCount : 0; }
juce::Identifier sampleSlotProperty (Kind kind, int slot)
{ return kind == Kind::drumSampler ? drumPadSampleProperty (slot) : juce::Identifier (samplerSampleProperty); }
juce::File NativeEffectPlugin::sampleFile (int slot) const
{
    if (slot < 0 || slot >= sampleSlotCount (kind)) return {};
    const auto path = state[sampleSlotProperty (kind, slot)].toString();
    return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}
void NativeEffectPlugin::loadSample (int slot)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto file = sampleFile (slot);
    if (file == loadedSamples[(size_t) slot]) return;
    loadedSamples[(size_t) slot] = file;
    std::unique_ptr<instruments::SampleData> data;
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    if (file != juce::File() && samplerSampleProblem (file).isEmpty())
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
        {
            const auto frames = static_cast<int> (reader->lengthInSamples);
            juce::AudioBuffer<float> buffer (reader->numChannels > 1 ? 2 : 1, frames);
            if (reader->read (&buffer, 0, frames, 0, true, true))
            {
                data = std::make_unique<instruments::SampleData>();
                data->sampleRate = reader->sampleRate;
                data->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
                data->right.assign (buffer.getReadPointer (buffer.getNumChannels() - 1), buffer.getReadPointer (buffer.getNumChannels() - 1) + frames);
            }
        }
    publishedSamples[(size_t) slot].store (data.get(), std::memory_order_release);
    if (data) samples.push_back (std::move (data));
    retireSamples();
}
void NativeEffectPlugin::relocateSample (int slot, const juce::File& copy)
{
    // The copy has the same audio, so keep the decoded data and only follow the new path.
    loadedSamples[(size_t) slot] = copy;
    state.setProperty (sampleSlotProperty (kind, slot), copy.getFullPathName(), nullptr);
}
void NativeEffectPlugin::retireSamples()
{
    // Older samples are freed once the audio thread has acknowledged every current one.
    std::array<const instruments::SampleData*, Session::drumPadCount> current {};
    for (size_t slot = 0; slot < current.size(); ++slot)
    {
        current[slot] = publishedSamples[slot].load (std::memory_order_acquire);
        if (acknowledgedSamples[slot].load (std::memory_order_acquire) != current[slot]) return;
    }
    std::erase_if (samples, [&current] (const auto& s) { return std::find (current.begin(), current.end(), s.get()) == current.end(); });
}
juce::Result NativeEffectPlugin::resolveSamples (juce::ValueTree& tree, const juce::File& directory)
{
    if (tree.hasType (tracktion::IDs::PLUGIN) && tree.getProperty (tracktion::IDs::type).toString() == xmlTypeName)
    {
        const auto kind = static_cast<Kind> (static_cast<int> (tree.getProperty ("effectKind", 0)));
        for (int i = 0; i < tree.getNumProperties(); ++i)
        {
            const auto name = tree.getPropertyName (i);
            if (! name.toString().startsWith ("halftrackSample") && ! name.toString().startsWith ("halftrackPad")) continue;
            int slot = -1;
            for (int s = 0; s < sampleSlotCount (kind); ++s) if (sampleSlotProperty (kind, s) == name) slot = s;
            if (slot < 0) return juce::Result::fail ("A device references a sample it cannot play.");
            const auto path = tree[name].toString();
            if (path.isEmpty()) continue;
            const auto file = directory.getChildFile (path);
            if (const auto problem = samplerSampleProblem (file); problem.isNotEmpty()) return juce::Result::fail (problem);
            tree.setProperty (name, file.getFullPathName(), nullptr);
        }
    }
    for (auto child : tree) if (auto result = resolveSamples (child, directory); result.failed()) return result;
    return juce::Result::ok();
}
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
            if (! parse (f.key, f.initial, v) || v < f.minimum || v > f.maximum || ((f.choices || f.integer) && std::floor (v) != v))
                return juce::Result::fail ("The project contains invalid native effect settings.");
        }
        // Control automation: a known control, at most 4096 ordered points of knob positions.
        for (const auto& curve : tree)
        {
            if (! curve.hasType (tracktion::IDs::AUTOMATIONCURVE)) continue;
            if (effectParameterIndex (static_cast<Kind> (static_cast<int> (type)), curve[tracktion::IDs::paramID].toString().toRawUTF8()) < 0 || curve.getNumChildren() > 4096)
                return juce::Result::fail ("The project contains invalid device automation.");
            double previous = -1;
            for (const auto& point : curve)
            {
                const auto time = point.getProperty (tracktion::IDs::t, 0.0), value = point.getProperty (tracktion::IDs::v, 0.0);
                const auto t = static_cast<double> (time), v = static_cast<double> (value);
                if (! std::isfinite (t) || ! std::isfinite (v) || t < 0 || t < previous || v < 0 || v > 1)
                    return juce::Result::fail ("The project contains invalid device automation.");
                previous = t;
            }
        }
    }
    for (const auto& child : tree) if (auto result = validateState (child); result.failed()) return result;
    return juce::Result::ok();
}
void NativeEffectPlugin::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    Plugin::valueTreePropertyChanged (tree, property);
    if (tree == state)
        for (int slot = 0; slot < sampleSlotCount (kind); ++slot)
            if (property == sampleSlotProperty (kind, slot)) { loadSample (slot); return; }
    if (tree == state && ! writing) publish();
}
void NativeEffectPlugin::publish()
{
    pendingSettings = readSettings(); pending = ! parameters.push (pendingSettings);
    // Controls that do not read a curve follow the manual settings.
    for (size_t i = 0; i < controls.size(); ++i)
        if (! readsCurve[i].load (std::memory_order_relaxed))
            if (const auto position = effectValueToProportion (kind, (int) i, pendingSettings.values[i]); controls[i]->getCurrentValue() != position)
                controls[i]->setParameter (position, juce::dontSendNotification);
}
void NativeEffectPlugin::refreshAutomation()
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto settings = readSettings();
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto& curve = controls[i]->getCurve();
        const bool reads = ! curve.bypass.get() && curve.getNumPoints() > 0;
        controls[i]->updateStream();
        if (reads) controls[i]->updateToFollowCurve (edit.getTransport().getPosition());
        else controls[i]->setParameter (effectValueToProportion (kind, (int) i, settings.values[i]), juce::dontSendNotification);
        readsCurve[i].store (reads, std::memory_order_release);
    }
}
void NativeEffectPlugin::timerCallback()
{
    if (samples.size() > 1) retireSamples();
    if (effectUsesTempo (kind) && static_cast<float> (edit.tempoSequence.getTempo (0)->getBpm()) != pendingSettings.tempoBpm)
        publish();
    else if (pending) pending = ! parameters.push (pendingSettings);
}
void NativeEffectPlugin::prepareForOfflineRender() { JUCE_ASSERT_MESSAGE_THREAD parameters.resetWhileStopped(); publish(); }
void NativeEffectPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate; processor.prepare (sampleRate, kind); audioSettings = pendingSettings;
    midiProcessor.prepare (sampleRate); midiProcessor.configure (pendingSettings);
    if (effectIsInstrument (kind)) { instrument.prepare (sampleRate, kind); instrument.configure (pendingSettings); }
    midiScratch.reserve (static_cast<int> (midiOutput.size()) + 256);
}
void NativeEffectPlugin::reset() { processor.reset(); midiProcessor.reset(); if (effectIsInstrument (kind)) instrument.reset(); }
void NativeEffectPlugin::renderInstrument (const tracktion::PluginRenderContext& context) noexcept
{
    if (! context.destBuffer || context.bufferNumSamples <= 0) return;
    auto& buffer = *context.destBuffer;
    buffer.clear (context.bufferStartSample, context.bufferNumSamples);
    auto* left = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0, context.bufferStartSample) : nullptr;
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, context.bufferStartSample) : nullptr;
    const auto rate = sampleRate > 0 ? sampleRate : 48000.0;
    int position = 0;
    if (auto* midi = context.bufferForMidiMessages)
    {
        if (midi->isAllNotesOff) instrument.reset();
        for (const auto& message : *midi)
        {
            const auto at = std::clamp (static_cast<int> (std::llround ((message.getTimeStamp() + context.midiBufferOffset) * rate)), position, context.bufferNumSamples);
            instrument.render (left ? left + position : nullptr, right ? right + position : nullptr, at - position);
            position = at;
            if (message.isNoteOn()) instrument.noteOn (message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
            else if (message.isNoteOff()) instrument.noteOff (message.getChannel(), message.getNoteNumber());
            else if (message.isPitchWheel()) instrument.pitchWheel (message.getChannel(), message.getPitchWheelValue());
            else if (message.isController()) instrument.controlChange (message.getChannel(), message.getControllerNumber(), message.getControllerValue());
        }
    }
    instrument.render (left ? left + position : nullptr, right ? right + position : nullptr, context.bufferNumSamples - position);
}
void NativeEffectPlugin::processMidi (const tracktion::PluginRenderContext& context) noexcept
{
    auto* midi = context.bufferForMidiMessages;
    if (midi == nullptr || context.bufferNumSamples <= 0) return;
    const auto rate = sampleRate > 0 ? sampleRate : 48000.0;
    const auto toSample = [&] (const juce::MidiMessage& m)
    { return std::clamp (static_cast<int> (std::llround ((m.getTimeStamp() + context.midiBufferOffset) * rate)), 0, context.bufferNumSamples - 1); };
    int inputs = 0, outputs = 0;
    for (const auto& m : *midi)
        if (m.isNoteOnOrOff() && inputs < static_cast<int> (midiInput.size()))
        {
            const auto* raw = m.getRawData();
            midiInput[static_cast<size_t> (inputs++)] = { toSample (m), raw[0], raw[1], raw[2], m.mpeSourceID };
        }
    if (midi->isAllNotesOff) outputs = midiProcessor.allNotesOff (0, midiOutput.data(), static_cast<int> (midiOutput.size()));
    // Negative (pre-roll or stopped) positions use the processor's free-running clock.
    const auto timeline = context.isPlaying || context.isRendering ? context.editTime.getStart().inSeconds() : -1.0;
    outputs += midiProcessor.process (midiInput.data(), inputs, context.bufferNumSamples, timeline,
                                      midiOutput.data() + outputs, static_cast<int> (midiOutput.size()) - outputs);
    // Rebuild in time order: generated notes interleave with retained non-note events.
    midiScratch.clear(); midiScratch.isAllNotesOff = midi->isAllNotesOff;
    const auto generated = [&] (const midifx::Event& e)
    {
        midiScratch.addMidiMessage (juce::MidiMessage (e.status, e.data1, e.data2), e.sample / rate - context.midiBufferOffset, e.source);
    };
    int next = 0;
    for (auto& m : *midi)
    {
        if (m.isNoteOnOrOff()) continue;
        const auto at = toSample (m);
        while (next < outputs && midiOutput[static_cast<size_t> (next)].sample <= at) generated (midiOutput[static_cast<size_t> (next++)]);
        midiScratch.add (std::move (m));
    }
    while (next < outputs) generated (midiOutput[static_cast<size_t> (next++)]);
    midi->swapWith (midiScratch);
}
void NativeEffectPlugin::applyToBuffer (const tracktion::PluginRenderContext& context)
{
    const juce::ScopedNoDenormals noDenormals; Settings next; bool changed = false;
    for (int i = 0; i < 64 && parameters.pop (next); ++i) changed = true;
    if (changed) audioSettings = next;
    // Controls whose automation reads this block override their manual settings.
    bool automatedNow = false;
    for (size_t i = 0; i < controls.size(); ++i) automatedNow = automatedNow || readsCurve[i].load (std::memory_order_acquire);
    if (automatedNow)
    {
        next = audioSettings;
        for (size_t i = 0; i < controls.size(); ++i)
            if (readsCurve[i].load (std::memory_order_acquire)) next.values[i] = effectProportionToValue (kind, (int) i, controls[i]->getCurrentValue());
    }
    else next = audioSettings;
    if (changed || automatedNow || automated) { processor.configure (next); midiProcessor.configure (next); if (effectIsInstrument (kind)) instrument.configure (next); }
    automated = automatedNow;
    if (kind == Kind::sampler)
    {
        const auto* sample = publishedSamples[0].load (std::memory_order_acquire);
        instrument.setSample (sample);
        acknowledgedSamples[0].store (sample, std::memory_order_release);
    }
    else if (kind == Kind::drumSampler)
        for (size_t pad = 0; pad < publishedSamples.size(); ++pad)
        {
            const auto* sample = publishedSamples[pad].load (std::memory_order_acquire);
            instrument.setPadSample (static_cast<int> (pad), sample);
            acknowledgedSamples[pad].store (sample, std::memory_order_release);
        }
    if (effectIsMidi (kind)) { processMidi (context); return; }
    if (effectIsInstrument (kind)) { renderInstrument (context); return; }
    if (! context.destBuffer || context.bufferNumSamples <= 0 || context.destBuffer->getNumChannels() == 0) return;
    auto* left = context.destBuffer->getWritePointer (0, context.bufferStartSample);
    auto* right = context.destBuffer->getNumChannels() > 1 ? context.destBuffer->getWritePointer (1, context.bufferStartSample) : nullptr;
    if (context.isPlaying || context.isRendering) processor.setTimelinePosition (context.editTime.getStart().inSeconds());
    processor.processSamples (left, right, context.bufferNumSamples);
}
}
