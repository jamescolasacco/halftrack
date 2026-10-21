#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Realtime and offline agreement. One project, an audio clip through a Utility at -6 dB and a MIDI
// note through the built-in synth, is exported offline and played through the synthetic device.
// Aligned on the device's start, the two outputs agree sample by sample after the short ramp
// that starting playback applies.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS render agreement: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
}

int runRenderAgreementTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("render-agreement-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("sine.wav");
        {
            juce::AudioBuffer<float> x (2, 3 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.3 * std::sin (juce::MathConstants<double>::twoPi * 330.0 * n / 48000)));
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a three-second 330 Hz sine");
        }
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.importAudio (source, 0, 0), "put it on track 1");
        ok (s.addUtility (0), "add a Utility");
        auto utility = s.snapshot().tracks[0].plugins.back(); auto settings = utility.utility; settings.gainDb = -6;
        ok (s.setUtilitySettings (utility.id, settings), "at -6 dB");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        auto keys = s.snapshot().tracks.back();
        auto synth = keys.synth; synth.waveform = 0; synth.attackSeconds = .01f; synth.releaseSeconds = .05f;
        ok (s.setSynthSettings (keys.id, synth), "a sawtooth synth");
        ok (s.addMidiClip (keys.id, 1, 2), "a two-beat clip at beat 1");
        juce::StringArray added;
        ok (s.addMidiNotes (s.snapshot().tracks.back().clips[0].id, { { "", 57, 0, 1.5, .8f } }, added), "one note");
        pumpSyntheticTestDevice (s, 8);

        const auto file = root.getChildFile ("offline.wav");
        ok (s.exportWav (file, 48000, 32, 0), "render offline");
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        check (reader != nullptr, "read the render");
        juce::AudioBuffer<float> offline (2, (int) reader->lengthInSamples); reader->read (&offline, 0, offline.getNumSamples(), 0, true, true);

        pumpSyntheticTestDevice (s, 8);
        s.seek (0); ok (s.play(), "play in real time");
        const auto realtime = captureSyntheticTestDevice (s, offline.getNumSamples() / 256 + 64);
        s.stop();

        // The device starts playback on a block boundary a little after the play request; find
        // the start as the offset where the two agree best, then compare everything after it.
        const auto difference = [&] (int offset, int from, int to)
        {
            double worst = 0;
            for (int c = 0; c < 2; ++c)
                for (int n = from; n < to && n + offset < realtime.getNumSamples(); ++n)
                    worst = std::max (worst, (double) std::abs (realtime.getSample (c, n + offset) - offline.getSample (c, n)));
            return worst;
        };
        int best = -1; double bestError = 1e9;
        for (int offset = 0; offset < 64 * 256; ++offset)
            if (const auto e = difference (offset, 4800, 9600); e < bestError) { bestError = e; best = offset; }
        check (best >= 0 && bestError < 1e-5, "the realtime output lines up with the render (offset " + juce::String (best) + " samples)");
        // Starting playback ramps the first few dozen samples in to avoid a click; a render has no such start.
        const auto error = difference (best, 64, offline.getNumSamples());
        check (error < 1e-4, "after the start ramp it agrees sample by sample through the note and the clip (" + juce::String (error, 8) + ")");
        const auto sineAlone = offline.getMagnitude (0, 4800, 4800), withNote = offline.getMagnitude (0, 48000 / 2 + 2000, 4800);
        check (sineAlone > .1f && withNote > sineAlone + .05f, "the comparison covers the sine and then the synth note over it ("
                                                                 + juce::String (sineAlone, 3) + ", then " + juce::String (withNote, 3) + ")");
        std::cout << "Render agreement passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL render agreement: " << e.what() << std::endl; return 1; }
}
}
