#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Simultaneous multitrack capture, through the synthetic device. Track B records track A's
// constant 0.5 signal (Audio From) while track C records the generated stereo input, a 440 Hz
// sine on the left and 660 Hz on the right. One pass keeps two aligned takes, each in its own
// closed file holding only its own signal, and one undo removes both.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS multitrack recording: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr && reader->numChannels == 2, "open " + file.getFileName());
    juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
// The strength of a frequency in a channel, from its correlation with a sine and a cosine.
double strength (const juce::AudioBuffer<float>& x, int channel, double frequency)
{
    double s = 0, c = 0;
    for (int n = 0; n < x.getNumSamples(); ++n)
    {
        const auto phase = juce::MathConstants<double>::twoPi * frequency * n / 48000;
        s += x.getSample (channel, n) * std::sin (phase); c += x.getSample (channel, n) * std::cos (phase);
    }
    return 2 * std::sqrt (s * s + c * c) / std::max (1, x.getNumSamples());
}
double mean (const juce::AudioBuffer<float>& x, int channel)
{
    double sum = 0; for (int n = 0; n < x.getNumSamples(); ++n) sum += x.getSample (channel, n);
    return sum / std::max (1, x.getNumSamples());
}
}

int runMultitrackRecordingTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("multitrack-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("half.wav");
        {
            juce::AudioBuffer<float> x (2, 4 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .5f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a constant 0.5 signal");
        }
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Takes").getChildFile ("Takes.halftrack")), "save a recording destination");
        ok (s.importAudio (source, 0, 0), "put the signal on track A");
        ok (s.addAudioTrack ("C"), "add track C");
        const auto tracks = [&] { return s.snapshot().tracks; };
        const auto a = tracks()[0].id, b = tracks()[1].id;
        const int c = (int) tracks().size() - 1;
        ok (s.setTrackOutput (a, { Session::OutputKind::sendsOnly, {} }), "play A Sends Only");
        ok (s.setTrackInputTrack (b, a), "B takes A's audio");
        ok (s.setTrackInput (c, s.inputNames()[0]), "C takes the generated input");
        ok (s.setTrackArmed (1, true), "arm B"); ok (s.setTrackArmed (c, true), "arm C");
        pumpSyntheticTestDevice (s, 8);

        s.seek (0); ok (s.record(), "record both");
        pumpSyntheticTestDevice (s, 187);
        s.stop();
        check (! s.snapshot().recording && ! s.snapshot().playing, "Stop ends the pass");
        const auto takeB = tracks()[1].clips, takeC = tracks()[(size_t) c].clips;
        check (takeB.size() == 1 && takeC.size() == 1 && takeB[0].file != takeC[0].file && takeB[0].file.existsAsFile() && takeC[0].file.existsAsFile(),
               "one pass keeps a take on each armed track, each in its own file");
        check (std::abs (takeB[0].start - takeC[0].start) < 1e-9 && std::abs (takeB[0].length - takeC[0].length) < 256.0 / 48000,
               "the takes are aligned");
        const auto xb = read (takeB[0].file), xc = read (takeC[0].file);
        check (std::abs (mean (xb, 0) - .5) < .01 && std::abs (mean (xb, 1) - .5) < .01 && strength (xb, 0, 440) < .01 && strength (xb, 1, 660) < .01,
               "B's take holds only A's signal");
        check (strength (xc, 0, 440) > .15 && strength (xc, 1, 660) > .15 && strength (xc, 0, 660) < .01 && strength (xc, 1, 440) < .01
               && std::abs (mean (xc, 0)) < .01, "C's take holds only the input, each channel its own sine");
        s.undo();
        check (tracks()[1].clips.empty() && tracks()[(size_t) c].clips.empty(), "one undo removes both takes");
        std::cout << "Multitrack recording passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL multitrack recording: " << e.what() << std::endl; return 1; }
}
}
