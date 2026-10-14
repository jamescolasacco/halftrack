#include <tracktion_engine/tracktion_engine.h>
#include "PluginScanProbe.h"
#include <atomic>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>

namespace halftrack
{
namespace
{
namespace te = tracktion;
void check (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
    std::cout << "PASS scanner: " << message << std::endl;
}
void setProbeEnvironment (const char* value)
{
   #if JUCE_WINDOWS
    _putenv_s ("HALFTRACK_TEST_SCAN_PROBE", value == nullptr ? "" : value);
   #else
    if (value == nullptr) unsetenv ("HALFTRACK_TEST_SCAN_PROBE");
    else setenv ("HALFTRACK_TEST_SCAN_PROBE", value, 1);
   #endif
}
struct ProbeEnvironment
{
    explicit ProbeEnvironment (const juce::File& directory)
    {
        if (const auto* value = std::getenv ("HALFTRACK_TEST_SCAN_PROBE")) previous = value;
        setProbeEnvironment (directory.getFullPathName().toRawUTF8());
    }
    ~ProbeEnvironment() { setProbeEnvironment (previous ? previous->c_str() : nullptr); }
    std::optional<std::string> previous;
};
struct ScanProperties final : te::PropertyStorage
{
    explicit ScanProperties (juce::File directory) : PropertyStorage ("Halftrack Scanner Tests"), root (std::move (directory)) {}
    juce::File getAppPrefsFolder() override { return root; }
    juce::String getUserName() override { return "Scanner test"; }
    juce::File root;
};
struct ScanBehaviour final : te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override { return false; }
    bool addSystemAudioIODeviceTypes() override { return false; }
    bool shouldOpenAudioInputByDefault() override { return false; }
    bool canScanPluginsOutOfProcess() override { return true; }
    juce::File getPluginScanExecutable() override { return executable; }
    juce::File executable { juce::File::getSpecialLocation (juce::File::currentExecutableFile) };
};
struct ScanUI final : te::UIBehaviour
{
    void showWarningAlert (const juce::String&, const juce::String& message) override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        warnings.add (message);
    }
    juce::StringArray copyWarnings()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return warnings;
    }
    std::mutex mutex;
    juce::StringArray warnings;
};
struct Event
{
    juce::String kind, mode;
    int pid = 0;
};
std::vector<Event> events (const juce::File& root)
{
    std::vector<Event> result;
    for (const auto& file : root.getChildFile ("events").findChildFiles (juce::File::findFiles, false, "*.txt"))
    {
        const auto parts = juce::StringArray::fromTokens (file.loadFileAsString().trim(), "\t", {});
        if (parts.size() == 3) result.push_back ({ parts[0], parts[2], parts[1].getIntValue() });
    }
    return result;
}
void setMode (const juce::File& root, const juce::String& mode)
{
    check (root.getChildFile ("mode.txt").replaceWithText (mode), "select fixture mode " + mode);
}

// Exercises the public scanner from the same worker/message-thread split as
// PluginListComponent. The deadline kills only this test process on a stuck
// native scan; no unbounded join or force-killed test thread can reach CI.
void runWorker (te::PluginManager& manager, std::function<void()> work,
                std::function<void()> whileWaiting = {}, double timeoutMs = 15000.0)
{
    std::atomic<bool> done { false };
    std::exception_ptr failure;
    std::thread worker ([&]
    {
        try { work(); } catch (...) { failure = std::current_exception(); }
        done.store (true, std::memory_order_release);
    });
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (! done.load (std::memory_order_acquire) && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (whileWaiting) whileWaiting();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
    }
    if (! done.load (std::memory_order_acquire))
    {
        manager.abortCurrentPluginScan();
        const auto abortDeadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        while (! done.load (std::memory_order_acquire) && juce::Time::getMillisecondCounterHiRes() < abortDeadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        if (! done.load (std::memory_order_acquire))
        {
            std::cerr << "FAIL scanner: worker exceeded scan and cancellation deadlines" << std::endl;
            std::_Exit (2);
        }
        worker.join();
        manager.knownPluginList.scanFinished();
        throw std::runtime_error ("scanner exceeded its deadline");
    }
    worker.join();
    if (failure) std::rethrow_exception (failure);
}
}

