#pragma once

#include "Session.h"

// The Tuner's pitch detection: YIN (de Cheveigné and Kawahara, 2002) with parabolic refinement of
// the period, for monophonic input between 30 Hz and 4.2 kHz. It runs on the message thread over
// frames the Tuner device copied from the audio thread.
namespace halftrack::tuner
{
inline constexpr double minimumHz = 30, maximumHz = 4200;
inline constexpr int analysisFrames = 4096;
// Nothing for silence (below -80 dBFS RMS), aperiodic input or a pitch outside the range.
std::optional<Session::TunerReading> detect (const float* samples, int count, double sampleRate, double referenceHz);
// A MIDI note's name with its octave (69 is A4), spelled with sharps or flats.
juce::String noteName (int note, bool flats = false);
}
