#include "Collections.h"

namespace halftrack
{
juce::String Collections::defaultName (int collection)
{
    static const char* defaults[count] { "Favorites", "Red", "Yellow", "Green", "Blue", "Purple", "Grey" };
    return juce::isPositiveAndBelow (collection, count) ? defaults[collection] : juce::String();
}

juce::Colour Collections::defaultColour (int collection)
{
    static const juce::uint32 defaults[count] { 0xffffa63d, 0xffe5534b, 0xffe8cf4a, 0xff6cc66a, 0xff4f9de8, 0xffa77be0, 0xff9a9a9a };
    return juce::Colour (juce::isPositiveAndBelow (collection, count) ? defaults[collection] : 0xff9a9a9a);
}

juce::String Collections::name (int collection) const
{
    if (! juce::isPositiveAndBelow (collection, count)) return {};
    return names[(size_t) collection].isNotEmpty() ? names[(size_t) collection] : defaultName (collection);
}

juce::Colour Collections::colour (int collection) const
{
    if (! juce::isPositiveAndBelow (collection, count)) return defaultColour (collection);
    return colours[(size_t) collection].value_or (defaultColour (collection));
}

juce::Result Collections::rename (int collection, const juce::String& newName)
{
    if (! juce::isPositiveAndBelow (collection, count)) return juce::Result::fail ("Choose a collection.");
    const auto trimmed = newName.trim();
    if (trimmed.length() > 64) return juce::Result::fail ("A collection's name is at most 64 characters.");
    const auto previous = names;
    names[(size_t) collection] = trimmed == defaultName (collection) ? juce::String() : trimmed;
    if (auto saved = save(); saved.failed()) { names = previous; return saved; }
    return juce::Result::ok();
}

juce::Result Collections::setColour (int collection, juce::Colour newColour)
{
    if (! juce::isPositiveAndBelow (collection, count)) return juce::Result::fail ("Choose a collection.");
    const auto previous = colours;
    colours[(size_t) collection] = newColour.withAlpha (1.0f) == defaultColour (collection) ? std::nullopt : std::optional<juce::Colour> (newColour.withAlpha (1.0f));
    if (auto saved = save(); saved.failed()) { colours = previous; return saved; }
    return juce::Result::ok();
}

juce::Result Collections::load (const juce::File& source)
{
    std::vector<std::pair<juce::String, std::set<int>>> loaded;
    std::array<juce::String, count> loadedNames;
    std::array<std::optional<juce::Colour>, count> loadedColours;
    if (source.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse (source);
        if (xml == nullptr || ! xml->hasTagName ("HALFTRACKCOLLECTIONS")) return juce::Result::fail ("The collections file could not be read.");
        for (auto* collection : xml->getChildWithTagNameIterator ("COLLECTION"))
        {
            const auto index = collection->getStringAttribute ("index");
            if (! index.containsOnly ("0123456789") || index.isEmpty() || ! juce::isPositiveAndBelow (index.getIntValue(), count)) continue;
            loadedNames[(size_t) index.getIntValue()] = collection->getStringAttribute ("name").trim().substring (0, 64);
            if (const auto hex = collection->getStringAttribute ("colour"); hex.length() == 8 && hex.containsOnly ("0123456789abcdefABCDEF"))
                loadedColours[(size_t) index.getIntValue()] = juce::Colour::fromString (hex);
        }
        for (auto* item : xml->getChildWithTagNameIterator ("ITEM"))
        {
            const auto key = item->getStringAttribute ("key");
            std::set<int> tags;
            for (const auto& token : juce::StringArray::fromTokens (item->getStringAttribute ("collections"), ",", {}))
                if (const int tag = token.getIntValue(); juce::isPositiveAndBelow (tag, count) && token.containsOnly ("0123456789")) tags.insert (tag);
            if (key.isNotEmpty() && ! tags.empty()) loaded.emplace_back (key, tags);
        }
    }
    file = source; items = std::move (loaded); names = loadedNames; colours = loadedColours;
    return juce::Result::ok();
}

juce::Result Collections::toggle (const juce::String& key, int collection)
{
    if (key.isEmpty() || ! juce::isPositiveAndBelow (collection, count)) return juce::Result::fail ("Choose an item and a collection.");
    const auto previous = items;
    auto at = std::find_if (items.begin(), items.end(), [&key] (const auto& item) { return item.first == key; });
    if (at == items.end()) items.emplace_back (key, std::set<int> { collection });
    else if (! at->second.erase (collection)) at->second.insert (collection);
    else if (at->second.empty()) items.erase (at);
    if (auto saved = save(); saved.failed()) { items = previous; return saved; }
    return juce::Result::ok();
}

bool Collections::contains (const juce::String& key, int collection) const
{
    for (const auto& item : items) if (item.first == key) return item.second.count (collection) > 0;
    return false;
}

juce::StringArray Collections::keys (int collection) const
{
    juce::StringArray result;
    for (const auto& item : items) if (item.second.count (collection)) result.add (item.first);
    return result;
}

std::vector<int> Collections::collectionsOf (const juce::String& key) const
{
    for (const auto& item : items) if (item.first == key) return { item.second.begin(), item.second.end() };
    return {};
}

juce::Result Collections::save() const
{
    if (file == juce::File()) return juce::Result::ok(); // Without a user library they last for this run.
    juce::XmlElement xml ("HALFTRACKCOLLECTIONS");
    for (int i = 0; i < count; ++i)
        if (names[(size_t) i].isNotEmpty() || colours[(size_t) i])
        {
            auto* collection = xml.createNewChildElement ("COLLECTION");
            collection->setAttribute ("index", i);
            if (names[(size_t) i].isNotEmpty()) collection->setAttribute ("name", names[(size_t) i]);
            if (colours[(size_t) i]) collection->setAttribute ("colour", colours[(size_t) i]->toString());
        }
    for (const auto& [key, tags] : items)
    {
        juce::StringArray list; for (auto tag : tags) list.add (juce::String (tag));
        auto* item = xml.createNewChildElement ("ITEM");
        item->setAttribute ("key", key); item->setAttribute ("collections", list.joinIntoString (","));
    }
    if (auto r = file.getParentDirectory().createDirectory(); r.failed()) return r;
    juce::TemporaryFile temporary (file);
    if (! xml.writeTo (temporary.getFile()) || ! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("The collections could not be saved to " + file.getFullPathName() + ".");
    return juce::Result::ok();
}
}
