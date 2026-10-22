#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <optional>
#include <vector>

namespace halftrack::tempo
{
// A sound's tempo from its content, not its length, for warping long samples.
//
// The onset curve is how much a 1024-point spectrum rises at each step of 10.7 ms. Its
// autocorrelation, read as a comb over four beats, picks the tempo between the minimum and the
// maximum. A tempo outside that range shows there as its double or half. The beat grid is the
// period and phase on which the onsets of the whole sound line up best, with the bass's onsets
// counting as much as all the others, so kicks rather than hats mark the beat. A line through the
// transients Beats warping finds on the grid's beats then sets the tempo to 0.01 BPM. The first
// beat is the grid's first strong onset, on its transient.
struct Estimate
{
    double bpm = 0;
    double firstBeatSeconds = 0;
    // How much more the tempo's comb holds than the median tempo's, from 1: steady beats give 2 or more.
    double confidence = 0;
};

class OnsetCurve
{
public:
    explicit OnsetCurve (double sampleRate);
    ~OnsetCurve();
    void add (const float* const* channels, int numChannels, int numSamples);
    // Adds at most the first maximumSeconds of a file, a block at a time; false if it could not be read.
    bool add (juce::AudioFormatReader&, double maximumSeconds = 600);
    // One value a step, the first centred on the sound's start.
    const std::vector<float>& values() const noexcept { return curve; }
    // The same below 200 Hz.
    const std::vector<float>& bassValues() const noexcept { return bass; }
    double secondsPerValue() const noexcept { return hop / rate; }
    // The transients Beats warping finds, in seconds, as they arrive.
    const std::vector<double>& transients() const noexcept { return transientSeconds; }
    double seconds() const noexcept { return (double) samples / rate; }
private:
    struct Spectrum;
    std::unique_ptr<Spectrum> spectrum;
    double rate;
    int hop, size, bassBins;
    std::vector<float> curve, bass, window, recent, previous;
    std::vector<double> transientSeconds;
    long long samples = 0;
    int filled = 0;
};

// Nothing when the sound has no steady beat to find.
std::optional<Estimate> estimate (const OnsetCurve&, double minimumBpm = 80, double maximumBpm = 160);
std::optional<Estimate> estimate (const juce::AudioBuffer<float>&, double sampleRate, double minimumBpm = 80, double maximumBpm = 160);
// Reads at most the first maximumSeconds of a file, a block at a time.
std::optional<Estimate> estimate (juce::AudioFormatReader&, double maximumSeconds = 600, double minimumBpm = 80, double maximumBpm = 160);
}
