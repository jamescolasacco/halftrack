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
    };
    enum class FollowAction { off, stop, replay, previous, next, first, last, cycle };
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
        polySynth, fmSynth, drumKit, wavetableSynth, sampler, drumSampler };
    static constexpr int effectKindCount = 30;
    static constexpr int drumPadCount = 16; // Drum Sampler pads play notes 36 (C1) to 51.
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
    // Coordinate mapping matches native interpolation, including the volume fader taper.
    static float automationValueToProportion (AutomationTarget, float value);
    static float automationProportionToValue (AutomationTarget, float proportion);
    enum class NoteTransform { quantizeStarts, quantizeEnds, reverse, invertPitch, legato, halfTime, doubleTime, fixedLength };
    struct MidiNoteInfo
    {
        juce::String id;
        int pitch = 60;
        double startBeat = 0, lengthBeats = 1;
        float velocity = 0.8f;
        bool active = true;
    };
    enum class FadeCurve { linear = 1, convex, concave, sCurve };
    // Warped audio follows the project tempo; its length is fixed in beats.
    enum class WarpMode { off, stretch, efficient };
    struct WarpSettings
    {
        WarpMode mode = WarpMode::off;
        double bpm = 120; // The source's own tempo.
        bool operator== (const WarpSettings&) const = default;
    };
    // Tempo guess for a source: whole 4/4 bars, doubled or halved into 80-160 BPM.
    static double estimateClipTempo (double sourceSeconds);
    struct AudioClipSettings
    {
        float gainDb = 0;
        double fadeInSeconds = 0, fadeOutSeconds = 0;
        FadeCurve fadeInCurve = FadeCurve::linear, fadeOutCurve = FadeCurve::linear;
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
    };
    struct LocatorInfo { juce::String id, name; double position = 0; };
    struct SceneInfo { juce::String id, name; };
    struct SlotInfo
    {
        juce::String id;
        bool hasClip = false, playing = false, queued = false, stopQueued = false;
        bool recording = false; // A Session recording is capturing into this empty slot.
        ClipInfo clip;
        double playPositionBeats = -1;
    };
    struct PluginInfo
    {
        juce::String id, name;
        bool enabled = true, isUtility = false;
        UtilitySettings utility;
        bool isNativeEffect = false;
        EffectSettings effect;
        juce::File sample; // A Sampler instrument's loaded sample, if any.
        std::vector<juce::File> padSamples; // A Drum Sampler's sample per pad (empty files for empty pads).
        // Arrangement automation of native device controls, in the controls' own units.
        struct ControlAutomation { int parameter = 0; AutomationLane lane; bool operator== (const ControlAutomation&) const = default; };
        std::vector<ControlAutomation> automation;
    };
    struct TrackInfo
    {
        juce::String id, name;
        juce::Colour colour;
        float gainDb = 0, pan = 0, peakL = 0, peakR = 0;
        bool mute = false, solo = false, armed = false, isMidi = false, isReturn = false;
        bool frozen = false; // Plays a render of its clips and devices; devices and clips are locked.
        SendMode sendMode = SendMode::post;
        std::vector<SendInfo> sends;
        SynthSettings synth;
        AutomationLane volumeAutomation, panAutomation;
        MonitorMode monitor = MonitorMode::off;
        juce::String inputName;
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
    struct MetronomeSettings
    {
        bool enabled = false, accent = true, recordingOnly = false;
        float gain = .6f;
        int countInBars = 0; // 0, 1 or 2; current project meter is 4/4.
        bool operator== (const MetronomeSettings&) const = default;
    };
    struct Snapshot
    {
        juce::String name;
        juce::File projectFile;
        uint64_t projectGeneration = 0; // Transient identity; changes only on successful New/Load.
        bool dirty = false, playing = false, recording = false, looping = false;
        double position = 0, duration = 0, tempo = 120, loopStart = 0, loopEnd = 8, cpu = 0;
        float masterPeakL = 0, masterPeakR = 0, masterGainDb = 0;
        AutomationLane masterVolumeAutomation;
        std::vector<TrackInfo> tracks;
        std::vector<GroupInfo> groups;
        std::vector<SceneInfo> scenes;
        std::vector<LocatorInfo> locators;
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
    juce::Result saveProject (const juce::File&); // Copies media into <name> Assets beside the .halftrack file.
    juce::Result addAudioTrack (juce::String name = {});
    juce::Result addMidiTrack (juce::String name = {});
    juce::Result addReturnTrack (juce::String name = {});
    juce::Result setTrackSendGain (int sourceTrackIndex, const juce::String& returnTrackId, float decibels);
    juce::Result setReturnSendMode (const juce::String& returnTrackId, SendMode);
    juce::Result importMidiFile (const juce::File&, double startBeat, std::vector<juce::String>& clipIds);
    juce::Result exportMidiClip (const juce::String& clipId, const juce::File&);
    juce::Result addMidiClip (const juce::String& trackId, double startBeat, double lengthBeats);
    juce::Result addScene (juce::String name = {});
    juce::Result removeScene (int sceneIndex);
    juce::Result duplicateScene (const juce::String& sceneId, juce::String& duplicatedSceneId);
    juce::Result moveScene (const juce::String& sceneId, int destinationIndex);
    juce::Result renameScene (int sceneIndex, const juce::String& name);
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
    juce::Result splitMidiNotes (const juce::String& clipId, const juce::StringArray& noteIds, double splitBeat, juce::StringArray& insertedIds);
    juce::Result transformMidiNotes (const juce::String& clipId, const juce::StringArray& noteIds, NoteTransform, double gridBeats);
    juce::Result removeMidiNote (const juce::String& clipId, const juce::String& noteId);
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
    juce::Result setAudioClipRange (const juce::String& clipId, double startSeconds, double lengthSeconds, double sourceOffsetSeconds);
    juce::Result splitAudioClip (const juce::String& clipId, double atSeconds, juce::String& rightClipId);
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
    juce::Result setMasterVolumeAutomation (const AutomationLane&);
    juce::Result setTrackGain (int trackIndex, float decibels);
    juce::Result setTrackPan (int trackIndex, float pan);
    // Atomic, strictly ordered beat points. Empty points restore manual control.
    juce::Result setTrackAutomation (const juce::String& trackId, AutomationTarget, const AutomationLane&);
    // Automates one native device control (by parameter index) with values in the control's own units.
    juce::Result setDeviceAutomation (const juce::String& pluginId, int parameter, const AutomationLane&);
    // Knob-position mapping of a native control, used by automation editors; integer controls round.
    static float deviceAutomationValueToProportion (EffectKind, int parameter, float value);
    static float deviceAutomationProportionToValue (EffectKind, int parameter, float proportion);
    juce::Result setTrackMute (int trackIndex, bool);
    juce::Result setTrackSolo (int trackIndex, bool);
    juce::StringArray inputNames (bool midi = false) const;
    juce::Result setTrackInput (int trackIndex, const juce::String& inputName);
    juce::Result setTrackArmed (int trackIndex, bool);
    juce::Result setTrackMonitor (int trackIndex, MonitorMode);
    juce::Result play();
    void stop();
    juce::Result record();
    void seek (double seconds);
    void setTempo (double bpm);
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
    // Export pumps native events; edits reject while it owns the render graph.
    bool isExporting() const;
    void cancelExport();

    juce::AudioDeviceManager& audioDeviceManager();
    juce::KnownPluginList& knownPlugins();
    juce::AudioPluginFormatManager& pluginFormats();
    juce::File pluginScanDeadMansFile() const;
    juce::Result addNativeEffect (int trackIndex, EffectKind);
    juce::Result setNativeEffectSettings (const juce::String& pluginId, const EffectSettings&);
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
