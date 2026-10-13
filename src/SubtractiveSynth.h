#pragma once

#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace halftrack
{
// Owned by one processing thread. All storage is fixed before audio starts.
class SubtractiveSynthDSP
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setSettings (const Session::SynthSettings&) noexcept;
    void noteOn (int channel, int pitch, float velocity) noexcept;
    void noteOff (int channel, int pitch) noexcept;
    void controlChange (int channel, int controller, int value) noexcept;
    void render (float* left, float* right, int samples) noexcept;
    int activeVoices() const noexcept;
private:
    struct Voice
    {
        double phase = 0, increment = 0, filter1 = 0, filter2 = 0;
        float envelope = 0, releaseStep = 0, velocity = 0;
        int channel = 0, pitch = 0, stage = 0;
        bool keyDown = false;
        uint64_t age = 0;
    };
    void release (Voice&) noexcept;
    static double polyBlep (double phase, double increment) noexcept;
    std::array<Voice, 16> voices {};
    std::array<double, 128> frequencies {};
    std::array<bool, 16> sustainPedal {};
    Session::SynthSettings settings;
    double sampleRate = 48000, a1 = 0, a2 = 0, a3 = 0;
    float attackStep = 1, decayStep = 1;
    uint64_t nextAge = 0;
};

// Single message-thread producer / single processing-thread consumer. The
// release/acquire indices publish complete parameter packets, never partial state.
class SynthParameterQueue
{
public:
    bool push (const Session::SynthSettings&) noexcept;
    bool pop (Session::SynthSettings&) noexcept;
    void resetWhileStopped() noexcept; // Caller must have drained/stopped the consumer.
private:
    static constexpr uint32_t capacity = 64;
    std::array<Session::SynthSettings, capacity> packets {};
    std::atomic<uint32_t> readIndex { 0 }, writeIndex { 0 };
    static_assert (std::atomic<uint32_t>::is_always_lock_free);
};

class SubtractiveSynthPlugin final : public tracktion::Plugin, private juce::Timer
{
public:
    explicit SubtractiveSynthPlugin (tracktion::PluginCreationInfo);
    ~SubtractiveSynthPlugin() override;
    static const char* getPluginName() { return "Subtractive"; }
    static inline const char* xmlTypeName = "halftrackSubtractive";
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return "Subtractive synthesizer"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    bool isSynth() override { return true; }
    bool takesMidiInput() override { return true; }
    bool takesAudioInput() override { return false; }
    int getNumOutputChannelsGivenInputs (int) override { return 2; }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    Session::SynthSettings getSettings() const;
    void setSettings (const Session::SynthSettings&);
    void prepareForOfflineRender(); // Message thread, with playback graph removed.
private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void publishSettings();
    void timerCallback() override;
    SubtractiveSynthDSP synth;
    SynthParameterQueue parameters;
    Session::SynthSettings pendingSettings;
    bool pending = false, writingSettings = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubtractiveSynthPlugin)
};
}
