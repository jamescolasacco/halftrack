#include <juce_core/juce_core.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

namespace halftrack
{
namespace
{
template<typename T> struct CFHandle
{
    T value = nullptr;
    ~CFHandle() { if (value) CFRelease (value); }
};
CFStringRef stringValue (CFDictionaryRef dictionary, CFStringRef key)
{
    const auto value = CFDictionaryGetValue (dictionary, key);
    return value && CFGetTypeID (value) == CFStringGetTypeID() ? static_cast<CFStringRef> (value) : nullptr;
}
bool isString (CFStringRef value, CFStringRef expected)
{ return value && CFEqual (value, expected); }
}

// AudioComponentRegister is explicitly process-local (Apple AudioComponent.h).
// The generated fixture stays in the build directory. Its bundle remains loaded
// until process shutdown because registered factory pointers have no unregister
// operation. Normal launches are inert when the test-only environment is absent.
juce::Result registerAudioUnitTestFixture()
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("HALFTRACK_TEST_AU_PLUGIN", {});
    if (path.isEmpty()) return juce::Result::ok();
    static CFHandle<CFBundleRef> fixture;
    if (fixture.value) return juce::Result::ok();
    const juce::File file (path);
    if (! file.isDirectory() || ! file.hasFileExtension ("component"))
        return juce::Result::fail ("Controlled Audio Unit bundle does not exist.");
    CFHandle<CFURLRef> url { CFURLCreateFromFileSystemRepresentation (kCFAllocatorDefault,
        reinterpret_cast<const UInt8*> (path.toRawUTF8()), static_cast<CFIndex> (path.getNumBytesAsUTF8()), true) };
    CFHandle<CFBundleRef> bundle { url.value ? CFBundleCreate (kCFAllocatorDefault, url.value) : nullptr };
    if (! bundle.value || ! isString (CFBundleGetIdentifier (bundle.value), CFSTR ("org.halftrack.testgain")))
        return juce::Result::fail ("Unexpected Audio Unit test bundle identity.");
    auto* info = CFBundleGetInfoDictionary (bundle.value);
    const auto entries = info ? CFDictionaryGetValue (info, CFSTR ("AudioComponents")) : nullptr;
    if (! entries || CFGetTypeID (entries) != CFArrayGetTypeID() || CFArrayGetCount (static_cast<CFArrayRef> (entries)) != 1)
        return juce::Result::fail ("Audio Unit fixture must describe exactly one component.");
    const auto entry = CFArrayGetValueAtIndex (static_cast<CFArrayRef> (entries), 0);
    if (! entry || CFGetTypeID (entry) != CFDictionaryGetTypeID())
        return juce::Result::fail ("Invalid Audio Unit component metadata.");
    const auto metadata = static_cast<CFDictionaryRef> (entry);
    const auto factoryName = stringValue (metadata, CFSTR ("factoryFunction"));
    const auto name = stringValue (metadata, CFSTR ("name"));
    const auto versionValue = CFDictionaryGetValue (metadata, CFSTR ("version"));
    int64_t version = 0;
    if (! isString (stringValue (metadata, CFSTR ("type")), CFSTR ("aufx"))
        || ! isString (stringValue (metadata, CFSTR ("subtype")), CFSTR ("Htgn"))
        || ! isString (stringValue (metadata, CFSTR ("manufacturer")), CFSTR ("Htrk"))
        || ! factoryName || ! name || ! versionValue || CFGetTypeID (versionValue) != CFNumberGetTypeID()
        || ! CFNumberGetValue (static_cast<CFNumberRef> (versionValue), kCFNumberSInt64Type, &version)
        || version < 0 || version > 0xffffffffLL)
        return juce::Result::fail ("Unexpected Audio Unit test component description.");
    CFHandle<CFErrorRef> loadError;
    if (! CFBundleLoadExecutableAndReturnError (bundle.value, &loadError.value))
        return juce::Result::fail ("Could not load controlled Audio Unit executable.");
    auto factory = reinterpret_cast<AudioComponentFactoryFunction> (CFBundleGetFunctionPointerForName (bundle.value, factoryName));
    if (! factory) return juce::Result::fail ("Controlled Audio Unit factory symbol is missing.");
    AudioComponentDescription description {};
    description.componentType = kAudioUnitType_Effect;
    description.componentSubType = 'Htgn';
    description.componentManufacturer = 'Htrk';
    if (CFDictionaryGetValue (metadata, CFSTR ("sandboxSafe")) == kCFBooleanTrue)
        description.componentFlags = kAudioComponentFlag_SandboxSafe;
    if (! AudioComponentRegister (&description, name, static_cast<UInt32> (version), factory))
        return juce::Result::fail ("Process-local Audio Unit registration failed.");
    fixture.value = bundle.value;
    bundle.value = nullptr;
    return juce::Result::ok();
}
}
