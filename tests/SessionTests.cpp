#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace halftrack
{
namespace
{
struct FirstCallbackObservation
{
    double entryMilliseconds = -1, exitMilliseconds = -1;
    float peak = 0;
};

// A deterministic input/output device. It never enumerates or opens system hardware.
class SyntheticDevice final : public juce::AudioIODevice
{
public:
    SyntheticDevice() : AudioIODevice ("Generated test signal", "Halftrack test device") {}
    ~SyntheticDevice() override { stop(); }
    juce::StringArray getOutputChannelNames() override { return { "Left", "Right" }; }
    juce::StringArray getInputChannelNames() override { return { "Generated left", "Generated right" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 256 }; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger& in, const juce::BigInteger& out, double, int) override
    { inputs = in; outputs = out; opened = true; return {}; }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* next) override
    { stop(); callback = next; if (callback) callback->audioDeviceAboutToStart (this); }
    void stop() override
    { if (auto* old = std::exchange (callback, nullptr)) old->audioDeviceStopped(); }
    bool isPlaying() override { return callback != nullptr; }
    // A driver-initiated restart, such as a device reporting new details: same layout, same callback.
    void restart() { if (auto* current = callback) { stop(); start (current); } }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 256; }
    double getCurrentSampleRate() override { return 48000; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs; }
    juce::BigInteger getActiveInputChannels() const override { return inputs; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
    int64_t samplePosition() const { return sample; }
    float pump (int blocks, int dispatchMilliseconds = 2, juce::AudioBuffer<float>* captured = nullptr,
                FirstCallbackObservation* firstCallback = nullptr)
    {
        // Test-only stress control: delay message dispatch without changing the generated audio clock.
        if (const auto value=juce::SystemStats::getEnvironmentVariable("HALFTRACK_TEST_DISPATCH_MS",{}); value.isNotEmpty())
            dispatchMilliseconds=juce::jlimit(1,100,value.getIntValue());
        float peak = 0;
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> in (2, 256), out (2, 256);
            for (int i = 0; i < 256; ++i, ++sample)
            {
                in.setSample (0, i, 0.2f * std::sin (juce::MathConstants<double>::twoPi * sample * 440.0 / 48000.0));
                in.setSample (1, i, 0.2f * std::sin (juce::MathConstants<double>::twoPi * sample * 660.0 / 48000.0));
            }
            out.clear();
            if (callback)
            {
                if (firstCallback != nullptr && block == 0)
                {
                    // Opt-in test diagnostics only. The high-resolution clock
                    // does not refresh the dependency's cached MIDI-age clock.
                    firstCallback->entryMilliseconds = juce::Time::getMillisecondCounterHiRes();
                    callback->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 256, {});
                    firstCallback->exitMilliseconds = juce::Time::getMillisecondCounterHiRes();
                }
                else callback->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 256, {});
            }
            const float blockPeak = out.getMagnitude (0, 256);
            peak = juce::jmax (peak, blockPeak);
            if (firstCallback != nullptr && block == 0) firstCallback->peak = blockPeak;
            if (captured) for(int channel=0;channel<2;++channel) captured->copyFrom(channel,block*256,out,channel,0,256);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (dispatchMilliseconds);
        }
        return peak;
    }
private:
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::BigInteger inputs, outputs;
    bool opened = false;
    int64_t sample = 0;
};
class SyntheticDeviceType final : public juce::AudioIODeviceType
{
public:
    SyntheticDeviceType() : AudioIODeviceType ("Halftrack test device") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return { "Generated test signal" }; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return new SyntheticDevice; }
};

void require (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
    std::cout << "PASS " << message << std::endl;
}
void success (const juce::Result& result, const juce::String& message)
{
    require (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : ""));
}
tracktion::engine::PropertyStorage& settingsOf (Session& session)
{
    return dynamic_cast<tracktion::engine::DeviceManager::TracktionEngineAudioDeviceManager&> (session.audioDeviceManager())
        .engine.getPropertyStorage();
}
int activeInputs (Session& session)
{
    auto* device = session.audioDeviceManager().getCurrentAudioDevice();
    return device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : -1;
}

