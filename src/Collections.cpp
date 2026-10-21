#include "Collections.h"

namespace halftrack
{
juce::String Collections::name (int collection)
{
    static const char* names[count] { "Favorites", "Red", "Yellow", "Green", "Blue", "Purple", "Grey" };
    return juce::isPositiveAndBelow (collection, count) ? names[collection] : juce::String();
}

juce::Colour Collections::colour (int collection)
{
    static const juce::uint32 colours[count] { 0xffffa63d, 0xffe5534b, 0xffe8cf4a, 0xff6cc66a, 0xff4f9de8, 0xffa77be0, 0xff9a9a9a };
    return juce::Colour (juce::isPositiveAndBelow (collection, count) ? colours[collection] : 0xff9a9a9a);
}

juce::Result Collections::load (const juce::File& source)
{
    std::vector<std::pair<juce::String, std::set<int>>> loaded;
    if (source.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse (source);
        if (xml == nullptr || ! xml->hasTagName ("HALFTRACKCOLLECTIONS")) return juce::Result::fail ("The collections file could not be read.");
        for (auto* item : xml->getChildWithTagNameIterator ("ITEM"))
        {
            const auto key = item->getStringAttribute ("key");
            std::set<int> tags;
            for (const auto& token : juce::StringArray::fromTokens (item->getStringAttribute ("collections"), ",", {}))
                if (const int tag = token.getIntValue(); juce::isPositiveAndBelow (tag, count) && token.containsOnly ("0123456789")) tags.insert (tag);
            if (key.isNotEmpty() && ! tags.empty()) loaded.emplace_back (key, tags);
        }
    }
    file = source; items = std::move (loaded);
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
