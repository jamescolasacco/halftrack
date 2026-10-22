#include "VideoMedia.h"

// Movies need a platform media framework; only macOS's is used so far.
namespace halftrack::video
{
namespace
{
juce::Result unsupported() { return juce::Result::fail ("Movies are supported on macOS only."); }
}
bool supported() { return false; }
bool isMovie (const juce::File& file) { return file.hasFileExtension ("mov;mp4;m4v"); }
juce::Result probe (const juce::File&, Info&) { return unsupported(); }
juce::Result extractSound (const juce::File&, const juce::File&) { return unsupported(); }
juce::Result writeWithSound (const juce::File&, double, double, const juce::File&, const juce::File&) { return unsupported(); }
juce::Result writeTestMovie (const juce::File&, int, double, int, int, double, double) { return unsupported(); }
juce::Result testFrameAt (const juce::File&, double, int&) { return unsupported(); }
}