// A second test process saves a one-input device setup, as a measurement once did
// into the settings file every test process shared. A Session here must not open it.
void settingsIsolationTests (const juce::File& root)
{
    const auto report = root.getChildFile ("settings-probe.txt");
    juce::ChildProcess probe;
    // The AppKit flag stops a crash-recovery prompt from blocking the child after an earlier test process was killed.
    require (probe.start (juce::StringArray { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                              "--self-test-settings-probe", report.getFullPathName(),
                                              "-ApplePersistenceIgnoreState", "YES" }),
             "start a second test process");
    const bool finished = probe.waitForProcessToFinish (60000);
    if (! finished) probe.kill();
    const auto output = probe.readAllProcessOutput();
    if (! finished || probe.getExitCode() != 0) std::cerr << output << std::endl;
    require (finished && probe.getExitCode() == 0, "the second process exits after saving its device setup");
    const auto lines = juce::StringArray::fromLines (report.loadFileAsString());
    require (lines.size() == 4 && lines[2] == "1" && lines[3] == "saved",
             "the second process opened one input and saved that setup to " + lines[1]);
    const juce::File probeFolder (lines[0]);
    require (! probeFolder.exists(), "the second process removed its settings folder at exit");

    Session session (false, std::make_unique<SyntheticDeviceType>());
    auto& storage = settingsOf (session);
    const auto folder = storage.getAppPrefsFolder();
    const auto shared = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Halftrack Tests");
    require (folder != probeFolder && folder.isAChildOf (juce::File::getSpecialLocation (juce::File::tempDirectory))
             && storage.getPropertiesFile().getFile().isAChildOf (folder) && storage.getAppCacheFolder() == folder
             && folder != shared && ! folder.isAChildOf (shared),
             "this process keeps test settings and temporary files in its own folder " + folder.getFullPathName());
    require (activeInputs (session) == 2 && session.audioDeviceManager().getAudioDeviceSetup().useDefaultInputChannels,
             "a Session here opens both generated inputs, not the other process's one-input setup");
}
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    require (reader != nullptr, "read " + file.getFileName());
    require (reader->sampleRate == 48000 && reader->numChannels == 2 && reader->lengthInSamples == 48000,
             "48 kHz stereo export retains exactly one second");
    juce::AudioBuffer<float> buffer (2, static_cast<int> (reader->lengthInSamples));
    require (reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true), "decode export samples");
    bool finite = true;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            finite = finite && std::isfinite (buffer.getSample (channel, sample));
    require (finite, "all decoded samples are finite, including silent margins");
    return buffer;
}
void compare (const juce::AudioBuffer<float>& actual, const juce::AudioBuffer<float>& original,
              float leftGain, float rightGain, const juce::String& label)
{
    double squaredError = 0;
    float maxError = 0;
    require (actual.getNumChannels() == original.getNumChannels()
             && actual.getNumSamples() == original.getNumSamples(), label + " retains the reference dimensions");
    bool finite = true;
    // Validate every value. Only gain error excludes startup/dezippering ramps;
    // each render is a fresh graph. Boundary alignment has its own latency suite.
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < original.getNumSamples(); ++sample)
        {
            const auto expected = original.getSample (channel, sample) * (channel == 0 ? leftGain : rightGain);
            const auto value = actual.getSample (channel, sample);
            finite = finite && std::isfinite (expected) && std::isfinite (value);
            if (sample < 4096 || sample >= original.getNumSamples() - 1024) continue;
            auto error = std::abs (value - expected);
            squaredError += error * error;
            maxError = juce::jmax (maxError, error);
        }
    require (finite && std::isfinite (squaredError) && maxError < 0.0001f, label + " PCM max error " + juce::String (maxError, 8)
             + ", squared error " + juce::String (squaredError, 8));
}

