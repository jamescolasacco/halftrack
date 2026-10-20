#pragma once
#include "Session.h"
#include "EffectProcessors.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <cmath>
#include <atomic>
#include <span>
#include <vector>

namespace halftrack
{
struct EffectParameter
{
    const char* key;
    const char* label;
    float minimum, maximum, initial, midpoint;
    const char* unit;
    const char* choices = nullptr;
};
const char* effectName (Session::EffectKind) noexcept;
std::span<const EffectParameter> effectParameters (Session::EffectKind) noexcept;
Session::EffectSettings defaultEffectSettings (Session::EffectKind) noexcept;
bool validEffectSettings (const Session::EffectSettings&) noexcept;
// Browser grouping shown in the device menu.
const char* effectCategory (Session::EffectKind) noexcept;
bool effectUsesTempo (Session::EffectKind) noexcept;
double effectTailSeconds (Session::EffectKind, double sampleRate) noexcept;
double effectLatencySeconds (Session::EffectKind, double sampleRate) noexcept;
// Index of a named control in the kind's parameter table, or -1.
int effectParameterIndex (Session::EffectKind, const char* key) noexcept;

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
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    bool noTail() override { return kind == Session::EffectKind::compressor || kind == Session::EffectKind::gate || kind == Session::EffectKind::autoPan || kind == Session::EffectKind::glueCompressor; }
    double getLatencySeconds() override { return effectLatencySeconds (kind, sampleRate); }
    double getTailLength() const override { return effectTailSeconds (kind, sampleRate); }
    bool takesMidiInput() override { return false; }
    bool takesAudioInput() override { return true; }
    int getNumOutputChannelsGivenInputs (int) override { return 2; }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    Session::EffectSettings readSettings() const;
    void writeSettings (const Session::EffectSettings&);
    static juce::Result validateState (const juce::ValueTree&);
    void prepareForOfflineRender();
private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void publish();
    void timerCallback() override;
    const Session::EffectKind kind;
    NativeEffectDSP processor;
    EffectParameterQueue parameters;
    Session::EffectSettings pendingSettings;
    bool pending = false, writing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeEffectPlugin)
};
std::unique_ptr<juce::Component> makeNativeEffectEditor (Session&, const juce::String&);
#if HALFTRACK_TESTS_ENABLED
int runNativeEffectEditorTests (const juce::File&);
#endif
}
