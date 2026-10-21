#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace halftrack
{
// The computer MIDI keyboard's keys, after the reference. A S D F G H J K L ; ' play the white keys
// from C, and W E T Y U O P the black keys between them, over an octave and a half. Z and X move
// the octave down and up, so the A key plays from C-2 (note 0) to C8 (note 120); C and V move the
// velocity down and up in steps of 20, from 1 to 127. A key held keeps the note it began until it
// is released, whatever the octave has become meanwhile.
class ComputerKeyboard
{
public:
    struct Event { int note = 0; float velocity = 0; bool on = false; bool operator== (const Event&) const = default; };

    // The note a key plays from the A key's note, or -1 for other keys.
    static int offsetOf (juce::juce_wchar key) noexcept
    {
        static constexpr const char* keys = "awsedftgyhujkolp;'";
        const auto lower = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (key);
        for (int i = 0; keys[i] != 0; ++i) if (lower == (juce::juce_wchar) keys[i]) return i;
        return -1;
    }
    static bool isControl (juce::juce_wchar key) noexcept
    { const auto lower = juce::CharacterFunctions::toLowerCase (key); return lower == 'z' || lower == 'x' || lower == 'c' || lower == 'v'; }
    static bool handles (juce::juce_wchar key) noexcept { return offsetOf (key) >= 0 || isControl (key); }

    // A key pressed, by its key code (the one isDown is later asked about) and character. A note key
    // not already held gives its note-on; Z, X, C and V change the octave or velocity.
    std::optional<Event> press (int keyCode, juce::juce_wchar character)
    {
        const auto lower = juce::CharacterFunctions::toLowerCase (character);
        if (lower == 'z') { octaveNumber = std::max (-2, octaveNumber - 1); return std::nullopt; }
        if (lower == 'x') { octaveNumber = std::min (8, octaveNumber + 1); return std::nullopt; }
        if (lower == 'c') { velocityValue = velocityValue >= 127 ? 120 : std::max (1, velocityValue - 20); return std::nullopt; }
        if (lower == 'v') { velocityValue = velocityValue <= 1 ? 20 : std::min (127, velocityValue + 20); return std::nullopt; }
        const int offset = offsetOf (character);
        if (offset < 0 || held.count (keyCode) != 0) return std::nullopt;
        const int note = (octaveNumber + 2) * 12 + offset;
        if (note > 127) return std::nullopt;
        held[keyCode] = note;
        return Event { note, (float) velocityValue / 127.0f, true };
    }
    // Note-offs for the held keys that isDown says are no longer down.
    std::vector<Event> release (const std::function<bool (int keyCode)>& isDown)
    {
        std::vector<Event> ended;
        for (auto it = held.begin(); it != held.end();)
            if (! isDown (it->first)) { ended.push_back ({ it->second, 0, false }); it = held.erase (it); }
            else ++it;
        return ended;
    }
    // Note-offs for every held key, as when the keyboard is turned off.
    std::vector<Event> releaseAll() { return release ([] (int) { return false; }); }
    int octave() const noexcept { return octaveNumber; }   // The A key plays C of this octave (C3 is note 60).
    int velocity() const noexcept { return velocityValue; }
    bool holding() const noexcept { return ! held.empty(); }

private:
    std::map<int, int> held; // Key code to the note it began.
    int octaveNumber = 3, velocityValue = 100;
};
}
