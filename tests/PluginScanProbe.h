#pragma once
#include <juce_core/juce_core.h>
#include <cstdlib>
#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <unistd.h>
#endif

// Only the original test fixture's construction and test child startup use this
// protocol. It never runs in an audio callback or touches installed plug-ins.
namespace halftrack::scan_test
{
inline int processID()
{
   #if JUCE_WINDOWS
    return _getpid();
   #else
    return static_cast<int> (getpid());
   #endif
}
inline juce::File probeDirectory()
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("HALFTRACK_TEST_SCAN_PROBE", {});
    return path.isEmpty() ? juce::File() : juce::File (path);
}
inline juce::String mode()
{
    const auto root = probeDirectory();
    return root == juce::File() ? juce::String() : root.getChildFile ("mode.txt").loadFileAsString().trim();
}
inline void record (const juce::String& event, const juce::String& currentMode)
{
    const auto root = probeDirectory();
    if (root != juce::File())
        root.getChildFile ("events").getChildFile (juce::Uuid().toString() + ".txt")
            .replaceWithText (event + "\t" + juce::String (processID()) + "\t" + currentMode + "\n");
}
inline void onFixtureConstruction()
{
    const auto root = probeDirectory();
    if (root == juce::File()) return;
    const auto currentMode = mode();
    record ("construct", currentMode);
    // A forbidden parent-process fallback must be observable without terminating
    // the test host itself. Deliberate faults apply only to the scanner child.
    if (root.getChildFile ("parent-pid.txt").loadFileAsString().getIntValue() == processID()) return;
    if (currentMode == "crash") std::_Exit (71);
    if (currentMode == "block")
    {
        const auto until = juce::Time::getMillisecondCounterHiRes() + 4000.0;
        while (juce::Time::getMillisecondCounterHiRes() < until && mode() == "block")
            juce::Thread::sleep (5);
    }
}
}
