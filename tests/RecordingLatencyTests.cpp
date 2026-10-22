#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Recorded placement against latency, through a loopback device: what it plays comes back to its
// input a fixed number of samples later, as through a cable from an output to an input, and it
// reports whatever latency the test chooses. A click played from one track is recorded on another.
// Reported truthfully, the take's click lands where the click played; misreported, it lands late by
// the difference, and Driver Error Compensation of that much puts it back.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS recording latency: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blockSize = 256;
constexpr double rate = 48000;

class LoopbackDevice final : public juce::AudioIODevice
{
public:
    LoopbackDevice() : AudioIODevice ("Loopback", "Halftrack loopback test device") { returned.setSize (2, 1 << 16); returned.clear(); }
    ~LoopbackDevice() override { stop(); }
    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return { "In 1", "In 2" }; }
    juce::Array<double> getAvailableSampleRates() override { return { rate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { blockSize }; }
    int getDefaultBufferSize() override { return blockSize; }
    juce::String open (const juce::BigInteger& in, const juce::BigInteger& out, double, int) override { inputs = in; outputs = out; opened = true; return {}; }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* next) override { stop(); callback = next; if (callback) callback->audioDeviceAboutToStart (this); }
    void stop() override { if (auto* old = std::exchange (callback, nullptr)) old->audioDeviceStopped(); }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs; }
    juce::BigInteger getActiveInputChannels() const override { return inputs; }
    int getOutputLatencyInSamples() override { return reportedOutput; }
    int getInputLatencyInSamples() override { return reportedInput; }

    // The round trip from output to input, and what the device says it is.
    int delay = 700, reportedInput = 300, reportedOutput = 400;

    void pump (int blocks)
    {
        const int size = returned.getNumSamples();
        juce::AudioBuffer<float> in (2, blockSize), out (2, blockSize);
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto at = sample + i - delay;
                    in.setSample (c, i, at >= 0 ? returned.getSample (c, (int) (at % size)) : 0.0f);
                }
            out.clear();
            if (callback != nullptr) callback->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, blockSize, {});
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i) returned.setSample (c, (int) ((sample + i) % size), out.getSample (c, i));
            sample += blockSize;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    }
private:
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::BigInteger inputs, outputs;
    juce::AudioBuffer<float> returned;
    int64_t sample = 0;
    bool opened = false;
};
class LoopbackDeviceType final : public juce::AudioIODeviceType
{
public:
    LoopbackDeviceType() : AudioIODeviceType ("Halftrack loopback test device") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return { "Loopback" }; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return new LoopbackDevice; }
};

// The first sample at least half a click's height, in seconds of the timeline: the take's start
// plus its place in the take's file.
double clickAt (const Session::ClipInfo& take)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (take.file));
    if (reader == nullptr) throw std::runtime_error ("could not read the take");
    juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    for (int n = (int) std::round (take.sourceOffset * reader->sampleRate); n < audio.getNumSamples(); ++n)
        if (std::abs (audio.getSample (0, n)) >= .4f) return take.start + n / reader->sampleRate - take.sourceOffset;
    return -1;
}
}

int runRecordingLatencyTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("recording-latency-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        // One click at one second.
        const auto clickFile = root.getChildFile ("click.wav");
        {
            juce::AudioBuffer<float> audio (1, (int) (2 * rate));
            audio.clear();
            for (int n = 0; n < 48; ++n) audio.setSample (0, (int) rate + n, .8f * (1 - n / 48.0f));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (clickFile.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
            check (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write a click at one second");
        }
        Session s (false, std::make_unique<LoopbackDeviceType>());
        auto* device = dynamic_cast<LoopbackDevice*> (s.audioDeviceManager().getCurrentAudioDevice());
        check (device != nullptr, "the loopback device is open");
        ok (s.saveProject (root.getChildFile ("Latency").getChildFile ("Latency.halftrack")), "save a project");
        ok (s.addAudioTrack ("Source"), "a source track");
        const int source = (int) s.snapshot().tracks.size() - 1;
        ok (s.importAudio (clickFile, source, 0), "with the click");
        ok (s.addAudioTrack ("Take"), "a track to record on");
        const int take = (int) s.snapshot().tracks.size() - 1;
        ok (s.setTrackInput (take, s.inputNames()[0]), "from the loopback input");
        ok (s.setTrackMonitor (take, Session::MonitorMode::off), "unmonitored, so nothing feeds back");
        for (int t = 0; t < (int) s.snapshot().tracks.size(); ++t)
            if (t != source && t != take) ok (s.setTrackMute (t, true), "mute the other tracks");
        const auto latency = s.audioLatency();
        check (std::abs (latency.inputMs - 300 * 1000 / rate) < 1e-6 && std::abs (latency.outputMs - 400 * 1000 / rate) < 1e-6 && latency.compensationMs == 0
                   && std::abs (latency.overallMs() - 700 * 1000 / rate) < 1e-6,
               "the reported latency reads back: " + juce::String (latency.overallMs(), 3) + " ms overall");

        const auto recordClick = [&]
        {
            const auto before = s.snapshot().tracks[(size_t) take].clips.size();
            ok (s.setTrackArmed (take, true), "arm the take track");
            s.seek (0); device->pump (16);
            ok (s.record(), "record");
            device->pump ((int) (2.2 * rate / blockSize));
            s.stop(); device->pump (16);
            ok (s.setTrackArmed (take, false), "disarm");
            const auto clips = s.snapshot().tracks[(size_t) take].clips;
            check (clips.size() == before + 1, "a take was recorded");
            const auto at = clickAt (clips.back());
            ok (s.removeClip (take, (int) clips.size() - 1), "remove the take");
            return at;
        };
        const auto offset = [] (double at) { return juce::String ((at - 1) * rate, 1) + " samples"; };
        const auto truthful = recordClick();
        check (truthful > 0 && std::abs (truthful - 1) * rate <= 1, "reported truthfully, the take's click lands where it played (" + offset (truthful) + ")");

        device->reportedOutput = 200; // The driver says 200 samples less than the trip takes.
        const auto late = recordClick();
        check (std::abs ((late - 1) * rate - 200) <= 1, "misreported by 200 samples, the click lands 200 samples late (" + offset (late) + ")");

        auto preferences = s.preferences();
        preferences.driverErrorCompensationMs = 200 * 1000 / rate;
        ok (s.setPreferences (preferences), "Driver Error Compensation of 200 samples");
        check (std::abs (s.audioLatency().overallMs() - 700 * 1000 / rate) < 1e-6, "the overall latency includes it");
        const auto corrected = recordClick();
        check (std::abs (corrected - 1) * rate <= 1, "with it, the click lands where it played again (" + offset (corrected) + ")");

        preferences.driverErrorCompensationMs = 250;
        check (s.setPreferences (preferences).failed(), "compensation beyond 200 ms is refused");
        ok (s.resetPreferences(), "reset the preferences");
        root.deleteRecursively();
        std::cout << "Recording latency passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL recording latency: " << e.what() << std::endl; return 1; }
}
}
