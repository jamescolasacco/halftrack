#include "Session.h"
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>

// An audio device that disappears: through a device type whose "Interface" can be unplugged and
// plugged in again while a "Built-in" device stays. When the interface goes, playback stops rather
// than carrying on through whatever the device manager falls back to, the project is kept, and a
// notice names the device; when it returns it is reopened, with another notice. Choosing Built-in
// by hand is a choice, not a loss, and a settings restart still resumes playback.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS device loss: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
bool waitFor (double seconds, const std::function<bool()>& done)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000;
    while (juce::Time::getMillisecondCounterHiRes() < end) { if (done()) return true; juce::MessageManager::getInstance()->runDispatchLoopUntil (10); }
    return done();
}

// A device the test pumps, as the synthetic test device is: each block runs its callback on the
// message thread and then lets pending messages through.
class PumpedDevice final : public juce::AudioIODevice
{
public:
    explicit PumpedDevice (const juce::String& name) : AudioIODevice (name, "Switchable test devices") {}
    ~PumpedDevice() override { close(); }
    juce::StringArray getOutputChannelNames() override { return { "Left", "Right" }; }
    juce::StringArray getInputChannelNames() override { return { "In 1", "In 2" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0, 96000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 256, 512 }; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger& in, const juce::BigInteger& out, double sampleRate, int size) override
    {
        if (getAvailableSampleRates().contains (sampleRate)) rate = sampleRate;
        if (getAvailableBufferSizes().contains (size)) bufferSize = size;
        inputs = in; outputs = out; opened = true; return {};
    }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* c) override { if (c == nullptr) return; stop(); callback = c; callback->audioDeviceAboutToStart (this); }
    void stop() override { if (auto* stopped = std::exchange (callback, nullptr)) stopped->audioDeviceStopped(); }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bufferSize; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs; }
    juce::BigInteger getActiveInputChannels() const override { return inputs; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
    void pump (int blocks)
    {
        juce::AudioBuffer<float> in (2, bufferSize), out (2, bufferSize);
        for (int b = 0; b < blocks; ++b)
        {
            in.clear(); out.clear();
            // Only the channels that are open, as a driver passes them.
            if (callback != nullptr) callback->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), inputs.countNumberOfSetBits(), out.getArrayOfWritePointers(),
                                                                                 juce::jmax (1, outputs.countNumberOfSetBits()), bufferSize, {});
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
    }
private:
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::BigInteger inputs, outputs;
    int bufferSize = 256;
    double rate = 48000;
    bool opened = false;
};
class SwitchableDeviceType final : public juce::AudioIODeviceType
{
public:
    SwitchableDeviceType() : AudioIODeviceType ("Switchable test devices") {}
    void setInterfacePresent (bool present) { interfacePresent = present; callDeviceChangeListeners(); }
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override
    { juce::StringArray names; if (interfacePresent) names.add ("Interface"); names.add ("Built-in"); return names; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* device, bool inputs) const override { return device != nullptr ? getDeviceNames (inputs).indexOf (device->getName()) : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& output, const juce::String&) override
    { return getDeviceNames (false).contains (output) ? new PumpedDevice (output) : nullptr; }
private:
    bool interfacePresent = true;
};
// Runs blocks on whichever pumped device is open, or just lets messages through.
void pump (juce::AudioDeviceManager& devices, int blocks)
{
    if (auto* device = dynamic_cast<PumpedDevice*> (devices.getCurrentAudioDevice())) device->pump (blocks);
    else juce::MessageManager::getInstance()->runDispatchLoopUntil (blocks * 5);
}
}

int runDeviceLossTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("device-loss-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        auto owned = std::make_unique<SwitchableDeviceType>();
        auto& type = *owned;
        Session s (false, std::move (owned));
        auto& devices = s.audioDeviceManager();
        const auto current = [&] { auto* d = devices.getCurrentAudioDevice(); return d != nullptr ? d->getName() : juce::String(); };
        check (current() == "Interface" && s.takeDeviceNotice().isEmpty(), "the Session opens the interface");
        // Chosen in Audio Settings, as a user's saved settings record it, so the manager has a fallback.
        check (devices.setAudioDeviceSetup (devices.getAudioDeviceSetup(), true).isEmpty() && current() == "Interface", "choose the interface");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        ok (s.addMidiClip (s.snapshot().tracks.back().id, 0, 64), "with a long clip to play");
        const auto tracks = s.snapshot().tracks.size();
        pump (devices, 8);
        s.seek (0); ok (s.play(), "play");
        // Choosing the interface rebuilt the device list, so playback can take a while to begin on a
        // loaded machine: give it up to two seconds of blocks.
        pump (devices, 100);
        for (int i = 0; i < 6 && s.snapshot().position <= .2; ++i) pump (devices, 50);
        check (s.snapshot().playing && s.snapshot().position > .2, "the interface plays (at " + juce::String (s.snapshot().position, 3) + " s)");

        // Unplugged: whatever the device manager falls back to (here, nothing: JUCE's generic manager
        // retries the missing device; CoreAudio's falls back to the system output), playback stops.
        type.setInterfacePresent (false);
        check (waitFor (3, [&] { return current() != "Interface"; }), "the interface closes");
        const auto fallback = current();
        pump (devices, 300); // Longer than a settings restart's resume.
        const auto notice = s.takeDeviceNotice();
        check (! s.snapshot().playing && s.snapshot().tracks.size() == tracks, "playback stops, and the project is kept");
        check (notice.contains ("Interface") && notice.contains ("disconnected") && (fallback.isEmpty() || notice.contains (fallback)),
               "a notice names the device, and where audio goes if anywhere (" + notice + ")");
        check (s.takeDeviceNotice().isEmpty(), "the notice is given once");

        // Plugged back in: reopened.
        type.setInterfacePresent (true);
        check (waitFor (3, [&] { return current() == "Interface"; }), "the interface is reopened when it returns");
        check (s.takeDeviceNotice().contains ("connected again"), "with a notice");
        // The engine rebuilds its device lists after the reopen; let that settle, as a user would.
        pump (devices, 100);
        s.seek (0); ok (s.play(), "play again");
        pump (devices, 100);
        check (s.snapshot().playing && s.snapshot().position > .2, "and it plays");

        // A settings restart on the chosen device still resumes playback.
        auto setup = devices.getAudioDeviceSetup(); setup.bufferSize = 512;
        check (devices.setAudioDeviceSetup (setup, true).isEmpty(), "change the buffer size");
        const auto before = s.snapshot().position;
        pump (devices, 200);
        check (s.snapshot().playing && s.snapshot().position > before + .2 && s.takeDeviceNotice().isEmpty(), "playback resumes after a settings restart, with no notice");
        s.stop();

        // Choosing Built-in by hand is a choice: unplugging the interface then changes nothing.
        setup.outputDeviceName = "Built-in";
        check (devices.setAudioDeviceSetup (setup, true).isEmpty() && waitFor (2, [&] { return current() == "Built-in"; }), "choose Built-in by hand");
        pump (devices, 8);
        s.seek (0); ok (s.play(), "play on Built-in");
        pump (devices, 50);
        type.setInterfacePresent (false);
        pump (devices, 200);
        check (current() == "Built-in" && s.snapshot().playing && s.takeDeviceNotice().isEmpty(), "unplugging the unused interface stops nothing and says nothing");
        type.setInterfacePresent (true);
        pump (devices, 100);
        check (current() == "Built-in", "and its return does not take over");
        s.stop();
        std::cout << "Device loss passed" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL device loss: " << e.what() << std::endl; return 1; }
}
}
