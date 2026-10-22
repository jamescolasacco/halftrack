#pragma once

#include <juce_core/juce_core.h>

namespace halftrack::pluginbinary
{
// The CPU architectures a plug-in binary holds ("arm64", "x86_64", "i386", "arm"), read from its
// Mach-O (thin or universal), PE or ELF header. Empty when the file is none of these.
juce::StringArray architectures (const juce::File& binary);
// The binary inside a plug-in bundle (a .vst3 or .component folder), or the file itself.
juce::File binaryOf (const juce::File& plugin);
// This process's architecture, in the same names.
juce::String hostArchitecture();
// Why a plug-in cannot run in this process, when its binary lacks this process's architecture:
// the plug-in, what it is built for, and what to do. Empty when it can, or cannot be told.
juce::String architectureProblem (const juce::File& plugin, const juce::String& name);
}
