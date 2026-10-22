#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace halftrack
{
// A test device that calls its callback from a real-time thread of its own, one block each block
// period, as a driver does, while the message thread is left to the test. Its input is a 440 Hz
// sine on the left and 660 Hz on the right, continuous from the first block. Each callback's start
// lateness and duration are kept in storage allocated before it starts. It never opens hardware.
class RealtimeTestDevice final : public juce::AudioIODevice, private juce::Thread
{
public:
    static constexpr int maximumBlocks = 1 << 18;
    RealtimeTestDevice() : AudioIODevice ("Real-time test signal", "Halftrack real-time test device"), Thread ("Halftrack test audio")
    { durations.resize (maximumBlocks); lateness.resize (maximumBlocks); }
    ~RealtimeTestDevice() override { close(); }
    juce::StringArray getOutputChannelNames() override { return { "Left", "Right" }; }
    juce::StringArray getInputChannelNames() override { return { "Generated left", "Generated right" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 256 }; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger& in, const juce::BigInteger& out, double, int) override { inputs = in; outputs = out; opened = true; return {}; }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* next) override
    {
        stop();
        if (next == nullptr) return;
        next->audioDeviceAboutToStart (this);
        callback = next;
        startRealtimeThread (juce::Thread::RealtimeOptions{}.withPeriodMs (1000.0 * blockSize / rate));
    }
    void stop() override
    {
        stopThread (2000);
        if (auto* old = callback.exchange (nullptr)) old->audioDeviceStopped();
    }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs; }
    juce::BigInteger getActiveInputChannels() const override { return inputs; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    // Callbacks so far, and each one's duration and how late it started, in milliseconds.
    int blocks() const { return std::min (count.load(), maximumBlocks); }
    float duration (int block) const { return durations[(size_t) block]; }
    float late (int block) const { return lateness[(size_t) block]; }
    // The generated input sample on a channel at a sample position.
    static float generated (int channel, int64_t sample) { return 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * sample * (channel == 0 ? 440.0 : 660.0) / 48000.0); }
    static constexpr double rate = 48000;
    static constexpr int blockSize = 256;

private:
    void run() override
    {
        juce::AudioBuffer<float> in (2, blockSize), out (2, blockSize);
        int64_t sample = 0;
        const double period = 1000.0 * blockSize / rate;
        double deadline = juce::Time::getMillisecondCounterHiRes() + period;
        while (! threadShouldExit())
        {
            // Wait for the block's deadline, as a driver's interrupt would come.
            while (juce::Time::getMillisecondCounterHiRes() < deadline - 1.0) juce::Thread::sleep (1);
            while (juce::Time::getMillisecondCounterHiRes() < deadline) {}
            const double started = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < blockSize; ++i, ++sample) { in.setSample (0, i, generated (0, sample)); in.setSample (1, i, generated (1, sample)); }
            out.clear();
            if (auto* current = callback.load()) current->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, blockSize, {});
            const double finished = juce::Time::getMillisecondCounterHiRes();
            if (const int index = count.fetch_add (1); index < maximumBlocks)
            { durations[(size_t) index] = (float) (finished - started); lateness[(size_t) index] = (float) (started - deadline); }
            deadline += period;
            // After a long stall, a driver carries on from now rather than racing to catch up.
            if (finished > deadline + 10 * period) deadline = finished + period;
        }
    }
    std::atomic<juce::AudioIODeviceCallback*> callback { nullptr };
    std::vector<float> durations, lateness;
    std::atomic<int> count { 0 };
    juce::BigInteger inputs, outputs;
    bool opened = false;
};

class RealtimeTestDeviceType final : public juce::AudioIODeviceType
{
public:
    RealtimeTestDeviceType() : AudioIODeviceType ("Halftrack real-time test device") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return { "Real-time test signal" }; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return new RealtimeTestDevice; }
};
}
