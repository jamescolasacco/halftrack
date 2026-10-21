#include "Spectrum.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace halftrack::spectrum
{
std::vector<float> analyse (const float* samples, int count)
{
    if (samples == nullptr || std::find (blockSizes.begin(), blockSizes.end(), count) == blockSizes.end()) return {};
    const int order = juce::roundToInt (std::log2 ((double) count));
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) count * 2, 0.0f);
    // A periodic Hann window; its coherent gain of 1/2 is divided out below.
    for (int n = 0; n < count; ++n)
        data[(size_t) n] = samples[n] * (float) (.5 - .5 * std::cos (2 * juce::MathConstants<double>::pi * n / count));
    fft.performFrequencyOnlyForwardTransform (data.data(), true);
    std::vector<float> result ((size_t) count / 2 + 1);
    for (int k = 0; k <= count / 2; ++k)
    {
        // A sine of amplitude a centred on bin k gives |X| = a·N/4 after the window.
        const double amplitude = data[(size_t) k] * 4.0 / count;
        result[(size_t) k] = amplitude > 0 ? std::max (floorDb, (float) (20 * std::log10 (amplitude))) : floorDb;
    }
    return result;
}
}
