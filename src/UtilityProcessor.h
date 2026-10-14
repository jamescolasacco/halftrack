#pragma once
#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>

namespace halftrack
{
class UtilityDSP final
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void configure (const Session::UtilitySettings&) noexcept;
    void processSamples (float* left, float* right, int samples) noexcept;
private:
    struct Biquad
    {
        double z1 = 0, z2 = 0;
        double tick (double input, const std::array<double, 5>& coefficients) noexcept;
    };
    struct FirstOrder
    {
        double state = 0;
        double tick (double input, const std::array<double, 3>& coefficients) noexcept;
    };
    Session::UtilitySettings settings;
    std::array<double, 5> current { 1, 1, 1, 1, 1 }, target { 1, 1, 1, 1, 1 }, step {};
    std::array<double, 5> lowPassCoefficients {}, highPassCoefficients {}, dcCoefficients {};
    std::array<double, 3> firstLowCoefficients {}, firstHighCoefficients {};
    FirstOrder firstLow, firstHigh;
    Biquad lowMid, highSide;
    std::array<Biquad, 2> dcFilters;
    double rate = 48000;
    int remaining = 0;
    bool processed = false;
};

class UtilityParameterQueue final
{
public:
    bool push (const Session::UtilitySettings&) noexcept;
    bool pop (Session::UtilitySettings&) noexcept;
    void resetWhileStopped() noexcept;
private:
    static constexpr uint32_t capacity = 64;
    std::array<Session::UtilitySettings, capacity> packets {};
    std::atomic<uint32_t> readIndex { 0 }, writeIndex { 0 };
    static_assert (std::atomic<uint32_t>::is_always_lock_free);
};

class UtilityPlugin final : public tracktion::Plugin, private juce::Timer
{
public:
    explicit UtilityPlugin (tracktion::PluginCreationInfo);
    ~UtilityPlugin() override;
    static inline const char* xmlTypeName = "halftrackUtility";
    static const char* getPluginName() { return "Utility"; }
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return "Channel utility"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    bool takesMidiInput() override { return false; }
    bool takesAudioInput() override { return true; }
    int getNumOutputChannelsGivenInputs (int) override { return 2; }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    Session::UtilitySettings readSettings() const;
    void writeSettings (const Session::UtilitySettings&);
    static bool validSettings (const Session::UtilitySettings&) noexcept;
    static juce::Result validateState (const juce::ValueTree&);
    void prepareForOfflineRender();
private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void publish();
    void timerCallback() override;
    UtilityDSP processor;
    UtilityParameterQueue parameters;
    Session::UtilitySettings pendingSettings;
    bool pending = false, writing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UtilityPlugin)
};
}
