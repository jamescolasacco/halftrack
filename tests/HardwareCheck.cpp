#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <vector>

// A manual check on a real audio interface (first a MOTU M4), run by hand with the interface
// attached and never by CTest. It opens the interface through a Session with temporary settings,
// so the user's own Halftrack settings are untouched, and plays only quiet generated signals.
//
//   --hardware-check <folder> [device-name]           device facts, output-only playback, and a
//                                                     MIDI take recorded with two virtual inputs
//   --hardware-check <folder> [device-name] loopback  also round-trip latency and recorded placement,
//                                                     with a cable from output 1 to input 3
//   --hardware-check <folder> [device-name] unplug    play, wait for the interface to be unplugged,
//                                                     then for it to return, and play again
//
// Only input 3 is ever opened, and only for the loopback measurement; no other input is captured.
// Results are printed and written to hardware-report.txt in the folder.
namespace halftrack
{
namespace te = tracktion::engine;
namespace
{
struct Report
{
    juce::String text;
    void line (const juce::String& l) { text << l << "\n"; std::cout << l << std::endl; }
};
void wait (double seconds)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000;
    while (juce::Time::getMillisecondCounterHiRes() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}
bool waitFor (double seconds, const std::function<bool()>& done)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000;
    while (juce::Time::getMillisecondCounterHiRes() < end) { if (done()) return true; juce::MessageManager::getInstance()->runDispatchLoopUntil (20); }
    return done();
}
// A stereo file at 48 kHz: a quiet tone at a level, or single-sample clicks at the given seconds.
juce::File writeSignal (const juce::File& file, double seconds, const std::vector<double>& clicks, double level = .03)
{
    juce::AudioBuffer<float> x (2, (int) (seconds * 48000)); x.clear();
    if (clicks.empty())
        for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (level * std::sin (juce::MathConstants<double>::twoPi * 440 * n / 48000)));
    for (const auto at : clicks) for (int c = 0; c < 2; ++c) x.setSample (c, (int) std::lround (at * 48000), .5f);
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    if (writer) writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples());
    return file;
}
// Sends a click on output 1 at chosen samples and keeps input 3 around each, in the device callback.
// Everything is allocated before the device runs; the callback only reads and writes its buffers.
struct RoundTrip final : juce::AudioIODeviceCallback
{
    static constexpr int clicks = 5, spacing = 24000, window = 12000;
    std::vector<float> captured = std::vector<float> ((size_t) clicks * window, 0.0f);
    std::atomic<int64_t> start { -1 };
    std::atomic<bool> done { false };
    int64_t sample = 0;
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
        if (start.load() < 0) start.store (sample + 4800); // A tenth of a second after the first block.
        const auto first = start.load();
        for (int i = 0; i < n; ++i, ++sample)
        {
            const auto since = sample - first;
            if (since < 0) continue;
            const auto click = since / spacing, offset = since % spacing;
            if (click >= clicks) { done.store (true); continue; }
            if (offset == 0 && numOut > 0 && out[0] != nullptr) out[0][i] = .5f;
            if (offset < window && numIn > 0 && in[0] != nullptr) captured[(size_t) (click * window + offset)] = in[0][i];
        }
    }
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};
// CoreAudio with its inputs hidden until a measurement allows them, so opening the device never
// opens a microphone; device list changes (an unplugged interface) pass through.
struct HardwareType final : juce::AudioIODeviceType, private juce::AudioIODeviceType::Listener
{
    explicit HardwareType (const bool& allow) : AudioIODeviceType ("CoreAudio"), allowInputs (allow) { inner->addListener (this); }
    ~HardwareType() override { inner->removeListener (this); }
    void scanForDevices() override { inner->scanForDevices(); }
    juce::StringArray getDeviceNames (bool wantInputs) const override { return wantInputs && ! allowInputs ? juce::StringArray() : inner->getDeviceNames (wantInputs); }
    int getDefaultDeviceIndex (bool forInput) const override { return forInput && ! allowInputs ? -1 : inner->getDefaultDeviceIndex (forInput); }
    int getIndexOfDevice (juce::AudioIODevice* device, bool asInput) const override { return asInput && ! allowInputs ? -1 : inner->getIndexOfDevice (device, asInput); }
    bool hasSeparateInputsAndOutputs() const override { return inner->hasSeparateInputsAndOutputs(); }
    juce::AudioIODevice* createDevice (const juce::String& output, const juce::String& input) override { return inner->createDevice (output, allowInputs ? input : juce::String()); }
private:
    void audioDeviceListChanged() override { callDeviceChangeListeners(); }
    std::unique_ptr<juce::AudioIODeviceType> inner { juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio() };
    const bool& allowInputs;
};
// The first sample in a buffer above a threshold, or -1.
int onset (const float* x, int count, float threshold)
{ for (int i = 0; i < count; ++i) if (std::abs (x[i]) > threshold) return i; return -1; }
}

