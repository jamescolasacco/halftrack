#include "PluginBinary.h"
#include "Session.h"
#include <iostream>
#include <stdexcept>

// Plug-in architectures: headers written by the test for thin and universal Mach-O, PE and ELF
// binaries name the architectures they hold, and other files name none. A bundle built only for
// another architecture than this process's is explained (what it is built for, what Halftrack
// runs as, what to do), as is inserting it; a bundle that holds this process's architecture is not.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS plugin binary: " << text << std::endl; }
void put32 (juce::MemoryOutputStream& out, uint32_t v, bool big)
{ if (big) out.writeIntBigEndian ((int) v); else out.writeInt ((int) v); }
juce::File write (const juce::File& file, const juce::MemoryBlock& bytes)
{
    if (! file.getParentDirectory().createDirectory().wasOk() || ! file.replaceWithData (bytes.getData(), bytes.getSize())) throw std::runtime_error ("could not write " + file.getFullPathName().toStdString());
    return file;
}
juce::MemoryBlock machO (uint32_t cpu)
{
    juce::MemoryOutputStream out; put32 (out, 0xFEEDFACF, false); put32 (out, cpu, false); for (int i = 0; i < 6; ++i) put32 (out, 0, false);
    return out.getMemoryBlock();
}
juce::MemoryBlock universal (std::initializer_list<uint32_t> cpus)
{
    juce::MemoryOutputStream out; put32 (out, 0xCAFEBABE, true); put32 (out, (uint32_t) cpus.size(), true);
    for (const auto cpu : cpus) { put32 (out, cpu, true); put32 (out, 0, true); put32 (out, 4096, true); put32 (out, 0, true); put32 (out, 12, true); }
    return out.getMemoryBlock();
}
juce::MemoryBlock pe (uint16_t machine)
{
    juce::MemoryBlock bytes (0x90, true);
    auto* p = static_cast<uint8_t*> (bytes.getData());
    p[0] = 'M'; p[1] = 'Z'; p[0x3C] = 0x80;
    p[0x80] = 'P'; p[0x81] = 'E'; p[0x84] = (uint8_t) (machine & 0xFF); p[0x85] = (uint8_t) (machine >> 8);
    return bytes;
}
juce::MemoryBlock elf (uint16_t machine)
{
    juce::MemoryBlock bytes (64, true);
    auto* p = static_cast<uint8_t*> (bytes.getData());
    p[0] = 0x7F; p[1] = 'E'; p[2] = 'L'; p[3] = 'F'; p[4] = 2; p[5] = 1; p[18] = (uint8_t) (machine & 0xFF); p[19] = (uint8_t) (machine >> 8);
    return bytes;
}
// A plug-in bundle laid out as this platform's VST3 bundles are, holding one binary.
juce::File bundle (const juce::File& folder, const juce::String& name, const juce::MemoryBlock& binary, const juce::String& architecture)
{
    const auto root = folder.getChildFile (name + ".vst3");
   #if JUCE_MAC
    juce::ignoreUnused (architecture);
    write (root.getChildFile ("Contents").getChildFile ("MacOS").getChildFile (name), binary);
   #elif JUCE_WINDOWS
    write (root.getChildFile ("Contents").getChildFile (architecture == "arm64" ? "arm64-win" : "x86_64-win").getChildFile (name + ".vst3"), binary);
   #else
    write (root.getChildFile ("Contents").getChildFile (architecture == "arm64" ? "aarch64-linux" : "x86_64-linux").getChildFile (name + ".so"), binary);
   #endif
    return root;
}
juce::MemoryBlock nativeBinary (const juce::String& architecture)
{
   #if JUCE_MAC
    return machO (architecture == "arm64" ? 0x0100000C : 0x01000007);
   #elif JUCE_WINDOWS
    return pe (architecture == "arm64" ? 0xAA64 : 0x8664);
   #else
    return elf (architecture == "arm64" ? 183 : 62);
   #endif
}
}

int runPluginBinaryTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("plugin-binary-" + juce::Uuid().toString()); check (root.createDirectory().wasOk(), "create fixtures");
        using namespace pluginbinary;
        check (architectures (write (root.getChildFile ("arm.dylib"), machO (0x0100000C))) == juce::StringArray { "arm64" }, "a thin arm64 Mach-O");
        check (architectures (write (root.getChildFile ("intel.dylib"), machO (0x01000007))) == juce::StringArray { "x86_64" }, "a thin x86_64 Mach-O");
        check (architectures (write (root.getChildFile ("both.dylib"), universal ({ 0x01000007, 0x0100000C }))) == juce::StringArray { "x86_64", "arm64" }, "a universal Mach-O");
        check (architectures (write (root.getChildFile ("x64.dll"), pe (0x8664))) == juce::StringArray { "x86_64" }
                   && architectures (write (root.getChildFile ("arm.dll"), pe (0xAA64))) == juce::StringArray { "arm64" }, "PE binaries for x64 and Arm64");
        check (architectures (write (root.getChildFile ("x64.so"), elf (62))) == juce::StringArray { "x86_64" }
                   && architectures (write (root.getChildFile ("arm.so"), elf (183))) == juce::StringArray { "arm64" }, "ELF binaries for x86-64 and AArch64");
        juce::MemoryBlock text ("not a binary at all", 19);
        check (architectures (write (root.getChildFile ("readme.txt"), text)).isEmpty() && architectures (root.getChildFile ("missing")).isEmpty(), "other files and missing ones name none");

        const auto host = hostArchitecture();
        check (host == "arm64" || host == "x86_64", "this process runs as " + host);
        const auto other = host == "arm64" ? juce::String ("x86_64") : juce::String ("arm64");
        const auto foreign = bundle (root, "Foreign Probe", nativeBinary (other), other);
        const auto native = bundle (root, "Native Probe", nativeBinary (host), host);
        check (binaryOf (foreign).existsAsFile() && architectures (binaryOf (foreign)) == juce::StringArray { other }, "the bundle's binary is found inside it");
        const auto problem = architectureProblem (foreign, "Foreign Probe");
        check (problem.contains ("Foreign Probe") && problem.contains (other) && problem.contains (host) && problem.contains ("install a version"),
               "a bundle built for " + other + " only is explained (" + problem + ")");
        check (architectureProblem (native, "Native Probe").isEmpty(), "one built for " + host + " is not");

        // Inserting it says why it cannot load.
        Session s (false);
        juce::PluginDescription description;
        description.name = "Foreign Probe"; description.pluginFormatName = "VST3"; description.fileOrIdentifier = foreign.getFullPathName(); description.manufacturerName = "Halftrack Tests";
        const auto inserted = s.addPlugin (0, description);
        check (inserted.failed() && inserted.getErrorMessage() == problem, "inserting it fails with the explanation");
        std::cout << "Plugin binary passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL plugin binary: " << e.what() << std::endl; return 1; }
}
}
