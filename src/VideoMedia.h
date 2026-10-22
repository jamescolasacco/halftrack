#pragma once

#include <juce_graphics/juce_graphics.h>

// Movies, after the reference's video support: a movie's picture plays beside the Arrangement and
// its sound becomes an audio clip. This reads and writes movies through the system's media
// framework (AVFoundation on macOS); elsewhere movies are not supported.
namespace halftrack::video
{
bool supported();
// QuickTime and MPEG-4 movies, by their extension.
bool isMovie (const juce::File&);
struct Info
{
    double seconds = 0, frameRate = 0;
    int width = 0, height = 0;
    bool hasSound = false;
};
juce::Result probe (const juce::File& movie, Info&);
// The movie's sound as a 24-bit WAV at its own rate, or silence of the movie's length when it has none.
juce::Result extractSound (const juce::File& movie, const juce::File& wav);
// A QuickTime movie of the picture from `fromSeconds` for `seconds` (no longer than the movie has),
// with `sound` (an audio file) as its sound. The picture is copied, not re-encoded.
juce::Result writeWithSound (const juce::File& movie, double fromSeconds, double seconds, const juce::File& sound, const juce::File& output);
// For tests: a movie of up to 64 frames, each showing its number as six black or white stripes (the
// lowest bit on the left), with a sine as its sound.
juce::Result writeTestMovie (const juce::File& movie, int frames, double frameRate, int width, int height, double toneHz, double sampleRate);
// The number a test movie's frame shows at a time, read from its stripes.
juce::Result testFrameAt (const juce::File& movie, double seconds, int& frame);
}
