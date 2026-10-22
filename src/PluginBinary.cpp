#include "PluginBinary.h"

namespace halftrack::pluginbinary
{
namespace
{
uint32_t little32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint32_t big32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; }
uint16_t little16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
uint16_t big16 (const uint8_t* p) { return (uint16_t) ((p[0] << 8) | p[1]); }
juce::String machOName (uint32_t cpu)
{
    switch (cpu)
    {
        case 0x0100000C: return "arm64";
        case 0x01000007: return "x86_64";
        case 7: return "i386";
        case 12: return "arm";
        default: return {};
    }
}
juce::String friendly (const juce::String& architecture)
{
    if (architecture == "x86_64") return "Intel (x86_64)";
    if (architecture == "i386") return "32-bit Intel (i386)";
    if (architecture == "arm64") return "Arm (arm64)";
    return architecture;
}
}

juce::StringArray architectures (const juce::File& binary)
{
    juce::MemoryBlock head;
    if (! binary.existsAsFile()) return {};
    {
        juce::FileInputStream in (binary);
        if (! in.openedOk()) return {};
        in.readIntoMemoryBlock (head, 4096);
    }
    const auto* p = static_cast<const uint8_t*> (head.getData());
    const auto size = head.getSize();
    juce::StringArray found;
    if (size < 8) return found;
    const auto magicBig = big32 (p), magicLittle = little32 (p);
    if (magicBig == 0xCAFEBABE || magicBig == 0xCAFEBABF)
    {
        // A universal binary: each slice's CPU type, big-endian.
        const auto count = big32 (p + 4);
        const size_t stride = magicBig == 0xCAFEBABF ? 32 : 20;
        if (count > 0 && count < 32)
            for (uint32_t i = 0; i < count && 8 + (i + 1) * stride <= size; ++i)
                if (const auto name = machOName (big32 (p + 8 + i * stride)); name.isNotEmpty()) found.addIfNotAlreadyThere (name);
        return found;
    }
    if (magicLittle == 0xFEEDFACF || magicLittle == 0xFEEDFACE)
    {
        if (const auto name = machOName (little32 (p + 4)); name.isNotEmpty()) found.add (name);
        return found;
    }
    if (p[0] == 'M' && p[1] == 'Z' && size >= 0x40)
    {
        const auto at = little32 (p + 0x3C);
        if (at + 6 <= size && p[at] == 'P' && p[at + 1] == 'E' && p[at + 2] == 0 && p[at + 3] == 0)
        {
            const auto machine = little16 (p + at + 4);
            if (machine == 0x8664) found.add ("x86_64");
            else if (machine == 0xAA64) found.add ("arm64");
            else if (machine == 0x14C) found.add ("i386");
        }
        return found;
    }
    if (p[0] == 0x7F && p[1] == 'E' && p[2] == 'L' && p[3] == 'F' && size >= 20)
    {
        const auto machine = p[5] == 2 ? big16 (p + 18) : little16 (p + 18);
        if (machine == 62) found.add ("x86_64");
        else if (machine == 183) found.add ("arm64");
        else if (machine == 3) found.add ("i386");
        else if (machine == 40) found.add ("arm");
    }
    return found;
}

juce::File binaryOf (const juce::File& plugin)
{
    if (! plugin.isDirectory()) return plugin;
    const auto contents = plugin.getChildFile ("Contents");
    // macOS bundles keep the binary in Contents/MacOS.
    if (const auto macos = contents.getChildFile ("MacOS"); macos.isDirectory())
    {
        if (const auto named = macos.getChildFile (plugin.getFileNameWithoutExtension()); named.existsAsFile()) return named;
        if (const auto files = macos.findChildFiles (juce::File::findFiles, false); ! files.isEmpty()) return files[0];
    }
    // VST3 bundles on Windows and Linux keep one per architecture folder.
    for (const auto* folder : { "x86_64-win", "arm64-win", "arm64ec-win", "x86-win", "x86_64-linux", "aarch64-linux", "i386-linux" })
        if (const auto dir = contents.getChildFile (folder); dir.isDirectory())
            if (const auto files = dir.findChildFiles (juce::File::findFiles, false); ! files.isEmpty()) return files[0];
    return {};
}

juce::String hostArchitecture()
{
   #if defined (__aarch64__) || defined (_M_ARM64)
    return "arm64";
   #elif defined (__x86_64__) || defined (_M_X64)
    return "x86_64";
   #elif defined (__i386__) || defined (_M_IX86)
    return "i386";
   #else
    return {};
   #endif
}

juce::String architectureProblem (const juce::File& plugin, const juce::String& name)
{
    const auto host = hostArchitecture();
    // A Windows or Linux VST3 bundle holds a folder per architecture: this process's is the one that counts.
    juce::StringArray held;
    if (plugin.isDirectory() && ! plugin.getChildFile ("Contents").getChildFile ("MacOS").isDirectory())
    {
        for (const auto* folder : { "x86_64-win", "arm64-win", "arm64ec-win", "x86-win", "x86_64-linux", "aarch64-linux", "i386-linux" })
            if (const auto dir = plugin.getChildFile ("Contents").getChildFile (folder); dir.isDirectory())
                for (const auto& file : dir.findChildFiles (juce::File::findFiles, false))
                    for (const auto& a : architectures (file)) held.addIfNotAlreadyThere (a);
    }
    else held = architectures (binaryOf (plugin));
    if (host.isEmpty() || held.isEmpty() || held.contains (host)) return {};
    juce::StringArray built;
    for (const auto& a : held) built.add (friendly (a));
    return "\"" + name + "\" is built for " + built.joinIntoString (" and ") + " only, and Halftrack runs as " + friendly (host)
         + ". Halftrack does not bridge plug-ins between architectures: install a version of the plug-in built for " + friendly (host) + ".";
}
}
