#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <optional>
#include <set>
#include <vector>

// Browser collections: seven tags, Favorites and six colours, that browser items can carry, as in
// the reference browser's Collections. An item is named by a key such as "device:12" or
// "sample:/path/file.wav". Collections are kept in one file, written atomically on each change,
// with any names and colours given to them, as the reference's labels can be renamed and coloured.
namespace halftrack
{
class Collections
{
public:
    static constexpr int count = 7;
    static juce::String defaultName (int collection);
    static juce::Colour defaultColour (int collection);
    juce::String name (int collection) const;
    juce::Colour colour (int collection) const;
    // A collection's own name, or its default again for an empty one, and its colour; each saves.
    juce::Result rename (int collection, const juce::String& name);
    juce::Result setColour (int collection, juce::Colour);

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
    std::array<juce::String, count> names;                     // Empty for the default name.
    std::array<std::optional<juce::Colour>, count> colours;
    juce::Result save() const;
};
}
