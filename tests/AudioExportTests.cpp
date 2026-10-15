#include "Session.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
namespace
{
void check (bool value, const juce::String& message)
{
    if (! value) throw std::runtime_error (message.toStdString());
    std::cout << "PASS audio export: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{
    check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : ""));
}
juce::AudioBuffer<float> read (const juce::File& file, Session::ExportFormat format, double rate, int bits, int samples)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.createInputStream()));
    check (reader && reader->sampleRate == rate && reader->bitsPerSample == (unsigned) bits
        && reader->numChannels == 2 && reader->lengthInSamples == samples, "container retains rate, depth, stereo channels and exact duration");
    juce::FileInputStream header (file);
    char bytes[12] {}; check (header.read (bytes, 12) == 12, "read actual container header");
    const auto tag = juce::String::fromUTF8 (bytes, 4);
    check ((format == Session::ExportFormat::wav && tag == "RIFF" && juce::String::fromUTF8 (bytes + 8, 4) == "WAVE")
        || (format == Session::ExportFormat::aiff && tag == "FORM" && juce::String::fromUTF8 (bytes + 8, 3) == "AIF")
        || (format == Session::ExportFormat::flac && tag == "fLaC"), "selected format writes its real container, not a renamed WAV");
    juce::AudioBuffer<float> result (2, samples);
    check (reader->read (&result, 0, samples, 0, true, true), "decode all exported frames");
    return result;
}
void compare (const juce::AudioBuffer<float>& actual, const juce::AudioBuffer<float>& expected, double gain, double tolerance)
{
    check (actual.getNumSamples() >= expected.getNumSamples(), "render includes the entire source");
    double worst = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int n = 0; n < actual.getNumSamples(); ++n)
        {
            const double value = actual.getSample (channel, n);
            if (! std::isfinite (value)) throw std::runtime_error ("Nonfinite exported frame");
            if (n >= expected.getNumSamples() && value != 0) throw std::runtime_error ("Nonzero dry export tail");
            const double reference = n < expected.getNumSamples() ? expected.getSample (channel, n) * gain : 0;
            worst = std::max (worst, std::abs (value - reference));
        }
    check (worst <= tolerance, "every stereo frame follows the independent mix oracle; error=" + juce::String (worst, 10));
}
}
int runAudioExportTests (const juce::File& artifacts)
{
    try
    {
        using Format = Session::ExportFormat;
        const auto root = artifacts.getChildFile ("audio-export-" + juce::Uuid().toString());
        ok (root.createDirectory(), "create isolated generated fixtures");
        juce::AudioBuffer<float> source (2, 12000); source.clear();
        for (int n = 1024; n < 4096; ++n) source.setSample (0, n, .125f + (n % 17) / 65536.0f);
        for (int n = 6144; n < 9216; ++n) source.setSample (1, n, -.25f - (n % 19) / 65536.0f);
        const auto input = root.getChildFile ("generated.wav");
        {
            juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> stream (input.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer && writer->writeFromAudioSampleBuffer (source, 0, 12000), "write exactly representable generated source");
        }
        Session session (false);
        ok (session.importAudio (input, 0), "import stereo source");
        ok (session.setMasterGain (-6), "render through actual master gain");
        const double gain = std::pow (10.0, -6.0 / 20);
        for (const auto format : { Format::wav, Format::aiff, Format::flac })
            for (const int bits : { 16, 24, 32 })
            {
                if (format != Format::wav && bits == 32) continue;
                const auto output = root.getChildFile (juce::String ((int) format) + "-" + juce::String (bits) + ".audio");
                ok (session.exportAudio (output, format, 48000, bits, .25), "export selected format and depth with explicit tail");
                const auto actual = read (output, format, 48000, bits, 24000);
                compare (actual, source, gain, bits == 16 ? 1.0 / 32768 : 3e-7);
            }
        const auto wave = root.getChildFile ("resampled.wav");
        ok (session.exportWav (wave, 44100, 24, .25), "legacy WAV API uses the shared resampling path");
        const auto reference = read (wave, Format::wav, 44100, 24, 22050);
        for (const auto format : { Format::aiff, Format::flac })
        {
            const auto output = root.getChildFile (juce::String ((int) format) + "-resampled.audio");
            ok (session.exportAudio (output, format, 44100, 24, .25), "resample new format through the complete mix");
            compare (read (output, format, 44100, 24, 22050), reference, 1, 0);
            const auto protectedFile = root.getChildFile (juce::String ((int) format) + "-protected.audio");
            check (protectedFile.replaceWithText ("existing audio"), "prepare protected export destination");
            check (session.exportAudio (protectedFile, format, 48000, 32).failed()
                && protectedFile.loadFileAsString() == "existing audio", "unsupported depth fails without replacing existing data");
            bool dispatched = false, guarded = false;
            juce::Timer::callAfterDelay (10, [&]
            {
                dispatched = true;
                guarded = session.isExporting() && session.newProject().failed()
                    && session.exportAudio (root.getChildFile ("nested.audio"), format).failed();
                session.cancelExport();
            });
            const auto cancelled = session.exportAudio (protectedFile, format, 48000, 24, 60);
            check (dispatched && guarded && cancelled.failed() && cancelled.getErrorMessage() == "Export cancelled.", "new format honors cancellation and rejects reentrant project/export changes");
            check (protectedFile.loadFileAsString() == "existing audio" && ! root.getChildFile ("nested.audio").exists(), "cancelled export preserves existing audio and creates no nested export");
        }
        const auto rejected = root.getChildFile ("rejected.audio");
        check (session.exportAudio (rejected, static_cast<Format> (999)).failed()
            && session.exportAudio (rejected, Format::flac, std::numeric_limits<double>::quiet_NaN()).failed()
            && session.exportAudio (rejected, Format::aiff, 48000, 24, -1).failed()
            && ! rejected.exists(), "invalid format, rate and tail reject before touching a destination");
        session.undo(); check (std::abs (session.snapshot().masterGainDb) < 1e-4, "export and rejected exports retain edit history");
        session.redo(); check (std::abs (session.snapshot().masterGainDb + 6) < 1e-4, "export retains redo");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL audio export: " << error.what() << std::endl;
        return 1;
    }
}
}