void panLawTests (const juce::File& root)
{
    // Non-overlapping left/right tones identify both diagonal gains and any
    // unwanted channel crossfeed. Silent margins let the graph settle without
    // discarding any samples from the comparison or hiding clip-edge output.
    juce::AudioBuffer<float> fixture (2, 48000);
    fixture.clear();
    for (int i = 8192; i < 16384; ++i)
        fixture.setSample (0, i, 0.125f * std::sin (juce::MathConstants<double>::twoPi * i * 440.0 / 48000.0));
    for (int i = 24576; i < 32768; ++i)
        fixture.setSample (1, i, 0.125f * std::sin (juce::MathConstants<double>::twoPi * i * 660.0 / 48000.0));
    const auto source = root.getChildFile ("pan-matrix-source.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
        require (writer && writer->writeFromAudioSampleBuffer (fixture, 0, fixture.getNumSamples()), "write isolated stereo pan matrix fixture");
    }
    fixture = read (source); // Use the actual quantized source as the reference.
    Session session (false);
    success (session.importAudio (source, 0), "import pan matrix fixture");
    success (session.addMidiTrack ("Mixer persistence probe"), "create MIDI track with the same mixer");
    const auto output = root.getChildFile ("pan-matrix.wav");
    auto compareMatrix = [&] (double gainL, double gainR, const juce::String& label)
    {
        success (session.exportWav (output), "render " + label);
        const auto rendered = read (output);
        double maxError = 0;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < fixture.getNumSamples(); ++sample)
                maxError = std::max (maxError, std::abs (double (rendered.getSample (channel, sample))
                    - double (fixture.getSample (channel, sample)) * (channel == 0 ? gainL : gainR)));
        require (gainL == 0 && gainR == 0 ? maxError == 0 : maxError < 0.000002,
                 label + " complete-window matrix max error " + juce::String (maxError, 9));
    };
    // Independent mathematical specification: theta=(pan+1)*pi/4,
    // gains=sqrt(2)*[cos(theta),sin(theta)] times the user volume gain.
    auto comparePan = [&] (double pan, double gain, const juce::String& label)
    {
        const double theta = (pan + 1.0) * juce::MathConstants<double>::pi / 4.0;
        compareMatrix (gain * std::sqrt (2.0) * std::cos (theta),
                       gain * std::sqrt (2.0) * std::sin (theta), label);
    };
    for (int step = -10; step <= 10; ++step)
    {
        const double pan = step / 10.0;
        success (session.setTrackPan (0, float (pan)), "set pan " + juce::String (pan, 1));
        comparePan (pan, 1.0, "unity-center pan " + juce::String (pan, 1));
    }
    success (session.setTrackGain (0, -12), "set low fader gain");
    success (session.setTrackPan (0, -0.4f), "set intermediate pan at low gain");
    comparePan (-0.4, std::pow (10.0, -12.0 / 20.0), "pan with -12 dB fader");
    success (session.setTrackGain (0, 6), "set engine's maximum fader gain");
    success (session.setTrackPan (0, 1), "set hard pan at maximum fader gain");
    comparePan (1, std::pow (10.0, 6.0 / 20.0), "maximum fader preserves pan headroom");
    success (session.setTrackGain (0, -100), "set fader to silence");
    compareMatrix (0, 0, "zero gain is exactly silent");
    success (session.setTrackGain (0, 0), "restore unity fader");
    success (session.setTrackPan (0, 0.6f), "set pan for mute/solo/undo");
    success (session.setTrackMute (0, true), "mute panned channel");
    compareMatrix (0, 0, "panned mute");
    session.undo();
    require (! session.snapshot().tracks[0].mute, "undo restores mute state");
    comparePan (0.6, 1, "undo mute restores panned signal");
    session.redo();
    require (session.snapshot().tracks[0].mute, "redo restores mute state");
    compareMatrix (0, 0, "redo mute restores exact silence");
    session.undo();
    success (session.importAudio (source, 1), "add second matrix source");
    success (session.setTrackSolo (0, true), "solo panned channel");
    comparePan (0.6, 1, "panned solo excludes second source");
    session.undo();
    require (! session.snapshot().tracks[0].solo, "undo restores solo state");
    const double soloTheta = 1.6 * juce::MathConstants<double>::pi / 4.0;
    compareMatrix (1 + std::sqrt (2.0) * std::cos (soloTheta),
                   1 + std::sqrt (2.0) * std::sin (soloTheta), "undo solo restores both tracks");
    session.redo();
    require (session.snapshot().tracks[0].solo, "redo restores solo state");
    comparePan (0.6, 1, "redo solo excludes second source");
    success (session.setTrackSolo (0, false), "clear panned solo");
    success (session.removeClip (1, 0), "remove second matrix source");
    success (session.setTrackPan (0, -0.3f), "change pan before undo");
    session.undo();
    comparePan (0.6, 1, "undo pan restores curve");
    session.redo();
    comparePan (-0.3, 1, "redo pan restores curve");

    const auto project = root.getChildFile ("Pan law.halftrack");
    success (session.saveProject (project), "save normalized mixers");
    success (session.newProject(), "reset before mixer reopen");
    success (session.loadProject (project), "reopen normalized mixer types");
    comparePan (-0.3, 1, "saved mixer restores exact pan behavior");
    require (std::abs (session.snapshot().tracks[0].gainDb) < 0.001f
             && std::abs (session.snapshot().tracks[0].pan + 0.3f) < 0.00001f,
             "normalization preserves facade fader and pan values");

    auto xml = juce::XmlDocument::parse (project);
    int converted = 0;
    auto makeLegacy = [&] (auto&& self, juce::XmlElement& node) -> void
    {
        if (node.hasTagName ("PLUGIN") && node.getStringAttribute ("type") == "halftrack-mixer")
        {
            ++converted;
            node.setAttribute ("type", "volume");
            node.setAttribute ("panLaw", 0); // Engine's legacy default: linear.
        }
        for (auto* child = node.getFirstChildElement(); child != nullptr; child = child->getNextElement())
            self (self, *child);
    };
    require (xml != nullptr, "parse saved mixer project");
    makeLegacy (makeLegacy, *xml);
    require (converted == 3, "both audio tracks and the MIDI track persist the normalized mixer type");
    const auto legacy = root.getChildFile ("Legacy pan law.halftrack");
    require (xml->writeTo (legacy), "create isolated legacy project fixture");
    success (session.loadProject (legacy), "load pre-correction mixer type");
    compareMatrix (1.3, 0.7, "legacy mixer retains linear gains");
    success (session.saveProject (legacy), "save legacy mixer without migration");
    success (session.newProject(), "reset before legacy reopen");
    success (session.loadProject (legacy), "reopen legacy mixer");
    compareMatrix (1.3, 0.7, "legacy save and reopen retains previous sound");
}

