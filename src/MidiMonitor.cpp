#include "MidiMonitor.h"
#include <algorithm>

namespace halftrack::midimonitor
{
namespace
{
constexpr const char* classNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
struct Quality { const char* suffix; std::initializer_list<int> intervals; };
// Each chord's intervals above its root; a set of pitch classes matches at most one.
const Quality qualities[] {
    { "", { 0, 4, 7 } }, { "m", { 0, 3, 7 } }, { "dim", { 0, 3, 6 } }, { "aug", { 0, 4, 8 } },
    { "sus2", { 0, 2, 7 } }, { "sus4", { 0, 5, 7 } }, { "5", { 0, 7 } },
    { "7", { 0, 4, 7, 10 } }, { "maj7", { 0, 4, 7, 11 } }, { "m7", { 0, 3, 7, 10 } }, { "mMaj7", { 0, 3, 7, 11 } },
    { "m7b5", { 0, 3, 6, 10 } }, { "dim7", { 0, 3, 6, 9 } }, { "7sus4", { 0, 5, 7, 10 } }, { "6", { 0, 4, 7, 9 } }, { "m6", { 0, 3, 7, 9 } },
    { "add9", { 0, 2, 4, 7 } }, { "9", { 0, 2, 4, 7, 10 } }, { "maj9", { 0, 2, 4, 7, 11 } }, { "m9", { 0, 2, 3, 7, 10 } } };
int maskOf (std::initializer_list<int> intervals) { int mask = 0; for (const int i : intervals) mask |= 1 << i; return mask; }
juce::String channelText (int status) { return " ch " + juce::String ((status & 0x0f) + 1); }
}

juce::String noteName (int note)
{
    note = std::clamp (note, 0, 127);
    return juce::String (classNames[note % 12]) + juce::String (note / 12 - 2);
}

juce::String describe (const Session::MidiMonitorMessage& m)
{
    const int type = m.status & 0xf0;
    switch (type)
    {
        case 0x90: if (m.data2 > 0) return "Note On " + noteName (m.data1) + " " + juce::String (m.data2) + channelText (m.status); [[fallthrough]];
        case 0x80: return "Note Off " + noteName (m.data1) + channelText (m.status);
        case 0xa0: return "Poly Pressure " + noteName (m.data1) + " " + juce::String (m.data2) + channelText (m.status);
        case 0xb0: return "CC " + juce::String (m.data1) + " " + juce::String (m.data2) + channelText (m.status);
        case 0xc0: return "Program " + juce::String (m.data1 + 1) + channelText (m.status);
        case 0xd0: return "Pressure " + juce::String (m.data1) + channelText (m.status);
        case 0xe0:
        {
            const int bend = (m.data1 | (m.data2 << 7)) - 8192;
            return "Pitch Bend " + juce::String (bend > 0 ? "+" : "") + juce::String (bend) + channelText (m.status);
        }
        default: return "System " + juce::String::toHexString (m.status).toUpperCase();
    }
}

juce::String chordName (const std::vector<int>& pitches)
{
    if (pitches.empty()) return {};
    int classes = 0;
    for (const int p : pitches) classes |= 1 << (((p % 12) + 12) % 12);
    const int bass = ((*std::min_element (pitches.begin(), pitches.end()) % 12) + 12) % 12;
    if ((classes & (classes - 1)) == 0) return classNames[bass];
    // The bass first, then the other pitch classes upward from it.
    for (int step = 0; step < 12; ++step)
    {
        const int root = (bass + step) % 12;
        if (((classes >> root) & 1) == 0) continue;
        const int relative = ((classes >> root) | (classes << (12 - root))) & 0xfff;
        for (const auto& quality : qualities)
            if (maskOf (quality.intervals) == relative)
                return juce::String (classNames[root]) + quality.suffix + (root == bass ? juce::String() : "/" + juce::String (classNames[bass]));
    }
    return {};
}
}