int runPluginScanTests (const juce::File& destination)
{
    try
    {
        const auto* configured = std::getenv ("HALFTRACK_TEST_PLUGIN");
        check (configured != nullptr, "controlled VST3 fixture path is supplied");
        const auto builtPlugin = juce::File (configured);
        check (builtPlugin.isDirectory(), "controlled VST3 bundle exists");
        const auto root = destination.getChildFile ("plugin-scan-" + juce::Uuid().toString());
        check (root.getChildFile ("events").createDirectory().wasOk(), "create private scanner artifacts");
        const auto plugin = root.getChildFile (builtPlugin.getFileName());
        check (builtPlugin.copyDirectoryTo (plugin), "copy only the generated fixture into private artifacts");
        // JUCE can describe a VST3 from moduleinfo.json without loading it. Remove
        // this copy's cache so PID, crash and cancellation assertions exercise code.
        for (const auto& metadata : plugin.findChildFiles (juce::File::findFiles, true, "moduleinfo.json"))
            check (metadata.deleteFile(), "disable cached descriptions in the private fixture copy");
        check (root.getChildFile ("parent-pid.txt").replaceWithText (juce::String (scan_test::processID())), "record host process identity");
        ProbeEnvironment environment (root);
        auto ui = std::make_unique<ScanUI>();
        auto* scanUI = ui.get();
        auto behaviour = std::make_unique<ScanBehaviour>();
        auto* scanBehaviour = behaviour.get();
        te::Engine engine (std::make_unique<ScanProperties> (root), std::move (ui), std::move (behaviour));
        auto& manager = engine.getPluginManager();
        manager.setUsesSeparateProcessForScanning (true);
        check (manager.usesSeparateProcessForScanning(), "native scanner requires a separate process");
        manager.knownPluginList.clear();
        manager.knownPluginList.clearBlacklistedFiles();
        auto* format = manager.pluginFormatManager.getFormat (0);
        for (auto* candidate : manager.pluginFormatManager.getFormats())
            if (candidate->getName() == "VST3") format = candidate;
        check (format != nullptr && format->getName() == "VST3", "VST3 format is registered");
        auto scanOnce = [&]
        {
            juce::OwnedArray<juce::PluginDescription> found;
            manager.knownPluginList.scanAndAddFile (plugin.getFullPathName(), false, found, *format);
            return found.size();
        };
        auto finish = [&]
        {
            manager.knownPluginList.scanFinished();
            manager.knownPluginList.clearBlacklistedFiles();
        };

        // Deterministic regression: the scanner cannot start this nonexistent
        // private executable. The old fallback constructs the fixture here.
        scanBehaviour->executable = root.getChildFile ("nonexistent-scanner");
        check (! scanBehaviour->executable.exists(), "controlled scanner executable does not exist");
        setMode (root, "launch-denied");
        int found = -1;
        runWorker (manager, [&] { found = scanOnce(); });
        const auto rejected = events (root);
        bool sawParentConstruction = false;
        for (const auto& event : rejected)
        {
            sawParentConstruction |= event.kind == "construct" && event.pid == scan_test::processID();
        }
        check (found == 0 && ! sawParentConstruction, "failed child startup never constructs a plug-in in the host");
        check (! scanUI->copyWarnings().isEmpty(), "failed child startup reports an error");
        // Infrastructure failure is not evidence that the plug-in crashed. Keep
        // the same registry and its blacklist intact when retrying this scan.
        manager.knownPluginList.scanFinished();
        scanBehaviour->executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        setMode (root, "normal");
        runWorker (manager, [&] { found = scanOnce(); });
        check (found == 1, "same-list retry succeeds after scanner startup failure without clearing the blacklist");
        check (! manager.knownPluginList.getBlacklistedFiles().contains (plugin.getFullPathName()),
               "scanner startup failure does not classify the untested fixture as crashed");
        runWorker (manager, [&]
        {
            for (int i = 0; i < 64; ++i)
                if (scanOnce() != 1) throw std::runtime_error ("lost or mismatched IPC scan reply at request " + std::to_string (i));
        });
        check (manager.knownPluginList.getTypes().size() == 1, "64 real child scans return one stable plug-in identity");
        std::set<int> healthyChildren;
        bool allConstructorsIsolated = true;
        for (const auto& event : events (root))
            if (event.kind == "construct" && event.mode == "normal")
            {
                allConstructorsIsolated = allConstructorsIsolated && event.pid != scan_test::processID();
                healthyChildren.insert (event.pid);
            }
        check (allConstructorsIsolated && healthyChildren.size() == 1, "64 repeated replies came from one retained child, with no host construction");
        finish();

        setMode (root, "crash");
        runWorker (manager, [&] { found = scanOnce(); });
        std::set<int> crashedChildren;
        for (const auto& event : events (root))
            if (event.kind == "construct" && event.mode == "crash") crashedChildren.insert (event.pid);
        check (found == 0 && crashedChildren.size() == 2 && ! crashedChildren.contains (scan_test::processID()),
               "native scanner survives two controlled child crashes and its one retry");
        check (manager.knownPluginList.getBlacklistedFiles().contains (plugin.getFullPathName()), "crashed fixture is marked failed");
        finish();

        setMode (root, "normal");
        runWorker (manager, [&] { found = scanOnce(); });
        check (found == 1, "fresh child discovers fixture after crash recovery");
        finish();

        setMode (root, "block");
        bool cancellationSent = false;
        double cancelledAt = 0;
        runWorker (manager, [&] { found = scanOnce(); }, [&]
        {
            if (cancellationSent) return;
            for (const auto& event : events (root))
                if (event.kind == "construct" && event.mode == "block" && event.pid != scan_test::processID())
                {
                    cancellationSent = true;
                    cancelledAt = juce::Time::getMillisecondCounterHiRes();
                    manager.abortCurrentPluginScan();
                    break;
                }
        });
        check (cancellationSent && found == 0, "cancel returns while a real child is inside fixture construction");
        check (juce::Time::getMillisecondCounterHiRes() - cancelledAt < 2000.0, "cancel does not wait for the fixture's four-second stall");
        finish();
        setMode (root, "normal");
        runWorker (manager, [&] { found = scanOnce(); });
        check (found == 1, "fresh child discovers fixture after cancellation");
        finish();
        std::cout << "Scanner acceptance passed. Artifacts: " << root.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL scanner: " << error.what() << std::endl;
        return 1;
    }
}
}
