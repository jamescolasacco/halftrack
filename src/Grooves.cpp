#include "Grooves.h"
#include <cmath>

namespace halftrack
{
namespace grooves
{
namespace
{
bool finite (double v, double low, double high) noexcept { return std::isfinite (v) && v >= low && v <= high; }
uint64_t mix (uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
}

double baseBeats (Session::GrooveBase base) noexcept
{
    switch (base)
    {
        case Session::GrooveBase::quarter: return 1;
        case Session::GrooveBase::eighth: return .5;
        case Session::GrooveBase::eighthTriplet: return 1.0 / 3;
        case Session::GrooveBase::sixteenth: return .25;
        case Session::GrooveBase::sixteenthTriplet: return 1.0 / 6;
        case Session::GrooveBase::thirtySecond: return .125;
    }
    return .25;
}

bool valid (const Session::GrooveInfo& g) noexcept
{
    if (g.name.trim().isEmpty() || g.name.length() > 128) return false;
    if (static_cast<int> (g.base) < 0 || static_cast<int> (g.base) >= Session::grooveBaseCount) return false;
    if (! finite (g.lengthBeats, 1.0 / 960, 1024)) return false;
    if (! finite (g.quantize, 0, 100) || ! finite (g.timing, 0, 100) || ! finite (g.random, 0, 100) || ! finite (g.velocity, -100, 100)) return false;
    if (g.events.size() > (size_t) Session::maximumGrooveEvents) return false;
    double previous = 0;
    for (const auto& e : g.events)
    {
        if (! finite (e.beat, previous, g.lengthBeats) || e.beat >= g.lengthBeats || ! finite (e.velocity, 1, 127)) return false;
        previous = e.beat;
    }
    return true;
}

Pattern::Pattern (const Session::GrooveInfo& g, float globalAmountPercent)
{
    step = baseBeats (g.base);
    const double amount = juce::jlimit (0.0f, Session::maximumGrooveAmount, globalAmountPercent) / 100.0;
    quantize = g.quantize / 100.0;
    timing = g.timing / 100.0 * amount;
    random = g.random / 100.0 * amount;
    velocity = g.velocity / 100.0 * amount;
    const auto count = (size_t) std::max<long long> (1, std::llround (g.lengthBeats / step));
    slots.resize (count);
    float loudest = 1;
    for (const auto& e : g.events) loudest = std::max (loudest, e.velocity);
    for (const auto& e : g.events)
    {
        const auto line = std::llround (e.beat / step);
        auto& slot = slots[(size_t) (((line % (long long) count) + (long long) count) % (long long) count)];
        const auto offset = e.beat - (double) line * step;
        // Two events on one line: the closer one defines it.
        if (slot.used && std::abs (slot.offset) <= std::abs (offset)) continue;
        slot = { true, offset, e.velocity / loudest - 1.0 };
    }
}

bool Pattern::neutral() const noexcept
{
    if (quantize != 0 || random != 0) return false;
    for (const auto& slot : slots)
        if (slot.used && ((timing != 0 && slot.offset != 0) || (velocity != 0 && slot.accent != 0))) return false;
    return true;
}

Pattern::Placement Pattern::place (double beat, int noteVelocity, uint64_t seed) const noexcept
{
    const auto line = std::llround (beat / step);
    const auto count = (long long) slots.size();
    const auto& slot = slots[(size_t) (((line % count) + count) % count)];
    auto position = beat + quantize * ((double) line * step - beat);
    double level = noteVelocity;
    if (slot.used)
    {
        position += timing * slot.offset;
        level *= 1.0 + velocity * slot.accent;
    }
    if (random != 0)
    {
        const auto unit = (double) (mix (seed) >> 11) * (1.0 / 9007199254740992.0); // [0, 1)
        position += random * (unit * 2 - 1) * step * .5;
    }
    return { position, juce::jlimit (1, 127, (int) std::lround (level)) };
}

uint64_t noteSeed (uint64_t clip, int pitch, double beat) noexcept
{
    return mix (mix (clip) ^ ((uint64_t) (uint32_t) pitch << 48) ^ (uint64_t) std::llround (beat * 3840.0));
}

Session::GrooveInfo extract (const juce::String& name, const std::vector<Session::MidiNoteInfo>& notes, double lengthBeats)
{
    Session::GrooveInfo g;
    g.name = name.trim().isEmpty() ? juce::String ("Groove") : name.trim().substring (0, 128);
    g.lengthBeats = juce::jlimit (1.0 / 960, 1024.0, lengthBeats);
    std::vector<Session::GrooveEvent> events;
    for (const auto& n : notes)
        if (n.active && n.startBeat >= 0 && n.startBeat < g.lengthBeats)
            events.push_back ({ n.startBeat, juce::jlimit (1.0f, 127.0f, n.velocity * 127.0f) });
    std::sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; });
    for (const auto& e : events)
    {
        if (! g.events.empty() && e.beat - g.events.back().beat < 1.0 / 960) g.events.back().velocity = std::max (g.events.back().velocity, e.velocity);
        else if ((int) g.events.size() < Session::maximumGrooveEvents) g.events.push_back (e);
    }
    return g;
}

std::vector<Session::GrooveInfo> library()
{
    std::vector<Session::GrooveInfo> result;
    const std::array<std::pair<Session::GrooveBase, const char*>, 6> grids {{ { Session::GrooveBase::quarter, "1/4" },
        { Session::GrooveBase::eighth, "1/8" }, { Session::GrooveBase::eighthTriplet, "1/8T" }, { Session::GrooveBase::sixteenth, "1/16" },
        { Session::GrooveBase::sixteenthTriplet, "1/16T" }, { Session::GrooveBase::thirtySecond, "1/32" } }};
    // Quantize grooves pull notes fully onto their grid and move nothing else.
    for (const auto& [base, label] : grids)
    {
        Session::GrooveInfo g; g.name = juce::String ("Quantize ") + label; g.base = base; g.quantize = 100; g.timing = 0;
        result.push_back (std::move (g));
    }
    // Swing grooves: every second grid line lands at the given percentage of the pair it starts
    // (below 75%, where it would be measured against the next pair instead).
    const std::array<std::pair<Session::GrooveBase, const char*>, 3> swings {{ { Session::GrooveBase::eighth, "8th" },
        { Session::GrooveBase::sixteenth, "16th" }, { Session::GrooveBase::thirtySecond, "32nd" } }};
    for (const auto& [base, label] : swings)
        for (const int percent : { 54, 58, 62, 66, 71 })
        {
            Session::GrooveInfo g; g.name = juce::String (label) + " Swing " + juce::String (percent); g.base = base;
            const auto pair = baseBeats (base) * 2;
            const auto late = percent == 66 ? 2.0 / 3 : percent / 100.0;
            for (int i = 0; i * pair < g.lengthBeats - 1e-9; ++i)
            {
                g.events.push_back ({ i * pair, 127 });
                g.events.push_back ({ i * pair + pair * late, 127 });
            }
            result.push_back (std::move (g));
        }
    return result;
}
}

