#include "BuildInfo.h"
#include "HalftrackBuildInfo.h"
#include "PluginBinary.h"

namespace halftrack::buildinfo
{
juce::String version() { return HALFTRACK_VERSION; }
juce::String commit() { return HALFTRACK_COMMIT; }
juce::String platform()
{
   #if JUCE_MAC
    const juce::String system ("macOS");
   #elif JUCE_WINDOWS
    const juce::String system ("Windows");
   #else
    const juce::String system ("Linux");
   #endif
    return (system + " " + pluginbinary::hostArchitecture()).trim();
}
juce::String summary() { return "Halftrack " + version() + " (" + commit() + ", " + platform() + ")"; }
}
