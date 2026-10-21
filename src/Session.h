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
    enum class LaunchQuantisation { none, beat, bar };
    enum class RecordQuantisation { none, quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtysecond };
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
    };
    enum class FollowAction { off, stop, replay, previous, next, first, last, cycle, any, other };
    // A track's side of the crossfader; unassigned tracks ignore it.
    enum class CrossfadeAssign { none, a, b };
    // Clip launch behaviour on press/release of its launch button.
    enum class LaunchMode { trigger, gate, toggle, repeat };
    // Per-clip launch grid; global follows the project setting.
    enum class ClipQuantisation { global, none, eightBars, fourBars, twoBars, bar, half, quarter, eighth, sixteenth, thirtysecond };
    struct LaunchSettings
    {
        LaunchMode mode = LaunchMode::trigger;
        ClipQuantisation quantisation = ClipQuantisation::global;
        bool legato = false; // Start in phase with the clip already playing on the track.
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
    struct SendInfo { juce::String returnTrackId; float gainDb = -100.0f; };
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
    enum class AutomationTarget { volume, pan };
    struct AutomationPoint
    {
        double beat = 0;
        float value = 0; // Decibels for volume, -1..1 for pan.
        bool operator== (const AutomationPoint&) const = default;
    };
    struct AutomationLane
    {
        bool enabled = true;
        std::vector<AutomationPoint> points;
        bool operator== (const AutomationLane&) const = default;
    };
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
    // Coordinate mapping matches native interpolation, including the volume fader taper.
    static float automationValueToProportion (AutomationTarget, float value);
    static float automationProportionToValue (AutomationTarget, float proportion);
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
    struct MidiNoteInfo
    {
        juce::String id;
        int pitch = 60;
        double startBeat = 0, lengthBeats = 1;
        float velocity = 0.8f;
        bool active = true;
        float chance = 1;      // The probability the note plays on each pass of its clip, 0 to 1.
        int velocityRange = 0; // Each pass adds a random velocity between 0 and this, -127 to 127.
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
    enum class FadeCurve { linear = 1, convex, concave, sCurve };
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
    // Tempo guess for a source: whole 4/4 bars, doubled or halved into 80-160 BPM.
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
        bool operator== (const AudioClipSettings&) const = default;
    };
    struct ClipInfo
    {
        juce::String id, name;
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
        std::vector<WarpMarker> warpMarkers; // A warped audio clip's markers, in order.
        std::vector<ClipEnvelope> envelopes; // Envelopes with points, in the order they were added.
        bool mediaMissing = false; // An audio clip whose file is not on disk plays silence.
    };
    struct LocatorInfo { juce::String id, name; double position = 0; };
    // The project meter. Beats are quarter notes at every meter, so a bar is numerator x 4 / denominator beats.
    struct TimeSignature
    {
        int numerator = 4, denominator = 4;
        double beatsPerBar() const noexcept { return numerator * 4.0 / denominator; }
        bool operator== (const TimeSignature&) const = default;
    };
    // A scene's tempo and meter, when set, become the project's as the scene launches.
    struct SceneInfo
    {
        juce::String id, name;
        std::optional<double> tempo;
        std::optional<TimeSignature> timeSignature;
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
        EffectSettings effect;
        juce::File sample; // A Sampler instrument's loaded sample, if any.
        std::vector<juce::File> padSamples; // A Drum Sampler's sample per pad (empty files for empty pads).
        // Arrangement automation of native device controls, in the controls' own units.
        struct ControlAutomation { int parameter = 0; AutomationLane lane; bool operator== (const ControlAutomation&) const = default; };
        std::vector<ControlAutomation> automation;
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
    // A track's output ("Audio To"): Master, Sends Only, or another audio track, whose devices and
    // mixer then play it as if it were that track's input.
    enum class OutputKind { master, sendsOnly, track };
    enum class InputTap { preFX, postFX, postMixer };
    struct TrackOutput { OutputKind kind = OutputKind::master; juce::String trackId; bool operator== (const TrackOutput&) const = default; };
    struct TrackInfo
    {
        juce::String id, name;
        juce::Colour colour;
        float gainDb = 0, pan = 0, peakL = 0, peakR = 0;
        bool mute = false, solo = false, armed = false, isMidi = false, isReturn = false;
        bool frozen = false; // Plays a render of its clips and devices; devices and clips are locked.
        CrossfadeAssign crossfade = CrossfadeAssign::none;
        double delayMs = 0; // Track Delay: positive plays later, negative earlier.
        SendMode sendMode = SendMode::post;
        std::vector<SendInfo> sends;
        SynthSettings synth;
        AutomationLane volumeAutomation, panAutomation;
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
        std::vector<SlotInfo> slots;
        bool launcherActive = false;
        juce::String groupId; // Empty for ungrouped tracks.
    };
    // A group sums its member tracks through its own mixer, like a bus.
    struct GroupInfo
    {
        juce::String id, name;
        juce::Colour colour;
        float gainDb = 0, pan = 0, peakL = 0, peakR = 0;
        bool mute = false, solo = false, folded = false;
        juce::StringArray memberIds; // Track order.
    };
    // A point on the tempo map: the tempo ramps linearly from each point to the next and holds after the last.
    struct TempoPoint
    {
        double beat = 0, bpm = 120;
        bool operator== (const TempoPoint&) const = default;
    };
    struct MetronomeSettings
    {
        bool enabled = false, accent = true, recordingOnly = false;
        float gain = .6f;
        int countInBars = 0; // 0, 1 or 2 bars of the project meter.
        bool operator== (const MetronomeSettings&) const = default;
    };
    // A MIDI mapping (see setMidiLearnTarget): a controller on a channel and the target it drives
    // between minimum and maximum, in the target's own units.
    struct MidiMapping
    {
        juce::String target;
        int channel = 1, controller = 0;
        float minimum = 0, maximum = 1;
        bool operator== (const MidiMapping&) const = default;
    };
    struct Snapshot
    {
        juce::String name;
        juce::File projectFile;
        uint64_t projectGeneration = 0; // Transient identity; changes only on successful New/Load.
        bool dirty = false, playing = false, recording = false, looping = false;
        bool sessionRecording = false; // Session takes are recording or a clip is overdubbing.
        double position = 0, duration = 0, tempo = 120, loopStart = 0, loopEnd = 8, cpu = 0;
        double insertMarker = 0; // Seconds: where Play starts.
        TimeSignature timeSignature;
        // The tempo map; one point at beat 0 for a constant tempo. `tempo` is the tempo at the playhead.
        std::vector<TempoPoint> tempoMap { TempoPoint {} };
        float masterPeakL = 0, masterPeakR = 0, masterGainDb = 0;
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
        float previewVolumeDb = 0;
        bool automationOverridden = false; // Some lane with points is switched off.
        float grooveAmount = 100; // Global Amount, 0-130%: scales every groove's Timing, Random and Velocity.
        LaunchQuantisation launchQuantisation = LaunchQuantisation::bar;
        MetronomeSettings metronome;
        RecordQuantisation recordQuantisation = RecordQuantisation::none;
        double countInBeatsRemaining = 0;
    };

    explicit Session (bool enableAudioDevices = true, std::unique_ptr<juce::AudioIODeviceType> customDeviceType = {});
    ~Session() override;
    Snapshot snapshot() const;
    juce::Result newProject();
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
    juce::Result addAudioTrack (juce::String name = {});
    juce::Result addMidiTrack (juce::String name = {});
    juce::Result addReturnTrack (juce::String name = {});
    juce::Result setTrackSendGain (int sourceTrackIndex, const juce::String& returnTrackId, float decibels);
    juce::Result setReturnSendMode (const juce::String& returnTrackId, SendMode);
    juce::Result importMidiFile (const juce::File&, double startBeat, std::vector<juce::String>& clipIds);
    juce::Result exportMidiClip (const juce::String& clipId, const juce::File&);
    // Browser preview: a separate preview Edit plays an audio file on the audio device, beside the
    // project and outside it, at the preview volume. Another file replaces it; it stops at its end.
    juce::Result previewFile (const juce::File&);
    void stopPreview();
    juce::Result setPreviewVolume (float decibels); // -70 to +6 dB.
    juce::Result addMidiClip (const juce::String& trackId, double startBeat, double lengthBeats);
    // Take lanes. Arrangement recording puts each pass on an armed track in a take lane (reusing a lane
    // with nothing at or after the pass's start) as well as the main lane. Comping copies a lane's
    // material in a time range into the main lane, replacing what was there, as one undo step;
    // cycling replaces it with the next or previous lane holding material in the range.
    juce::Result insertTakeLane (const juce::String& trackId, juce::String& laneId);
    juce::Result removeTakeLane (const juce::String& laneId);
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
    juce::Result moveScene (const juce::String& sceneId, int destinationIndex);
    juce::Result renameScene (int sceneIndex, const juce::String& name);
    // Each is one undo step; an empty value clears it. Tempos run 20 to 999 BPM, meters 1 to 99 over 1, 2, 4, 8 or 16.
    juce::Result setSceneTempo (const juce::String& sceneId, std::optional<double> bpm);
    juce::Result setSceneTimeSignature (const juce::String& sceneId, std::optional<TimeSignature>);
    juce::Result importAudioToSlot (const juce::File&, int trackIndex, int sceneIndex);
    juce::Result addMidiClipToSlot (const juce::String& trackId, int sceneIndex, double lengthBeats = 4);
    juce::Result clearSlot (int trackIndex, int sceneIndex);
    juce::Result launchSlot (int trackIndex, int sceneIndex);
    // A launch button press/release applying the slot clip's launch mode.
    juce::Result pressSlot (int trackIndex, int sceneIndex);
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
    juce::Result setTrackInstrument (const juce::String& trackId, std::optional<EffectKind> instrument);
    juce::Result showInstrumentEditor (int trackIndex);
    // Loads an audio file into the track's Sampler, switching the instrument to a Sampler first if needed (one undo step).
    juce::Result setInstrumentSample (const juce::String& trackId, const juce::File& sample);
    // Loads (or with an empty file clears) a Drum Sampler pad, switching the instrument to a Drum Sampler if needed.
    juce::Result setDrumPadSample (const juce::String& trackId, int pad, const juce::File& sample);
    juce::Result duplicateTrack (const juce::String& trackId, juce::String& duplicateId);
    // Freeze renders a track's Arrangement clips through its devices and plays the render; one undo step each.
    juce::Result setTrackFrozen (const juce::String& trackId, bool frozen);
    // Flatten replaces a frozen track's clips and devices with one audio clip of its render.
    juce::Result flattenTrack (const juce::String& trackId);
    // Groups top-level audio/MIDI tracks (not returns) into a new group placed at the first one.
    juce::Result groupTracks (const juce::StringArray& trackIds, juce::String& groupId);
    juce::Result ungroupTracks (const juce::String& groupId);
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
    juce::Result importAudio (const juce::File&, int trackIndex = -1, double startSeconds = 0);
    juce::Result setAudioClipSettings (const juce::String& clipId, const AudioClipSettings&);
    juce::Result setAudioClipReversed (const juce::String& clipId, bool reversed);
    // Arrangement clips may turn warping off; Session slots always follow the tempo.
    juce::Result setAudioClipWarp (const juce::String& clipId, const WarpSettings&);
    // Replaces a warped audio clip's warp markers, which must increase in both source time and beat
    // and lie within the source; an empty list removes them. One undo step. Turning warp off keeps
    // them for when it is turned back on.
    juce::Result setAudioClipWarpMarkers (const juce::String& clipId, const std::vector<WarpMarker>&);
    juce::Result setAudioClipRange (const juce::String& clipId, double startSeconds, double lengthSeconds, double sourceOffsetSeconds);
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
    // Consolidate replaces each track's material in the selection with one clip spanning it: MIDI
    // clips join into one MIDI clip, and audio clips render (without devices) into a new file in
    // the project's Assets. Tracks without clips in the selection are left alone.
    juce::Result consolidateRange (const juce::StringArray& trackIds, double startSeconds, double endSeconds, juce::StringArray& newClipIds);
    // Insert Silence: everything at or after start moves later by length across all tracks, locators
    // and automation; clips crossing start split (audio) or open a gap between their notes (MIDI).
    juce::Result insertTime (double startSeconds, double lengthSeconds);
    // Delete Time: removes [start, start + length) everywhere and moves later material earlier.
    juce::Result deleteTime (double startSeconds, double lengthSeconds);
    juce::Result duplicateAudioClip (const juce::String& clipId, juce::String& duplicateId);
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
    juce::Result removeClip (int trackIndex, int clipIndex);
    juce::Result setClipFollowAction (const juce::String& clipId, const FollowSettings&);
    juce::Result setClipActive (const juce::String& clipId, bool active);
    juce::Result renameClip (const juce::String& clipId, const juce::String& name);
    juce::Result renameTrack (int trackIndex, const juce::String&);
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
    // Atomic, strictly ordered beat points. Empty points restore manual control.
    juce::Result setTrackAutomation (const juce::String& trackId, AutomationTarget, const AutomationLane&);
    // Routes a track's output. Return tracks and tracks in a group play into Master or their group;
    // a route that would lead back into the track is refused. One undo step.
    juce::Result setTrackOutput (const juce::String& trackId, const TrackOutput&);
    // The audio tracks a track's output can go to, in order.
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
    // or ":pan"), the master volume ("master") or a native device control ("device:<plugin>:<index>").
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
    // A target's range in its own units, or nothing for an unknown target.
    std::optional<std::pair<float, float>> midiTargetRange (const juce::String& target) const;
    // Knob-position mapping of a native control, used by automation editors; integer controls round.
    static float deviceAutomationValueToProportion (EffectKind, int parameter, float value);
    static float deviceAutomationProportionToValue (EffectKind, int parameter, float proportion);
    juce::Result setTrackMute (int trackIndex, bool);
    juce::Result setTrackSolo (int trackIndex, bool);
    juce::StringArray inputNames (bool midi = false) const;
    juce::Result setTrackInput (int trackIndex, const juce::String& inputName);
    juce::Result setTrackArmed (int trackIndex, bool);
    juce::Result setTrackMonitor (int trackIndex, MonitorMode);
    // Play and Record start from the insert marker when stopped; Stop leaves the play position
    // where it stopped. Continue plays on from there. Return to Start puts the insert marker at the
    // beginning. Locating (seek) moves the play position and the insert marker together; placing the
    // insert marker moves the play position too only while stopped.
    juce::Result play();
    juce::Result continuePlayback();
    void stop();
    juce::Result record();
    void seek (double seconds);
    void setInsertMarker (double seconds);
    void returnToStart();
    void setTempo (double bpm); // Constant tempo only; ignored while the tempo is automated.
    // Replaces the tempo map in one undo step: the first point at beat 0, beats increasing, 20-999 BPM.
    // Clips, automation and the loop keep their beats. One point restores a constant tempo.
    juce::Result setTempoMap (const std::vector<TempoPoint>&);
    // Conversions through the tempo map.
    double beatsAt (double seconds) const;
    double secondsAt (double beats) const;
    // One project meter: numerator 1-99 over 1, 2, 4, 8 or 16. Tempo stays in quarter notes per minute.
    juce::Result setTimeSignature (int numerator, int denominator);
    juce::Result tapTempo (double monotonicSeconds); // Message-thread tap time, not project time.
    juce::Result setMetronome (const MetronomeSettings&);
    juce::Result setRecordQuantisation (RecordQuantisation);
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
    juce::Result addPlugin (int trackIndex, const juce::PluginDescription&);
    juce::Result setPluginEnabled (int trackIndex, int pluginIndex, bool);
    juce::Result removePlugin (int trackIndex, int pluginIndex);
    juce::Result setPluginParameter (int trackIndex, int pluginIndex, int parameterIndex, float normalisedValue);
    juce::Result showPluginEditor (int trackIndex, int pluginIndex);
    void closePluginEditors();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

int runSessionTests (const juce::File& artifactDirectory);
int runMidiTests (const juce::File& artifactDirectory);
}
