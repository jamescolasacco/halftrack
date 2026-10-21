#pragma once

#include <tracktion_engine/tracktion_engine.h>

// Halftrack's warp modes for the engine's time stretcher, after the reference's: Beats plays the
// segments between transients each at its own speed, Tones and Complex Pro stretch with
// Signalsmith Stretch (Tones at a chosen grain size, Complex Pro keeping formants when
// transposed), and Texture stretches with overlapping grains. Each mode's settings travel in the
// stretch options' four Halftrack values, stored so that zeros are the defaults.
namespace halftrack::warp
{
// Registers the stretchers with the engine; safe to call more than once.
void registerStretchers();

// Beats: how a segment's tail plays once its audio runs out, and 100 - Transient Envelope.
namespace beats { constexpr int loopMode = 1, fade = 2; }
// Tones and Texture: Grain Size 1-100 (0 for the mode's default) and Texture's Flux 0-100.
namespace grains { constexpr int size = 0, flux = 1; }
// Complex Pro: 100 - Formants, the share of a transposition that moves the formants.
namespace complexPro { constexpr int formantShift = 0; }

enum class TransientLoop { off, forward, backAndForth };

// The transients Beats warping and slicing find: a jump of the fast envelope well above the
// recent average, at least 30 ms after the last.
class TransientDetector
{
public:
    explicit TransientDetector (double rate);
    void reset();
    // One frame's level, the mean of its channels' magnitudes; true when a transient starts there.
    bool next (float level, long long frame);
    // Counts a transient at a frame, as the start of the audio is.
    void mark (long long frame) { started = true; last = frame; }
private:
    float fast = 0, slow = 0, fastRelease = 0, slowCoefficient = 0;
    long long shortest = 0, last = 0;
    bool started = false;
};
// The transients of a whole buffer after its start, as frames from its start.
std::vector<long long> findTransients (const juce::AudioBuffer<float>&, double rate);
}
