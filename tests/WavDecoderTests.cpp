#include "Session.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Exports checked by a decoder independent of the library that wrote them. A track plays a 0.5
// amplitude 440 Hz sine; each export is parsed here from its RIFF bytes: the RIFF and WAVE tags, the
// fmt chunk's tag, channels, rate, byte rate, block align and bit depth, and a data chunk of whole
// frames. Its samples are decoded by hand and compared with the sine computed directly, within
// the format's quantization.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS wav decoder: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
uint32_t u32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t u16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
struct Wav
{
    int format = 0, channels = 0, rate = 0, byteRate = 0, blockAlign = 0, bits = 0;
    std::vector<std::vector<double>> samples; // By channel.
};
Wav parse (const juce::File& file, const juce::String& name)
{
    juce::MemoryBlock bytes; check (file.loadFileAsData (bytes), "load " + name);
    const auto* data = static_cast<const uint8_t*> (bytes.getData());
    const auto size = bytes.getSize();
    check (size >= 12 && std::memcmp (data, "RIFF", 4) == 0 && std::memcmp (data + 8, "WAVE", 4) == 0 && u32 (data + 4) == size - 8,
           name + " is a RIFF WAVE file whose size field matches its length");
    Wav wav;
    const uint8_t* samples = nullptr; size_t sampleBytes = 0;
    for (size_t at = 12; at + 8 <= size;)
    {
        const auto chunk = u32 (data + at + 4);
        check (at + 8 + chunk <= size, name + " chunks fit in the file");
        if (std::memcmp (data + at, "fmt ", 4) == 0)
        {
            const auto* f = data + at + 8;
            wav.format = u16 (f); wav.channels = u16 (f + 2); wav.rate = (int) u32 (f + 4); wav.byteRate = (int) u32 (f + 8); wav.blockAlign = u16 (f + 12); wav.bits = u16 (f + 14);
            // WAVE_FORMAT_EXTENSIBLE carries the real tag at the start of its sub-format GUID.
            if (wav.format == 0xfffe && chunk >= 40) wav.format = u16 (f + 24);
        }
        if (std::memcmp (data + at, "data", 4) == 0) { samples = data + at + 8; sampleBytes = chunk; }
        at += 8 + chunk + (chunk & 1);
    }
    check (samples != nullptr && wav.channels > 0 && wav.blockAlign == wav.channels * wav.bits / 8 && wav.byteRate == wav.rate * wav.blockAlign
           && sampleBytes % (size_t) wav.blockAlign == 0, name + " has a consistent fmt chunk and a data chunk of whole frames");
    const auto frames = sampleBytes / (size_t) wav.blockAlign;
    wav.samples.assign ((size_t) wav.channels, std::vector<double> (frames));
    const int width = wav.bits / 8;
    for (size_t n = 0; n < frames; ++n)
        for (int c = 0; c < wav.channels; ++c)
        {
            const auto* p = samples + n * (size_t) wav.blockAlign + (size_t) (c * width);
            double value = 0;
            if (wav.format == 3 && wav.bits == 32) { float f; std::memcpy (&f, p, 4); value = f; }
            else if (wav.format == 1 && wav.bits == 16) value = (int16_t) u16 (p) / 32768.0;
            else if (wav.format == 1 && wav.bits == 24) { int32_t v = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8; value = v / 8388608.0; }
            else if (wav.format == 1 && wav.bits == 32) value = (int32_t) u32 (p) / 2147483648.0;
            else check (false, name + " uses a format this decoder reads");
            wav.samples[(size_t) c][n] = value;
        }
    return wav;
}
}

int runWavDecoderTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("wav-decoder-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("sine.wav");
        const auto sine = [] (int n) { return .5 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * n / 48000); };
        {
            juce::AudioBuffer<float> x (2, 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) sine (n));
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a one-second 440 Hz sine");
        }
        Session s (false);
        ok (s.importAudio (source, 0, 0), "put it on a track");
        for (const auto [bits, tag, tolerance] : { std::tuple { 16, 1, 1.5 / 32768 }, { 24, 1, 1.5 / 8388608 }, { 32, 3, 1e-6 } })
        {
            const auto name = juce::String (bits) + "-bit export";
            const auto file = root.getChildFile ("export-" + juce::String (bits) + ".wav");
            ok (s.exportWav (file, 48000, bits, 0), "write the " + name);
            const auto wav = parse (file, name);
            check (wav.format == tag && wav.channels == 2 && wav.rate == 48000 && wav.bits == bits, name + " declares " + (tag == 3 ? "IEEE float" : "PCM") + ", stereo, 48 kHz");
            check (wav.samples[0].size() == 48000, name + " holds exactly one second");
            double worst = 0;
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 48000; ++n) worst = std::max (worst, std::abs (wav.samples[(size_t) c][(size_t) n] - sine (n)));
            check (worst <= tolerance, name + " decodes to the sine within its quantization (" + juce::String (worst * std::ldexp (1.0, bits - 1), 3) + " LSB)");
        }
        std::cout << "WAV decoder passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL wav decoder: " << e.what() << std::endl; return 1; }
}
}