// Audio Settings applies its changes straight to the AudioDeviceManager, and a driver can restart
// its device on its own. Both make the engine rebuild its devices while a take is recording.
void deviceChangeRecordingTests (const juce::File& root)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const bool slotTake : { false, true })
        for (const bool driverRestart : { false, true })
        {
            const auto label = juce::String (slotTake ? "Session slot take" : "Arrangement take")
                             + (driverRestart ? " across a driver restart" : " across an input-channel change");
            Session session (false, std::make_unique<SyntheticDeviceType>());
            auto& devices = session.audioDeviceManager();
            const auto pump = [&devices] (int blocks) { dynamic_cast<SyntheticDevice&> (*devices.getCurrentAudioDevice()).pump (blocks); };
            const auto folder = root.getChildFile (juce::String ("Device change ") + (slotTake ? "slot" : "arrangement") + (driverRestart ? " restart" : " channels"));
            success (session.saveProject (folder.getChildFile ("Take.halftrack")), label + ": save the destination");
            success (session.setTrackInput (0, session.inputNames()[0]), label + ": assign the generated input");
            success (session.setTrackArmed (0, true), label + ": arm the track");
            success (session.setTrackMonitor (0, Session::MonitorMode::off), label + ": record without monitoring");
            pump (4);
            session.seek (0);
            success (slotTake ? session.recordSlot (0, 0) : session.record(), label + ": start the take");
            pump (94); // 24,064 generated frames; the take keeps all but the first block.
            if (driverRestart)
                dynamic_cast<SyntheticDevice&> (*devices.getCurrentAudioDevice()).restart();
            else
            {
                // As the Audio Settings input-channel toggle does, without storing it as the chosen
                // setup: the engine saves that to the shared test settings, and later sessions reuse it.
                auto setup = devices.getAudioDeviceSetup();
                setup.useDefaultInputChannels = false;
                setup.inputChannels.setBit (1, false);
                require (devices.setAudioDeviceSetup (setup, false).isEmpty()
                         && devices.getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits() == 1,
                         label + ": the device reopens with one input channel");
            }
            pump (40);
            const auto during = session.snapshot();
            require (! during.recording && ! during.playing && ! during.tracks[0].slots[0].recording,
                     label + ": the take ends where the device stopped, as Stop does");
            const auto checkTake = [&] (const juce::File& file, const juce::String& what)
            {
                std::unique_ptr<juce::AudioFormatReader> reader (file.existsAsFile() ? formats.createReaderFor (file) : nullptr);
                require (reader != nullptr && file.isAChildOf (root), label + ": " + what + " keeps its recording in the project assets");
                juce::AudioBuffer<float> samples (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
                require (std::abs (reader->lengthInSamples - 24064) <= 256 && reader->read (&samples, 0, samples.getNumSamples(), 0, true, true)
                         && samples.getMagnitude (0, samples.getNumSamples()) > .15f,
                         label + ": " + what + " holds half a second of the generated input");
            };
            const auto kept = session.snapshot().tracks[0];
            if (slotTake)
            {
                require (kept.slots[0].hasClip && kept.slots[0].clip.isAudio && kept.clips.empty(), label + ": the take is the slot's clip");
                checkTake (kept.slots[0].clip.file, "the slot clip");
            }
            else
            {
                require (kept.clips.size() == 1 && ! kept.slots[0].hasClip, label + ": the take is an Arrangement clip");
                checkTake (kept.clips[0].file, "the Arrangement clip");
            }
            // The rebuilt devices record the next take.
            session.seek (2);
            success (slotTake ? session.recordSlot (0, 1) : session.record(), label + ": start another take after the change");
            pump (94);
            session.stop();
            pump (4);
            const auto next = session.snapshot().tracks[0];
            if (slotTake) { require (next.slots[1].hasClip, label + ": the next take is a slot clip"); checkTake (next.slots[1].clip.file, "the next slot take"); }
            else { require (next.clips.size() == 2, label + ": the next take is another Arrangement clip"); checkTake (next.clips[1].file, "the next Arrangement take"); }
        }
}

