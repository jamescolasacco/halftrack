#include "Session.h"
#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace halftrack
{
juce::Result runRangeExportTaskCancellationTests (const juce::File&);
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
juce::AudioBuffer<float> read (const juce::File& file, Session::ExportFormat format, double rate, int bits, int samples, int channels = 2)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.createInputStream()));
    check (reader && reader->sampleRate == rate && reader->bitsPerSample == (unsigned) bits
        && reader->numChannels == (unsigned) channels && reader->lengthInSamples == samples, "container retains rate, depth, channel count and exact duration");
    juce::FileInputStream header (file);
    char bytes[12] {}; check (header.read (bytes, 12) == 12, "read actual container header");
    const auto tag = juce::String::fromUTF8 (bytes, 4);
    check ((format == Session::ExportFormat::wav && tag == "RIFF" && juce::String::fromUTF8 (bytes + 8, 4) == "WAVE")
        || (format == Session::ExportFormat::aiff && tag == "FORM" && juce::String::fromUTF8 (bytes + 8, 3) == "AIF")
        || (format == Session::ExportFormat::flac && tag == "fLaC"), "selected format writes its real container, not a renamed WAV");
    juce::AudioBuffer<float> result (channels, samples);
    check (reader->read (&result, 0, samples, 0, true, true), "decode all exported frames");
    return result;
}
void compare (const juce::AudioBuffer<float>& actual, const juce::AudioBuffer<float>& expected, double gain, double tolerance)
{
    check (actual.getNumChannels() == expected.getNumChannels() && actual.getNumSamples() >= expected.getNumSamples(), "render includes every source channel and frame");
    double worst = 0;
    for (int channel = 0; channel < actual.getNumChannels(); ++channel)
        for (int n = 0; n < actual.getNumSamples(); ++n)
        {
            const double value = actual.getSample (channel, n);
            if (! std::isfinite (value)) throw std::runtime_error ("Nonfinite exported frame");
            if (n >= expected.getNumSamples() && value != 0) throw std::runtime_error ("Nonzero dry export tail");
            const double reference = n < expected.getNumSamples() ? expected.getSample (channel, n) * gain : 0;
            worst = std::max (worst, std::abs (value - reference));
        }
    check (worst <= tolerance, "every exported frame follows the independent mix oracle; error=" + juce::String (worst, 10));
}
juce::AudioBuffer<float> slice (const juce::AudioBuffer<float>& source, int start, int count)
{
    juce::AudioBuffer<float> result (source.getNumChannels(), count);
    for (int channel = 0; channel < source.getNumChannels(); ++channel)
        result.copyFrom (channel, 0, source, channel, start, count);
    return result;
}
void writeFloat (const juce::File& file, const juce::AudioBuffer<float>& source, double rate = 48000)
{
    juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate)
        .withNumChannels (source.getNumChannels()).withBitsPerSample (32)
        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    check (writer && writer->isFloatingPoint() && writer->writeFromAudioSampleBuffer (source, 0, source.getNumSamples()), "write generated floating-point fixture");
}
Session::AudioExportSettings rangeSettings (double start, double end, double rate = 48000)
{
    Session::AudioExportSettings settings; settings.sampleRate = rate; settings.bitDepth = 32;
    settings.timeRange = juce::Range<double> (start, end); return settings;
}
void rangeExportTests (const juce::File& root, Session& session, const juce::AudioBuffer<float>& source, double masterGain)
{
    using Format = Session::ExportFormat;
    const auto fullFile = root.getChildFile ("range-full.wav"), rangeFile = root.getChildFile ("range-selected.wav");
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        ok (session.exportWav (fullFile, rate, 32), "render full float reference at selected sample rate");
        const auto full = read (fullFile, Format::wav, rate, 32, static_cast<int> (rate * .25));
        for (const auto frames : { juce::Range<int> (513, 1026), juce::Range<int> (1024, 1025), juce::Range<int> (511, 518) })
        {
            auto settings = rangeSettings ((frames.getStart() + .2) / rate, (frames.getEnd() + .4) / rate, rate);
            ok (session.exportAudio (rangeFile, settings), "round both endpoints to nearest output frame, including one-frame and block-boundary ranges");
            compare (read (rangeFile, Format::wav, rate, 32, frames.getLength()), slice (full, frames.getStart(), frames.getLength()), 1, 0);
        }
    }
    for (const auto format : { Format::wav, Format::aiff, Format::flac })
        for (const bool downmix : { false, true })
        {
            auto settings = rangeSettings (1024.0 / 48000, 4096.0 / 48000);
            settings.format = format; settings.bitDepth = 24; settings.normalizePeak = true; settings.peakCeilingDb = -3; settings.mono = downmix;
            ok (session.exportAudio (rangeFile, settings), "normalize selected window in every container and output channel mode");
            auto expected = slice (source, 1024, 3072);
            if (downmix)
            {
                juce::AudioBuffer<float> mixed (1, 3072);
                for (int n = 0; n < 3072; ++n) mixed.setSample (0, n, .5f * (expected.getSample (0, n) + expected.getSample (1, n)));
                expected = std::move (mixed);
            }
            const double peak = expected.getMagnitude (0, expected.getNumSamples()) * masterGain;
            const double gain = masterGain * std::clamp (std::pow (10.0, -3.0 / 20) / (peak * 1.005 + 2.0 / 32768), 0.0, 100.0);
            compare (read (rangeFile, format, 48000, 24, 3072, downmix ? 1 : 2), expected, gain, 5e-7);
        }
    auto quietAfterLoud = rangeSettings (10240.0 / 48000, 10500.0 / 48000);
    quietAfterLoud.normalizePeak = true; quietAfterLoud.peakCeilingDb = -3;
    ok (session.exportAudio (rangeFile, quietAfterLoud), "normalize quiet selection after a louder event in its rendered prefix");
    const auto quietExpected = slice (source, 10240, 260);
    const double quietGain = masterGain * std::clamp (std::pow (10.0, -3.0 / 20)
        / (quietExpected.getMagnitude (0, 260) * masterGain * 1.005 + 2.0 / 32768), 0.0, 100.0);
    compare (read (rangeFile, Format::wav, 48000, 32, 260), quietExpected, quietGain, 3e-7);
    auto opposite = rangeSettings (10240.0 / 48000, 10500.0 / 48000); opposite.mono = true; opposite.normalizePeak = true;
    ok (session.exportAudio (rangeFile, opposite), "normalize opposite-channel selected mono");
    check (read (rangeFile, Format::wav, 48000, 32, 260, 1).getMagnitude (0, 260) == 0, "selected mono cancellation remains exact silence");
    auto silence = rangeSettings (1, 1.125); silence.normalizePeak = true;
    ok (session.exportAudio (rangeFile, silence), "export empty interval after a nonempty arrangement");
    check (read (rangeFile, Format::wav, 48000, 32, 6000).getMagnitude (0, 6000) == 0, "empty interval retains exact requested silent duration");

    ok (session.addUtility (0), "insert gain before range normalization");
    const auto utility = session.snapshot().tracks[0].plugins[0]; auto driven = utility.utility; driven.gainDb = 24;
    ok (session.setUtilitySettings (utility.id, driven), "drive float intermediate above full scale");
    auto over = rangeSettings (6144.0 / 48000, 9216.0 / 48000);
    ok (session.exportAudio (rangeFile, over), "export unclipped over-range floating point selection");
    const auto overOutput = read (rangeFile, Format::wav, 48000, 32, 3072);
    const auto overSource = slice (source, 6144, 3072);
    const double drivenGain = masterGain * std::pow (10.0, 24.0 / 20);
    compare (overOutput, overSource, drivenGain, 1e-6);
    check (overOutput.getMagnitude (0, 3072) > 1.5f, "intermediate and final float WAV retain samples above 1.0");
    over.normalizePeak = true; over.bitDepth = 24; over.peakCeilingDb = -6;
    ok (session.exportAudio (rangeFile, over), "normalize driven selection before final integer encoding");
    const double peak = overSource.getMagnitude (0, 3072) * drivenGain;
    const double normalized = drivenGain * std::clamp (std::pow (10.0, -6.0 / 20) / (peak * 1.005 + 2.0 / 32768), 0.0, 100.0);
    compare (read (rangeFile, Format::wav, 48000, 24, 3072), overSource, normalized, 5e-7);
    session.undo(); session.undo();

    const auto project = root.getChildFile ("range-history.halftrack");
    ok (session.saveProject (project), "save range history checkpoint");
    ok (session.setTrackPan (0, .3f), "create redo checkpoint before export"); session.undo();
    const auto before = session.snapshot();
    const auto preserved = root.getChildFile ("range-protected.audio");
    check (preserved.replaceWithText ("keep existing audio"), "prepare protected range destination");
    const double nan = std::numeric_limits<double>::quiet_NaN(), infinity = std::numeric_limits<double>::infinity();
    for (const auto range : { juce::Range<double> (-1, 1), juce::Range<double> (0, 0), juce::Range<double> (2, 1),
                             juce::Range<double> (nan, 1), juce::Range<double> (0, infinity), juce::Range<double> (0, nan),
                             juce::Range<double> (0, Session::maximumExportRangeSeconds + 1), juce::Range<double> (0, .1 / 48000) })
    {
        auto invalid = rangeSettings (0, 1); invalid.timeRange = range;
        check (session.exportAudio (preserved, invalid).failed() && preserved.loadFileAsString() == "keep existing audio", "invalid range rejects atomically");
    }
    auto roundedLimit = rangeSettings (0, Session::maximumExportRangeSeconds, 48000.000003);
    check (session.exportAudio (preserved, roundedLimit).failed() && preserved.loadFileAsString() == "keep existing audio", "rounded endpoint cannot cross native timeline limit at fractional rates");
    auto tail = rangeSettings (0, .1); tail.tailSeconds = .001;
    check (session.exportAudio (preserved, tail).failed() && preserved.loadFileAsString() == "keep existing audio", "selected range rejects extra tail");
    bool dispatched = false, guarded = false;
    juce::Timer::callAfterDelay (10, [&]
    {
        dispatched = true; guarded = session.isExporting() && ! session.canUndo() && ! session.canRedo()
            && session.newProject().failed() && session.setTrackPan (0, -.5f).failed()
            && session.exportAudio (root.getChildFile ("nested-range.wav"), rangeSettings (0, 1)).failed();
        session.cancelExport();
    });
    const auto cancelled = session.exportAudio (preserved, rangeSettings (0, 60));
    check (dispatched && guarded && cancelled.failed() && cancelled.getErrorMessage() == "Export cancelled."
        && preserved.loadFileAsString() == "keep existing audio", "selected prefix cancellation preserves destination and blocks reentrant model edits");
    ok (session.exportAudio (rangeFile, rangeSettings (.02, .03)), "successful range export preserves history too");
    const auto after = session.snapshot();
    check (after.projectGeneration == before.projectGeneration && after.projectFile == before.projectFile
        && after.dirty == before.dirty && after.position == before.position && after.tempo == before.tempo
        && after.looping == before.looping && after.loopStart == before.loopStart && after.loopEnd == before.loopEnd
        && after.tracks[0].clips[0].id == before.tracks[0].clips[0].id && session.canRedo(), "range paths preserve project, transport and redo history");
    session.redo(); check (std::abs (session.snapshot().tracks[0].pan - .3f) < 1e-6, "redo remains functional after successful, rejected and cancelled range exports");
    session.undo();
}
void rangeHistoryTests (const juce::File& root)
{
    using Format = Session::ExportFormat;
    juce::AudioBuffer<float> signal (2, 144000); signal.clear();
    for (int n = 0; n < 480; ++n) { signal.setSample (0, n, .25f); signal.setSample (1, n, -.125f); }
    for (int n = 67200; n < 96000; ++n) { signal.setSample (0, n, .125f); signal.setSample (1, n, .0625f); }
    const auto input = root.getChildFile ("range-memory-input.wav"), fullFile = root.getChildFile ("range-memory-full.wav"), selectionFile = root.getChildFile ("range-memory-cut.wav");
    writeFloat (input, signal);
    Session delayed (false); ok (delayed.importAudio (input, 0), "import source crossing selected boundaries");
    ok (delayed.addNativeEffect (0, Session::EffectKind::delay), "insert long effect history");
    auto settings = defaultEffectSettings (Session::EffectKind::delay);
    settings.values[0] = settings.values[1] = 1000; settings.values[2] = 0; settings.values[3] = 1;
    ok (delayed.setNativeEffectSettings (delayed.snapshot().tracks[0].plugins[0].id, settings), "delay preceding impulse by one second");
    ok (delayed.addNativeEffect (0, Session::EffectKind::limiter), "include actual lookahead latency compensation");
    auto limiter = defaultEffectSettings (Session::EffectKind::limiter); limiter.values[1] = 0;
    ok (delayed.setNativeEffectSettings (delayed.snapshot().tracks[0].plugins[1].id, limiter), "keep delay fixture below limiter ceiling");
    ok (delayed.exportWav (fullFile, 48000, 32), "render full delay/latency reference");
    const auto full = read (fullFile, Format::wav, 48000, 32, 144000);
    for (const auto frames : { juce::Range<int> (47000, 49001), juce::Range<int> (48000, 48001), juce::Range<int> (115199, 120001) })
    {
        ok (delayed.exportAudio (selectionFile, rangeSettings (frames.getStart() / 48000.0, frames.getEnd() / 48000.0)), "render exact range with long prehistory and compensated limiter");
        compare (read (selectionFile, Format::wav, 48000, 32, frames.getLength()), slice (full, frames.getStart(), frames.getLength()), 1, 0);
    }
    check (full.getMagnitude (0, 48000, 480) > .2f, "selected delay output requires input one second before selection");
    {
        juce::AudioBuffer<float> monoInput (1, 2048);
        for (int n = 0; n < 2048; ++n) monoInput.setSample (0, n, .125f + static_cast<float> (n % 17) / 65536.0f);
        const auto monoFile = root.getChildFile ("range-mono-source.wav"); writeFloat (monoFile, monoInput);
        Session monoSource (false); ok (monoSource.importAudio (monoFile, 0), "import one-channel source");
        Session::AudioExportSettings fullMono; fullMono.bitDepth = 32; fullMono.mono = true;
        ok (monoSource.exportAudio (fullFile, fullMono), "render full forced-mono reference for mono source");
        const auto reference = read (fullFile, Format::wav, 48000, 32, 2048, 1);
        auto selection = rangeSettings (511.0 / 48000, 1537.0 / 48000); selection.mono = true;
        ok (monoSource.exportAudio (selectionFile, selection), "select mono source through stereo prefix and mono output");
        compare (read (selectionFile, Format::wav, 48000, 32, 1026, 1), slice (reference, 511, 1026), 1, 0);
    }
    Session midi (false); ok (midi.addMidiTrack ("Held note range"), "create held-note fixture");
    const auto track = midi.snapshot().tracks.back().id;
    ok (midi.addMidiClip (track, 0, 6), "create three-second MIDI clip");
    const auto clip = midi.snapshot().tracks.back().clips[0].id;
    ok (midi.addMidiNote (clip, 60, 0, 6, .7f), "start note well before selection");
    ok (midi.exportWav (fullFile, 48000, 32), "render complete held-note reference");
    const auto instrument = read (fullFile, Format::wav, 48000, 32, 144000);
    ok (midi.exportAudio (selectionFile, rangeSettings (.99, 1.01)), "export already-playing instrument without retriggering it");
    const auto selected = read (selectionFile, Format::wav, 48000, 32, 960);
    compare (selected, slice (instrument, 47520, 960), 1, 0);
    check (selected.getMagnitude (0, 960) > .001f, "held-note selected output is audible and retains oscillator/envelope state");
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
        for (int n = 10240; n < 10500; ++n)
        { source.setSample (0, n, .125f); source.setSample (1, n, -.125f); }
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
        juce::AudioBuffer<float> mono (1, 12000);
        for (int n = 0; n < 12000; ++n) mono.setSample (0, n, .5f * (source.getSample (0, n) + source.getSample (1, n)));
        const auto monoFile = root.getChildFile ("both-channels-mono.wav");
        Session::AudioExportSettings monoSettings { Format::wav, 48000, 24, .25, true };
        ok (session.exportAudio (monoFile, monoSettings), "export complete stereo mix as mono");
        const auto monoRender = read (monoFile, Format::wav, 48000, 24, 24000, 1);
        compare (monoRender, mono, gain, 3e-7);
        check (monoRender.getMagnitude (0, 10240, 260) == 0, "opposite stereo channels cancel exactly in the mono mix");

        for (const auto format : { Format::wav, Format::aiff, Format::flac })
            for (const int bits : { 16, 24, 32 })
            {
                if (format != Format::wav && bits == 32) continue;
                const auto output = root.getChildFile (juce::String ((int) format) + "-" + juce::String (bits) + ".audio");
                ok (session.exportAudio (output, format, 48000, bits, .25), "export selected format and depth with explicit tail");
                const auto actual = read (output, format, 48000, bits, 24000);
                compare (actual, source, gain, bits == 16 ? 1.0 / 32768 : 3e-7);
            }
        for (const auto format : { Format::wav, Format::aiff, Format::flac })
            for (const bool downmix : { false, true })
            {
                const auto& expected = downmix ? mono : source;
                Session::AudioExportSettings settings { format, 48000, 24, .25, downmix, true, -3 };
                const auto file = root.getChildFile (juce::String ((int) format) + (downmix ? "-mono-normalized.audio" : "-stereo-normalized.audio"));
                ok (session.exportAudio (file, settings), "normalize chosen output channels and container");
                const auto result = read (file, format, 48000, 24, 24000, downmix ? 1 : 2);
                const double originalPeak = expected.getMagnitude (0, expected.getNumSamples()) * gain;
                const double ceiling = std::pow (10.0, -3.0 / 20);
                // The pinned engine uses conservative peak headroom and caps boost at 40 dB.
                // Derive expected output from the generated signal, independently of rendered peak metadata.
                const double normalizedGain = gain * std::clamp (ceiling / (originalPeak * 1.005 + 2.0 / 32768), 0.0, 100.0);
                compare (result, expected, normalizedGain, 5e-7);
                const auto peak = result.getMagnitude (0, result.getNumSamples());
                check (peak <= ceiling && peak > ceiling * .98, "normalized peak approaches its ceiling without exceeding it");
                check (std::abs (session.snapshot().masterGainDb + 6) < 1e-4, "normalization leaves the project's master fader unchanged");
            }
        ok (session.setMasterGain (-100), "prepare silent normalized mix");
        const auto silentFile = root.getChildFile ("silent-normalized.flac");
        ok (session.exportAudio (silentFile, { Format::flac, 48000, 24, 0, true, true, -1 }), "normalize silence without inventing signal");
        check (read (silentFile, Format::flac, 48000, 24, 12000, 1).getMagnitude (0, 12000) == 0, "silent mono normalization stays exactly silent");
        session.undo();
        auto invalidPeak = monoSettings; invalidPeak.peakCeilingDb = std::numeric_limits<float>::quiet_NaN();
        const auto protectedPeak = root.getChildFile ("invalid-normalization.wav");
        check (protectedPeak.replaceWithText ("preserve destination"), "prepare invalid normalization destination");
        check (session.exportAudio (protectedPeak, invalidPeak).failed() && protectedPeak.loadFileAsString() == "preserve destination", "nonfinite peak ceiling rejects before filesystem mutation");
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
        rangeExportTests (root, session, source, gain);
        rangeHistoryTests (root);
        ok (runRangeExportTaskCancellationTests (root), "cancel deterministically during peak scan, between passes, and both output conversion modes");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL audio export: " << error.what() << std::endl;
        return 1;
    }
}
}
