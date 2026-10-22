#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// A test instrument that reports what its host tells it, in its audio. The left channel carries
// each note-on's velocity at the exact sample the host delivered it. The right channel carries
// the host's beat position as a sawtooth that restarts on every beat: its fraction of a beat times
// one half, negative while the host renders offline and absent while it is stopped. Positions
// where the sawtooth restarts are the host's beats, whatever gain follows the plug-in.
class TestProbeProcessor final : public juce::AudioProcessor
{
public:
    TestProbeProcessor() : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "Halftrack Test Probe"; }
    bool acceptsMidi() const override { return true; }
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
    { return layouts.getMainInputChannelSet().isDisabled() && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        if (buffer.getNumChannels() < 2) return;
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (const auto event : midi)
            if (const auto message = event.getMessage(); message.isNoteOn() && juce::isPositiveAndBelow (event.samplePosition, buffer.getNumSamples()))
                left[event.samplePosition] = message.getFloatVelocity();
        auto* head = getPlayHead();
        if (head == nullptr) return;
        const auto position = head->getPosition();
        if (! position || ! position->getIsPlaying() || ! position->getPpqPosition() || ! position->getBpm()) return;
        const double perSample = *position->getBpm() / 60.0 / getSampleRate();
        const float sign = isNonRealtime() ? -1.0f : 1.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double beat = *position->getPpqPosition() + i * perSample;
            right[i] = sign * .5f * (float) (beat - std::floor (beat));
        }
    }
    using AudioProcessor::processBlock;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestProbeProcessor(); }
