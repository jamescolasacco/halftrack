#pragma once

#include "Session.h"

// The Spectrum device's analysis: a Hann-windowed FFT of one block, in dBFS. A full-scale sine
// centred on a bin reads 0 dB there. It runs on the message thread over frames the device copied
// from the audio thread.
namespace halftrack::spectrum
{
inline constexpr std::array<int, 5> blockSizes { 1024, 2048, 4096, 8192, 16384 };
inline constexpr float floorDb = -200;
// Magnitudes of bins 0 to count/2; `count` must be one of blockSizes.
std::vector<float> analyse (const float* samples, int count);
}
