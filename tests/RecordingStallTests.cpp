#include "Session.h"
#include <atomic>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

// Recording through stalled audio callbacks, with the synthetic device. A test-only callback on the
// device manager spins inside chosen callbacks, after the engine's own, so the load meter counts
// the time as the engine's: as when a loaded machine preempts the audio or graph threads. A single
// 40 ms stall, a burst of six 8 ms callbacks and a 100 ms stall each drive the meter past 0.98,
// where the engine's default silenced the next callback and dropped its input. The take must still
// hold every generated input sample in order, and the output must keep playing through each stall.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS recording stalls: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr double rate = 48000;
constexpr int blockSize = 256;
// The synthetic device's input on a channel at a sample position.
float generated (int channel, int64_t sample) { return 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * sample * (channel == 0 ? 440.0 : 660.0) / rate); }

// Spins for the requested time in the next callback. It adds nothing to the output.
class Staller final : public juce::AudioIODeviceCallback
{
public:
    std::atomic<double> nextMilliseconds { 0 };
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs, int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        // The device manager mixes this callback's scratch output into the device's.
        for (int c = 0; c < numOutputs; ++c) if (outputs[c] != nullptr) juce::FloatVectorOperations::clear (outputs[c], numSamples);
        if (const double milliseconds = nextMilliseconds.exchange (0); milliseconds > 0)
            for (const double until = juce::Time::getMillisecondCounterHiRes() + milliseconds; juce::Time::getMillisecondCounterHiRes() < until;) {}
    }
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};
}

int runRecordingStallTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("recording-stalls-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("steady.wav");
        {
            juce::AudioBuffer<float> x (2, 6 * (int) rate);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .25f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a steady 0.25 signal");
        }
        Staller staller;
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Stalls").getChildFile ("Stalls.halftrack")), "save a recording destination");
        ok (s.importAudio (source, 1, 0), "track 2 plays the steady signal");
        ok (s.setTrackInput (0, s.inputNames()[0]), "track 1 takes the generated input");
        ok (s.setTrackArmed (0, true), "arm track 1");
        captureSyntheticTestDevice (s, 8);
        auto& devices = s.audioDeviceManager();
        devices.addAudioCallback (&staller);

        // Stalls by the block of the take they fall in, in milliseconds; a block lasts 5.33 ms.
        std::map<int, double> stalls { { 100, 40.0 }, { 300, 100.0 } };
        for (int block = 200; block < 206; ++block) stalls[block] = 8.0;
        const std::vector<std::pair<int, int>> groups { { 100, 100 }, { 200, 205 }, { 300, 300 } };
        constexpr int blocks = 450;
        std::vector<float> loudness ((size_t) blocks), meter ((size_t) blocks);
        s.seek (0); ok (s.record(), "record");
        for (int block = 0; block < blocks; ++block)
        {
            if (const auto stall = stalls.find (block); stall != stalls.end()) staller.nextMilliseconds = stall->second;
            loudness[(size_t) block] = captureSyntheticTestDevice (s, 1).getMagnitude (0, blockSize);
            meter[(size_t) block] = (float) devices.getCpuUsage();
        }
        check (s.snapshot().recording, "it recorded throughout");
        s.stop();
        devices.removeAudioCallback (&staller);
        for (const auto& [first, last] : groups)
            check (meter[(size_t) last] >= 0.98f, "the stall at block " + juce::String (first) + " drove the load meter to " + juce::String (meter[(size_t) last], 2)
                   + ", past the 0.98 at which the engine's default silenced the next callback");

        // The take: the generated input, every sample in order from its first.
        const auto takeFile = [&]
        {
            for (int i = 0; i < 100; ++i)
            {
                const auto now = s.snapshot();
                for (const auto& clip : now.tracks[0].clips) if (clip.isAudio && clip.file.existsAsFile()) return clip.file;
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            }
            return juce::File();
        }();
        check (takeFile.existsAsFile(), "the take was saved");
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (takeFile));
        check (reader != nullptr && reader->numChannels == 2, "open the take");
        juce::AudioBuffer<float> take (2, (int) reader->lengthInSamples);
        reader->read (&take, 0, take.getNumSamples(), 0, true, true);
        check (take.getNumSamples() >= (blocks - 4) * blockSize && take.getNumSamples() <= blocks * blockSize,
               "the take lasts the " + juce::String (blocks) + " blocks recorded (" + juce::String (take.getNumSamples() / (double) blockSize, 2) + " blocks)");
        // Where in the generated signal the take begins: the two sines repeat every 2400 samples.
        int offset = -1;
        for (int k = 0; k < 2400 && offset < 0; ++k)
        {
            bool same = true;
            for (int n = 0; n < 64 && same; ++n)
                same = std::abs (take.getSample (0, n) - generated (0, k + n)) < 1e-3f && std::abs (take.getSample (1, n) - generated (1, k + n)) < 1e-3f;
            if (same) offset = k;
        }
        check (offset >= 0, "the take begins with the generated input");
        int wrong = 0, firstWrong = -1;
        for (int n = 0; n < take.getNumSamples(); ++n)
            for (int c = 0; c < 2; ++c)
                if (std::abs (take.getSample (c, n) - generated (c, offset + n)) >= 1e-3f) { if (firstWrong < 0) firstWrong = n; ++wrong; }
        check (wrong == 0, "every one of its " + juce::String (take.getNumSamples()) + " samples follows in order, none lost or repeated"
               + (wrong > 0 ? " (first wrong at " + juce::String (firstWrong) + ", block " + juce::String (firstWrong / blockSize) + ")" : juce::String()));

        // The output: once playback sounds, no block falls silent.
        int sounding = 0;
        while (sounding < blocks && loudness[(size_t) sounding] < .05f) ++sounding;
        check (sounding < 20, "playback sounds from block " + juce::String (sounding));
        juce::StringArray silent;
        for (int block = sounding; block < blocks; ++block) if (loudness[(size_t) block] < .05f) silent.add (juce::String (block));
        check (silent.isEmpty(), "the output kept playing through every stall" + (silent.isEmpty() ? juce::String() : " (silent blocks " + silent.joinIntoString (", ") + ")"));
        std::cout << "Recording stalls passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL recording stalls: " << e.what() << std::endl; return 1; }
}
}
