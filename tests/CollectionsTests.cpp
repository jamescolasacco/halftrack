#include "Collections.h"
#include <iostream>
#include <stdexcept>

// Browser collections: toggling items in and out, their order, saving and reloading, and refusing
// a malformed file or an unwritable one without losing what is there.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS collections: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
}

int runCollectionsTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("collections-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto file = root.getChildFile ("Library").getChildFile ("Collections.xml");
        Collections c;
        ok (c.load (file), "load collections that do not exist yet");
        check (c.keys (0).isEmpty() && Collections::name (0) == "Favorites" && Collections::name (7).isEmpty(), "they start empty, with Favorites first");
        ok (c.toggle ("device:3", 0), "add a device to Favorites");
        ok (c.toggle ("sample:/library/kick.wav", 0), "and a sample");
        ok (c.toggle ("device:3", 2), "put the device in Yellow too");
        check (c.keys (0) == juce::StringArray { "device:3", "sample:/library/kick.wav" } && c.collectionsOf ("device:3") == std::vector<int> { 0, 2 },
               "a collection lists its items in the order added, and an item knows its collections");
        check (file.existsAsFile(), "the collections are saved as they change");
        Collections reloaded;
        ok (reloaded.load (file), "reload them");
        check (reloaded.keys (0) == c.keys (0) && reloaded.contains ("device:3", 2), "they reload as saved");
        ok (c.toggle ("device:3", 0), "take the device out of Favorites");
        check (! c.contains ("device:3", 0) && c.contains ("device:3", 2), "it stays in Yellow");
        ok (c.toggle ("device:3", 2), "and out of Yellow");
        check (c.collectionsOf ("device:3").empty() && c.keys (0).size() == 1, "an item in no collection is forgotten");
        check (c.toggle ({}, 0).failed() && c.toggle ("device:3", 7).failed(), "an empty key or a collection past the seventh is refused");

        const auto bad = root.getChildFile ("Bad.xml");
        check (bad.replaceWithText ("<NOT_COLLECTIONS/>"), "write a file that is not collections");
        check (c.load (bad).failed() && c.keys (0).size() == 1, "loading it is refused, keeping the collections");
       #if ! JUCE_WINDOWS
        check (file.getParentDirectory().setReadOnly (true, false), "make the library unwritable");
        const auto refused = c.toggle ("device:5", 1);
        file.getParentDirectory().setReadOnly (false, false);
        check (refused.failed() && ! c.contains ("device:5", 1), "a change that cannot be saved is refused and undone");
       #endif
        std::cout << "Collections passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL collections: " << e.what() << std::endl; return 1; }
}
}
