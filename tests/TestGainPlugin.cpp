#include <juce_audio_processors/juce_audio_processors.h>

// Deterministic external plug-in fixture: no I/O, allocation, or locking in processBlock.
class TestGain final : public juce::AudioProcessor
{
public:
    TestGain() : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat ({"gain", 1}, "Gain", 0.0f, 1.0f, 0.5f));
    }
    const juce::String getName() const override { return "Halftrack Test Gain"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == l.getMainOutputChannelSet()
            && (l.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
                || l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
    }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        b.applyGain (gain->get());
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::MemoryOutputStream stream (dest, false);
        stream.writeFloat (gain->get());
    }
    void setStateInformation (const void* data, int bytes) override
    {
        if (bytes >= 4)
        {
            juce::MemoryInputStream stream (data, static_cast<size_t> (bytes), false);
            auto value = stream.readFloat();
            if (std::isfinite (value)) gain->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
        }
    }
private:
    juce::AudioParameterFloat* gain = nullptr;
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestGain(); }
