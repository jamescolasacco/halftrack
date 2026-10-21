#pragma once
#include "Session.h"
#include "EffectProcessors.h"
#include "MidiEffects.h"
#include "Instruments.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <cmath>
#include <atomic>
#include <span>
#include <vector>

namespace halftrack
{
namespace racks { class RackEngine; }
struct EffectParameter
{
    const char* key;
    const char* label;
    float minimum, maximum, initial, midpoint;
    const char* unit;
    const char* choices = nullptr;
    bool integer = false;
};
const char* effectName (Session::EffectKind) noexcept;
std::span<const EffectParameter> effectParameters (Session::EffectKind) noexcept;
Session::EffectSettings defaultEffectSettings (Session::EffectKind) noexcept;
bool validEffectSettings (const Session::EffectSettings&) noexcept;
// Browser grouping shown in the device menu.
const char* effectCategory (Session::EffectKind) noexcept;
bool effectUsesTempo (Session::EffectKind) noexcept;
bool effectIsMidi (Session::EffectKind) noexcept;
bool effectIsInstrument (Session::EffectKind) noexcept;
bool effectIsRack (Session::EffectKind) noexcept;
// Analysis devices pass audio unchanged and let the message thread read their recent input.
bool effectIsAnalysis (Session::EffectKind) noexcept;
double effectTailSeconds (Session::EffectKind, double sampleRate) noexcept;
double effectLatencySeconds (Session::EffectKind, double sampleRate) noexcept;
// Index of a named control in the kind's parameter table, or -1.
int effectParameterIndex (Session::EffectKind, const char* key) noexcept;
// A Sampler's sample path: absolute in a live edit, project-relative in a saved file.
inline constexpr const char* samplerSampleProperty = "halftrackSample";
// A Drum Sampler pad's sample path, pads numbered from zero.
inline juce::Identifier drumPadSampleProperty (int pad) { return juce::Identifier ("halftrackPad" + juce::String (pad + 1)); }
// Sample slots an instrument kind holds: one for the Sampler, one per pad for the Drum Sampler.
int sampleSlotCount (Session::EffectKind) noexcept;
juce::Identifier sampleSlotProperty (Session::EffectKind, int slot);
inline constexpr double maximumSampleSeconds = 600;
// Knob position (0-1) of a control value with the editor's skew, and back; integer controls round.
float effectValueToProportion (Session::EffectKind, int parameter, float value) noexcept;
float effectProportionToValue (Session::EffectKind, int parameter, float proportion) noexcept;
// Why a file cannot be a Sampler sample, or empty when it can.
juce::String samplerSampleProblem (const juce::File&);

class NativeEffectDSP final
{
public:
    NativeEffectDSP();
    void prepare (double sampleRate, Session::EffectKind); // Allocates only during setup.
    void configure (const Session::EffectSettings&) noexcept;
    void reset() noexcept;
    void processSamples (float* left, float* right, int samples) noexcept;
    // Called at a block start while the transport plays or renders.
    void setTimelinePosition (double seconds) noexcept;
    static std::array<double, 5> eqCoefficients (int type, double frequency, double gain, double q, double sampleRate) noexcept;
private:
    struct Filter
    {
        std::array<double, 5> c { 1, 0, 0, 0, 0 };
        double z1 = 0, z2 = 0;
        double tick (double) noexcept;
    };
    double delayRead (int channel, double frames) const noexcept;
    Session::EffectSettings settings;
    double rate = 48000, envelope = 0, attack = 0, release = 0, damping = 0;
    double outputGain = 1, wet = 1, wetTarget = 1, gainTarget = 1;
    double delayLeft = 1, delayRight = 1, delayTargetLeft = 1, delayTargetRight = 1;
    std::array<std::array<Filter, 8>, 2> eq;
    std::array<std::vector<double>, 2> delays;
    std::array<double, 2> delayTone {};
    size_t cursor = 0;
    std::array<std::vector<double>, 8> room;
    std::array<int, 8> roomLength {}, roomCursor {};
    std::array<double, 8> roomTone {}, roomFeedback {};
    double crusherLevels = 128, crusherDrive = 1, crusherCountdown = 0;
    std::array<double, 2> crusherHeld {};
    double gateThreshold = 0, gateCloseThreshold = 0, gateFloor = 0, gateEnvelope = 0;
    double gateAttackStep = 1, gateReleaseStep = 1;
    int gateHoldSamples = 0, gateHoldRemaining = 0;
    bool gateOpen = false;
    std::vector<double> limiterPeaks;
    std::vector<uint64_t> limiterPeakFrames;
    uint64_t limiterFrame = 0;
    size_t limiterHead = 0, limiterSize = 0;
    double limiterGain = 1, limiterCeiling = 1, limiterDrive = 1, limiterRelease = 0;
    effects::Saturator saturator;
    effects::Overdrive overdrive;
    effects::AutoFilter autoFilter;
    effects::AutoPan autoPan;
    effects::ModulatedDelay modulatedDelay;
    effects::Phaser phaser;
    effects::ThreeBandEq eqThree;
    effects::ChannelEq channelEq;
    effects::Pedal pedal;
    effects::BeatRepeat beatRepeat;
    effects::FilterDelay filterDelay;
    effects::Erosion erosion;
    effects::Resonators resonators;
    effects::VinylDistortion vinyl;
    effects::GlueCompressor glue;
    effects::FrequencyShifter shifter;
    int ramp = 0;
    bool processed = false;
};
class EffectParameterQueue final
{
public:
    bool push (const Session::EffectSettings&) noexcept;
    bool pop (Session::EffectSettings&) noexcept;
    void resetWhileStopped() noexcept;
private:
    static constexpr uint32_t capacity = 64;
    std::array<Session::EffectSettings, capacity> packets {};
    std::atomic<uint32_t> readIndex { 0 }, writeIndex { 0 };
    static_assert (std::atomic<uint32_t>::is_always_lock_free);
};
class NativeEffectPlugin final : public tracktion::Plugin, private juce::Timer
{
public:
    explicit NativeEffectPlugin (tracktion::PluginCreationInfo);
    ~NativeEffectPlugin() override;
    static inline const char* xmlTypeName = "halftrackNativeEffect";
    static const char* getPluginName() { return "Native effect"; }
    juce::String getName() const override { return effectName (kind); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return getName(); }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    bool noTail() override { return kind == Session::EffectKind::compressor || kind == Session::EffectKind::gate || kind == Session::EffectKind::autoPan || kind == Session::EffectKind::glueCompressor; }
    double getLatencySeconds() override;
    double getTailLength() const override;
    bool takesMidiInput() override { return effectIsMidi (kind) || effectIsInstrument (kind); }
    bool takesAudioInput() override { return ! effectIsMidi (kind) && ! effectIsInstrument (kind); }
    bool isSynth() override { return effectIsInstrument (kind); }
    // Instruments sound from MIDI alone; renders such as freezes check this before rendering a MIDI track.
    bool producesAudioWhenNoAudioInput() override { return effectIsInstrument (kind) || isAutomationNeeded(); }
    Session::EffectKind getKind() const noexcept { return kind; }
    int getNumOutputChannelsGivenInputs (int) override { return effectIsMidi (kind) ? 0 : 2; }
    BusLayout getBusses() const override { return effectIsMidi (kind) ? BusLayout {} : BusLayout::singleStereoInOut(); }
    Session::EffectSettings readSettings() const;
    void writeSettings (const Session::EffectSettings&);
    static juce::Result validateState (const juce::ValueTree&);
    // Resolves saved Sampler paths against the project directory, rejecting missing or unreadable samples.
    static juce::Result resolveSamples (juce::ValueTree&, const juce::File& projectDirectory);
    void prepareForOfflineRender();
    juce::File sampleFile (int slot = 0) const;
    // The automatable control for a parameter index, holding knob positions. It is not attached
    // to the stored setting, so automation never overwrites the manual value.
    tracktion::AutomatableParameter* control (int parameter) const noexcept
    { return parameter >= 0 && parameter < (int) controls.size() ? controls[(size_t) parameter] : nullptr; }
    int numControls() const noexcept { return (int) controls.size(); }
    // Message thread, after curve edits, undo or load: which controls read their curves. A curve
    // with one point holds its value; with more it streams; an empty or switched-off lane plays the manual value.
    void refreshAutomation();
    // After Save collects a sample, point at the copy without decoding it again or adding history.
    void relocateSample (int slot, const juce::File&);
    // A rack kind's chains, or nullptr for a plain device.
    racks::RackEngine* rackEngine() const noexcept { return rack.get(); }
    // Audio thread, from an enclosing rack right before it processes this device: control values
    // its macros drive (NaN where none does), valid until the rack clears them after the block.
    void setRackControls (const float* values) noexcept { rackControls = values; }
    // Audio thread, from an enclosing rack: an instrument fades its voices out from this sample
    // of the next block, as a Drum Rack choke group does.
    void chokeAt (int sample) noexcept;
    // Message thread, for analysis devices: the latest `frames` input frames of one channel (0 left,
    // 1 right, 2 their mean), and the rate they were processed at. False before the device has
    // processed audio or when the copy was overtaken.
    bool latestInput (std::vector<float>& destination, int frames, double& rate, int channel = 2) const;
    static constexpr int analysisCapacity = 32768;
private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    bool rackStructure (const juce::ValueTree& parent) const;
    void publish();
    void timerCallback() override;
    const Session::EffectKind kind;
    void processMidi (const tracktion::PluginRenderContext&) noexcept;
    void renderInstrument (const tracktion::PluginRenderContext&) noexcept;
    instruments::InstrumentDSP instrument;
    NativeEffectDSP processor;
    midifx::Processor midiProcessor;
    std::array<midifx::Event, 512> midiInput {};
    std::array<midifx::Event, 2048> midiOutput {};
    tracktion::MidiMessageArray midiScratch;
    EffectParameterQueue parameters;
    // Sampler: decoded samples stay alive until the audio thread acknowledges a newer one.
    std::vector<tracktion::AutomatableParameter*> controls;
    std::array<std::atomic<bool>, std::tuple_size_v<decltype (Session::EffectSettings::values)>> readsCurve {};
    Session::EffectSettings audioSettings; // Audio thread: the last manual settings received.
    bool automated = false;
    void loadSample (int slot);
    void retireSamples();
    std::vector<std::unique_ptr<instruments::SampleData>> samples;
    std::array<std::atomic<const instruments::SampleData*>, Session::drumPadCount> publishedSamples {}, acknowledgedSamples {};
    static_assert (std::atomic<const instruments::SampleData*>::is_always_lock_free);
    std::array<juce::File, Session::drumPadCount> loadedSamples;
    Session::EffectSettings pendingSettings;
    bool pending = false, writing = false;
    std::unique_ptr<racks::RackEngine> rack;
    // Analysis devices: a ring the audio thread writes and the message thread copies.
    std::unique_ptr<std::array<std::array<std::atomic<float>, analysisCapacity>, 2>> analysis;
    std::atomic<uint64_t> analysisWritten { 0 };
    std::atomic<double> analysisRate { 0 };
    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free && std::atomic<double>::is_always_lock_free);
    const float* rackControls = nullptr; // Audio thread.
    std::array<int, 16> chokes {};       // Audio thread: pending choke samples.
    int chokeCount = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeEffectPlugin)
};
// Compact editors are three rows of small controls for the inline device view.
// With showAutomation, right-clicking a control offers to open its automation.
std::unique_ptr<juce::Component> makeNativeEffectEditor (Session&, const juce::String&, bool compact = false,
                                                         std::function<void (int parameter)> showAutomation = {});
// Whether a control displays as a percentage, as the device editor shows it.
bool effectShowsPercent (const EffectParameter&) noexcept;
#if HALFTRACK_TESTS_ENABLED
int runNativeEffectEditorTests (const juce::File&);
#endif
}