std::vector<Session::GrooveInfo> Session::grooveLibrary() { return grooves::library(); }

const char* Session::grooveBaseName (GrooveBase base) noexcept
{
    switch (base)
    {
        case GrooveBase::quarter: return "1/4";
        case GrooveBase::eighth: return "1/8";
        case GrooveBase::eighthTriplet: return "1/8T";
        case GrooveBase::sixteenth: return "1/16";
        case GrooveBase::sixteenthTriplet: return "1/16T";
        case GrooveBase::thirtySecond: return "1/32";
    }
    return "1/16";
}

juce::Result Session::readGrooveFile (const juce::File& file, GrooveInfo& result)
{
    juce::MemoryBlock data;
    if (file.getSize() > 64 * 1024 * 1024 || ! file.loadFileAsData (data)) return juce::Result::fail ("The groove file could not be read.");
    juce::String text;
    if (data.getSize() > 2 && (uint8_t) data[0] == 0x1f && (uint8_t) data[1] == 0x8b)
    {
        juce::MemoryInputStream compressed (data, false);
        juce::GZIPDecompressorInputStream unzipped (&compressed, false, juce::GZIPDecompressorInputStream::gzipFormat);
        text = unzipped.readEntireStreamAsString();
    }
    else text = data.toString();
    const auto fail = juce::Result::fail ("This is not a groove file Halftrack can read.");
    auto xml = juce::parseXML (text);
    auto* groove = xml != nullptr && xml->hasTagName ("Ableton") ? xml->getChildByName ("Groove") : nullptr;
    if (groove == nullptr) return fail;
    const auto value = [] (const juce::XmlElement* parent, const char* tag) -> juce::String
    {
        auto* child = parent != nullptr ? parent->getChildByName (tag) : nullptr;
        return child != nullptr ? child->getStringAttribute ("Value") : juce::String();
    };
    const auto number = [] (const juce::String& text, double& out)
    {
        const auto s = text.trim().toStdString(); char* end = nullptr;
        out = std::strtod (s.c_str(), &end);
        return ! s.empty() && end == s.c_str() + s.size() && std::isfinite (out);
    };
    auto* clip = groove->getChildByName ("Clip");
    clip = clip != nullptr ? clip->getChildByName ("Value") : nullptr;
    clip = clip != nullptr ? clip->getChildByName ("MidiClip") : nullptr;
    auto* loop = clip != nullptr ? clip->getChildByName ("Loop") : nullptr;
    double loopStart = 0, loopEnd = 0, grid = 0, quantize = 0, timing = 0, random = 0, velocity = 0;
    if (loop == nullptr || ! number (value (loop, "LoopStart"), loopStart) || ! number (value (loop, "LoopEnd"), loopEnd)
        || ! number (value (groove, "Grid"), grid) || ! number (value (groove, "QuantizationAmount"), quantize) || ! number (value (groove, "TimingAmount"), timing)
        || ! number (value (groove, "RandomAmount"), random) || ! number (value (groove, "VelocityAmount"), velocity)
        || grid != std::floor (grid) || grid < 0 || grid >= grooveBaseCount)
        return fail;
    GrooveInfo g;
    g.name = value (groove, "Name").trim();
    if (g.name.isEmpty()) g.name = file.getFileNameWithoutExtension();
    g.name = g.name.substring (0, 128);
    g.base = static_cast<GrooveBase> ((int) grid);
    g.lengthBeats = loopEnd - loopStart;
    g.quantize = (float) quantize; g.timing = (float) timing; g.random = (float) random; g.velocity = (float) velocity;
    std::vector<GrooveEvent> events;
    if (auto* notes = clip->getChildByName ("Notes"))
        if (auto* keys = notes->getChildByName ("KeyTracks"))
            for (auto* key : keys->getChildWithTagNameIterator ("KeyTrack"))
                if (auto* list = key->getChildByName ("Notes"))
                    for (auto* note : list->getChildWithTagNameIterator ("MidiNoteEvent"))
                    {
                        double time = 0, level = 0;
                        if (! number (note->getStringAttribute ("Time"), time) || ! number (note->getStringAttribute ("Velocity"), level)) return fail;
                        if (note->getStringAttribute ("IsEnabled", "true") == "false") continue;
                        if (time - loopStart >= 0 && time - loopStart < g.lengthBeats)
                            events.push_back ({ time - loopStart, juce::jlimit (1.0f, 127.0f, (float) level) });
                    }
    std::sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; });
    for (const auto& e : events)
        if (! g.events.empty() && e.beat - g.events.back().beat < 1.0 / 960) g.events.back().velocity = std::max (g.events.back().velocity, e.velocity);
        else g.events.push_back (e);
    if (! grooves::valid (g)) return juce::Result::fail ("The groove file's settings are out of range.");
    result = std::move (g);
    return juce::Result::ok();
}
}
