#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <set>
#include <vector>

// Browser collections: seven tags, Favorites and six colours, that browser items can carry, as in
// the reference browser's Collections. An item is named by a key such as "device:12" or
// "sample:/path/file.wav". Collections are kept in one file, written atomically on each change.
namespace halftrack
{
class Collections
{
public:
    static constexpr int count = 7;
    static juce::String name (int collection);
    static juce::Colour colour (int collection);

    // Reads the file, keeping the collections empty if it is missing; an unreadable file is refused
    // and leaves the collections as they were.
    juce::Result load (const juce::File&);
    // Adds the item to the collection or takes it out, and saves.
    juce::Result toggle (const juce::String& key, int collection);
    bool contains (const juce::String& key, int collection) const;
    // The collection's items, in the order they were added.
    juce::StringArray keys (int collection) const;
    std::vector<int> collectionsOf (const juce::String& key) const;

private:
    juce::File file;
    std::vector<std::pair<juce::String, std::set<int>>> items; // In the order they were first tagged.
    juce::Result save() const;
};
}
