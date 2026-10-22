#include "Session.h"
#include "PluginScanProbe.h"
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

namespace halftrack
{
void useHardClipEdges();
juce::Result registerAudioUnitTestFixture();
namespace
{
namespace te = tracktion;
void check (bool value, const juce::String& message)
{
    if (! value) throw std::runtime_error (message.toStdString());
    std::cout << "PASS AUv2: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
struct ScanProperties final : te::PropertyStorage
{
    explicit ScanProperties (juce::File directory) : PropertyStorage ("Halftrack AU Tests"), root (std::move (directory)) {}
    juce::File getAppPrefsFolder() override { return root; }
    juce::String getUserName() override { return "AU test"; }
    juce::File root;
};
struct ScanBehaviour final : te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override { return false; }
    bool addSystemAudioIODeviceTypes() override { return false; }
    bool shouldOpenAudioInputByDefault() override { return false; }
    bool canScanPluginsOutOfProcess() override { return true; }
};
struct ScanUI final : te::UIBehaviour
{
    void showWarningAlert (const juce::String&, const juce::String& message) override
    { std::cerr << "AU test scanner: " << message << std::endl; }
};
struct ProbeEnvironment
{
    explicit ProbeEnvironment (const juce::File& directory)
    {
        if (const auto* value = std::getenv ("HALFTRACK_TEST_SCAN_PROBE")) previous = value;
        setenv ("HALFTRACK_TEST_SCAN_PROBE", directory.getFullPathName().toRawUTF8(), 1);
    }
    ~ProbeEnvironment()
    {
        if (previous) setenv ("HALFTRACK_TEST_SCAN_PROBE", previous->c_str(), 1);
        else unsetenv ("HALFTRACK_TEST_SCAN_PROBE");
    }
    std::optional<std::string> previous;
};
juce::PluginDescription scanFixture (const juce::File& root)
{
    ok (root.getChildFile ("events").createDirectory(), "create isolated scan evidence");
    te::Engine engine (std::make_unique<ScanProperties> (root), std::make_unique<ScanUI>(), std::make_unique<ScanBehaviour>());
    auto& manager = engine.getPluginManager();
    // JUCE identifies registered AUv2s by component codes, not arbitrary paths.
    const juce::String identifier = "AudioUnit:Effects/aufx,Htgn,Htrk";
    juce::AudioPluginFormat* format = nullptr;
    for (auto* candidate : manager.pluginFormatManager.getFormats())
        if (candidate->getName() == "AudioUnit") format = candidate;
    check (format != nullptr && format->fileMightContainThisPluginType (identifier), "process-local component is discoverable as an Audio Unit");
    juce::PluginDescription candidate; candidate.fileOrIdentifier = identifier;
    check (! format->requiresUnblockedMessageThreadDuringCreation (candidate), "registered fixture uses synchronous AUv2 creation");
    check (root.getChildFile ("parent-pid.txt").replaceWithText (juce::String (scan_test::processID()))
           && root.getChildFile ("mode.txt").replaceWithText ("au-normal"), "prepare controlled child scan");
    ProbeEnvironment environment (root);
    manager.setUsesSeparateProcessForScanning (true);
    manager.knownPluginList.removeFromBlacklist (identifier);
    check (manager.usesSeparateProcessForScanning(), "AU scanner uses a separate process");
    juce::OwnedArray<juce::PluginDescription> found;
    std::exception_ptr failure;
    std::atomic<bool> done { false };
    std::thread worker ([&]
    {
        try { manager.knownPluginList.scanAndAddFile (identifier, false, found, *format); }
        catch (...) { failure = std::current_exception(); }
        done.store (true, std::memory_order_release);
    });
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 20000.0;
    while (! done.load (std::memory_order_acquire) && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
    if (! done.load (std::memory_order_acquire))
    {
        manager.abortCurrentPluginScan();
        const auto abortDeadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        while (! done.load (std::memory_order_acquire) && juce::Time::getMillisecondCounterHiRes() < abortDeadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        if (! done.load (std::memory_order_acquire))
        {
            std::cerr << "FAIL AUv2: scanner did not finish or abort" << std::endl;
            std::_Exit (2);
        }
        worker.join();
        manager.knownPluginList.scanFinished();
        throw std::runtime_error ("AU scanner exceeded deadline");
    }
    worker.join();
    manager.knownPluginList.scanFinished();
    if (failure) std::rethrow_exception (failure);
    check (found.size() == 1 && found[0]->pluginFormatName == "AudioUnit"
           && found[0]->fileOrIdentifier == identifier && found[0]->numInputChannels == 2
           && found[0]->numOutputChannels == 2, "isolated scanner returns one stereo AUv2 description");
    int childConstructions = 0, parentConstructions = 0;
    for (const auto& file : root.getChildFile ("events").findChildFiles (juce::File::findFiles, false, "*.txt"))
    {
        const auto parts = juce::StringArray::fromTokens (file.loadFileAsString().trim(), "\t", {});
        if (parts.size() != 3 || parts[0] != "construct" || parts[2] != "au-normal") continue;
        if (parts[1].getIntValue() == scan_test::processID()) ++parentConstructions;
        else ++childConstructions;
    }
    check (childConstructions > 0 && parentConstructions == 0, "AU scan constructs the fixture only in the child process");
    return *found[0];
}
juce::AudioBuffer<float> read (const juce::File& file, int frames)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->sampleRate == 48000 && reader->numChannels == 2
           && reader->lengthInSamples == frames, "render has exact stereo frame count and sample rate");
    juce::AudioBuffer<float> result (2, frames);
    check (reader->read (&result, 0, frames, 0, true, true), "read entire AU render");
    return result;
}
void compare (const juce::AudioBuffer<float>& actual, const juce::AudioBuffer<float>& expected,
              float gain, const juce::String& name)
{
    check (actual.getNumChannels() == expected.getNumChannels() && actual.getNumSamples() == expected.getNumSamples(), name + " dimensions");
    float maximum = 0;
    bool finite = true;
    for (int channel = 0; channel < actual.getNumChannels(); ++channel)
        for (int sample = 0; sample < actual.getNumSamples(); ++sample)
        {
            const auto value = actual.getSample (channel, sample);
            const auto reference = expected.getSample (channel, sample) * gain;
            finite = finite && std::isfinite (value) && std::isfinite (reference);
            maximum = std::max (maximum, std::abs (value - reference));
        }
    check (finite && maximum < 0.000002f, name + " every sample finite and aligned; max error " + juce::String (maximum, 9));
}
}

int runAudioUnitTests (const juce::File& destination)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto configured = juce::SystemStats::getEnvironmentVariable ("HALFTRACK_TEST_AU_PLUGIN", {});
        check (configured.isNotEmpty() && juce::File (configured).exists(), "controlled AUv2 fixture is supplied");
        ok (registerAudioUnitTestFixture(), "process-local fixture registration is idempotent");
        const auto root = destination.getChildFile ("au-v2-" + juce::Uuid().toString());
        ok (root.createDirectory(), "create private AU artifacts");
        const auto description = scanFixture (root.getChildFile ("scan"));
        Session session (false);
        constexpr int frames = 48000;
        juce::AudioBuffer<float> reference (2, frames);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < frames; ++sample)
                reference.setSample (channel, sample, static_cast<float> ((sample * 37 + channel * 13) % 97 - 48) / 512.0f);
        const auto source = root.getChildFile ("source.wav");
        {
            juce::WavAudioFormat format;
            std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer && writer->writeFromAudioSampleBuffer (reference, 0, frames), "write independent varying stereo stimulus including both clip edges");
        }
        reference = read (source, frames);
        ok (session.importAudio (source, 0), "import AU stimulus");
        auto render = [&] (float gain, const juce::String& name)
        {
            const auto target = root.getChildFile (name + ".wav");
            ok (session.exportWav (target), "export " + name);
            compare (read (target, frames), reference, gain, name);
        };
        render (1, "dry");
        ok (session.addPlugin (0, description), "insert actual AUv2 processor");
        render (0.5f, "default-gain");
        ok (session.setPluginParameter (0, 0, 0, 0.25f), "change AU gain through production parameter API");
        render (0.25f, "quarter-gain");
        const auto project = root.getChildFile ("Session.halftrack");
        ok (session.saveProject (project), "save non-default AU state");
        ok (session.newProject(), "destroy previous AU graph");
        ok (session.loadProject (project), "reopen saved AU project");
        check (session.snapshot().tracks[0].plugins.size() == 1, "reopened graph contains the external processor");
        render (0.25f, "restored-gain");
        ok (session.setPluginEnabled (0, 0, false), "bypass AU processor");
        render (1, "bypassed");
        ok (session.setPluginEnabled (0, 0, true), "enable AU processor again");
        render (0.25f, "enabled-again");
        ok (session.removePlugin (0, 0), "remove AU processor");
        check (session.snapshot().tracks[0].plugins.empty(), "AU removal changes the real graph");
        render (1, "removed");
        std::cout << "Controlled AUv2 acceptance passed. Artifacts: " << root.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL AUv2: " << error.what() << std::endl;
        return 1;
    }
}
}
