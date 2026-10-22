#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <optional>
#include <vector>

namespace halftrack::mackie
{
// The Mackie Control Universal protocol, as surfaces that speak it (and the reference's support for
// them) use it: eight channel strips of fader, V-Pot, Rec/Solo/Mute/Select buttons and a scribble
// strip, a master fader and transport buttons. This is the protocol only; the Session acts on it.
enum class Button
{
    recArm, solo, mute, select, // Strip buttons, with the strip.
    vpotPush,                  // A V-Pot pressed, with the strip.
    bankLeft, bankRight, channelLeft, channelRight,
    cycle, click, rewind, fastForward, stop, play, record,
    faderTouch,                // A fader touched, with the strip (8 for the master).
    other
};

struct Action
{
    enum class Kind { fader, vpot, button, jog };
    Kind kind = Kind::button;
    int strip = 0;            // 0-7, or 8 for the master fader.
    float position = 0;       // A fader's position, 0-1.
    int ticks = 0;            // A V-Pot's or the jog wheel's turn: positive clockwise.
    Button button = Button::other;
    bool pressed = false;
};

// What a surface message asks for, or nothing for a message the protocol does not use.
std::optional<Action> parse (const juce::MidiMessage&);

// Feedback to the surface.
juce::MidiMessage faderPosition (int strip, float position);            // Moves a motorised fader.
juce::MidiMessage buttonLight (Button, int strip, bool on);              // Lights a button.
juce::MidiMessage vpotRing (int strip, float pan);                       // A V-Pot ring showing a pan, -1 to 1.
juce::MidiMessage meter (int strip, float peak);                         // A strip's level meter, from a 0-1 peak.
// The scribble strips' text: two rows of 56 characters, 7 to a strip.
juce::MidiMessage display (int row, int column, const juce::String& text);
juce::String stripText (const juce::String& name); // A name fitted to a strip's 6 characters and a space.

// The note number of a button (strip buttons add their strip), or -1.
int noteFor (Button, int strip = 0);
}
