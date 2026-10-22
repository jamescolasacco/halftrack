#include <juce_audio_processors/juce_audio_processors.h>

// A test effect with a stereo main bus, a stereo sidechain input and a stereo auxiliary output, as
// sidechain compressors and multi-output instruments have. It shows in its main output what reaches
// each input: the left channel carries the main input's left channel and the right channel the
// sidechain's left channel (the main input's right channel while the host disables the sidechain).
// The auxiliary output carries the main input unchanged.
class TestSidechainProcessor final : public juce::AudioProcessor
{
public:
    TestSidechainProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Aux", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "Halftrack Test Sidechain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto stereoOrOff = [] (const juce::AudioChannelSet& set) { return set.isDisabled() || set == juce::AudioChannelSet::stereo(); };
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && stereoOrOff (layouts.getChannelSet (true, 1)) && stereoOrOff (layouts.getChannelSet (false, 1));
    }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        auto main = getBusBuffer (buffer, true, 0);
        auto sidechain = getBusBuffer (buffer, true, 1);
        auto output = getBusBuffer (buffer, false, 0);
        auto aux = getBusBuffer (buffer, false, 1);
        const int n = buffer.getNumSamples();
        // Main and output share the buffer's first channels; read before writing.
        for (int i = 0; i < n; ++i)
        {
            const float left = main.getSample (0, i), right = main.getSample (1, i);
            const float side = sidechain.getNumChannels() > 0 ? sidechain.getSample (0, i) : right;
            if (aux.getNumChannels() >= 2) { aux.setSample (0, i, left); aux.setSample (1, i, right); }
            output.setSample (0, i, left);
            output.setSample (1, i, side);
        }
    }
    using AudioProcessor::processBlock;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestSidechainProcessor(); }
