#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace halftrack
{
// All public calls, including snapshots, belong on JUCE's message thread.
// Positions and lengths are seconds; pan is -1 (left) through +1 (right).
class Session final : public juce::ChangeBroadcaster
{
public:
    // The global launch grid, as the reference's: None, 8 Bars to 1 Bar, then 1/2 to 1/32 with the
    // triplets (launchQuantisations lists them in its menu's order). Later values keep the first ones' numbers.
    enum class LaunchQuantisation { none, beat, bar, eightBars, fourBars, twoBars, half, halfTriplet, quarterTriplet, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtysecond };
    // Record Quantization, as the reference's: the combined grids snap to the nearer straight or triplet point.
    enum class RecordQuantisation { none, quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtysecond, eighthAndTriplet, sixteenthAndTriplet };
    enum class MonitorMode { off, automatic, on };
    enum class SendMode { pre, post };
    enum class ExportFormat { wav, aiff, flac };
    static constexpr double maximumExportRangeSeconds = 172800;
    struct AudioExportSettings
    {
        ExportFormat format = ExportFormat::wav;
        double sampleRate = 48000;
        int bitDepth = 24;
        double tailSeconds = 0;
        bool mono = false, normalizePeak = false;
        float peakCeilingDb = -1;
        // Exact [start, end) seconds, rounded to output frames. Requires zero tail.
        std::optional<juce::Range<double>> timeRange;
        bool stems = false; // Chosen in the export window: render every track to its own file with exportStems.
        // With stems, only these tracks and groups, each to its own file and without Master: the
        // reference's Selected Tracks Only, or a single track chosen by name. Empty renders them all.
        juce::StringArray stemTracks;
        // Export Video: also a QuickTime movie beside the audio (its name with .mov), of the picture of
        // the video clip under the export's start for the exported time, with the exported sound. An
        // existing movie there is kept unless replaceVideo says to replace it.
        bool video = false, replaceVideo = false;
    };
    enum class FollowAction { off, stop, replay, previous, next, first, last, cycle, any, other };
    // A track's side of the crossfader; unassigned tracks ignore it.
    enum class CrossfadeAssign { none, a, b };
    // Clip launch behaviour on press/release of its launch button.
    enum class LaunchMode { trigger, gate, toggle, repeat };
    // Per-clip launch grid; global follows the project setting.
    // A clip's launch grid: Global, then the reference's choices (clipQuantisations gives its menu's order).
    enum class ClipQuantisation { global, none, eightBars, fourBars, twoBars, bar, half, quarter, eighth, sixteenth, thirtysecond, halfTriplet, quarterTriplet, eighthTriplet, sixteenthTriplet };
    struct LaunchSettings
    {
        LaunchMode mode = LaunchMode::trigger;
        ClipQuantisation quantisation = ClipQuantisation::global;
        bool legato = false; // Start in phase with the clip already playing on the track.
        // Velocity, 0 to 1 (shown as a percentage): how much a launching note's velocity sets the
        // clip's volume, as the reference's Velocity Amount does (static reading of its code). A
        // launch blends its velocity v (a note's over 127; 1 for any other launch) with it into
        // f = v x amount + 1 - amount, which scales an audio clip's gain position along the
        // reference's clip gain scale (-70 to +24 dB) and a MIDI clip's note-on velocities (never
        // below 1), from that launch until the next.
        float velocityAmount = 0;
        bool operator== (const LaunchSettings&) const = default;
    };
    struct FollowSettings
    {
        FollowAction action = FollowAction::off;
        double afterBeats = 0;
        // Action B plays when action A is not chosen; chanceA is A's probability (0-1). Off as B does nothing.
        FollowAction actionB = FollowAction::off;
        double chanceA = 1;
        bool operator== (const FollowSettings&) const = default;
    };
    struct SynthSettings
    {
        float cutoffHz = 5000, resonance = 0.15f, attackSeconds = 0.01f, decaySeconds = 0.12f;
        float sustain = 0.65f, releaseSeconds = 0.25f, level = 0.15f, pulseWidth = 1.0f;
        int waveform = 0; // 0 = saw, 1 = pulse (default square), 2 = sine, 3 = white noise.
    };
    struct UtilitySettings
    {
        enum class Channel { stereo, left, right, swap };
        float gainDb = 0, balance = 0, width = 1, midSide = 0, bassFrequency = 120;
        Channel channel = Channel::stereo;
        bool midSideMode = false, invertLeft = false, invertRight = false, mono = false;
        bool bassMono = false, bassAudition = false, mute = false, dcFilter = false;
        bool operator== (const UtilitySettings&) const = default;
    };
    // Appended kinds keep their persisted integer value; never reorder.
    enum class EffectKind { equalizer, compressor, delay, reverb, bitcrusher, gate, limiter,
        saturator, overdrive, autoFilter, autoPan, chorus, phaser, flanger, eqThree, glueCompressor, frequencyShifter,
        // MIDI effects process notes before a MIDI track's instrument.
        arpeggiator, chord, scale, pitch, velocity, random, noteLength,
        // Instruments replace a MIDI track's built-in synthesizer through setTrackInstrument.
        polySynth, fmSynth, drumKit, wavetableSynth, sampler, drumSampler,
        // Racks hold parallel chains of devices. Instrument and Drum Racks are instruments; a MIDI
        // Effect Rack is a MIDI effect; an Audio Effect Rack is an audio device.
        instrumentRack, audioEffectRack, midiEffectRack, drumRack,
        // Devices added after the racks.
        channelEq, tuner, spectrum, pedal, beatRepeat, filterDelay, erosion, resonators, vinylDistortion, ccControl, alignDelay, dynamicTube, noteEcho, midiMonitor };
    // Every persisted kind, racks included. Devices added after the racks follow them, so loop over
    // all kinds and skip racks rather than assuming plain devices come first.
    static constexpr int deviceKindCount = 48;
    static constexpr bool isRackKind (EffectKind kind) noexcept
    { return kind == EffectKind::instrumentRack || kind == EffectKind::audioEffectRack || kind == EffectKind::midiEffectRack || kind == EffectKind::drumRack; }
    static constexpr int drumPadCount = 16; // Drum Sampler pads play notes 36 (C1) to 51.
    // A rack's EffectSettings hold its macros (0-127) and then its chain selector (0-127), so the
    // ordinary device commands set and automate them.
    static constexpr int rackMacroCount = 16, rackChainSelector = 16;
    static constexpr int maximumRackChains = 128, maximumRackReturns = 6, maximumChainDevices = 32, maximumMacroMappings = 256;
    // A zone passes values from low to high. Values below fadeLow or above fadeHigh are inside its
    // fade ranges, which fall linearly to the zone's edges.
    struct RackZone
    {
        int low = 0, high = 127, fadeLow = 0, fadeHigh = 127;
        bool operator== (const RackZone&) const = default;
    };
    struct RackChainSettings
    {
        juce::String name;
        float volumeDb = 0, pan = 0; // -100 dB is silence; pan uses the track mixer's law.
        bool active = true, solo = false; // Active is the chain activator.
        RackZone key, velocity { 1, 127, 1, 127 }, select { 0, 0, 0, 0 };
        int receiveNote = 36; // Drum Rack chains: the pad's note, or -1 for All Notes.
        int playNote = 60;    // Drum Rack chains: the note the chain's devices receive.
        int chokeGroup = 0;   // Drum Rack chains: 0 for none, or 1 to 16.
        // Drum Rack chains: post-fader sends to the rack's return chains, -100 dB for off.
        std::array<float, maximumRackReturns> sendsDb { -100, -100, -100, -100, -100, -100 };
        bool operator== (const RackChainSettings&) const = default;
    };
    // A macro drives one target between minimum and maximum, in the target's own units; a minimum
    // above the maximum inverts it. The target is a device inside the rack and one of its controls,
    // a chain (parameter 0 volume, 1 pan, 2 to 7 its sends) or the rack itself (its chain selector).
    struct MacroMapping
    {
        int macro = 0;
        juce::String targetId;
        int parameter = 0;
        float minimum = 0, maximum = 1;
        bool operator== (const MacroMapping&) const = default;
    };
    struct RackChainInfo;
    struct EffectSettings
    {
        EffectKind kind = EffectKind::equalizer;
        std::array<float, 128> values {};
        float tempoBpm = 120; // Derived project tempo, never a separate stored device setting.
        bool operator== (const EffectSettings&) const = default;
    };
    // A mixer's automatable controls. The split pans belong to track and group mixers, and sound in
    // Split Stereo pan mode (see setPanMode); the Main has volume and pan.
    enum class AutomationTarget { volume, pan, leftSplitPan, rightSplitPan };
    struct AutomationPoint
    {
        double beat = 0;
        float value = 0; // Decibels for volume, -1..1 for the pans.
        // The shape of the segment to the next point, as the engine plays it: 0 straight, otherwise a
        // curve, -0.5 to 0.5 bowing it (positive changing late), and beyond to ±1 bowing it further
        // with its ends held.
        float curve = 0;
        bool operator== (const AutomationPoint&) const = default;
    };
    // The value a segment reaches part way along it (0 to 1), in the lane's 0-1 proportions, as the
    // engine plays its curve.
    static float curveProportion (float from, float to, float curve, double along);
    struct AutomationLane
    {
        bool enabled = true;
        std::vector<AutomationPoint> points;
        bool operator== (const AutomationLane&) const = default;
    };
    // A send to a return track: its level, from -100 (Off) to 0 dB, and its automation in the same
    // decibels (see setSendAutomation).
    struct SendInfo { juce::String returnTrackId; float gainDb = -100.0f; AutomationLane automation; };
    // Clip envelopes, after the reference's: while a clip plays it changes a control of its track's
    // mixer or of a device on that track. Modulation moves the control from its value: volume as a
    // fraction (0-1) of the fader position, pan by -1 to +1, a device control by -1 to +1 halves of
    // its range. Automation, for Session clips only, sets the control in its own units (decibels,
    // pan, a native control's units or a plug-in's 0-1). Points are beats from the clip's content
    // start. A linked envelope follows the clip and its loop; an unlinked one loops on its own
    // length from the clip's start.
    enum class EnvelopeMode { modulation, automation };
    struct EnvelopeTarget
    {
        juce::String pluginId; // Empty for the track's mixer.
        int parameter = 0;     // The mixer's 0 volume or 1 pan, a native control or a plug-in parameter index.
        bool operator== (const EnvelopeTarget&) const = default;
    };
    struct ClipEnvelope
    {
        EnvelopeTarget target;
        EnvelopeMode mode = EnvelopeMode::modulation;
        std::vector<AutomationPoint> points;
        bool unlinked = false;
        double loopBeats = 4; // An unlinked envelope's loop, 1 to 1024 beats.
        bool operator== (const ClipEnvelope&) const = default;
    };
    // Coordinate mapping matches native interpolation, including the volume law.
    static float automationValueToProportion (AutomationTarget, float value);
    static float automationProportionToValue (AutomationTarget, float proportion);
    // A send's knob position, 0 to 1, for a level in decibels (-100 for Off), and back, along the send law.
    static float sendValueToProportion (float decibels);
    static float sendProportionToValue (float proportion);
    // A pan as the reference shows it (static reading of its pan formatter): C within 0.01 of the
    // centre, otherwise 1 to 50 L or R, rounded. panFromText reads such text back, or a bare number
    // as R, and C as the centre.
    static juce::String panText (float pan);
    static float panFromText (const juce::String&);
    // A level control's travel in decibels, as the reference's faders and knobs move: the position
    // along its volume law (to +6 dB) or its send law (to 0 dB). Silence, at the bottom, reads as
    // `silence`: -100, or the cue volume's -70.
    static juce::NormalisableRange<double> levelRange (bool send, double silence = -100);
    enum class NoteTransform { quantizeStarts, quantizeEnds, reverse, invertPitch, legato, halfTime, doubleTime, fixedLength };
    // Note tools after the reference's transformation and generative tools; settings follow each
    // tool's field table in NoteTools.h. Appended tools keep their value; never reorder.
    enum class NoteTool { strum, chop, span, recombine, velocityShaper, arpeggiate, ornament, connect, euclidean, seed, fitToScale };
    static constexpr int noteToolCount = 11;
    struct NoteToolSettings
    {
        NoteTool tool = NoteTool::strum;
        std::array<double, 8> values {};
        bool operator== (const NoteToolSettings&) const = default;
    };
    // MPE, after the reference's: a note's own pitch bend in semitones (-48 to 48), slide (CC 74)
    // and pressure (both 0-1), each a curve of points at beats from the note's start, ramping
    // between them and holding the last. A clip with any plays as MPE: each note on its own
    // channel, with a pitch bend range of 48 semitones there.
    enum class NoteExpression { pitchBend, slide, pressure };
    struct ExpressionPoint { double beat = 0; float value = 0; bool operator== (const ExpressionPoint&) const = default; };
    struct MidiNoteInfo
    {
        juce::String id;
        int pitch = 60;
        double startBeat = 0, lengthBeats = 1;
        float velocity = 0.8f;
        bool active = true;
        float chance = 1;      // The probability the note plays on each pass of its clip, 0 to 1.
        int velocityRange = 0; // Each pass adds a random velocity between 0 and this, -127 to 127.
        std::vector<ExpressionPoint> pitchBend, slide, pressure;
    };
    // MIDI controller lanes in a clip: CC numbers 0-119, plus pitch bend and channel pressure.
    // Each point's value holds until the next point (step playback).
    static constexpr int pitchBendLane = 128, channelPressureLane = 129;
    struct ControllerPoint
    {
        double beat = 0; // From the clip start.
        int value = 0;   // 0-127, or -8192 to 8191 for pitch bend.
        bool operator== (const ControllerPoint&) const = default;
    };
    struct ControllerLane
    {
        int controller = 0;
        std::vector<ControllerPoint> points;
        bool operator== (const ControllerLane&) const = default;
    };
    // A clip's scale: the piano roll highlights its notes and Fit to Scale moves notes into it.
    // Scale modes are appended only; never reorder.
    static constexpr int scaleModeCount = 16;
    struct ScaleSettings
    {
        bool enabled = false;
        int root = 0; // Pitch class, 0 = C.
        int mode = 0; // Index into scaleModeName / scaleIntervals.
        bool operator== (const ScaleSettings&) const = default;
    };
    static const char* scaleModeName (int mode) noexcept;
    static uint16_t scaleIntervals (int mode) noexcept; // Bit n set when the scale holds n semitones above its root.
    static bool inScale (const ScaleSettings&, int pitch) noexcept; // Every pitch is in a disabled scale.
    static int fitToScale (const ScaleSettings&, int pitch) noexcept; // The nearest scale pitch; ties go down.
    // The groove pool. A groove is a pattern of note timings and velocities measured against its Base
    // grid; MIDI clips that use one play their notes moved and accented by it without being changed,
    // until the groove is committed. Bases are appended only; never reorder.
    enum class GrooveBase { quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond };
    static constexpr int grooveBaseCount = 6, maximumGrooveEvents = 4096, maximumGrooves = 256;
    static constexpr float maximumGrooveAmount = 130;
    struct GrooveEvent
    {
        double beat = 0;      // From the groove's start.
        float velocity = 127; // 1-127.
        bool operator== (const GrooveEvent&) const = default;
    };
    struct GrooveInfo
    {
        juce::String id, name;
        GrooveBase base = GrooveBase::sixteenth;
        double lengthBeats = 4; // The pattern repeats every lengthBeats (1/960 to 1024 beats).
        // Percentages: Quantize, Timing and Random 0-100, Velocity -100 to 100.
        float quantize = 0, timing = 100, random = 0, velocity = 0;
        std::vector<GrooveEvent> events; // Ordered by beat, inside [0, lengthBeats).
        bool operator== (const GrooveInfo&) const = default;
    };
    // Halftrack's own generated grooves (swing at several amounts); their ids are empty.
    static std::vector<GrooveInfo> grooveLibrary();
    // Reads a groove file in the reference's .agr format (plain or gzip XML) for addGroove; its id is empty.
    static juce::Result readGrooveFile (const juce::File&, GrooveInfo&);
    static const char* grooveBaseName (GrooveBase) noexcept;
    // Smoothstep (3x^2 - 2x^3) is the shape of the reference's default fades.
    enum class FadeCurve { linear = 1, convex, concave, sCurve, smoothstep };
    // Warped audio follows the project tempo; its length is fixed in beats. The stretching modes keep
    // its pitch; Re-Pitch plays it faster or slower, so its pitch follows the tempo. Stretch is the
    // reference's Complex and Efficient a cheaper Complex; Beats, Tones, Texture and Complex Pro
    // follow its other modes. Beats plays the segments between transients each at its own speed,
    // Tones and Texture stretch in grains, and Complex Pro keeps formants when transposed.
    enum class WarpMode { off, stretch, efficient, repitch, beats, tones, texture, complexPro };
    // What a Beats segment plays once its audio runs out before the next transient.
    enum class TransientLoop { off, forward, backAndForth };
    struct WarpSettings
    {
        WarpMode mode = WarpMode::off;
        double bpm = 120; // The source's own tempo.
        // Each mode's own settings; the others keep their defaults.
        TransientLoop transientLoop = TransientLoop::off; // Beats.
        float transientEnvelope = 100;                    // Beats, 0-100: below 100 each segment fades out.
        float grainSize = 0;                              // Tones and Texture, 1-100, or 0 for the mode's default (30, 65).
        float flux = 0;                                   // Texture, 0-100: how far grains wander.
        float formants = 100;                             // Complex Pro, 0-100%: how much of the formants stay when transposed.
        bool operator== (const WarpSettings&) const = default;
    };
    // A warp marker pins a point of a warped clip's source to a beat, counted from the source's start
    // at the clip's own tempo. Between markers the audio stretches to fit; before the first it runs
    // from the source's start, and after the last it keeps the clip's tempo.
    struct WarpMarker { double sourceSeconds = 0, beat = 0; bool operator== (const WarpMarker&) const = default; };
    // Tempo guess for a source from its length: whole 4/4 bars, doubled or halved into 80-160 BPM.
    // Auto-Warp Long Samples instead finds a long sample's tempo in its content when it can.
    static double estimateClipTempo (double sourceSeconds);
    struct AudioClipSettings
    {
        float gainDb = 0;
        double fadeInSeconds = 0, fadeOutSeconds = 0;
        FadeCurve fadeInCurve = FadeCurve::linear, fadeOutCurve = FadeCurve::linear;
        // Transposition of a clip warped in a stretching mode, keeping its speed: -48 to 48
        // semitones and -50 to 50 cents.
        int transposeSemitones = 0;
        float detuneCents = 0;
        // Default edge fades, after the reference's Create Fades on Clip Edges: 4 ms smoothstep
        // fades, shown above as their lengths; two touching clips' default edges crossfade. Setting
        // a length or curve of an edge makes it an ordinary fade, and setting the flag again makes it
        // the default. Arrangement clips only.
        bool defaultFadeIn = false, defaultFadeOut = false;
        bool operator== (const AudioClipSettings&) const = default;
    };
    // A MIDI or warped audio clip's loop, in the beats of its content, after the reference's Loop
    // switch with its Position and Length.
    // An editing grid, after the reference's: the Arrangement's, and each clip's own for its editor. Its steps form one ladder: 512 bars,
    // 256, ... 1 bar, then the note values 1/2, 1/4, ... 1/16384. Fixed, the step is one of them;
    // adaptive, it is the one closest to `intervalPixels` wide at the zoom on a log scale, never
    // with lines closer than 8 pixels nor as long as half the view. A tuplet step is 2/n of a note
    // value's length; bars take none. Saved with the project, outside the undo history, as the
    // reference saves its grid with the Set.
    struct EditGrid
    {
        bool fixed = false;
        int numerator = 1, denominator = 16; // A note value 1/d, or with denominator 1 a number of bars.
        int intervalPixels = 20;             // Adaptive line spacing.
        int tuplet = 2;                      // 2 plain, 3 triplet, 5 quintuplet, 7 septuplet.
        bool snap = true;
        bool operator== (const EditGrid&) const = default;
    };
    struct GridValue
    {
        int numerator = 1, denominator = 16;
        bool bars() const noexcept { return denominator == 1; }
        bool operator== (const GridValue&) const = default;
    };
    struct ClipLoop { bool on = false; double start = 0, length = 0; bool operator== (const ClipLoop&) const = default; };
    struct ClipInfo
    {
        juce::String id, name;
        juce::Colour colour; // Its own colour, or its track's while it has none.
        double start = 0, length = 0, sourceOffset = 0;
        juce::File file;
        bool isMidi = false, isAudio = false, canEditAudioRange = false, canEditAudioFades = false;
        bool reversed = false, canReverse = false, canEditMidiClip = false, active = true;
        double sourceLength = 0;
        AudioClipSettings audio;
        WarpSettings warp;
        bool canWarp = false;
        FollowSettings follow;
        LaunchSettings launch;
        double startBeat = 0, lengthBeats = 0;
        std::vector<MidiNoteInfo> notes;
        std::vector<ControllerLane> controllers; // Lanes with points, by controller number.
        ScaleSettings scale;
        juce::String grooveId; // The pool groove a MIDI clip plays with; empty for None.
        juce::String infoText; // Its own info text, from Edit Info Text; empty for none.
        std::vector<WarpMarker> warpMarkers; // A warped audio clip's markers, in order.
        // MIDI and warped audio clips: the beat of their content heard at the clip's start (the
        // start marker) and their loop. Unlooped, the loop gives the content the clip plays.
        bool canLoop = false;
        double contentStartBeat = 0;
        ClipLoop loop;
        std::vector<ClipEnvelope> envelopes; // Envelopes with points, in the order they were added.
        bool mediaMissing = false; // An audio clip whose file is not on disk plays silence.
        // A video clip: an audio clip holding a movie's sound, whose picture plays with it.
        juce::File videoFile;
        EditGrid grid; // Its editor's grid.
    };
    // A locator; songStart while it is the song's start (see setLocatorSongStart).
    struct LocatorInfo { juce::String id, name; double position = 0; bool songStart = false; };
    // The project meter. Beats are quarter notes at every meter, so a bar is numerator x 4 / denominator beats.
    struct TimeSignature
    {
        int numerator = 4, denominator = 4;
        double beatsPerBar() const noexcept { return numerator * 4.0 / denominator; }
        bool operator== (const TimeSignature&) const = default;
    };
    // A time signature marker, as the reference's: from its beat the bars are of its meter, the first
    // of them starting there. One that falls inside a bar leaves that bar short, a fragmentary bar.
    struct TimeSignatureChange
    {
        double beat = 0;
        TimeSignature meter;
        bool operator== (const TimeSignatureChange&) const = default;
    };
    // The bar a beat is in, counted from 0 at the project's start: its start and length in beats,
    // its meter, and whether a marker after it cuts it short.
    struct BarInfo
    {
        int number = 0;
        double start = 0, length = 4;
        TimeSignature meter;
        bool fragmentary = false;
    };
    // Bars through time signature markers (in beat order, the first at beat 0), as playback counts them.
    static BarInfo barAt (const std::vector<TimeSignatureChange>&, double beat);
    static double barStart (const std::vector<TimeSignatureChange>&, int bar);
    // A scene's tempo and meter, when set, become the project's as the scene launches.
    struct SceneInfo
    {
        juce::String id, name;
        std::optional<double> tempo;
        std::optional<TimeSignature> timeSignature;
        juce::String infoText; // Its own info text, from Edit Info Text; empty for none.
    };
    struct SlotInfo
    {
        juce::String id;
        bool hasClip = false, playing = false, queued = false, stopQueued = false;
        bool recording = false; // A Session recording is capturing into this empty slot.
        bool overdubbing = false; // Session Record is adding the notes played to this MIDI clip.
        ClipInfo clip;
        double playPositionBeats = -1;
    };
    // Where another track's audio is taken: before its devices, after them, or after its mixer.
    enum class InputTap { preFX, postFX, postMixer };
    // A hosted plug-in's sidechain, as in the reference: a plug-in with an auxiliary audio input
    // hears another track's audio there, at its Pre FX, Post FX (the default) or Post Mixer tap,
    // while the sidechain is on. The source is not heard through the plug-in's track.
    struct SidechainSettings
    {
        bool on = false;
        juce::String sourceTrackId;
        InputTap tap = InputTap::postFX;
        bool operator== (const SidechainSettings&) const = default;
    };
    struct PluginInfo
    {
        juce::String id, name;
        bool enabled = true, isUtility = false;
        // An external plug-in that could not be loaded stays as a placeholder: it keeps its saved
        // state, passes audio through and saves unchanged. `loadError` says why.
        bool missing = false;
        juce::String loadError;
        UtilitySettings utility;
        bool isNativeEffect = false;
        bool isInstrument = false; // A hosted plug-in playing as its MIDI track's instrument.
        EffectSettings effect;
        juce::File sample; // A Sampler instrument's loaded sample, if any.
        std::vector<juce::File> padSamples; // A Drum Sampler's sample per pad (empty files for empty pads).
        // Arrangement automation of native device controls, in the controls' own units.
        struct ControlAutomation { int parameter = 0; AutomationLane lane; bool operator== (const ControlAutomation&) const = default; };
        std::vector<ControlAutomation> automation;
        // A hosted plug-in's configured parameters (see setPluginConfiguration), by index in pluginParameters.
        std::vector<int> configured;
        // A loaded hosted plug-in's audio buses as channel counts: its main bus first, then any
        // auxiliary inputs (sidechains) and outputs.
        std::vector<int> inputBuses, outputBuses;
        bool canSidechain = false; // It has an auxiliary input.
        SidechainSettings sidechain;
        // Racks: chains in order, a Drum Rack's return chains and the macro mappings.
        bool isRack = false;
        int visibleMacros = 8;
        std::array<juce::String, rackMacroCount> macroNames; // Names given to macros; empty for automatic names.
        std::vector<RackChainInfo> chains, returns;
        std::vector<MacroMapping> mappings;
    };
    struct RackChainInfo
    {
        juce::String id;
        RackChainSettings settings;
        std::vector<PluginInfo> devices; // MIDI effects, then at most one instrument, then audio effects.
    };
    // A take lane: recorded or added takes kept below a track's main lane, as in the reference's
    // comping. Take lane clips do not play; comping copies a range of a lane into the main lane.
    struct TakeLaneInfo { juce::String id, name; std::vector<ClipInfo> clips; };
    // A track's or group's output ("Audio To"): Master, Sends Only, or another audio track, whose devices
    // and mixer then play it as if it were that track's input.
    // A MIDI track without an instrument plays its MIDI to a MIDI port instead ("MIDI To"), on one channel.
    // external plays to one of the audio device's outputs (the reference's Audio To Ext. Out),
    // bypassing the master. group plays into the group the track or group is in, the default there,
    // which the reference names after the group; any other output leaves the group.
    enum class OutputKind { master, sendsOnly, track, midiPort, external, group };
    struct TrackOutput
    {
        OutputKind kind = OutputKind::master;
        juce::String trackId;
        juce::String port;   // midiPort: the MIDI output port's name; external: the audio output's name.
        int midiChannel = 1; // midiPort: 1-16.
        bool operator== (const TrackOutput&) const = default;
    };
    struct TrackInfo
    {
        // The name shown: the user's name, or an automatic one as the reference's (see renameTrack).
        // userName is empty while the name is automatic. numberLabel is what the reference's activator
        // shows: the track's number (groups counted, as in automatic names), or a return's letter.
        juce::String id, name, userName, numberLabel;
        juce::Colour colour;
        juce::String infoText; // Its own info text, from Edit Info Text; empty for none.
        float gainDb = 0, pan = 0, peakL = 0, peakR = 0;
        bool splitPan = false; // Split Stereo pan mode, with these pans of the left and right channels.
        float leftSplitPan = -1, rightSplitPan = 1;
        bool mute = false, solo = false, armed = false, isMidi = false, isReturn = false;
        int inputChannel = 0; // A MIDI track's MIDI From channel, 1 to 16; 0 for All Channels.
        bool frozen = false; // Plays a render of its clips and devices; devices and clips are locked.
        int height = 0;      // Its Arrangement height in pixels, or 0 for the default.
        bool folded = false; // Folded in the Arrangement to its header strip.
        CrossfadeAssign crossfade = CrossfadeAssign::none;
        double delayMs = 0; // Track Delay: positive plays later, negative earlier.
        SendMode sendMode = SendMode::post;
        std::vector<SendInfo> sends;
        SynthSettings synth;
        AutomationLane volumeAutomation, panAutomation, leftSplitAutomation, rightSplitAutomation;
        std::vector<TakeLaneInfo> takeLanes; // Top to bottom.
        juce::String auditionLane; // The take lane playing in place of the main lane, if any.
        TrackOutput output;
        MonitorMode monitor = MonitorMode::off;
        juce::String inputName;
        juce::String inputTrackId; // When the input is another track's output (Audio From a track), that track.
        InputTap inputTap = InputTap::postMixer;
        std::vector<ClipInfo> clips;
        std::vector<PluginInfo> plugins; // MIDI effects, then audio devices; never the instrument.
        PluginInfo instrument;           // isNativeEffect when a native instrument replaces the built-in synth.
        bool hasInstrument = false;      // A MIDI track with an instrument, built in, native or hosted; MIDI tracks can have none.
        std::vector<SlotInfo> slots;
        bool launcherActive = false;
        juce::String groupId; // Empty for ungrouped tracks.
        juce::String linkId;  // The tracks sharing it are linked for editing; empty when unlinked.
        bool autoArmed = false; // Armed by Auto-Arm because it is selected, rather than by hand.
    };
    // A group sums its member tracks through its own mixer, like a bus.
    struct GroupInfo
    {
        juce::String id, name, userName, numberLabel; // As TrackInfo's.
        juce::Colour colour;
        juce::String infoText; // As TrackInfo's.
        float gainDb = 0, pan = 0, peakL = 0, peakR = 0;
        bool splitPan = false; // As TrackInfo's.
        float leftSplitPan = -1, rightSplitPan = 1;
        bool mute = false, solo = false, folded = false;
        juce::String parentId;       // The group it is inside, if any.
        juce::StringArray memberIds; // Its own tracks, in order; groups inside it name it as their parent.
        // Where its header is: before Snapshot::tracks[position], or after every track at tracks.size().
        // An empty group has a place too.
        int position = 0;
        std::vector<PluginInfo> plugins; // Its audio devices, in chain order.
        std::vector<SendInfo> sends;     // One per return, as a track's.
        AutomationLane volumeAutomation, panAutomation, leftSplitAutomation, rightSplitAutomation; // As a track's.
        TrackOutput output; // Its Audio To, as a track's.
    };
    // A point on the tempo map: the tempo ramps linearly from each point to the next and holds after the last.
    struct TempoPoint
    {
        double beat = 0, bpm = 120;
        bool operator== (const TempoPoint&) const = default;
    };
    // The metronome's sound, after the reference's Sound choices (Classic by default).
    enum class MetronomeSound { classic, click, wood };
    static constexpr int metronomeSoundCount = 3;
    static juce::String metronomeSoundName (MetronomeSound);
    // The metronome's Rhythm choices, in the reference's order, and how one plays in a meter: the
    // tick in crotchets, and whether a division divides the bar (Auto and a bar always do).
    struct MetronomeRhythm { const char* name; float value; };
    static std::array<MetronomeRhythm, 10> metronomeRhythms();
    static bool metronomeRhythmFits (float rhythm, TimeSignature);
    static double metronomeTickBeats (float rhythm, TimeSignature);
    struct MetronomeSettings
    {
        bool enabled = false, accent = true, recordingOnly = false;
        float gain = .6f;
        // 0, 1, 2 or 4 bars of the project meter, as the reference offers. This is the application's
        // count-in (Preferences::countInBars): setting it is not an undo step or kept with a project.
        int countInBars = 0;
        // Rhythm, as the reference's: 0 Auto (the meter's denominator), -1 one tick a bar, or a
        // division in crotchets from metronomeRhythms. A division that does not divide the bar
        // plays as Auto. Kept with the project.
        float rhythm = 0;
        bool operator== (const MetronomeSettings&) const = default;
    };
    // A MIDI mapping (see setMidiLearnTarget): a controller, or a note, on a channel and the target
    // it drives between minimum and maximum, in the target's own units.
    struct MidiMapping
    {
        juce::String target;
        int channel = 1, controller = 0; // A note mapping's controller is its note number.
        float minimum = 0, maximum = 1;
        bool note = false;
        bool operator== (const MidiMapping&) const = default;
    };
    // A computer key and the target it drives in Key Map Mode (see setKeyLearnTarget).
    struct KeyMapping
    {
        juce::String target;
        juce::juce_wchar key = 0;
        float minimum = 0, maximum = 1;
        bool operator== (const KeyMapping&) const = default;
    };
    // The Session mixer's sections, as the reference's View menu shows and hides them; its default
    // Set shows In/Out, Sends, the Returns and the Mixer and hides Track Delay and the Crossfader. Kept with the
    // project as view state, not as undo steps.
    struct MixerSections
    {
        bool inOut = true, sends = true, returns = true, mixer = true, trackDelay = false, crossfader = false;
        bool shown = true; // The whole mixer in the Session View, as the reference's View > Mixer.
        bool operator== (const MixerSections&) const = default;
    };
    struct Snapshot
    {
        juce::String name;
        juce::File projectFile;
        uint64_t projectGeneration = 0; // Transient identity; changes only on successful New/Load.
        bool dirty = false, playing = false, recording = false, looping = false;
        bool sessionRecording = false; // Session takes are recording or a clip is overdubbing.
        bool recordWaiting = false; // Record was pressed with Start Playback with Record off, and waits for Play.
        double position = 0, duration = 0, tempo = 120, loopStart = 0, loopEnd = 8, cpu = 0;
        double insertMarker = 0; // Seconds: where Play starts.
        TimeSignature timeSignature; // The meter at the playhead.
        // The time signature markers in beat order, the first at beat 0.
        std::vector<TimeSignatureChange> timeSignatures { TimeSignatureChange {} };
        // The tempo map; one point at beat 0 for a constant tempo. `tempo` is the tempo at the playhead.
        std::vector<TempoPoint> tempoMap { TempoPoint {} };
        int nudging = 0; // Nudge Down (-1) or Up (+1) in effect on playback's tempo; the song tempo above stays.
        bool nudgeDownHeld = false, nudgeUpHeld = false; // The Nudge buttons, as the reference's transport keeps them.
        float masterPeakL = 0, masterPeakR = 0, masterGainDb = 0;
        std::vector<PluginInfo> mainPlugins; // The Main's audio devices, in chain order.
        float crossfader = 0; // -1 is fully A, +1 fully B.
        AutomationLane masterVolumeAutomation;
        std::vector<TrackInfo> tracks;
        std::vector<GroupInfo> groups;
        std::vector<SceneInfo> scenes;
        std::vector<LocatorInfo> locators;
        std::vector<GrooveInfo> grooves; // The groove pool, in pool order.
        bool automationArm = false, punchIn = false, punchOut = false;
        juce::File previewing; // The file the browser preview is playing, if any.
        std::vector<MidiMapping> midiMappings; // Those whose targets exist.
        juce::String midiLearnTarget;
        std::vector<KeyMapping> keyMappings; // Those whose targets exist.
        juce::String keyLearnTarget;
        float previewVolumeDb = -6; // The cue volume, which previews play at.
        // The cue: Cue Out (empty for the Master Out's), the cue volume, whether Cue mode is chosen,
        // whether Cue Out is apart from the Master Out so that it can apply, and whether it does.
        juce::String cueOutput;
        float cueVolumeDb = -6;
        bool cueModeChosen = false, cueAvailable = false, cueMode = false;
        bool soloInPlace = true;
        bool loopIsSongStart = false; // The loop brace's Set Song Start Time Here.
        MixerSections mixerSections;
        bool automationOverridden = false; // Some lane with points is switched off.
        float grooveAmount = 100; // Global Amount, 0-130%: scales every groove's Timing, Random and Velocity.
        LaunchQuantisation launchQuantisation = LaunchQuantisation::bar;
        MetronomeSettings metronome;
        RecordQuantisation recordQuantisation = RecordQuantisation::none;
        double countInBeatsRemaining = 0;
    };

    // A Session without audio devices uses no physical MIDI ports either, unless midiPortPrefix
    // names some: then only ports whose names start with it are used, as a test's own ports are.
    explicit Session (bool enableAudioDevices = true, std::unique_ptr<juce::AudioIODeviceType> customDeviceType = {}, juce::String midiPortPrefix = {});
    ~Session() override;
    Snapshot snapshot() const;
    juce::Result newProject();
    // A new project like the reference's built-in default Set (read from its bundle, 2026-10-22):
    // two MIDI tracks without instruments, two audio tracks, and returns with a reverb and a delay,
    // so 1-MIDI, 2-MIDI, 3-Audio, 4-Audio, A- and B- named by their devices, with eight scenes at
    // 120 BPM in 4/4. The interface starts and makes new projects from it unless a default set has
    // been saved; newProject keeps the plainer project of two audio tracks the suites build on.
    juce::Result newBuiltInDefaultProject();
    juce::Result loadProject (const juce::File&);
    juce::Result saveProject (const juce::File&);
    // Audio files and samples the project refers to that are not on disk. Opening and saving keep
    // their references, and clips whose file is missing play silence.
    juce::Array<juce::File> missingMedia() const;
    // Searches `folder` and its subfolders for missing files by name and relinks each readable
    // match; `found` is how many files were relinked. Not undoable; the project then needs saving.
    juce::Result locateMissingMedia (const juce::File& folder, int& found); // Copies media into <name> Assets beside the .halftrack file.
    // Crash recovery. writeRecovery atomically writes the open project, media by absolute path, without
    // collecting media or changing the project's file, history or unsaved state. recoverProject opens one
    // as an untitled project with unsaved changes and reports the project it was recovered from.
    juce::Result writeRecovery (const juce::File&);
    // A default set is a project template: saveDefaultSet writes the open project like a recovery copy,
    // and newProjectFromDefaultSet opens one as a new, untitled, unchanged project.
    juce::Result saveDefaultSet (const juce::File&);
    juce::Result newProjectFromDefaultSet (const juce::File&);
    juce::Result recoverProject (const juce::File& recovery, juce::File& recoveredFrom);
    // New tracks, as the reference's Insert commands make them (static reading of its code). An audio
    // or MIDI track goes right after the track `after` names, inside its group, or after a group
    // `after` names and everything in it; with `after` empty or naming a return it goes after the
    // other tracks, before the returns. A return track goes right after the return `after` names,
    // otherwise after the other returns. An empty name is automatic (see renameTrack). One undo step.
    // Save as Default Audio/MIDI Track, as the reference's: writes the track's devices and mixer at
    // their current values, without clips, automation, take lanes or sends, to a file. Frozen tracks
    // are refused. With the files named by setDefaultTrackFiles, new audio and MIDI tracks start from
    // them (not those of a new project, nor Slice to New MIDI Track's), with a new colour.
    juce::Result saveTrackAsDefault (const juce::String& trackId, const juce::File& file);
    void setDefaultTrackFiles (const juce::File& audio, const juce::File& midi);
    juce::Result addAudioTrack (juce::String name = {}, const juce::String& after = {});
    juce::Result addMidiTrack (juce::String name = {}, const juce::String& after = {});
    juce::Result addReturnTrack (juce::String name = {}, const juce::String& after = {});
    // A project has at most 12 return tracks, as the reference's Insert Return Track allows.
    static constexpr int maximumReturnTracks = 12;
    juce::Result setTrackSendGain (int sourceTrackIndex, const juce::String& returnTrackId, float decibels);
    // A group's send, as a track's: the reference's group mixers have one per return.
    juce::Result setGroupSendGain (const juce::String& groupId, const juce::String& returnTrackId, float decibels);
    // A send's automation, as the reference's Send lanes: beat points in decibels from -100 (Off) to
    // 0, the send's knob moving straight along the send law between them, curved as track automation
    // is. Empty points restore the send's manual level; points on a send that is Off make it, Off.
    // One undo step; refused while recording, except automation recording, or exporting.
    juce::Result setSendAutomation (const juce::String& trackId, const juce::String& returnTrackId, const AutomationLane&);
    juce::Result setReturnSendMode (const juce::String& returnTrackId, SendMode);
    // Imports a MIDI file's tracks from a beat; with `timeSignatures` its time signatures become
    // markers from there, as the reference offers when importing. One undo step.
    juce::Result importMidiFile (const juce::File&, double startBeat, std::vector<juce::String>& clipIds, bool timeSignatures = false);
    // A MIDI file's valid time signatures, in its beats.
    static std::vector<TimeSignatureChange> midiFileTimeSignatures (const juce::File&);
    juce::Result exportMidiClip (const juce::String& clipId, const juce::File&);
    // Browser preview: a separate preview Edit plays an audio file on the audio device, beside the
    // project and outside it, at the preview volume. Another file replaces it; it stops at its end.
    juce::Result previewFile (const juce::File&);
    void stopPreview();
    juce::Result setPreviewVolume (float decibels); // The cue volume, -70 (off) to +6 dB.
    // Cueing, after the reference's Cue Out, Cue Volume and Solo/Cue switch (static reading of its
    // code). The click and previews always play on Cue Out at the cue volume, as do cued tracks.
    // Cue mode turns the tracks' solo switches into cue switches: a cued track's Post FX signal,
    // before its fader, pan and activator, is heard on Cue Out at unity, and it plays on to its own
    // output as before. The switches keep their state between the modes, and groups cannot be
    // cued. Cue mode needs a Cue Out sharing no channel with the Master Out; otherwise Solo
    // applies, and the choice is kept for when it can. Cue Out and the mode are kept with the
    // project, not as undo steps; the cue volume (-6 dB at first) is kept and is one.
    juce::String cueOutput() const;
    juce::Result setCueOutput (const juce::String& outputName); // Empty for the Master Out's.
    float cueVolumeDb() const;
    juce::Result setCueVolume (float decibels); // -70 (off) to +6 dB.
    juce::Result setCueMode (bool cue);
    // Solo in Place, after the reference's (on at first, kept with the project, not an undo step):
    // soloing tracks keeps the returns audible, fed by the soloed tracks' sends, unless a return is
    // soloed. Off, soloed tracks are heard without the returns.
    bool soloInPlace() const;
    juce::Result setSoloInPlace (bool);
    // The loop brace's Set Song Start Time Here, as the reference's (static reading of its code):
    // while it is on, Play and Record from a stop start at the loop's start, the loop on or off,
    // leaving the insert marker where it is. Kept with the project; one undo step.
    bool loopIsSongStart() const;
    juce::Result setLoopIsSongStart (bool);
    // A locator's Set Song Start Time Here, as the reference's: the locator becomes the song's start,
    // so Play from a stop starts there, taking it from the loop brace or another locator, since
    // one thing holds it at a time; one undo step. Moving the locator moves the song's start and
    // deleting it clears it. The loop brace's setLoopIsSongStart takes it from a locator likewise.
    juce::Result setLocatorSongStart (const juce::String& locatorId, bool);
    // The Session mixer's sections, as the reference's View menu shows and hides them (see MixerSections).
    MixerSections mixerSections() const;
    void setMixerSections (const MixerSections&);
    juce::Result addMidiClip (const juce::String& trackId, double startBeat, double lengthBeats);
    // Take lanes. Arrangement recording puts each pass on an armed track in a take lane (reusing a lane
    // with nothing at or after the pass's start) as well as the main lane. Comping copies a lane's
    // material in a time range into the main lane, replacing what was there, as one undo step;
    // cycling replaces it with the next or previous lane holding material in the range.
    juce::Result insertTakeLane (const juce::String& trackId, juce::String& laneId);
    juce::Result removeTakeLane (const juce::String& laneId);
    // Deletes take lanes, with the lanes at their places on linked tracks, as one undo step, as the
    // reference's Delete All Take Lanes does for the selected tracks.
    juce::Result removeTakeLanes (const juce::StringArray& laneIds);
    // The tracks' take lanes from which nothing in their main lanes came, by comping or as the pass
    // recorded into both, for Delete Unused Take Lanes; as the reference counts a lane used when one of
    // its takes is in the main lane (0x103827a2c).
    juce::StringArray unusedTakeLanes (const juce::StringArray& trackIds) const;
    juce::Result renameTakeLane (const juce::String& laneId, const juce::String& name);
    juce::Result compTake (const juce::String& laneId, double startSeconds, double endSeconds);
    juce::Result cycleTake (const juce::String& trackId, double startSeconds, double endSeconds, bool next);
    // Auditions a take lane: it plays in place of its track's main lane, which the snapshot keeps
    // showing, until another lane is auditioned, auditioning stops, or anything is edited, undone,
    // redone, recorded, saved or exported. The swap keeps no undo history.
    juce::Result auditionTakeLane (const juce::String& laneId);
    void stopAuditioning();
    // Capture MIDI: the notes played into armed or In-monitored MIDI tracks are kept. Capturing turns
    // each track's latest phrase (since its last pause of over ten seconds) into a clip, bar-aligned:
    // in the Arrangement at the beats it was played while the transport ran, otherwise from the
    // insert marker at the current tempo; or into the track's first empty Session slot. One undo step.
    enum class CaptureDestination { arrangement, session };
    juce::Result captureMidi (CaptureDestination, juce::StringArray& createdClipIds);
    bool hasCapturedMidi() const;
    juce::Result addScene (juce::String name = {});
    juce::Result removeScene (int sceneIndex);
    juce::Result duplicateScene (const juce::String& sceneId, juce::String& duplicatedSceneId);
    // Capture and Insert Scene, after the reference: a new scene after the given one (or at the end)
    // holding the clips now playing, one per track, which play on in it without interruption; copies
    // of them stay in their old scenes. Refused when none plays. One undo step.
    juce::Result captureAndInsertScene (int afterSceneIndex, juce::String& sceneId);
    // Consolidate Time to New Scene, after the reference: each track's material in an Arrangement
    // time selection becomes one looping clip, as Consolidate makes it, in a new scene after the
    // given one (or at the end). The Arrangement is left as it was. One undo step.
    juce::Result consolidateTimeToNewScene (const juce::StringArray& trackIds, double startSeconds, double endSeconds, int afterSceneIndex, juce::String& sceneId);
    juce::Result moveScene (const juce::String& sceneId, int destinationIndex);
    juce::Result renameScene (int sceneIndex, const juce::String& name);
    // Each is one undo step; an empty value clears it. Tempos run 20 to 999 BPM, meters 1 to 99 over 1, 2, 4, 8 or 16.
    juce::Result setSceneTempo (const juce::String& sceneId, std::optional<double> bpm);
    juce::Result setSceneTimeSignature (const juce::String& sceneId, std::optional<TimeSignature>);
    juce::Result importAudioToSlot (const juce::File&, int trackIndex, int sceneIndex);
    juce::Result addMidiClipToSlot (const juce::String& trackId, int sceneIndex, double lengthBeats = 4);
    juce::Result clearSlot (int trackIndex, int sceneIndex);
    // Moving and copying Session clips, after the reference's grid. A clip goes to a slot of a track
    // of its kind (MIDI or audio), replacing any clip there. Moved within its track a clip keeps
    // playing; moved to another track it stops. A copy has new identities. Each is one undo step.
    juce::Result moveSlotClip (int fromTrack, int fromScene, int toTrack, int toScene, bool copy = false);
    // Duplicate: a copy in the slot below, or in a new scene inserted below when that slot is taken
    // or the clip is in the last scene. `duplicateScene` is where it went.
    juce::Result duplicateSlotClip (int trackIndex, int sceneIndex, int& duplicateScene);
    // The Session clipboard: a slot's clip, its audio kept in a copy of its own, so it pastes into
    // this or another project. There is one clipboard, as in the reference: a slot clip's copy also
    // pastes into the Arrangement (pasteMidiClip, pasteAudioClip), and an Arrangement clip's into a
    // slot, a MIDI clip keeping its beats. A time selection's copy clears it.
    juce::Result copySlotClip (int trackIndex, int sceneIndex, bool cut = false);
    bool hasSlotClipboard() const;
    // Pastes the copied clip into a slot; a copy of several slots pastes with its top-left clip there
    // and the rest at their places from it, all or none.
    juce::Result pasteSlotClip (int trackIndex, int sceneIndex);
    // Several slots at once, as the reference's grid selection, each one undo step: their clips moved
    // or copied by a number of tracks and scenes (refused if any would leave the grid or land on a
    // track of the wrong kind; clips there are replaced), cleared, or copied (or cut) keeping their
    // places from the top-left one.
    struct SlotRef { int track = -1, scene = -1; auto operator<=> (const SlotRef&) const = default; };
    juce::Result moveSlotClips (const std::vector<SlotRef>&, int trackDelta, int sceneDelta, bool copy = false);
    juce::Result clearSlots (const std::vector<SlotRef>&);
    juce::Result copySlotClips (const std::vector<SlotRef>&, bool cut = false);
    juce::Result launchSlot (int trackIndex, int sceneIndex);
    // A launch button press/release applying the slot clip's launch mode. velocity is a pressing
    // note's velocity over 127, which the clip's Velocity amount applies; any other press is 1.
    juce::Result pressSlot (int trackIndex, int sceneIndex, float velocity = 1.0f);
    juce::Result releaseSlot (int trackIndex, int sceneIndex);
    // Records the armed track's input into an empty slot from the next launch boundary. Pressing the
    // recording slot again ends the take on the grid and plays it in loop; stopping the track keeps it silent.
    juce::Result recordSlot (int trackIndex, int sceneIndex);
    // Session Record: armed tracks record new clips into the scene's empty slots, and an armed MIDI
    // track whose slot holds a MIDI clip launches it and overdubs, adding the notes played at their
    // looped positions as one undo step. Pressed again, new takes end on the launch grid and loop,
    // and overdubbing stops.
    juce::Result sessionRecord (int sceneIndex);
    juce::Result setClipLaunchSettings (const juce::String& clipId, const LaunchSettings&);
    juce::Result launchScene (int sceneIndex);
    juce::Result stopLauncherTrack (int trackIndex);
    juce::Result stopAllLauncherClips();
    juce::Result returnToArrangement();
    juce::Result setLaunchQuantisation (LaunchQuantisation);
    // The grids in the reference's menu order, with its names.
    static std::vector<std::pair<juce::String, LaunchQuantisation>> launchQuantisations();
    static std::vector<std::pair<juce::String, ClipQuantisation>> clipQuantisations();
    juce::Result addMidiNote (const juce::String& clipId, int pitch, double startBeat, double lengthBeats, float velocity);
    // Input IDs are ignored. Successful insertion replaces insertedIds in input order; failure leaves it unchanged.
    juce::Result addMidiNotes (const juce::String& clipId, const std::vector<MidiNoteInfo>& notes, juce::StringArray& insertedIds);
    juce::Result updateMidiNote (const juce::String& clipId, const juce::String& noteId,
                                 int pitch, double startBeat, double lengthBeats, float velocity);
    juce::Result updateMidiNotes (const juce::String& clipId, const std::vector<MidiNoteInfo>& notes);
    juce::Result setMidiNotesActive (const juce::String& clipId, const juce::StringArray& noteIds, bool active);
    // Each is one undo step for the notes given. A clip with notes that vary plays them differently
    // on each pass of its loop, through 64 passes that repeat.
    juce::Result setMidiNotesChance (const juce::String& clipId, const juce::StringArray& noteIds, float chance);
    // Sets one of a note's expression curves as one undo step; no points removes it.
    juce::Result setMidiNoteExpression (const juce::String& clipId, const juce::String& noteId, NoteExpression, const std::vector<ExpressionPoint>&);
    juce::Result setMidiNotesVelocityRange (const juce::String& clipId, const juce::StringArray& noteIds, int range);
    juce::Result splitMidiNotes (const juce::String& clipId, const juce::StringArray& noteIds, double splitBeat, juce::StringArray& insertedIds);
    juce::Result transformMidiNotes (const juce::String& clipId, const juce::StringArray& noteIds, NoteTransform, double gridBeats);
    // Applies a note tool in one undo step. A transformation replaces the chosen notes with its
    // result; a generator (Euclidean, Seed) fills the clip, replacing any chosen notes.
    juce::Result applyNoteTool (const juce::String& clipId, const juce::StringArray& noteIds, const NoteToolSettings&, juce::StringArray& resultIds);
    juce::Result removeMidiNote (const juce::String& clipId, const juce::String& noteId);
    // Replaces one controller lane of a MIDI clip in one undo step; no points removes the lane.
    juce::Result setMidiControllerLane (const juce::String& clipId, int controller, const std::vector<ControllerPoint>& points);
    juce::Result setClipScale (const juce::String& clipId, const ScaleSettings&);
    // Groove pool edits, one undo step each. addGroove copies a groove (from grooveLibrary or a file) into
    // the pool; extractGroove makes one from a MIDI clip's notes; setGroove replaces a pool groove's name,
    // Base, amounts and pattern; removeGroove sets the clips that used it to None.
    juce::Result addGroove (const GrooveInfo&, juce::String& grooveId);
    juce::Result extractGroove (const juce::String& clipId, juce::String& grooveId);
    juce::Result setGroove (const juce::String& grooveId, const GrooveInfo&);
    juce::Result removeGroove (const juce::String& grooveId);
    juce::Result setGrooveAmount (float percent);
    // A MIDI clip's groove; an empty id is None. Commit writes the groove into the clip's notes and sets it to None.
    juce::Result setClipGroove (const juce::String& clipId, const juce::String& grooveId);
    juce::Result commitClipGroove (const juce::String& clipId);
    juce::Result removeMidiNotes (const juce::String& clipId, const juce::StringArray& noteIds);
    juce::Result setSynthSettings (const juce::String& trackId, const SynthSettings&);
    // Replaces a MIDI track's instrument in one undo step; nullopt restores the built-in synth.
    // Giving an instrument to a track playing to a MIDI port returns its output to Master.
    juce::Result setTrackInstrument (const juce::String& trackId, std::optional<EffectKind> instrument);
    // Removes a MIDI track's instrument in one undo step, as deleting it in the reference does; the
    // track's MIDI can then play to a MIDI port.
    juce::Result removeTrackInstrument (const juce::String& trackId);
    // MIDI output ports, as the reference's MIDI Ports preferences list them. Track offers a port to
    // MIDI tracks' MIDI To; Sync sends it MIDI Clock with Song Position, Start, Continue and Stop
    // while the transport plays. Kept with the engine's device settings, not the project.
    struct MidiOutputPortInfo { juce::String name; bool track = false, sync = false; bool operator== (const MidiOutputPortInfo&) const = default; };
    std::vector<MidiOutputPortInfo> midiOutputPorts() const;
    juce::Result setMidiOutputPort (const juce::String& name, bool track, bool sync);
    // A Mackie Control surface (or one in its Mackie mode), as the reference's Control Surface
    // preferences set one. Its eight strips show eight tracks from the bank's first: moving faders
    // for volume, V-Pots for pan, Rec, Solo and Mute, names on the scribble strips. The master
    // fader, the transport, Cycle and Click work too, and every change goes through the same edits
    // (and undo) as the interface's. Halftrack opens the surface's ports itself and keeps the
    // engine's input for that port closed, so the surface plays into no track. An empty input is
    // no surface. Kept with the engine's settings.
    // External Sync, after the reference: an input port's Sync switch (kept with the engine's
    // settings) lets it send MIDI Clock. With External Sync on (the control bar's EXT), the transport
    // follows it: the tempo from the clock, drawn toward its position while playing; Start from the
    // beginning, Continue, Stop, and Song Position while stopped. The tempo it sets is not an undo
    // step, and the tempo cannot be set otherwise meanwhile. It needs a single tempo, not a map.
    juce::Result setMidiInputSync (const juce::String& port, bool sync);
    bool isMidiInputSync (const juce::String& port) const;
    juce::Result setExternalSync (bool);
    bool isExternalSync() const;
    struct ExternalClock { bool receiving = false; double bpm = 0; };
    ExternalClock externalClock() const; // Whether clock has arrived in the last half second, and its tempo.
    bool hasSyncInputs() const;           // Whether any available input has its Sync switch on.
    struct ControlSurface { juce::String input, output; bool operator== (const ControlSurface&) const = default; };
    juce::StringArray midiInputPorts() const;
    juce::Result setControlSurface (const ControlSurface&);
    ControlSurface controlSurface() const;
    int controlSurfaceBank() const; // The track the surface's first strip shows.
    // Sends the surface what it should show now; the Session also does this twenty times a second.
    void updateControlSurface();
    juce::Result showInstrumentEditor (int trackIndex);
    // Loads an audio file into the track's Sampler, switching the instrument to a Sampler first if needed (one undo step).
    juce::Result setInstrumentSample (const juce::String& trackId, const juce::File& sample);
    // Loads (or with an empty file clears) a Drum Sampler pad, switching the instrument to a Drum Sampler if needed.
    juce::Result setDrumPadSample (const juce::String& trackId, int pad, const juce::File& sample);
    juce::Result duplicateTrack (const juce::String& trackId, juce::String& duplicateId);
    // Duplicates tracks and groups, a group with everything in it, as the reference's Duplicate does:
    // the copies go as one block, in order, right after the last of them (after a group's last
    // track), in the group that one is in. Copies keep their devices, clips, sends and mixer; their
    // MIDI notes are new and links are not copied. Return and frozen tracks are refused for now.
    // duplicateIds lists the copies, groups and tracks, in order. One undo step.
    juce::Result duplicateTracks (const juce::StringArray& trackAndGroupIds, juce::StringArray& duplicateIds);
    // Whether duplicateTracks would duplicate these.
    bool canDuplicateTracks (const juce::StringArray& trackAndGroupIds) const;
    // The headers' Copy, Cut and Paste, as the reference's track header commands. Copy keeps copies of
    // tracks and groups, a group with everything in it, as Duplicate copies them. Paste inserts them,
    // as the reference's does, right before the first of the selected tracks and groups, in its group,
    // or after the other tracks with none selected; with new identities, and without sends to returns
    // the project lacks. One undo step. A selection with returns has no place for them. Cut copies,
    // then deletes as removeTracks does. Returns and frozen tracks are refused, as for Duplicate.
    // Copying leaves the project and its history alone.
    juce::Result copyTracks (const juce::StringArray& trackAndGroupIds);
    juce::Result cutTracks (const juce::StringArray& trackAndGroupIds);
    juce::Result pasteTracks (const juce::StringArray& selectedIds, juce::StringArray& pastedIds);
    bool hasTrackClipboard() const;
    bool canCutTracks (const juce::StringArray& trackAndGroupIds) const;
    // Freeze renders a track's Arrangement clips through its devices and plays the render; one undo step each.
    juce::Result setTrackFrozen (const juce::String& trackId, bool frozen);
    // Flatten replaces a frozen track's clips and devices with one audio clip of its render.
    juce::Result flattenTrack (const juce::String& trackId);
    // Bounce Track in Place, as the reference's: each selected audio or MIDI track (top-most; groups
    // and returns are left out) is frozen first if it is not, outside history. Its render then
    // replaces its Arrangement clips, cut at their edges, with a tail clip after any whose sound rings
    // on, and its devices, their automation and its take lanes go. Each active Session clip is rendered
    // through the devices into an audio clip in its slot (one loop, seamless, for a looping clip), with
    // its launch settings, follow actions and colour; deactivated ones go. A MIDI track becomes an audio
    // track. Mixer, sends, name and colour stay. One undo step, restoring the unfrozen tracks. MIDI
    // tracks without an instrument are refused, and frozen tracks with Session clips.
    juce::Result bounceTracksInPlace (const juce::StringArray& trackIds);
    bool canBounceTracksInPlace (const juce::StringArray& trackIds) const;
    // Bounce to New Track, as the reference's clip-area command: each of the tracks with an active clip
    // in the time range renders the range through its devices to a new audio track right after it, in
    // its group. The new track takes the source's mixer, sends, track delay, monitoring, crossfade
    // assignment, colour and output, with their automation, and its name with " Bounce"; the render
    // is cut at the bounced clips' edges. The source keeps its devices, and its clips, split at the
    // range's edges, are deactivated there. One undo step; newTrackIds lists the new tracks. Frozen
    // tracks and MIDI tracks without an instrument are refused when it runs, as the reference refuses
    // the latter; canBounceToNewTracks only asks for an active clip in the range, as its menu does.
    juce::Result bounceToNewTracks (const juce::StringArray& trackIds, double startSeconds, double endSeconds, juce::StringArray& newTrackIds);
    bool canBounceToNewTracks (const juce::StringArray& trackIds, double startSeconds, double endSeconds) const;
    // Bounce to New Track from Session slots, as the reference's slot command: each source track's listed
    // active Session clips are rendered through its devices into a new audio track after it, made as
    // bounceToNewTracks makes one, in the same slots and with the clips' settings, and the clips are
    // deactivated. One undo step.
    juce::Result bounceSlotsToNewTracks (const juce::StringArray& clipIds, juce::StringArray& newTrackIds);
    bool canBounceSlotsToNewTracks (const juce::StringArray& clipIds) const;
    // Groups audio and MIDI tracks and groups (not returns) into a new group, placed where the first
    // of them is: inside its group, if it is in one, as the reference nests groups. What is inside a
    // chosen group moves with it. Launching or stopping a group acts on every track inside it.
    juce::Result groupTracks (const juce::StringArray& trackAndGroupIds, juce::String& groupId);
    // Whether groupTracks would group these, as the reference's Group Tracks command is enabled.
    bool canGroupTracks (const juce::StringArray& trackAndGroupIds) const;
    // Clear All Envelopes, as the reference's header command: removes the selected tracks' and groups'
    // own Arrangement automation (mixer, sends, devices; a frozen track's mixer only) and their
    // Arrangement clips' envelopes for the same controls, take lanes' included, in one undo step.
    // Tracks in a selected group and Session clips keep theirs.
    juce::Result clearAllEnvelopes (const juce::StringArray& trackAndGroupIds);
    // Whether a selected track or group has Arrangement automation for clearAllEnvelopes to clear.
    bool canClearAllEnvelopes (const juce::StringArray& trackAndGroupIds) const;
    // A track's, group's, clip's or scene's own info text, as the reference's Edit Info Text gives one:
    // the Info view shows it while pointing at the track's header, the clip or the scene. Empty clears
    // it. Saved; one undo step.
    juce::Result setInfoText (const juce::String& id, const juce::String& text);
    // Gives tracks and groups a colour, opaque, as the reference's header colour grid does; one undo step.
    // As the reference's clips keep the colour they were made with, a recoloured track's clips without
    // a colour of their own keep the one they showed.
    juce::Result setTrackColours (const juce::StringArray& trackAndGroupIds, juce::Colour);
    // Assign Track Color to Clips, as the reference's header command: the tracks in a chosen group take
    // its colour, then the chosen tracks' and those tracks' clips show their track's colour again,
    // Arrangement clips (main lane) from the Arrangement, Session clips from the Session view. One
    // undo step.
    juce::Result assignTrackColourToClips (const juce::StringArray& trackAndGroupIds, bool arrangement);
    juce::Result ungroupTracks (const juce::String& groupId);
    // Moves tracks and groups into a group (empty for the top level), before a track or group there
    // (empty for its end, before the returns), as dragging headers does in the reference. Their
    // contents move with groups. A group cannot move into itself. Returns move only among the returns
    // (before one, or after the last with no place given), and other tracks stay before them. A group
    // the move empties stays, empty, as the reference's does. One undo step.
    juce::Result moveTracks (const juce::StringArray& trackAndGroupIds, const juce::String& groupId, const juce::String& beforeId);
    // Copies tracks and groups to a place given as moveTracks takes it, as a copying drag of headers
    // does in the reference: with new identities, as Duplicate copies them, and a group may be copied
    // into itself. Returns and frozen tracks are refused, as for Duplicate. One undo step.
    juce::Result copyTracksTo (const juce::StringArray& trackAndGroupIds, const juce::String& groupId, const juce::String& beforeId, juce::StringArray& copiedIds);
    // As renameTrack.
    juce::Result renameGroup (const juce::String& groupId, const juce::String& name);
    juce::Result setGroupGain (const juce::String& groupId, float decibels);
    juce::Result setGroupPan (const juce::String& groupId, float pan);
    juce::Result setGroupMute (const juce::String& groupId, bool);
    juce::Result setGroupSolo (const juce::String& groupId, bool);
    juce::Result setGroupFolded (const juce::String& groupId, bool);
    // A group's clip slot launches its members' clips in that scene on the global grid; members without one stop.
    juce::Result launchGroupSlot (const juce::String& groupId, int sceneIndex);
    juce::Result stopGroup (const juce::String& groupId);
    juce::Result moveTrack (const juce::String& trackId, int destinationIndex);
    juce::Result removeTrack (int trackIndex);
    // Deletes tracks and groups, a group with everything in it, as the reference's Delete does. Sends
    // to a deleted return go, and plug-ins taking their sidechain from a deleted track keep it without
    // a source. As the reference's Delete is enabled, a deletion led by a track or group may not leave
    // a group without tracks, or the project without tracks besides returns. One undo step.
    juce::Result removeTracks (const juce::StringArray& trackAndGroupIds);
    // Whether removeTracks would delete these.
    bool canRemoveTracks (const juce::StringArray& trackAndGroupIds) const;
    // Linked-track editing, after the reference's: Arrangement edits on a linked track apply to the
    // other tracks in its link. Moving a clip in time, trimming, deleting or (de)activating it does
    // the same to the clips on the linked tracks that start and end with it (within 1 ms); a split
    // cuts each linked track's clip at that time; fades change on linked audio clips whose fades
    // start where the edited one's did, which keep their own gain; time-selection edits include the
    // linked tracks; arming or disarming covers the linked tracks that have an input; and take lanes
    // are inserted, deleted, renamed and comped by their position on every linked track. Each edit
    // and its linked edits are one undo step, refused whole when a linked part cannot be done.
    // Linking the given audio and MIDI tracks makes them one link of their own, taking them out of
    // any they were in; unlinking takes tracks out. A link left with one track ends.
    juce::Result linkTracks (const juce::StringArray& trackIds, juce::String& linkId);
    juce::Result unlinkTracks (const juce::StringArray& trackIds);
    juce::Result importAudio (const juce::File&, int trackIndex = -1, double startSeconds = 0);
    // Imports a movie, after the reference: its sound becomes an audio clip (a new track's, by default,
    // named after the movie) that keeps the movie, so its picture plays with it in the Video window
    // and can be exported with the mix. The movie and its sound are collected into the project. A
    // video clip plays at its own speed: it is not warped or reversed. One undo step. Movies are
    // supported on macOS.
    juce::Result importVideo (const juce::File& movie, int trackIndex = -1, double startSeconds = 0);
    // The movie frame shown at a time: the first video clip there, track by track, that is active.
    struct VideoFrame { juce::File movie; double seconds = 0; juce::String clipId; };
    std::optional<VideoFrame> videoAt (double seconds) const;
    juce::Result setAudioClipSettings (const juce::String& clipId, const AudioClipSettings&);
    juce::Result setAudioClipReversed (const juce::String& clipId, bool reversed);
    // Arrangement clips may turn warping off; Session slots always follow the tempo.
    juce::Result setAudioClipWarp (const juce::String& clipId, const WarpSettings&);
    // Replaces a warped audio clip's warp markers, which must increase in both source time and beat
    // and lie within the source; an empty list removes them. One undo step. Turning warp off keeps
    // them for when it is turned back on.
    juce::Result setAudioClipWarpMarkers (const juce::String& clipId, const std::vector<WarpMarker>&);
    // The transients in an audio clip's source, in source seconds, as Beats warping finds them: where
    // the clip view shows them and a double-click pins a warp marker. The first ten minutes are read
    // once while the file is unchanged.
    std::vector<double> audioClipTransients (const juce::String& clipId);
    // Warp From Here (Straight), after the reference: a warped clip's audio from a point of its source
    // is taken as one steady tempo, found from its content. The clip keeps its markers before the
    // point, takes one at the point (on the transient there when one is close) where its audio
    // already plays, loses those after it, and plays at the found tempo from there. It keeps its
    // place and length. Refused when no steady beat is found. One undo step.
    juce::Result warpFromHere (const juce::String& clipId, double sourceSeconds);
    // Warp as N-Bar Loop: the audio a warped clip plays, from its start for its length, is taken
    // as that many bars of the project meter, with no markers. One undo step.
    juce::Result warpAsLoop (const juce::String& clipId, int bars);
    juce::Result setAudioClipRange (const juce::String& clipId, double startSeconds, double lengthSeconds, double sourceOffsetSeconds);
    // Where a MIDI or warped audio clip's content starts and its loop, in content beats. Looping,
    // the content plays from the start into the loop and round it for as long as the clip lasts;
    // the start must lie in the loop (the reference also allows one before it). Unlooped, it plays
    // from the start for the clip's length, and a warped audio clip ends with its source. A Session
    // clip unlooped plays once, from its start to where its loop ended. The clip keeps its place.
    // One undo step.
    juce::Result setClipLoop (const juce::String& clipId, const ClipLoop&, double contentStartBeat);
    // Places an Arrangement MIDI or warped audio clip's edges in project beats (a Session clip's at 0,
    // setting its length), with the content beat
    // heard at its start: moving the start edge and that beat together trims the clip without
    // moving its content. A looped clip lasts as long as it is made, repeating its loop; an unlooped
    // warped audio clip ends with its source. One undo step.
    juce::Result setClipBeats (const juce::String& clipId, double startBeat, double lengthBeats, double contentStartBeat);
    juce::Result splitAudioClip (const juce::String& clipId, double atSeconds, juce::String& rightClipId);
    // Splits an unlooped Arrangement MIDI clip; notes crossing the split become two notes and each
    // controller's value at the split starts the right-hand clip.
    juce::Result splitMidiClip (const juce::String& clipId, double atSeconds, juce::String& rightClipId);
    // Arrangement time-selection edits on the given tracks, each one undo step. Split cuts clips at
    // both edges; Delete removes the material inside; Duplicate copies it to just after the
    // selection, replacing what was there. Clips crossing an edge must be splittable there.
    juce::Result splitRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds);
    juce::Result deleteRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds);
    juce::Result duplicateRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds);
    // Crop, after the reference's (0x10379e2cc): each clip the selection reaches into loses what lies
    // outside it; clips wholly inside or outside it are left alone. canCropRange says whether a clip
    // reaches past the selection. One undo step.
    juce::Result cropRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds);
    bool canCropRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds) const;
    // Reverse, after the reference's (0x10379e774): the selection's edges split its clips, then each clip
    // inside it plays backwards: an audio clip its audio, a MIDI clip its notes mirrored in time within
    // the content it shows (its loop, when looped). One undo step.
    juce::Result reverseRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds);
    // Each listed clip, Arrangement or Session, takes the colour as its own, as the reference's clip
    // colour grid gives it (undo sManeuverSetClipColor). One undo step.
    juce::Result setClipColours (const juce::StringArray& clipIds, juce::Colour);
    // A time selection's clipboard, after the reference's Copy and Paste of selected time. Copying
    // keeps what the selected tracks hold in the range, their audio kept apart from the project;
    // cutting also deletes it, as Delete Selection does. Pasting places it from a track onward at a
    // time, one track of the same kind (audio or MIDI) for each copied track in order, replacing
    // what was there, as one undo step. Clips crossing the range must split there, as for Split.
    // A clip copy clears it, and it clears a clip copy.
    // With `onlyClips`, just those clips are kept (and cut), without the tracks' automation: the
    // reference's copy of selected clips.
    juce::Result copyRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds, bool cut = false, const juce::StringArray& onlyClips = {});
    bool hasRangeClipboard() const;
    double rangeClipboardLength() const;
    int rangeClipboardTracks() const;
    juce::Result pasteRange (const juce::String& firstTrackId, double startSeconds);
    // Consolidate replaces each track's material in the selection with one clip spanning it: MIDI
    // clips join into one MIDI clip, and audio clips render (without devices) into a new file in
    // the project's Assets. Tracks without clips in the selection are left alone.
    juce::Result consolidateRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds, juce::StringArray& newClipIds);
    // Insert Silence: everything at or after start moves later by length across all tracks, locators
    // and automation; clips crossing start split (audio) or open a gap between their notes (MIDI).
    juce::Result insertTime (double startSeconds, double lengthSeconds);
    // Delete Time: removes [start, start + length) everywhere and moves later material earlier.
    juce::Result deleteTime (double startSeconds, double lengthSeconds);
    // The reference's other time commands, each across every track but the returns and one undo
    // step. Cut Time copies the time to the range clipboard and deletes it, as Delete Time does.
    // Paste Time inserts the clipboard's length at a time, as Insert Silence does, and pastes it
    // there from a track onward. Duplicate Time inserts a copy of [start, end) just after it,
    // leaving the clipboard as it was.
    juce::Result cutTime (double startSeconds, double lengthSeconds);
    juce::Result pasteTime (const juce::String& firstTrackId, double atSeconds);
    juce::Result duplicateTime (double startSeconds, double endSeconds);
    juce::Result duplicateAudioClip (const juce::String& clipId, juce::String& duplicateId);
    // Preferences, after the reference's Record, Warp & Launch settings: whether arm and solo clicks
    // are exclusive (Cmd/Ctrl-click then adds), the warp mode clips get (Session clips always, and
    // Arrangement samples over 30 seconds when Auto-Warp Long Samples is on), and new Session
    // clips' launch mode and grid. Kept in a versioned file beside the engine's settings, saved
    // atomically; a missing file gives the defaults, an older version gains them for new settings,
    // and an unreadable one is set aside, keeping the defaults, with the reason reported.
    struct Preferences
    {
        bool exclusiveArm = true, exclusiveSolo = true;
        WarpMode defaultWarpMode = WarpMode::stretch;
        bool autoWarpLongSamples = false;
        LaunchMode defaultLaunchMode = LaunchMode::trigger;
        ClipQuantisation defaultLaunchQuantisation = ClipQuantisation::global;
        // The interface's look: the theme (0 Dark, 1 Light, 2 High Contrast) and the display zoom.
        int theme = 0;
        static constexpr int themeCount = 3;
        int zoomPercent = 100; // 50-200, in steps of 25.
        // Driver Error Compensation, after the reference's: recordings move this much earlier, -200 to
        // 200 ms, for a driver that reports its latency wrongly.
        double driverErrorCompensationMs = 0;
        // Follow Behavior, after the reference's: Page turns the view a page as the play head leaves
        // it; Scroll keeps the play head in the view's middle and scrolls continuously.
        bool scrollFollow = false;
        // Start Recording on Scene Launch, after the reference's (off by default): launching a scene
        // also records into its empty slots on armed tracks.
        bool startRecordingOnSceneLaunch = false;
        // Plug-In Sources, after the reference's Plug-Ins settings: Audio Units (macOS), the VST3
        // system folders, and a custom VST3 folder when its switch is on.
        bool useAudioUnits = true, useVst3SystemFolders = true, useVst3CustomFolder = false;
        // Create Fades on Clip Edges, after the reference's (on by default): new audio clips, and the
        // edges a split makes, get default fades.
        bool createFadesOnClipEdges = true;
        // The metronome's sound, kept with the preferences as the reference keeps its metronome's.
        MetronomeSound metronomeSound = MetronomeSound::classic;
        // The count-in, in bars (0, 1, 2 or 4), an application setting as the reference's is; it is
        // also shown, and set, in MetronomeSettings.
        int countInBars = 0;
        // Start Playback with Record, after the reference's (on): Record pressed while stopped starts
        // playback too. Off, it waits, lit, until Play or a Session launch starts the transport.
        bool startPlaybackWithRecord = true;
        juce::String vst3CustomFolder;
        bool operator== (const Preferences&) const = default;
    };
    // The audio device's latency as it reports it, with Driver Error Compensation. Recordings are placed
    // earlier by the overall latency, so what was heard is recorded where it played.
    struct AudioLatency
    {
        double inputMs = 0, outputMs = 0, compensationMs = 0;
        double overallMs() const noexcept { return inputMs + outputMs + compensationMs; }
    };
    AudioLatency audioLatency() const;
    static constexpr int preferencesVersion = 1;
    Preferences preferences() const;
    juce::Result setPreferences (const Preferences&);
    juce::Result resetPreferences();
    juce::File preferencesFile() const;
    // Why the preferences file could not be read when the Session started, or empty.
    juce::String preferencesProblem() const;
    // Slice to New MIDI Track, after the reference's: a new MIDI track after the clip's plays the
    // clip's audio through a Drum Rack with one pad per slice from C1 (note 36) up, from a MIDI clip
    // in the clip's place with a note per slice. Slices begin at the clip's transients, at its warp
    // markers, or at every division of its beats, and play at the source's own speed. At most 128
    // slices; the clip must be an unwarped or marker-free warped Arrangement clip, unless slicing
    // by markers. One undo step.
    enum class SliceBy { transients, warpMarkers, sixteenth, eighth, quarter, half, bar };
    juce::Result sliceToNewMidiTrack (const juce::String& clipId, SliceBy, juce::String& trackId);
    // Typed application clipboard. Copy retains independent media before Cut removes anything.
    juce::Result copyMidiClip (const juce::String& clipId, bool cut = false);
    bool hasMidiClipClipboard() const;
    juce::Result pasteMidiClip (const juce::String& trackId, double startSeconds, juce::String& pastedId);
    juce::Result duplicateMidiClip (const juce::String& clipId, juce::String& duplicateId);
    juce::Result copyAudioClip (const juce::String& clipId, bool cut = false);
    bool hasAudioClipClipboard() const;
    juce::Result pasteAudioClip (const juce::String& trackId, double startSeconds, juce::String& pastedId);
    juce::Result moveMidiClip (const juce::String& clipId, const juce::String& destinationTrackId, double startSeconds);
    juce::Result moveAudioClip (const juce::String& clipId, const juce::String& destinationTrackId, double startSeconds);
    juce::Result moveClip (int trackIndex, int clipIndex, double startSeconds);
    // A copy of an Arrangement audio or MIDI clip at a time on a track of its kind, as a copying drag
    // leaves; a MIDI clip keeps its beats, as a move does. One undo step.
    juce::Result copyClipTo (const juce::String& clipId, const juce::String& destinationTrackId, double startSeconds, juce::String& copyId);
    juce::Result removeClip (int trackIndex, int clipIndex);
    juce::Result setClipFollowAction (const juce::String& clipId, const FollowSettings&);
    juce::Result setClipActive (const juce::String& clipId, bool active);
    juce::Result renameClip (const juce::String& clipId, const juce::String& name);
    // Track names, as the reference's (static reading of its code). A track without a name of its
    // own shows an automatic one: its number in the track list (groups and grouped tracks counted,
    // returns not), "-", and a MIDI track's first instrument's name, else the name of the first clip
    // it was given (unless that is only the type word or a number and the type word), else Audio,
    // MIDI or Group. A return shows its letter (A, B, ...), "-", and its own name or its devices'
    // names joined with " | ", or Return. A name of its own is shown as it is, a leading run of #
    // becoming the track's number, padded with zeros to the run's length. Renaming trims the name;
    // an empty name, or the automatic name itself, makes it automatic again. One undo step.
    juce::Result renameTrack (int trackIndex, const juce::String&);
    // A track's height in the Arrangement, saved with the project outside the undo history, as the
    // reference saves its tracks' heights; 0 restores the default.
    static constexpr int minimumTrackHeight = 20, maximumTrackHeight = 600;
    juce::Result setTrackHeight (const juce::String& trackId, int pixels);
    // Folding a track shows only its header strip in the Arrangement; saved the same way.
    juce::Result setTrackFolded (const juce::String& trackId, bool folded);
    juce::Result setMasterGain (float decibels);
    juce::Result setTrackCrossfade (const juce::String& trackId, CrossfadeAssign);
    // Track Delay moves a track's output (after its devices, before its sends and mixer) from
    // -1000 ms to +1000 ms; negative delays use the engine's delay compensation.
    static constexpr double maximumTrackDelayMs = 1000;
    juce::Result setTrackDelay (const juce::String& trackId, double milliseconds);
    juce::Result setCrossfader (float position);
    // Dipless crossfader law: a side keeps unity until the fader passes the centre toward the other side.
    static float crossfadeGain (CrossfadeAssign, float position);
    juce::Result setMasterVolumeAutomation (const AutomationLane&);
    juce::Result setTrackGain (int trackIndex, float decibels);
    juce::Result setTrackPan (int trackIndex, float pan);
    // Split Stereo pan mode, after the reference's (static reading of its code): a track's or group's
    // mixer pans its left and right input channels on their own, with Left Split Stereo and Right
    // Split Stereo (-1 to 1, at first hard left and hard right) in place of its pan. Switching blends
    // the two over 75 samples. The ids name tracks or groups, all switched in one undo step, as the
    // reference switches every selected track. Mixers from projects older than Halftrack's own mixer
    // cannot switch.
    juce::Result setPanMode (const juce::StringArray& trackOrGroupIds, bool split);
    juce::Result setTrackSplitPan (int trackIndex, bool right, float pan);
    juce::Result setGroupSplitPan (const juce::String& groupId, bool right, float pan);
    // Selection linking, after the reference's (static reading of its code): with two or more
    // tracks selected, moving one selected track's volume, pan, split pans, a send or its activator
    // moves the same control of the other selected tracks, from the interface, MIDI, keys or a
    // control surface alike. Volume and sends move by the same decibels, the pans by the same
    // amount (among tracks in the same pan mode), and the activator takes the same state. Offsets
    // count from where the controls stood at their first linked move since the selection changed,
    // so a control held at an end of its travel gets its offset back. The moves are one undo step.
    // Automation playback is not linked. The interface reports its selection here.
    void setSelectedTracks (const juce::StringArray& trackIds);
    // A track's mixer control back to its default, as a double-click resets it: "volume" (0 dB),
    // "pan" (the centre), "leftsplit" and "rightsplit" (hard left and right) or "send:<return>"
    // (Off). Every linked track takes the same default.
    juce::Result resetTrackControl (int trackIndex, const juce::String& control);
    // Atomic, strictly ordered beat points. Empty points restore manual control.
    juce::Result setTrackAutomation (const juce::String& trackId, AutomationTarget, const AutomationLane&);
    // Routes a track's or group's output, as the reference's Audio To does: a track or group in a group
    // plays into it, or leaves it for Main, Sends Only, another track or an external output; returns
    // play into Main, Sends Only, a track or an external output. A route that would lead back into the
    // track, through outputs, groups, sends, track inputs or sidechains, is refused. One undo step.
    // As the reference's grouping does (0x1024bdbf8), a track moved into a group from Main plays into
    // it, and one leaving every group goes back to Main from its group; other outputs are kept.
    juce::Result setTrackOutput (const juce::String& trackId, const TrackOutput&);
    // The audio tracks a track's or group's output can go to, in order.
    juce::StringArray outputDestinations (const juce::String& trackId) const;
    // Audio From another track: its audio becomes this audio track's input, monitored and recorded
    // like any input. Sources are the tracks with audio output that this one does not feed, so
    // routes cannot loop. The tap is taken before the source's effects (after an instrument),
    // after them, or after its mixer.
    juce::StringArray inputSources (const juce::String& trackId) const;
    juce::Result setTrackInputTrack (const juce::String& trackId, const juce::String& sourceTrackId, InputTap = InputTap::postMixer);
    // Automates one native device control (by parameter index) with values in the control's own units,
    // or one of an external plug-in's parameters (by its index in pluginParameters) with values 0-1.
    juce::Result setDeviceAutomation (const juce::String& pluginId, int parameter, const AutomationLane&);
    // An external plug-in's automatable parameters in order, with normalised 0-1 values and the
    // plug-in's own text. Empty for an unknown id, a native device or a missing plug-in.
    struct PluginParameterInfo { juce::String name, text; float value = 0; };
    std::vector<PluginParameterInfo> pluginParameters (const juce::String& pluginId) const;
    // Configure, after the reference: the hosted plug-in parameters its device panel shows as
    // controls, which can be MIDI mapped. They are kept by the plug-in's own parameter IDs, so each
    // follows its parameter if a version of the plug-in reorders them. At most 128; one undo step.
    static constexpr int maximumConfiguredParameters = 128;
    juce::Result setPluginConfiguration (const juce::String& pluginId, const std::vector<int>& parameters);
    // Tracks a hosted plug-in's sidechain may take audio from: the audio and MIDI tracks other than
    // its own and the returns, minus those its track's audio reaches, which would feed back.
    juce::StringArray sidechainSources (const juce::String& pluginId) const;
    // Sets a hosted plug-in's sidechain as one undo step. The plug-in needs an auxiliary input; the
    // source, if any, must be one of sidechainSources. A sidechain that is on without a source hears nothing.
    juce::Result setPluginSidechain (const juce::String& pluginId, const SidechainSettings&);
    // Sets a hosted plug-in parameter (by index in pluginParameters) to a 0-1 value. Like a device
    // control, it is recorded while automation is captured and otherwise overrides its automation.
    juce::Result setPluginParameterValue (const juce::String& pluginId, int parameter, float value);
    // The hosted plug-in's own parameter behind one of pluginParameters, as its editor changes it. A
    // change there inside the editor's gesture is recorded while automation is captured, and
    // otherwise overrides its automation, as setPluginParameterValue's are.
    juce::AudioProcessorParameter* hostedPluginParameter (const juce::String& pluginId, int parameter);
    // Sets a clip's envelope for its target and mode as one undo step; no points removes it.
    juce::Result setClipEnvelope (const juce::String& clipId, const ClipEnvelope&);
    // Automation Arm, after the reference. While it is on and the transport records, moving a track's
    // volume or pan, the master volume or a native device control writes that lane at the playhead:
    // while an edit gesture holds the control (then the earlier automation resumes), or until recording
    // stops. Otherwise, moving a control whose lane plays automation overrides the lane: it stops
    // reading until reEnableAutomation, which re-enables every overridden lane in one undo step.
    juce::Result setAutomationArm (bool);
    juce::Result reEnableAutomation();
    // Punch-In and Punch-Out, after the reference: the loop brace marks the punch range whether or not
    // the loop plays. With Punch-In on, Arrangement recording keeps only what arrives from the loop
    // start; with Punch-Out on, only what arrives before the loop end. Recorded clips are trimmed to
    // the range when recording stops (audio keeps its timing), and captured automation respects it.
    juce::Result setPunchIn (bool);
    juce::Result setPunchOut (bool);
    // MIDI mappings, after the reference's MIDI Map Mode. A controller (a CC on a channel) drives one
    // target and a target has at most one controller: a track's volume or pan ("mixer:<track>:volume"
    // or ":pan"), a send ("send:<track>:<return>"), the master volume ("master") or a native device
    // control ("device:<plugin>:<index>").
    // The controller's 0-127 moves the control's fader or knob position between minimum and maximum,
    // in the control's own units. Every enabled MIDI input acts as a remote control.
    // While a learn target is set, the next controller maps to it (replacing the target's mapping and
    // the controller's previous one) and moves nothing. An empty target ends learning.
    juce::Result setMidiLearnTarget (const juce::String& target);
    juce::Result removeMidiMapping (const juce::String& target);
    juce::Result setMidiMappingRange (const juce::String& target, float minimum, float maximum);
    // A controller change, as it arrives from a MIDI input. Moves from one controller are one undo step
    // until it rests for half a second.
    juce::Result handleMidiControl (int channel, int controller, int value);
    // A note, as it arrives from a MIDI input; velocity 0 is its note-off. While learning, a note-on
    // maps the note as a controller would. As in the reference, a mapped note presses a clip slot
    // with its note-on and releases it with its note-off (following the clip's Launch Mode), presses
    // any other button with its note-on, and moves a control as a key does (see handleMappedKey). Mapped
    // notes and controllers, and every note and controller while one is being learned, stop acting
    // as track input.
    juce::Result handleMidiNote (int channel, int note, int velocity);
    // A target's range in its own units, or nothing for an unknown target or a button.
    std::optional<std::pair<float, float>> midiTargetRange (const juce::String& target) const;
    // Buttons can be mapped too, as in the reference: a track's activator, solo, arm and clip stop
    // ("track:<track>:mute", ":solo", ":arm", ":stop"), a clip slot ("slot:<track>:<scene id>"), a
    // scene ("scene:<scene id>"), a device's on/off ("device:<plugin>:on") and the transport
    // ("transport:play", ":stop", ":record", ":loop", ":metronome", ":stopClips"). As in the
    // reference, a controller sets a switch (an activator, solo, arm, device switch, Record, Loop or
    // Metronome) on at 64 and above and off below, and presses any other button as its value rises
    // above 63; a clip slot is released as it falls to 63 or below. A note or key presses: it
    // toggles a switch, presses a clip slot's launch button (following the clip's Launch Mode) and
    // launches or plays as a click would.
    static bool isButtonTarget (const juce::String& target);
    // Whether a target exists to be mapped: a control with a range, or a button.
    bool isMappableTarget (const juce::String& target) const;
    // Key Map Mode, after the reference: a computer key (a printable character other than a space,
    // without case) drives one target, and a target has one key. A key presses its button. On a
    // control it toggles a switch, steps a choice to the next (wrapping from the mapping's maximum
    // to its minimum), and sends anything else to its minimum from at or above the midpoint of the
    // mapping's range, in fader or knob position, or to its maximum below it. While a key learn target is set, the next
    // key maps to it (one undo step); an empty target ends learning.
    juce::Result setKeyLearnTarget (const juce::String& target);
    juce::Result removeKeyMapping (const juce::String& target);
    // A key press: maps it while learning, or presses its target. used says whether the key was
    // taken (learning, or a mapped key); other keys are left for the computer keyboard and shortcuts.
    juce::Result handleMappedKey (juce::juce_wchar key, bool& used);
    // A mapped key's release: releases a clip slot it pressed, for Gate and Repeat clips.
    juce::Result releaseMappedKey (juce::juce_wchar key);
    // Knob-position mapping of a native control, used by automation editors; integer controls round.
    static float deviceAutomationValueToProportion (EffectKind, int parameter, float value);
    static float deviceAutomationProportionToValue (EffectKind, int parameter, float proportion);
    juce::Result setTrackMute (int trackIndex, bool);
    juce::Result setTrackSolo (int trackIndex, bool);
    juce::StringArray inputNames (bool midi = false) const;
    // Input configuration, after the reference's Input Config: each pair of the audio device's
    // input channels is one stereo input or two mono ones, and each input can be switched off.
    // Inputs are named by their channels; a track keeps an input that still exists. Kept with the
    // device settings.
    struct AudioInputGroup { juce::String name; int firstChannel = 0, channels = 2; bool enabled = true; bool operator== (const AudioInputGroup&) const = default; };
    std::vector<AudioInputGroup> audioInputGroups() const;
    juce::Result setAudioInputPairStereo (int firstChannel, bool stereo);
    juce::Result setAudioInputEnabled (const juce::String& inputName, bool);
    // Output configuration, after the reference's Output Config: each pair of the audio device's
    // output channels is one stereo output or two mono ones, and each can be switched off. Kept with
    // the device settings. Tracks play to an output with Audio To Ext. Out (OutputKind::external).
    struct AudioOutputGroup { juce::String name; int firstChannel = 0, channels = 2; bool enabled = true; bool operator== (const AudioOutputGroup&) const = default; };
    std::vector<AudioOutputGroup> audioOutputGroups() const;
    juce::Result setAudioOutputPairStereo (int firstChannel, bool stereo);
    juce::Result setAudioOutputEnabled (const juce::String& outputName, bool);
    juce::StringArray outputNames() const; // The outputs switched on.
    // Master Out, kept with the project: the output the master, and so every track playing to
    // Master, goes to. Tracks cannot play to it with Ext. Out, since that is the master's.
    juce::String masterOutput() const;
    juce::Result setMasterOutput (const juce::String& outputName);
    // An MPE controller's input, after the reference's per-input MPE switch: its notes record with
    // their own pitch bend, slide and pressure, a lower zone of 15 member channels. Kept with the
    // device settings.
    juce::Result setMidiInputMpe (const juce::String& inputName, bool);
    bool isMidiInputMpe (const juce::String& inputName) const;
    // The computer MIDI keyboard, after the reference: a MIDI input named Computer Keyboard, which
    // tracks choose like any input and which All MIDI Ins includes. While it is on, the UI plays it
    // from the keys (see ComputerKeyboard.h). Turning it off ends its held notes. The input is made
    // when the Session starts with audio devices, and otherwise (in tests) when first turned on.
    static constexpr const char* computerKeyboardName = "Computer Keyboard";
    juce::Result setComputerKeyboardEnabled (bool);
    bool isComputerKeyboardEnabled() const;
    // A note from the computer keyboard on channel 1, velocity 0-1; refused while it is off. A
    // repeated note-on or a note-off for a note not held does nothing.
    juce::Result computerKeyboardNote (int note, float velocity, bool on);
    juce::Result setTrackInput (int trackIndex, const juce::String& inputName);
    // A MIDI track's MIDI From channel, as the reference's: 0 (All Channels) takes every channel from
    // its input, 1 to 16 only that one, for monitoring, playing, recording and Capture MIDI. One undo
    // step, kept with the project; refused while recording.
    juce::Result setTrackInputChannel (int trackIndex, int channel);
    juce::Result setTrackArmed (int trackIndex, bool);
    // Audio inputs need the system's permission: macOS asks once and keeps the answer in System
    // Settings. Without it a hardware input records silence, so arming, monitoring or recording
    // one is refused with what to do, and an undetermined permission is asked for then, without
    // waiting. Sessions without audio devices are always granted it.
    enum class InputPermission { granted, undetermined, denied };
    InputPermission inputPermission() const;
    // Stands in for the system's permission and its prompt (whose answer arrives later), for tests.
    void setInputPermissionSource (std::function<InputPermission()> status, std::function<void (std::function<void (bool)>)> request);
    // Auto-Arm, after the reference: with Exclusive Arm on and no track armed by hand, the selected
    // MIDI track is armed, and given All MIDI Ins with Auto monitoring when it has no input and no
    // other track has All MIDI Ins. Selecting another track (an empty id for none) releases it,
    // taking back an input it gave. Arming, disarming or choosing an input by hand ends it. It is
    // not an undo step, is not saved, and does not change while recording or exporting.
    juce::Result autoArmTrack (const juce::String& trackId);
    juce::String autoArmedTrack() const;
    juce::Result setTrackMonitor (int trackIndex, MonitorMode);
    // Play and Record start from the insert marker when stopped; Stop leaves the play position
    // where it stopped. Continue plays on from there. Return to Start puts the insert marker at the
    // beginning. Locating (seek) moves the play position and the insert marker together; placing the
    // insert marker moves the play position too only while stopped.
    juce::Result play();
    juce::Result continuePlayback();
    void stop();
    // Record from a stop starts playback too, unless Start Playback with Record is off: then it
    // waits until Play or a Session launch starts the transport (Continue records from where it is).
    juce::Result record();
    // The Arrangement Record button, as the reference's: pressed while recording, the recording ends
    // and playback carries on; pressed while waiting, it stops waiting; otherwise it records, Shift
    // reversing Start Playback with Record.
    juce::Result recordButton (bool shift = false);
    // Ends the Arrangement recording, keeping its takes as Stop does, while playback carries on.
    juce::Result stopRecording();
    void seek (double seconds);
    void setInsertMarker (double seconds);
    void returnToStart();
    void setTempo (double bpm); // Constant tempo only; ignored while the tempo is automated.
    // Nudge Down and Up, as the reference's momentary buttons: while one is held alone during playback,
    // playback's tempo ramps from the song tempo by up to 70% of it over six seconds, changing on 16th
    // notes, and the song keeps the beats it gained or lost. The song tempo and the project do not
    // change, and nothing happens while stopped or following external sync. Saving, rendering,
    // undo and tempo edits let the nudge go first.
    void setNudge (bool up, bool held);
    // Replaces the tempo map in one undo step: the first point at beat 0, beats increasing, 20-999 BPM.
    // Clips, automation and the loop keep their beats. One point restores a constant tempo.
    juce::Result setTempoMap (const std::vector<TempoPoint>&);
    // Conversions through the tempo map.
    double beatsAt (double seconds) const;
    double secondsAt (double beats) const;
    // The meter at the playhead, as the reference's Control Bar fields change it: numerator 1-99 over
    // 1, 2, 4, 8 or 16. Tempo stays in quarter notes per minute.
    juce::Result setTimeSignature (int numerator, int denominator);
    // Time signature markers, as the reference's. Inserting one at a beat after the start gives it a
    // meter, or changes the meter of one already there. A marker's meter can change and every marker
    // but the first, which stays at beat 0, can move or go. Each is one undo step. Indices are into
    // Snapshot::timeSignatures.
    static constexpr int maximumTimeSignatures = 4096;
    juce::Result insertTimeSignatureChange (double beat, TimeSignature);
    juce::Result setTimeSignatureChange (int index, TimeSignature);
    juce::Result moveTimeSignatureChange (int index, double beat);
    juce::Result removeTimeSignatureChange (int index);
    // A marker inside a bar leaves that bar short, a fragmentary bar. As the reference's commands:
    // Delete Fragmentary Bar Time deletes its time, and Complete Fragmentary Bar inserts the time it
    // lacks at its start, across the whole Arrangement, so the marker falls on the next bar line.
    // Insert and Delete Time move the markers after them, as everything else.
    juce::Result deleteFragmentaryBarTime (int markerIndex);
    juce::Result completeFragmentaryBar (int markerIndex);
    static constexpr int narrowestGrid = 8; // Pixels between the closest lines an adaptive grid draws.
    // The Arrangement's grid.
    EditGrid editGrid() const;
    juce::Result setEditGrid (const EditGrid&);
    // A clip's own grid, for its editor, as the reference gives every clip one: saved with the clip
    // and carried by its copies, outside the undo history.
    EditGrid clipGrid (const juce::String& clipId) const;
    juce::Result setClipGrid (const juce::String& clipId, const EditGrid&);
    // The ladder, coarsest first, and a value's length in quarter-note beats under a meter.
    static std::vector<GridValue> gridLadder();
    static double gridValueBeats (GridValue, int tuplet, TimeSignature);
    // The grid's value at a zoom, in pixels per quarter note, for a view so many beats wide.
    static GridValue gridValue (const EditGrid&, double pixelsPerBeat, double visibleBeats, TimeSignature);
    static double gridStepBeats (const EditGrid& g, double pixelsPerBeat, double visibleBeats, TimeSignature meter)
    { return gridValueBeats (gridValue (g, pixelsPerBeat, visibleBeats, meter), g.tuplet, meter); }
    // A value's name, as the Arrangement's readout shows it: "1/16", "1/8T", "5 : 1/8" for a 1/16
    // quintuplet, and bars as "1/1", "2/1".
    static juce::String gridValueName (GridValue, int tuplet);
    juce::Result tapTempo (double monotonicSeconds); // Message-thread tap time, not project time.
    juce::Result setMetronome (const MetronomeSettings&);
    juce::Result setRecordQuantisation (RecordQuantisation);
    // The Arrangement loop: its switch and brace. Saved with the project; as in the reference, not an
    // undo step.
    void setLoop (bool enabled, double startSeconds, double endSeconds);
    juce::Result addLocator (double seconds, const juce::String& name, juce::String& createdId);
    juce::Result updateLocator (const juce::String& id, const juce::String& name, double seconds);
    juce::Result removeLocator (const juce::String& id);
    juce::Result seekLocator (const juce::String& id);
    juce::Result navigateLocator (bool forward);
    void beginEditGesture (const juce::String& name);
    void endEditGesture();
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    juce::Result exportAudio (const juce::File&, const AudioExportSettings&);
    juce::Result exportAudio (const juce::File&, ExportFormat, double sampleRate = 48000, int bitDepth = 24, double tailSeconds = 0);
    juce::Result exportWav (const juce::File&, double sampleRate = 48000, int bitDepth = 24, double tailSeconds = 0);
    // Renders every track, group and return on its own (tracks and groups without return effects,
    // returns with their sends) and the Master mix into "<project> <name>" files in a folder.
    // Existing files are only replaced with overwrite; nothing changes in the project or its history.
    juce::Result exportStems (const juce::File& folder, const AudioExportSettings&, juce::StringArray& writtenFiles, bool overwrite = false);
    // Export pumps native events; edits reject while it owns the render graph.
    bool isExporting() const;
    void cancelExport();

    juce::AudioDeviceManager& audioDeviceManager();
    // When the audio device in use disappears (an unplugged interface), playback stops instead of
    // moving to the device the system falls back to, and that device is reopened when it returns.
    // Each such change leaves a notice for the user, returned once.
    juce::String takeDeviceNotice();
    // The file type and bit depth audio takes are written in, for every input. The inputs keep it
    // in the engine's settings, so it lasts between launches.
    enum class RecordingFileType { wav, aiff, flac };
    struct RecordingFormat
    {
        RecordingFileType type = RecordingFileType::wav;
        int bitDepth = 24;
        bool operator== (const RecordingFormat&) const = default;
    };
    RecordingFormat recordingFormat() const;
    juce::Result setRecordingFormat (const RecordingFormat&);
    // Audio Engine Off stops the transport, keeping any takes, and closes the audio device; On
    // reopens the last device. While it is off nothing plays, records or launches.
    juce::Result setAudioEngineEnabled (bool);
    bool isAudioEngineEnabled() const;
    juce::KnownPluginList& knownPlugins();
    juce::AudioPluginFormatManager& pluginFormats();
    juce::File pluginScanDeadMansFile() const;
    // Scans the plug-in sources the preferences enable, in the background and through the separate
    // scanner process where the engine has one, as the reference's Rescan does: new and changed
    // plug-ins are added, and those removed or outside the sources dropped. everything first forgets
    // the list, as the reference's Alt/Option-click on Rescan does. Refused while a scan runs.
    juce::Result rescanPlugins (bool everything = false);
    struct PluginScanStatus { bool scanning = false; float progress = 0; juce::String plugin; };
    PluginScanStatus pluginScanStatus() const;
    void cancelPluginScan();
    juce::Result addNativeEffect (int trackIndex, EffectKind);
    // A Tuner's latest pitch: the note (MIDI numbering, 69 is A4) nearest the detected frequency at the
    // device's reference pitch, and the distance from it in cents. Nothing for silence, aperiodic input,
    // or an id that is not a Tuner that has processed audio.
    struct TunerReading { double frequency = 0; int note = 69; double cents = 0; };
    std::optional<TunerReading> tunerReading (const juce::String& pluginId) const;
    // A Spectrum device's latest block, at its Block size and Channel: dBFS magnitudes of bins 0 to
    // block/2, binHz apart. Nothing for an id that is not a Spectrum that has processed audio.
    struct SpectrumReading { double binHz = 0; std::vector<float> decibels; };
    std::optional<SpectrumReading> spectrum (const juce::String& pluginId) const;
    // MIDI Monitor: up to the last 512 messages the device received, oldest first, timed in seconds
    // on its own clock; how many it has received in all; the notes held now in channel and pitch
    // order; and the chord they spell. Nothing for an unknown id or another kind of device.
    struct MidiMonitorMessage { double seconds = 0; int status = 0, data1 = 0, data2 = 0; };
    struct HeldNote { int channel = 0, pitch = 0, velocity = 0; };
    struct MidiMonitorReading { std::vector<MidiMonitorMessage> messages; uint64_t total = 0; std::vector<HeldNote> held; juce::String chord; };
    std::optional<MidiMonitorReading> midiMonitor (const juce::String& pluginId) const;
    // The notes held now on the MIDI inputs an armed or In-monitored MIDI track takes, in pitch
    // order, as step recording uses them.
    std::vector<HeldNote> heldInputNotes (const juce::String& trackId);
    // The MIDI editor's Preview switch, as the reference's: off at first, for every MIDI track in
    // the project, saved with it without being an undo step or an unsaved change.
    bool notePreview() const;
    void setNotePreview (bool on);
    // Starts or ends a note on a MIDI track's instrument (or MIDI To port) now, whether or not the
    // transport runs, as the editor's Preview plays notes. stopNotePreviews ends every one started.
    juce::Result previewNote (const juce::String& trackId, int pitch, float velocity, bool on);
    void stopNotePreviews();
    // Device presets: a native device's or Utility's settings, or a plug-in's identity and state, in
    // a .halftrackpreset file. Racks and sample choices are not included. Saving refuses to replace
    // a file unless asked to.
    static constexpr const char* presetExtension = ".halftrackpreset";
    struct DevicePreset
    {
        juce::String name;
        bool utility = false, plugin = false;
        EffectSettings effect;     // For native devices.
        UtilitySettings settings;  // For the Utility.
        juce::PluginDescription description; // For plug-ins, with the state they save.
        juce::MemoryBlock state;
    };
    static juce::Result readDevicePreset (const juce::File&, DevicePreset&);
    juce::Result saveDevicePreset (const juce::String& pluginId, const juce::File&, const juce::String& name, bool overwrite = false);
    // Adds the preset's device to a track (an instrument preset replaces the instrument) in one undo step.
    juce::Result loadDevicePreset (int trackIndex, const juce::File&, juce::String& pluginId);
    // Devices inside racks share these commands and setDeviceAutomation with track devices.
    juce::Result setNativeEffectSettings (const juce::String& pluginId, const EffectSettings&);
    // Racks: each command is one undo step and validates everything before changing the project.
    juce::Result addRackChain (const juce::String& rackId, const juce::String& name, juce::String& chainId, int index = -1);
    juce::Result removeRackChain (const juce::String& chainId);
    juce::Result moveRackChain (const juce::String& chainId, int destinationIndex);
    juce::Result duplicateRackChain (const juce::String& chainId, juce::String& duplicateId);
    juce::Result setRackChainSettings (const juce::String& chainId, const RackChainSettings&);
    // Chains keep MIDI effects, then one instrument, then audio effects; an instrument replaces the chain's instrument.
    juce::Result addRackDevice (const juce::String& chainId, EffectKind, juce::String& deviceId, int index = -1);
    // Adds a channel Utility among a chain's audio devices.
    juce::Result addRackUtility (const juce::String& chainId, juce::String& deviceId, int index = -1);
    juce::Result removeRackDevice (const juce::String& deviceId);
    juce::Result moveRackDevice (const juce::String& deviceId, int destinationIndex);
    juce::Result setRackDeviceEnabled (const juce::String& deviceId, bool);
    // Opens a device's own window, for devices on tracks or inside racks.
    juce::Result showDeviceEditor (const juce::String& deviceId);
    // Loads an audio file into a Sampler inside a rack.
    juce::Result setRackDeviceSample (const juce::String& deviceId, const juce::File&);
    // Adds the mapping, replacing any mapping of the same target control. The first mapping of a
    // macro moves the macro to the control's current position.
    juce::Result setRackMacroMapping (const juce::String& rackId, const MacroMapping&);
    juce::Result removeRackMacroMapping (const juce::String& rackId, int macro, const juce::String& targetId, int parameter);
    juce::Result setRackVisibleMacros (const juce::String& rackId, int count);
    // Names a macro (1-64 characters); an empty name returns it to its automatic name.
    juce::Result setRackMacroName (const juce::String& rackId, int macro, const juce::String& name);
    // Drum Rack: a sample on a pad (note 0-127) goes into the pad's Sampler, or a new chain that
    // receives the note and plays it at C3 with a one-shot Sampler; an empty file removes the pad's chains.
    juce::Result setDrumRackPadSample (const juce::String& rackId, int note, const juce::File&, juce::String& chainId);
    // Swaps two pads' chains by exchanging their receive notes.
    juce::Result swapDrumRackPads (const juce::String& rackId, int note, int otherNote);
    juce::Result addRackReturn (const juce::String& rackId, const juce::String& name, juce::String& returnId);
    juce::Result removeRackReturn (const juce::String& returnId);
    // Group: wraps built-in devices of one track (by ID, all MIDI effects or all audio devices) in
    // track order into a new rack with one chain, at the first device's position. groupInstrument
    // wraps the instrument and the built-in audio devices after it into an Instrument Rack.
    juce::Result groupDevices (int trackIndex, const juce::StringArray& pluginIds, juce::String& rackId);
    juce::Result groupInstrument (const juce::String& trackId, juce::String& rackId);
    // Ungroup: puts a one-chain rack's devices back on its track in the rack's place.
    juce::Result ungroupRack (const juce::String& rackId);
    juce::Result addUtility (int trackIndex);
    juce::Result setUtilitySettings (const juce::String& pluginId, const UtilitySettings&);
    juce::Result movePlugin (int trackIndex, int pluginIndex, int destinationIndex);
    // A hosted plug-in's role, from its description rather than its file: an instrument, a MIDI
    // effect (one whose category says MIDI, as Audio Units' MIDI processors do), or an audio effect.
    enum class PluginRole { instrument, audioEffect, midiEffect };
    static PluginRole pluginRole (const juce::PluginDescription&);
    // "Instrument", "Audio Effect" or "MIDI Effect", its format, and its main channel counts when known.
    static juce::String pluginSummary (const juce::PluginDescription&);
    // Inserts a hosted audio effect after the track's instrument. An instrument plug-in instead
    // becomes a MIDI track's instrument, replacing the one it had (one undo step); a MIDI effect
    // goes on a MIDI track ahead of its instrument. Both are refused on audio tracks.
    juce::Result addPlugin (int trackIndex, const juce::PluginDescription&);
    juce::Result setPluginEnabled (int trackIndex, int pluginIndex, bool);
    juce::Result removePlugin (int trackIndex, int pluginIndex);
    // Device chains, as the reference's tracks each have one: a track's (by its id), a group's (by
    // its id) or the Main's (mainChain). A group's or the Main's takes audio effects only, before its
    // mixer: native audio effects, the Utility and effect plug-ins. These take a chain's id; on a
    // track's chain they act as the track-index commands do.
    static constexpr const char* mainChain = "main";
    juce::Result addNativeEffect (const juce::String& chainId, EffectKind);
    juce::Result addUtility (const juce::String& chainId);
    juce::Result addPlugin (const juce::String& chainId, const juce::PluginDescription&);
    juce::Result movePlugin (const juce::String& chainId, int pluginIndex, int destinationIndex);
    juce::Result setPluginEnabled (const juce::String& chainId, int pluginIndex, bool);
    juce::Result removePlugin (const juce::String& chainId, int pluginIndex);
    juce::Result showPluginEditor (const juce::String& chainId, int pluginIndex);
    juce::Result setPluginParameter (int trackIndex, int pluginIndex, int parameterIndex, float normalisedValue);
    juce::Result showPluginEditor (int trackIndex, int pluginIndex);
    void closePluginEditors();

private:
    juce::Result startProject (bool builtInDefaultSet);
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::Result beginRecording (bool waitForPlay);
    void finishRecording (bool stopTransport);
    void recordIfWaiting(); // A launch or Continue started the transport: a waiting Record starts.
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

int runSessionTests (const juce::File& artifactDirectory);
int runMidiTests (const juce::File& artifactDirectory);
}
