#pragma once

#include "Session.h"

// MIDI Monitor's text: message descriptions, note names with C3 as note 60, and the chord that
// held notes spell. Everything here runs on the message thread.
namespace halftrack::midimonitor
{
juce::String noteName (int note);
// Such as "Note On C3 100", "CC 1 64" or "Pitch Bend +512", followed by the channel.
juce::String describe (const Session::MidiMonitorMessage&);
// The chord the pitches spell, such as "C", "Am7" or "C/E", named from the lowest note when it can
// be the root. One pitch class gives its name; an empty string means no chord matches.
juce::String chordName (const std::vector<int>& pitches);
}
