#pragma once

#include "Session.h"

// The metronome's sounds, Halftrack's own, made to the character of the reference's three
// (Classic, Click and Wood): each a plain tick and a higher accented one, synthesised offline as a
// mono buffer the engine's click track plays. The same sound, accent and rate always give the same
// samples.
namespace halftrack::metronome
{
juce::AudioBuffer<float> tick (Session::MetronomeSound, bool accent, double sampleRate);
// The tick's fundamental, in Hz, and its peak level.
double tickFrequency (Session::MetronomeSound, bool accent);
float tickPeak (Session::MetronomeSound, bool accent);
}
