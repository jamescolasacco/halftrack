#pragma once

#include <juce_core/juce_core.h>

namespace halftrack::buildinfo
{
// The version, from the project's CMake version ("0.1.0").
juce::String version();
// The source commit the build came from ("ad1f541"), with "-modified" when tracked files had
// uncommitted changes, or "unknown" without Git history.
juce::String commit();
// The operating system and this process's architecture ("macOS arm64").
juce::String platform();
// One line naming all three: "Halftrack 0.1.0 (ad1f541, macOS arm64)".
juce::String summary();
}