void recordingTests (const juce::File& root)
{
    Session recording (false, std::make_unique<SyntheticDeviceType>());
    auto* device = dynamic_cast<SyntheticDevice*> (recording.audioDeviceManager().getCurrentAudioDevice());
    require (device != nullptr, "recording uses only injected synthetic device");
    tracktion::engine::DeviceManager* engineDevices = nullptr;
    for (auto* engine : tracktion::engine::Engine::getEngines())
        if (&engine->getDeviceManager().deviceManager == &recording.audioDeviceManager())
            engineDevices = &engine->getDeviceManager();
    require (engineDevices != nullptr, "find injected device's engine without a production test hook");
    auto names = recording.inputNames();
    require (! names.isEmpty(), "engine discovers synthetic input channels");
    success (recording.saveProject (root.getChildFile ("Recording").getChildFile ("Take.halftrack")), "save recording destination");
    success (recording.setTrackInput (0, names[0]), "assign synthetic input");
    success (recording.setTrackArmed (0, true), "arm actual recording destination");
    success (recording.setTrackMonitor (0, Session::MonitorMode::off), "monitor off");
    require (device->pump (10) < 0.0001f, "monitor off suppresses live input");
    success (recording.setTrackMonitor (0, Session::MonitorMode::on), "monitor on");
    require (device->pump (20) > 0.05f, "monitor on routes synthetic input to output");
    success (recording.setTrackMonitor (0, Session::MonitorMode::off), "record without monitoring");
    recording.seek (0);
    success (recording.record(), "start real recording through injected driver");
    require (recording.snapshot().recording, "transport enters recording state");
    // Cross the engine's four-second initial MIDI refresh deadline while
    // feeding 51,200 audio frames before an explicit rescan. A device-list refresh must not
    // discard a take just because the message thread runs more slowly.
    device->pump (200, 30);
    require (recording.snapshot().recording, "MIDI refresh preserves an active recording");
    engineDevices->rescanMidiDeviceList();
    device->pump (20);
    require (recording.snapshot().recording, "explicit MIDI refresh also preserves the active take");
    recording.stop();
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
    while (engineDevices->getNumMidiInDevices() == 0 && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    require (engineDevices->getNumMidiInDevices() > 0, "deferred MIDI discovery resumes after recording stops");
    auto captured = recording.snapshot();
    require (! captured.recording && ! captured.tracks[0].clips.empty(), "stop recording creates an audio clip");
    const auto recordedClip = captured.tracks[0].clips[0];
    require (recordedClip.file.existsAsFile() && recordedClip.file.isAChildOf (root), "recorded WAV is inside project assets");
    require (recordedClip.length > 0.8 && recordedClip.length < 1.3, "recording duration follows supplied callback frames");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (recordedClip.file));
    require (reader != nullptr && reader->sampleRate == 48000, "recorded media has driver sample rate");
    juce::AudioBuffer<float> samples (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
    require (reader->read (&samples, 0, samples.getNumSamples(), 0, true, true), "read recorded signal");
    require (samples.getMagnitude (0, samples.getNumSamples()) > 0.15f, "recording contains generated signal");
    success (recording.exportWav (root.getChildFile ("recorded-mix.wav")), "export captured audio through mix graph");
    success (recording.saveProject (captured.projectFile), "save recorded clip and assets");
    // Session recording: the generated input records into an empty slot from one beat to the next grid stop.
    success (recording.setLaunchQuantisation (Session::LaunchQuantisation::beat), "Session recording on the beat grid");
    recording.seek (0); success (recording.play(), "start the transport for Session recording");
    device->pump (34);
    success (recording.recordSlot (0, 0), "record the input into an empty audio slot");
    require (recording.snapshot().tracks[0].slots[0].recording, "the audio slot reports recording");
    device->pump (230);
    success (recording.recordSlot (0, 0), "end the audio take on the next beat");
    device->pump (60);
    const auto slot = recording.snapshot().tracks[0].slots[0];
    require (! slot.recording && slot.hasClip && slot.clip.isAudio && slot.clip.file.existsAsFile() && slot.clip.file.isAChildOf (root),
             "the audio take is a slot clip with its recording in the project assets");
    require (std::abs (slot.clip.length - 1) < 1e-6 && slot.clip.warp.mode != Session::WarpMode::off && slot.clip.warp.bpm == 120 && slot.playing,
             "the audio take loops over its two beats, follows the tempo and plays at once");
    std::unique_ptr<juce::AudioFormatReader> take (formats.createReaderFor (slot.clip.file));
    require (take != nullptr && std::abs ((double) take->lengthInSamples - 48000) <= 512, "the audio take holds the second between punch-in and stop");
    juce::AudioBuffer<float> takeSamples (static_cast<int> (take->numChannels), static_cast<int> (take->lengthInSamples));
    require (take->read (&takeSamples, 0, takeSamples.getNumSamples(), 0, true, true) && takeSamples.getMagnitude (0, takeSamples.getNumSamples()) > .15f,
             "the audio take contains the generated input");
    recording.stop();
    deviceChangeRecordingTests (root);
}
}

