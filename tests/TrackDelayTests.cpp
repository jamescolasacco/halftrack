#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Track Delay. Track 1 plays a 0.5 impulse at 1.5 s and track 2 a 0.25 impulse at 2 s.
// Renders locate each impulse to the sample: a positive delay moves track 1 later, a
// negative one earlier while track 2 stays put, and a pre-fader send carries the delay.
namespace halftrack
{
namespace
{
constexpr double rate = 48000;
constexpr int frames = 3 * 48000, first = 72000, second = 96000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS track delay: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void write (const juce::File& f, int at, float level)
{
    juce::AudioBuffer<float> x (2, frames); x.clear();
    for (int c = 0; c < 2; ++c) x.setSample (c, at, level);
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
// The sample index of the first sample near the given level, or -1.
int find (const juce::AudioBuffer<float>& x, float level)
{
    for (int n = 0; n < x.getNumSamples(); ++n) if (std::abs (x.getSample (0, n) - level) < 1e-4f) return n;
    return -1;
}
struct Render { int first = -1, second = -1; float energy = 0; };
Render render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render");
    juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
    Render result { find (x, .5f), find (x, .25f), 0 };
    for (int n = 0; n < x.getNumSamples(); ++n) result.energy += x.getSample (0, n) * x.getSample (0, n);
    return result;
}
double delay (Session& s) { const auto snapshot = s.snapshot(); return snapshot.tracks[0].delayMs; }
}

int runTrackDelayTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("track-delay-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto a = root.getChildFile ("a.wav"), b = root.getChildFile ("b.wav"), file = root.getChildFile ("render.wav");
        write (a, first, .5f); write (b, second, .25f);
        Session s (false);
        ok (s.importAudio (a, 0, 0), "track 1 plays an impulse at 1.5 s"); ok (s.importAudio (b, 1, 0), "track 2 plays an impulse at 2 s");
        const auto id = s.snapshot().tracks[0].id;
        auto x = render (s, file);
        check (x.first == first && x.second == second && std::abs (x.energy - .3125f) < 1e-4f, "without delay both impulses play on time, once");
        check (s.setTrackDelay (id, 1001).failed() && s.setTrackDelay (id, std::nan ("")).failed() && s.setTrackDelay ("missing", 0).failed(),
               "delays beyond 1000 ms, non-finite values and unknown tracks are rejected");

        ok (s.setTrackDelay (id, 10), "delay track 1 by 10 ms");
        check (delay (s) == 10, "the track reports its delay");
        x = render (s, file);
        check (x.first == first + 480 && x.second == second && std::abs (x.energy - .3125f) < 1e-4f, "a positive delay plays track 1 480 samples later and leaves track 2");
        ok (s.setTrackDelay (id, -10), "pre-delay track 1 by 10 ms");
        x = render (s, file);
        check (x.first == first - 480 && x.second == second && std::abs (x.energy - .3125f) < 1e-4f, "a negative delay plays track 1 480 samples early and keeps track 2 on time");
        ok (s.setTrackDelay (id, 1000), "delay track 1 by a second");
        x = render (s, file);
        check (x.first == first + 48000 && x.second == second, "a one-second delay moves track 1 by 48000 samples");
        ok (s.setTrackDelay (id, -1000), "pre-delay track 1 by a second");
        x = render (s, file);
        check (x.first == first - 48000 && x.second == second, "a one-second pre-delay moves track 1 48000 samples early");
        s.undo(); check (delay (s) == 1000 && render (s, file).first == first + 48000, "undo restores the one-second delay");
        s.redo(); check (delay (s) == -1000 && render (s, file).first == first - 48000, "redo restores the pre-delay");
        ok (s.setTrackDelay (id, 0), "clear the delay");
        x = render (s, file);
        check (x.first == first && x.second == second, "zero delay passes the track unchanged");

        // A pre-fader send follows the delay: with the fader down only the return plays track 1.
        ok (s.addReturnTrack ("Return"), "add a return");
        const auto returnId = s.snapshot().tracks.back().id;
        ok (s.setReturnSendMode (returnId, Session::SendMode::pre), "make the return pre-fader");
        ok (s.setTrackSendGain (0, returnId, 0), "send track 1 at 0 dB");
        ok (s.setTrackGain (0, -100), "turn track 1's fader down");
        ok (s.setTrackDelay (id, 25), "delay track 1 by 25 ms");
        x = render (s, file);
        check (x.first == first + 1200 && x.second == second, "the send carries the delayed track");
        ok (s.setTrackDelay (id, -25), "pre-delay track 1 by 25 ms");
        check (render (s, file).first == first - 1200, "the send carries the pre-delayed track");

        // Reopening.
        const auto project = root.getChildFile ("Delay").getChildFile ("Delay.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create the project folder");
        ok (s.saveProject (project), "save the delayed track");
        ok (s.loadProject (project), "reopen it");
        check (delay (s) == -25 && render (s, file).first == first - 1200, "the delay reopens and renders the same");
        auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "parse the project");
        std::function<void (juce::XmlElement&)> corrupt = [&] (juce::XmlElement& e)
        { if (e.hasAttribute ("halftrackDelayMs")) e.setAttribute ("halftrackDelayMs", 5000.0); for (auto* c : e.getChildIterator()) corrupt (*c); };
        corrupt (*xml);
        const auto broken = project.getSiblingFile ("Broken.halftrack"); check (xml->writeTo (broken), "write an out-of-range delay");
        const auto loaded = s.loadProject (broken);
        check (loaded.failed() && loaded.getErrorMessage().contains ("track delay") && delay (s) == -25, "an out-of-range delay rejects the load and keeps the open project");
        ok (s.setTrackFrozen (id, true), "freeze track 1");
        check (s.setTrackDelay (id, 0).failed(), "a frozen track's delay is locked");
        std::cout << "Track delay passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL track delay: " << e.what() << std::endl; return 1; }
}
}
