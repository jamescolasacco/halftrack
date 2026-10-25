#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Halftrack's own analysis for Convert Harmony, Melody and Drums to New MIDI Track. As the reference divides a
// clip's audio into notes at its transient markers (its manual, section 13.5), the audio is cut at its transients
// and each part becomes notes: one pitch, found by YIN, for a melody; the pitches whose harmonics its spectrum
// holds most strongly, for harmony; and a kick, snare or hi-hat by where its first 40 ms' energy lies, for drums.
// The reference's own analysis was not read.
namespace halftrack::audiotomidi
{
enum class Kind { harmony, melody, drums };
struct Note { double start = 0, length = 0; int pitch = 60; float velocity = 1; }; // In seconds from the audio's start.
// The pads a Drum Rack's usual layout plays them on: kick C1, snare D1, closed hi-hat F#1.
inline constexpr int kickNote = 36, snareNote = 38, hihatNote = 42;
// The notes in mono audio cut at its transients (frames from its start), in time order and then by pitch.
std::vector<Note> convert (const float* samples, int count, double rate, Kind, std::vector<long long> transients);
// Halftrack's own one-shot drum sounds for the converted drums' Drum Rack, mono at the rate, for a kick, snare or
// hi-hat note.
juce::AudioBuffer<float> drumSound (int note, double rate);
}
