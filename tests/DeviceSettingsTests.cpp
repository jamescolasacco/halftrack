#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Sample rate and buffer size, through the synthetic device, which offers 44.1, 48 and 96 kHz with
// 128, 256 or 512-sample blocks. A 1 kHz sine clip plays at its pitch at 48 kHz; changing the
// device to 96 kHz with 512-sample blocks while it plays keeps the pitch and playback; a rate the
// device lacks leaves the setup as it was; and the next Session opens with the chosen setup.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS device settings: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
// The frequency of the left channel, from its rising zero crossings after the first quarter.
double frequency (const juce::AudioBuffer<float>& x, double rate)
{
    int first = -1, last = -1, crossings = 0;
    for (int n = x.getNumSamples() / 4 + 1; n < x.getNumSamples(); ++n)
        if (x.getSample (0, n - 1) < 0 && x.getSample (0, n) >= 0) { if (first < 0) first = n; last = n; ++crossings; }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
}
}

int runDeviceSettingsTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("device-settings-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("sine.wav");
        {
            juce::AudioBuffer<float> x (2, 20 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.5 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * n / 48000)));
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a 20 s 1 kHz sine at 48 kHz");
        }
        auto setupAfter = juce::AudioDeviceManager::AudioDeviceSetup {};
        {
            Session s (false, makeSyntheticTestDeviceType());
            auto& devices = s.audioDeviceManager();
            const auto device = [&] { return devices.getCurrentAudioDevice(); };
            check (device() != nullptr && device()->getCurrentSampleRate() == 48000 && device()->getCurrentBufferSizeSamples() == 256, "the device opens at 48 kHz with 256-sample blocks");
            ok (s.importAudio (source, 0, 0), "import the sine");
            pumpSyntheticTestDevice (s, 8);
            s.seek (0); ok (s.play(), "play");
            pumpSyntheticTestDevice (s, 16);
            check (std::abs (frequency (captureSyntheticTestDevice (s, 200), 48000) - 1000) < 2, "it sounds 1 kHz");

            auto setup = devices.getAudioDeviceSetup();
            setup.sampleRate = 96000; setup.bufferSize = 512;
            const auto error = devices.setAudioDeviceSetup (setup, true);
            check (error.isEmpty() && device()->getCurrentSampleRate() == 96000 && device()->getCurrentBufferSizeSamples() == 512,
                   "the device changes to 96 kHz with 512-sample blocks while playing");
            pumpSyntheticTestDevice (s, 16);
            check (s.snapshot().playing, "playback carries on through the change");
            const auto after = frequency (captureSyntheticTestDevice (s, 200), 96000);
            check (std::abs (after - 1000) < 2, "and the clip still sounds 1 kHz at the new rate (" + juce::String (after, 1) + " Hz)");
            setup = devices.getAudioDeviceSetup(); setup.bufferSize = 128;
            check (devices.setAudioDeviceSetup (setup, true).isEmpty() && device()->getCurrentBufferSizeSamples() == 128, "change to 128-sample blocks while playing");
            s.stop();
            pumpSyntheticTestDevice (s, 400);
            check (! s.snapshot().playing, "a Stop just after the change is kept");

            setup = devices.getAudioDeviceSetup(); setup.sampleRate = 22050;
            devices.setAudioDeviceSetup (setup, true);
            check (device()->getCurrentSampleRate() == 96000, "a rate the device lacks leaves the rate as it was");
            setupAfter = devices.getAudioDeviceSetup();
        }
        {
            Session next (false, makeSyntheticTestDeviceType());
            auto* device = next.audioDeviceManager().getCurrentAudioDevice();
            check (device != nullptr && device->getCurrentSampleRate() == setupAfter.sampleRate && device->getCurrentBufferSizeSamples() == setupAfter.bufferSize,
                   "the next Session opens the device with the chosen setup");
            auto setup = next.audioDeviceManager().getAudioDeviceSetup(); setup.sampleRate = 48000; setup.bufferSize = 256;
            check (next.audioDeviceManager().setAudioDeviceSetup (setup, true).isEmpty(), "back to 48 kHz with 256-sample blocks");
        }
        std::cout << "Device settings passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL device settings: " << e.what() << std::endl; return 1; }
}
}