std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType()
{
    return std::make_unique<SyntheticDeviceType>();
}

float pumpSyntheticTestDevice (Session& session, int blocks)
{
    auto* device = dynamic_cast<SyntheticDevice*> (session.audioDeviceManager().getCurrentAudioDevice());
    if (device == nullptr) throw std::runtime_error ("synthetic test device is not selected");
    return device->pump (blocks);
}

float pumpSyntheticTestDeviceObserved (Session& session, int blocks, double& firstEntryMilliseconds,
                                       double& firstExitMilliseconds, float& firstPeak)
{
    auto* device = dynamic_cast<SyntheticDevice*> (session.audioDeviceManager().getCurrentAudioDevice());
    if (device == nullptr) throw std::runtime_error ("synthetic test device is not selected");
    FirstCallbackObservation observation;
    const float peak = device->pump (blocks, 2, nullptr, &observation);
    firstEntryMilliseconds = observation.entryMilliseconds;
    firstExitMilliseconds = observation.exitMilliseconds;
    firstPeak = observation.peak;
    return peak;
}

int64_t syntheticTestSamplePosition(Session& session)
{
    auto* device=dynamic_cast<SyntheticDevice*>(session.audioDeviceManager().getCurrentAudioDevice());
    if(!device) throw std::runtime_error("synthetic test device is not selected");
    return device->samplePosition();
}

juce::AudioBuffer<float> captureSyntheticTestDevice (Session& session, int blocks)
{
    auto* device=dynamic_cast<SyntheticDevice*>(session.audioDeviceManager().getCurrentAudioDevice());
    if(!device || blocks<=0) throw std::runtime_error("invalid synthetic capture request");
    juce::AudioBuffer<float> result(2,blocks*256);result.clear();device->pump(blocks,1,&result);return result;
}

int runSettingsIsolationProbe (const juce::File& report)
{
    Session session (false, std::make_unique<SyntheticDeviceType>());
    auto& devices = session.audioDeviceManager();
    auto& storage = settingsOf (session);
    auto setup = devices.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setBit (0);
    const auto error = devices.setAudioDeviceSetup (setup, true);
    // The engine stores the device setup when it receives this change message.
    devices.dispatchPendingMessages();
    auto& settings = storage.getPropertiesFile();
    const bool saved = error.isEmpty() && settings.saveIfNeeded()
        && settings.getFile().loadFileAsString().contains ("audioDeviceInChans=\"1\"");
    const juce::StringArray lines { storage.getAppPrefsFolder().getFullPathName(), settings.getFile().getFullPathName(),
                                    juce::String (activeInputs (session)), saved ? "saved" : "not saved: " + error };
    return (report.replaceWithText (lines.joinIntoString ("\n")) && saved) ? 0 : 1;
}

int runRecordingTests (const juce::File& requestedDirectory)
{
    try
    {
        const auto root = requestedDirectory.getChildFile ("recording-" + juce::Uuid().toString());
        success (root.createDirectory(), "create isolated recording artifacts");
        recordingTests (root);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << std::endl;
        return 1;
    }
}

