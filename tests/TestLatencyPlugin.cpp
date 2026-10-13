#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

// An intentional, constant 127-sample processing delay. This fixture reports
// exactly the delay it introduces, so host compensation has a known oracle.
class TestLatency final : public juce::AudioProcessor
{
public:
    static constexpr int latencySamples = 127;
    TestLatency() : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    { setLatencySamples (latencySamples); }
    const juce::String getName() const override { return "Halftrack Test Latency"; }
    void prepareToPlay (double, int) override { reset(); }
    void releaseResources() override {}
    void reset() override
    {
        for (auto& channel : delay) channel.fill (0.0f);
        cursor = 0;
    }
    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet() == layout.getMainOutputChannelSet()
            && (layout.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
                || layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
    }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int channels = juce::jmin (2, buffer.getNumChannels());
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* values = buffer.getWritePointer (channel);
                const float incoming = values[sample];
                values[sample] = delay[static_cast<size_t> (channel)][static_cast<size_t> (cursor)];
                delay[static_cast<size_t> (channel)][static_cast<size_t> (cursor)] = incoming;
            }
            if (++cursor == latencySamples) cursor = 0;
        }
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& state) override { state.reset(); }
    void setStateInformation (const void*, int) override {}
private:
    std::array<std::array<float, latencySamples>, 2> delay {};
    int cursor = 0;
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestLatency(); }
