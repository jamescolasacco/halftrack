#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Track output routing ("Audio To"), with offline renders of a constant 0.5 signal. Routed to
// another audio track, a track plays through that track's mixer; Sends Only leaves only its sends;
// routes back into a track, from returns and from grouped tracks are refused. Solo follows the
// route, and the route is one undo step that reopens.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS routing: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
float level (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x.getNumSamples() > 24000 ? x.getSample (0, 24000) : 0.0f;
}
bool near (float a, float b) { return std::abs (a - b) < 1e-3f; }
}

int runRoutingTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("routing-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("half.wav");
        {
            juce::AudioBuffer<float> x (2, 2 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .5f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a constant 0.5 signal");
        }
        Session s (false);
        ok (s.importAudio (source, 0, 0), "put it on track A");
        const auto a = s.snapshot().tracks[0].id, b = s.snapshot().tracks[1].id;
        ok (s.setTrackGain (1, -6), "track B at -6 dB");
        const auto out = root.getChildFile ("out.wav");
        const auto bGain = std::pow (10.0f, -6 / 20.0f);
        check (near (level (s, out), .5f) && s.snapshot().tracks[0].output == Session::TrackOutput {}, "A plays into Master at its own level");
        check (s.outputDestinations (a) == juce::StringArray { b } && s.outputDestinations ("missing").isEmpty(), "A can play into B");

        ok (s.setTrackOutput (a, { Session::OutputKind::track, b }), "route A into B");
        check ((s.snapshot().tracks[0].output == Session::TrackOutput { Session::OutputKind::track, b }) && near (level (s, out), .5f * bGain),
               "A now plays through B's mixer");
        check (s.setTrackOutput (b, { Session::OutputKind::track, a }).failed() && ! s.outputDestinations (b).contains (a), "B cannot play back into A");
        s.undo();
        check (s.snapshot().tracks[0].output == Session::TrackOutput {} && near (level (s, out), .5f), "undo returns A to Master");
        s.redo();
        check (s.snapshot().tracks[0].output.kind == Session::OutputKind::track, "redo routes it again");
        ok (s.setTrackSolo (0, true), "solo A");
        check (near (level (s, out), .5f * bGain), "soloed, A is still heard through B");
        ok (s.setTrackSolo (0, false), "unsolo A");

        ok (s.setTrackOutput (a, { Session::OutputKind::sendsOnly, {} }), "Sends Only");
        check (s.snapshot().tracks[0].output.kind == Session::OutputKind::sendsOnly && std::abs (level (s, out)) < 1e-6f, "with no sends, A is silent");
        ok (s.addReturnTrack ("Return"), "add a return");
        const int ret = (int) s.snapshot().tracks.size() - 1; const auto retId = s.snapshot().tracks[(size_t) ret].id;
        ok (s.setTrackSendGain (0, retId, 0), "send A to it at 0 dB");
        check (near (level (s, out), .5f), "Sends Only leaves A heard through its send");
        check (s.setTrackOutput (retId, { Session::OutputKind::sendsOnly, {} }).failed() && s.outputDestinations (retId).isEmpty(), "returns play into Master");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        check (! s.outputDestinations (a).contains (s.snapshot().tracks.back().id), "MIDI tracks take no audio output");

        ok (s.setTrackOutput (a, { Session::OutputKind::track, b }), "route A into B again");
        const auto project = root.getChildFile ("Routing").getChildFile ("Routing.halftrack");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check ((s.snapshot().tracks[0].output == Session::TrackOutput { Session::OutputKind::track, b }), "the route reopens");
        check (near (level (s, out), .5f * bGain + .5f), "A is heard through B and through its send");
        ok (s.setTrackOutput (a, {}), "back to Master");
        check (s.snapshot().tracks[0].output == Session::TrackOutput {}, "A plays into Master again");
        std::cout << "Routing passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL routing: " << e.what() << std::endl; return 1; }
}
}