int runSessionTests (const juce::File& requestedDirectory)
{
    try
    {
        const auto root = requestedDirectory.getChildFile ("run-" + juce::Uuid().toString());
        success (root.createDirectory(), "create isolated artifacts");
        settingsIsolationTests (root);
        panLawTests (root);
        const auto source = root.getChildFile ("fixture.wav");
        juce::AudioBuffer<float> fixture (2, 48000);
        for (int sample = 0; sample < fixture.getNumSamples(); ++sample)
        {
            fixture.setSample (0, sample, 0.2f * std::sin (juce::MathConstants<double>::twoPi * sample * 440.0 / 48000.0));
            fixture.setSample (1, sample, 0.15f * std::sin (juce::MathConstants<double>::twoPi * sample * 660.0 / 48000.0));
        }
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            require (writer && writer->writeFromAudioSampleBuffer (fixture, 0, fixture.getNumSamples()), "generate deterministic fixture");
        }
        {
            Session early (false, std::make_unique<SyntheticDeviceType>());
            require (! early.snapshot().dirty, "audio-device initialization leaves a new project clean");
            success (early.setTrackGain (0, -3), "edit immediately after session construction");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            require (early.snapshot().dirty, "deferred initialization cannot clear a user's first edit");
        }
        Session session (false);
        require (session.snapshot().tracks.size() == 2, "new project has two audio tracks");
        require (! session.snapshot().dirty, "new project is initially clean");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (650);
        require (! session.snapshot().dirty, "new project stays clean after deferred engine initialization");
        require (session.record().failed(), "record guard rejects missing device without opening microphone");
        require (session.setTrackArmed (0, true).failed(), "arming requires an assigned input");
        require (session.importAudio (root.getChildFile ("missing.wav"), 0).failed(), "missing imports fail without changing project");
        success (session.importAudio (source, 0), "import audio and collect project media");
        auto imported = session.snapshot();
        require (imported.tracks[0].clips[0].file != source && imported.tracks[0].clips[0].file.existsAsFile(), "import owns copied media at " + imported.tracks[0].clips[0].file.getFullPathName());
        success (session.exportWav (root.getChildFile ("unity.wav")), "render unity mix");
        auto unity = read (root.getChildFile ("unity.wav"));
        compare (unity, fixture, 1, 1, "unity routing");
        success (session.exportWav (root.getChildFile ("tail.wav"), 48000, 24, 2.0), "render explicit two-second effect tail");
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (root.getChildFile ("tail.wav")));
            require (reader && reader->lengthInSamples == 144000 && reader->sampleRate == 48000,
                     "one-second source plus two-second tail exports exactly three seconds");
            juce::AudioBuffer<float> tail (2, 48000);
            require (reader->read (&tail, 0, 48000, 96000, true, true) && tail.getMagnitude (0, 48000) < 0.0001f,
                     "tail renders silence after a source without effects");
        }
        auto protectedExport = root.getChildFile ("cancelled.wav");
        require (protectedExport.replaceWithText ("Existing destination must survive cancellation"), "prepare export replacement guard");
        bool dispatchedDuringExport = false;
        juce::MessageManager::callAsync ([&]
        {
            dispatchedDuringExport = true;
            session.cancelExport();
        });
        auto cancelled = session.exportWav (protectedExport, 48000, 24, 60);
        require (dispatchedDuringExport && cancelled.failed() && cancelled.getErrorMessage() == "Export cancelled.",
                 "export processes message-loop cancellation while worker renders");
        require (protectedExport.loadFileAsString() == "Existing destination must survive cancellation",
                 "cancelled export preserves existing destination bytes");

        session.beginEditGesture ("Track volume");
        success (session.setTrackGain (0, -2), "begin volume drag");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (450);
        success (session.setTrackGain (0, -8), "continue volume drag across undo timer interval");
        success (session.setTrackGain (0, -12), "finish volume drag");
        session.endEditGesture();
        session.undo();
        require (std::abs (session.snapshot().tracks[0].gainDb) < 0.001f, "one undo restores entire long volume gesture");
        session.redo();
        require (std::abs (session.snapshot().tracks[0].gainDb + 12) < 0.001f, "one redo restores complete volume gesture");
        success (session.setTrackGain (0, 0), "restore gain after gesture test");
        session.beginEditGesture ("Track pan");
        success (session.setTrackPan (0, -0.25f), "begin pan drag");
        success (session.setTrackPan (0, -0.75f), "finish pan drag");
        session.endEditGesture(); session.undo();
        require (std::abs (session.snapshot().tracks[0].pan) < 0.001f, "one undo restores entire pan gesture");
        session.beginEditGesture ("Move clip");
        success (session.moveClip (0, 0, 0.1), "begin clip drag");
        success (session.moveClip (0, 0, 0.5), "finish clip drag");
        session.endEditGesture(); session.undo();
        require (std::abs (session.snapshot().tracks[0].clips[0].start) < 0.001, "one undo restores entire clip gesture");
        session.beginEditGesture ("Loop range");
        session.setLoop (true, 0.2, 0.8); session.setLoop (true, 0.3, 0.9);
        session.endEditGesture(); session.undo();
        require (session.snapshot().loopStart == 0 && session.snapshot().loopEnd == 8,
                 "one undo restores entire loop range gesture");
        success (session.setTrackGain (0, -6.0206f), "set half gain");
        success (session.exportWav (root.getChildFile ("gain.wav")), "render changed gain");
        compare (read (root.getChildFile ("gain.wav")), unity, 0.5f, 0.5f, "gain");
        session.undo();
        require (std::abs (session.snapshot().tracks[0].gainDb) < 0.001f, "undo restores gain");
        session.redo();
        require (std::abs (session.snapshot().tracks[0].gainDb + 6.0206f) < 0.001f, "redo restores gain");
        success (session.setTrackGain (0, 0), "restore unity gain");
        success (session.setTrackMute (0, true), "mute track");
        success (session.exportWav (root.getChildFile ("muted.wav")), "render muted track");
        compare (read (root.getChildFile ("muted.wav")), unity, 0, 0, "mute");
        success (session.setTrackMute (0, false), "unmute track");
        success (session.setTrackPan (0, -1), "pan hard left");
        success (session.exportWav (root.getChildFile ("panned.wav")), "render pan routing");
        auto panned = read (root.getChildFile ("panned.wav"));
        require (panned.getMagnitude (1, 4096, 40000) < 0.0001f && panned.getMagnitude (0, 4096, 40000) > 0.1f, "hard-left pan silences only right channel");
        success (session.setTrackPan (0, 0), "centre pan");
        success (session.importAudio (source, 1), "import second track");
        success (session.setTrackSolo (0, true), "solo first track");
        success (session.exportWav (root.getChildFile ("solo.wav")), "render solo routing");
        compare (read (root.getChildFile ("solo.wav")), unity, 1, 1, "solo excludes second track");
        success (session.setTrackSolo (0, false), "clear solo");
        success (session.exportWav (root.getChildFile ("sum.wav")), "render summed tracks");
        compare (read (root.getChildFile ("sum.wav")), unity, 2, 2, "two-track summing");
        success (session.removeClip (1, 0), "remove second clip");
        session.setTempo (93);
        session.setLoop (true, 0.25, 0.75);
        success (session.renameTrack (0, "Reference tone"), "rename track");
        auto project = root.getChildFile ("Original").getChildFile ("Session.halftrack");
        success (session.saveProject (project), "save native project");
        require (! session.snapshot().dirty, "successful save clears dirty flag");
        auto portable = root.getChildFile ("Portable");
        require (project.getParentDirectory().copyDirectoryTo (portable), "copy whole project and media to new location");
        require (source.deleteFile(), "delete original import source");
        success (session.newProject(), "reset before loading portable project");
        require (project.getParentDirectory().deleteRecursively(), "remove old project location");
        success (session.loadProject (portable.getChildFile ("Session.halftrack")), "load moved self-contained project");
        auto restored = session.snapshot();
        require (restored.tracks[0].name == "Reference tone" && std::abs (restored.tempo - 93) < 0.001,
                 "track name and tempo round trip");
        require (restored.looping && std::abs (restored.loopStart - 0.25) < 0.001 && std::abs (restored.loopEnd - 0.75) < 0.001,
                 "loop settings round trip");
        require (restored.tracks[0].clips[0].file.isAChildOf (portable), "portable relative media resolves inside moved project");
        success (session.exportWav (root.getChildFile ("portable.wav")), "render restored portable project");
        compare (read (root.getChildFile ("portable.wav")), unity, 1, 1, "portable project audio round trip");
        auto invalid = root.getChildFile ("invalid.halftrack");
        invalid.replaceWithText ("<HALFTRACK version=\"999\"/>");
        require (session.loadProject (invalid).failed() && session.snapshot().tracks[0].name == "Reference tone", "invalid load retains current project");

        if (const char* pluginPath = std::getenv ("HALFTRACK_TEST_PLUGIN"))
        {
            juce::OwnedArray<juce::PluginDescription> descriptions;
            const auto pluginFile = juce::File (juce::String (pluginPath));
            require (pluginFile.exists(), "controlled VST3 bundle exists at " + pluginFile.getFullPathName());
            for (auto* format : session.pluginFormats().getFormats())
                if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, pluginFile.getFullPathName());
            require (! descriptions.isEmpty(), "scan controlled VST3 test plug-in");
            success (session.addPlugin (0, *descriptions[0]), "instantiate VST3 gain plug-in");
            success (session.setPluginParameter (0, 0, 0, 0.25f), "set non-default VST3 parameter state");
            success (session.exportWav (root.getChildFile ("plugin.wav")), "render through VST3");
            compare (read (root.getChildFile ("plugin.wav")), unity, 0.25f, 0.25f, "VST3 processing");
            auto pluginProject = portable.getChildFile ("Plugin.halftrack");
            success (session.saveProject (pluginProject), "save VST3 state");
            success (session.newProject(), "discard current plug-in graph");
            success (session.loadProject (pluginProject), "restore VST3 project state");
            success (session.exportWav (root.getChildFile ("plugin-restored.wav")), "render restored VST3 state");
            compare (read (root.getChildFile ("plugin-restored.wav")), unity, 0.25f, 0.25f, "non-default VST3 state round trip");
            success (session.setPluginEnabled (0, 0, false), "bypass VST3");
            success (session.exportWav (root.getChildFile ("plugin-bypassed.wav")), "render bypassed VST3");
            compare (read (root.getChildFile ("plugin-bypassed.wav")), unity, 1, 1, "VST3 bypass");
            success (session.removePlugin (0, 0), "remove VST3");
            require (session.snapshot().tracks[0].plugins.empty(), "plugin removal changes real graph");
        }
        else std::cout << "SKIP controlled VST3 test: HALFTRACK_TEST_PLUGIN is unset" << std::endl;
        recordingTests (root);
        std::cout << "Halftrack acceptance tests passed. Artifacts: " << root.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << std::endl;
        return 1;
    }
}
}