int runHardwareCheck (const juce::File& folder, const juce::String& wanted, const juce::String& mode)
{
    Report report;
    folder.createDirectory();
    report.line ("Halftrack hardware check, " + juce::Time::getCurrentTime().toString (true, true) + ", mode " + (mode.isEmpty() ? juce::String ("basic") : mode));
    bool allowInputs = false; // Only the loopback measurement opens an input, and only input 3.
    Session s (false, std::make_unique<HardwareType> (allowInputs));
    auto& devices = s.audioDeviceManager();
    auto* type = devices.getCurrentDeviceTypeObject();
    if (type == nullptr) { report.line ("FAIL: the device type did not initialise"); return 2; }
    type->scanForDevices();
    const auto outputs = type->getDeviceNames (false);
    report.line ("Output devices: " + outputs.joinIntoString (", "));
    juce::String name;
    for (const auto& candidate : outputs) if (candidate.containsIgnoreCase (wanted)) name = candidate;
    if (name.isEmpty()) { report.line ("FAIL: no output device named like \"" + wanted + "\""); folder.getChildFile ("hardware-report.txt").replaceWithText (report.text); return 2; }

    const bool loopback = mode == "loopback";
    allowInputs = loopback;
    type->scanForDevices();
    const auto inputs = type->getDeviceNames (true);
    auto setup = devices.getAudioDeviceSetup();
    setup.outputDeviceName = name; setup.inputDeviceName = loopback && inputs.contains (name) ? name : juce::String();
    setup.sampleRate = 48000; setup.bufferSize = 256;
    setup.useDefaultOutputChannels = false; setup.outputChannels.clear(); setup.outputChannels.setRange (0, 2, true);
    setup.useDefaultInputChannels = false; setup.inputChannels.clear();
    if (loopback) setup.inputChannels.setBit (2); // Input 3 only.
    if (const auto error = devices.setAudioDeviceSetup (setup, true); error.isNotEmpty()) { report.line ("FAIL: could not open " + name + ": " + error); return 2; }
    wait (1.0);
    auto* device = devices.getCurrentAudioDevice();
    if (device == nullptr) { report.line ("FAIL: no device is open"); return 2; }
    juce::StringArray rates, sizes;
    for (const auto r : device->getAvailableSampleRates()) rates.add (juce::String (r, 0));
    for (const auto b : device->getAvailableBufferSizes()) sizes.add (juce::String (b));
    report.line ("Opened: " + device->getName() + " at " + juce::String (device->getCurrentSampleRate(), 0) + " Hz, " + juce::String (device->getCurrentBufferSizeSamples())
                 + " samples; outputs " + device->getOutputChannelNames().joinIntoString ("/") + "; inputs " + device->getInputChannelNames().joinIntoString ("/"));
    report.line ("Rates: " + rates.joinIntoString (", ") + "; buffer sizes: " + sizes.joinIntoString (", "));
    report.line ("Reported latency: output " + juce::String (device->getOutputLatencyInSamples()) + " samples, input " + juce::String (device->getInputLatencyInSamples()) + " samples");

    if (mode == "unplug")
    {
        s.importAudio (writeSignal (folder.getChildFile ("tone.wav"), 1300, {}, .003), 0, 0); // -50 dBFS while waiting.
        s.seek (0); s.play(); wait (1.0);
        const auto tracksBefore = s.snapshot().tracks.size();
        report.line ("Playing a quiet tone. UNPLUG THE INTERFACE NOW (waiting up to 600 s).");
        const auto unplugStart = juce::Time::getMillisecondCounterHiRes();
        const bool gone = waitFor (600, [&]
        {
            auto* current = devices.getCurrentAudioDevice();
            return current == nullptr || current->getName() != name || ! current->isOpen();
        });
        const auto noticed = (juce::Time::getMillisecondCounterHiRes() - unplugStart) / 1000;
        auto* now = devices.getCurrentAudioDevice();
        report.line (gone ? "Unplug noticed after " + juce::String (noticed, 1) + " s" : "FAIL: the unplug was not noticed within 600 s");
        report.line ("After unplugging: playing " + juce::String ((int) s.snapshot().playing) + ", tracks " + juce::String ((int) s.snapshot().tracks.size()) + " of " + juce::String ((int) tracksBefore)
                     + ", current device " + (now != nullptr ? now->getName() + (now->isOpen() ? " (open)" : " (closed)") : juce::String ("none")));
        wait (1.0);
        report.line ("Halftrack's notice: " + s.takeDeviceNotice() + "; playing a second later " + juce::String ((int) s.snapshot().playing));
        s.stop();
        report.line ("PLUG THE INTERFACE BACK IN NOW (waiting up to 600 s).");
        const bool back = waitFor (600, [&] { type->scanForDevices(); return type->getDeviceNames (false).contains (name); });
        report.line (back ? juce::String ("The interface is listed again") : juce::String ("FAIL: the interface did not return within 600 s"));
        if (back)
        {
            // Halftrack should reopen it by itself.
            const bool reopenedByItself = waitFor (10, [&] { auto* d = devices.getCurrentAudioDevice(); return d != nullptr && d->getName() == name && d->isOpen(); });
            report.line (juce::String (reopenedByItself ? "Halftrack reopened it by itself" : "FAIL: Halftrack did not reopen it within 10 s") + "; notice: " + s.takeDeviceNotice());
            const auto error = reopenedByItself ? juce::String() : devices.setAudioDeviceSetup (setup, true);
            wait (1.0);
            auto* reopened = devices.getCurrentAudioDevice();
            report.line ("Reopening: " + (error.isEmpty() ? juce::String ("no error") : error) + "; current device " + (reopened != nullptr ? reopened->getName() : juce::String ("none")));
            s.seek (0); s.play(); wait (2.0);
            const auto position = s.snapshot().position;
            s.stop();
            report.line ("Playing again: the transport advanced " + juce::String (position, 3) + " s in 2 s" + (position > 1.5 ? "" : " (FAIL: playback did not resume)"));
        }
        folder.getChildFile ("hardware-report.txt").replaceWithText (report.text);
        return gone && back ? 0 : 1;
    }

    // Output-only playback against the wall clock.
    s.importAudio (writeSignal (folder.getChildFile ("tone.wav"), 8, {}), 0, 0);
    const int xrunsBefore = device->getXRunCount();
    s.seek (0); s.play();
    const auto started = juce::Time::getMillisecondCounterHiRes();
    float peak = 0;
    waitFor (5.0, [&] { peak = std::max ({ peak, s.snapshot().masterPeakL, s.snapshot().masterPeakR }); return false; });
    const auto wall = (juce::Time::getMillisecondCounterHiRes() - started) / 1000, travelled = s.snapshot().position;
    s.stop(); wait (.5);
    report.line ("Playback: the transport advanced " + juce::String (travelled, 3) + " s in " + juce::String (wall, 3) + " s of wall time (" + juce::String ((travelled - wall) * 1000, 1)
                 + " ms, the reported position trails by up to one 50 Hz update); master peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS; xruns "
                 + juce::String (device->getXRunCount() - xrunsBefore));
    s.removeClip (0, 0);

    // A MIDI take with a second virtual input present (see the open question on test recordings).
    {
        auto& engineDevices = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (devices).engine.getDeviceManager();
        const auto controllerName = "Halftrack hardware check MIDI " + juce::Uuid().toString(), otherName = "Halftrack hardware check other " + juce::Uuid().toString();
        engineDevices.createVirtualMidiDevice (otherName);
        engineDevices.createVirtualMidiDevice (controllerName);
        engineDevices.rescanMidiDeviceListSynchronously();
        std::shared_ptr<te::VirtualMidiInputDevice> controller;
        for (auto d : engineDevices.getMidiInDevices()) if (d->getName() == controllerName) controller = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (d);
        if (controller == nullptr) report.line ("FAIL: the virtual controller was not made");
        else
        {
            controller->setMIDIInputSourceDevices ({}); controller->setChannelToUse (0); controller->setMinimumLengthMs (0); controller->setManualAdjustmentMs (0);
            const auto project = folder.getChildFile ("MidiTake").getChildFile ("MidiTake.halftrack");
            s.saveProject (project);
            s.addMidiTrack ("Keys");
            const int keys = (int) s.snapshot().tracks.size() - 1;
            s.setTrackInput (keys, controllerName); s.setTrackArmed (keys, true);
            wait (1.0);
            s.seek (0); s.record();
            const auto send = [&] (const juce::MidiMessage& m)
            {
                auto message = m; message.setTimeStamp (engineDevices.getCurrentStreamTime() - controller->getAdjustSecs());
                controller->handleIncomingMidiMessage (message, controller->getMPESourceID());
            };
            for (const double at : { .5, 1.5 })
            {
                waitFor (5, [&] { return s.snapshot().position >= at; });
                send (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)); wait (.1); send (juce::MidiMessage::noteOff (1, 60));
            }
            wait (.8); s.stop(); wait (.5);
            const auto clips = s.snapshot().tracks[(size_t) keys].clips;
            const auto clipStart = clips.empty() ? -1.0 : clips[0].start;
            const auto notes = clips.empty() ? 0 : (int) clips[0].notes.size();
            report.line ("MIDI take with a second virtual input: " + juce::String ((int) clips.size()) + " clip(s), starting at " + juce::String (clipStart, 3) + " s, with "
                         + juce::String (notes) + " of 2 notes" + (clips.size() == 1 && std::abs (clipStart) < .05 && notes == 2 ? " (as expected)" : " (FAIL: the take started late or lost notes)"));
            s.setTrackArmed (keys, false);
        }
    }

    if (loopback)
    {
        // Round trip in a device callback: a click out of output 1, found at input 3.
        RoundTrip trip;
        devices.addAudioCallback (&trip);
        waitFor (10, [&] { return trip.done.load(); });
        devices.removeAudioCallback (&trip);
        juce::StringArray found;
        std::vector<int> delays;
        for (int c = 0; c < RoundTrip::clicks; ++c)
            if (const int at = onset (trip.captured.data() + c * RoundTrip::window, RoundTrip::window, .05f); at >= 0) { delays.push_back (at); found.add (juce::String (at)); }
        const auto reported = device->getOutputLatencyInSamples() + device->getInputLatencyInSamples();
        report.line ("Round trip, output 1 to input 3: " + (found.isEmpty() ? juce::String ("no click arrived (FAIL: is the loopback cable in?)") : found.joinIntoString (", ") + " samples")
                     + "; reported output plus input latency " + juce::String (reported) + " samples");
        if (! delays.empty())
        {
            // Recorded placement: the Session records the click it plays; where does it land?
            const auto project = folder.getChildFile ("Loopback").getChildFile ("Loopback.halftrack");
            s.newProject(); s.saveProject (project);
            s.importAudio (writeSignal (folder.getChildFile ("clicks.wav"), 4, { .5, 1.5, 2.5 }), 0, 0);
            juce::String input;
            for (const auto& group : s.audioInputGroups()) if (group.firstChannel == 2 && group.channels == 2) s.setAudioInputPairStereo (2, false);
            for (const auto& group : s.audioInputGroups()) if (group.firstChannel == 2 && group.channels == 1) input = group.name;
            if (input.isEmpty()) report.line ("FAIL: input 3 is not available as a mono input");
            else
            {
                s.setTrackInput (1, input); s.setTrackArmed (1, true);
                wait (1.0);
                s.seek (0); s.record(); wait (3.5); s.stop(); wait (1.0);
                const auto clips = s.snapshot().tracks[1].clips;
                if (clips.empty()) report.line ("FAIL: nothing was recorded from input 3");
                else
                {
                    juce::AudioFormatManager formats; formats.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (clips[0].file));
                    juce::AudioBuffer<float> recorded (1, reader ? (int) reader->lengthInSamples : 0);
                    if (reader) reader->read (&recorded, 0, recorded.getNumSamples(), 0, true, false);
                    juce::StringArray offsets;
                    for (const double expected : { .5, 1.5, 2.5 })
                    {
                        // The click's sample in the file, from just before where it should be.
                        const int from = std::max (0, (int) std::lround ((expected - clips[0].start + clips[0].sourceOffset - .05) * 48000));
                        const int at = from < recorded.getNumSamples() ? onset (recorded.getReadPointer (0) + from, recorded.getNumSamples() - from, .05f) : -1;
                        if (at < 0) { offsets.add ("missing"); continue; }
                        const double timeline = clips[0].start + (from + at) / 48000.0 - clips[0].sourceOffset;
                        offsets.add (juce::String ((timeline - expected) * 48000, 1));
                    }
                    report.line ("Recorded placement: the clicks land at " + offsets.joinIntoString (", ") + " samples from where they were played (0 is exact)");
                }
                s.setTrackArmed (1, false);
            }
        }
    }
    folder.getChildFile ("hardware-report.txt").replaceWithText (report.text);
    report.line ("Report: " + folder.getChildFile ("hardware-report.txt").getFullPathName());
    return 0;
}
}
