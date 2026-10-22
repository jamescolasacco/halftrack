#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Hosted instruments and what a hosted plug-in is told, through the VST3 test probe (see
// TestProbePlugin.cpp). An instrument plug-in replaces a MIDI track's instrument (one undo step),
// is refused on an audio track, and keeps effects after it. Rendered offline at 120 and 90 BPM,
// every note-on arrives at its exact sample and every beat the host reports falls on its sample,
// with the host saying it renders offline; played through the synthetic device at 256- and
// 128-sample blocks, notes and beats keep their exact spacing and the host says it plays live.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS plugin timing: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
// Samples where the left channel carries a note-on.
std::vector<int> notesIn (const juce::AudioBuffer<float>& x)
{
    std::vector<int> found;
    for (int n = 0; n < x.getNumSamples(); ++n) if (std::abs (x.getSample (0, n)) > .05f) found.push_back (n);
    return found;
}
// Samples where the right channel's beat sawtooth restarts, and the sign of the sawtooth.
std::vector<int> beatsIn (const juce::AudioBuffer<float>& x, float& sign)
{
    float peak = 0, sum = 0;
    for (int n = 0; n < x.getNumSamples(); ++n) { peak = std::max (peak, std::abs (x.getSample (1, n))); sum += x.getSample (1, n); }
    sign = sum < 0 ? -1.0f : 1.0f;
    std::vector<int> found;
    for (int n = 1; n < x.getNumSamples(); ++n)
        if (sign * (x.getSample (1, n - 1) - x.getSample (1, n)) > .5f * peak) found.push_back (n);
    return found;
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    juce::AudioBuffer<float> x (2, reader ? (int) reader->lengthInSamples : 0);
    if (reader) reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
bool within (int a, double b, double tolerance = 1) { return std::abs (a - b) <= tolerance; }
}

int runPluginTimingTests (const juce::File& artifacts)
{
    try
    {
        const auto* probePath = std::getenv ("HALFTRACK_TEST_PROBE_PLUGIN");
        check (probePath != nullptr, "the VST3 probe fixture is configured");
        const auto root = artifacts.getChildFile ("plugin-timing-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (auto* format : s.pluginFormats().getFormats())
            if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, juce::File (juce::String::fromUTF8 (probePath)).getFullPathName());
        check (! descriptions.isEmpty() && descriptions[0]->isInstrument, "the probe scans as an instrument");
        const auto& probe = *descriptions[0];
        s.knownPlugins().addType (probe);

        // Hosting.
        check (s.addPlugin (0, probe).failed(), "an instrument plug-in is refused on an audio track");
        for (int t = 0; t < (int) s.snapshot().tracks.size(); ++t) ok (s.setTrackMute (t, true), "mute the other tracks");
        ok (s.addMidiTrack ("Probe"), "add a MIDI track");
        const int track = (int) s.snapshot().tracks.size() - 1;
        const auto trackId = s.snapshot().tracks[(size_t) track].id;
        ok (s.addPlugin (track, probe), "insert the probe");
        const auto devices = [&] { return s.snapshot().tracks[(size_t) track].plugins; };
        check (devices().size() == 1 && devices()[0].isInstrument && devices()[0].name == probe.name, "it becomes the track's instrument");
        s.undo();
        check (devices().empty(), "undo brings back the built-in synth");
        s.redo();
        ok (s.addNativeEffect (track, Session::EffectKind::eqThree), "add an EQ Three");
        check (devices().size() == 2 && devices()[0].isInstrument && ! devices()[1].isInstrument, "effects follow the instrument");
        ok (s.removePlugin (track, 1), "remove the EQ");
        ok (s.setTrackInstrument (trackId, Session::EffectKind::polySynth), "choose the Poly Synth");
        check (devices().empty(), "a native instrument replaces the probe");
        s.undo();
        check (devices().size() == 1 && devices()[0].isInstrument, "undo restores the probe");

        // Notes at four beats; offline at 120 BPM.
        ok (s.addMidiClip (trackId, 0, 16), "a 16-beat clip");
        juce::StringArray ids;
        const std::vector<double> beats { .5, 1.25, 3.0, 7.75 };
        std::vector<Session::MidiNoteInfo> notes;
        for (const auto beat : beats) notes.push_back ({ {}, 60, beat, .25, .8f });
        ok (s.addMidiNotes (s.snapshot().tracks[(size_t) track].clips[0].id, notes, ids), "notes at beats 0.5, 1.25, 3 and 7.75");
        for (const auto bpm : { 120.0, 90.0 })
        {
            s.setTempo (bpm);
            const auto x = render (s, root.getChildFile ("offline-" + juce::String (bpm, 0) + ".wav"));
            const double perBeat = 48000 * 60 / bpm;
            const auto heard = notesIn (x);
            bool exact = heard.size() == beats.size();
            for (size_t i = 0; exact && i < beats.size(); ++i) exact = heard[i] == (int) std::lround (beats[i] * perBeat);
            juce::StringArray at; for (const auto n : heard) at.add (juce::String (n));
            check (exact, "offline at " + juce::String (bpm, 0) + " BPM every note arrives at its exact sample (" + at.joinIntoString (", ") + ")");
            float sign = 0;
            const auto reported = beatsIn (x, sign);
            bool aligned = reported.size() >= 15;
            for (const auto n : reported) aligned = aligned && within (n, std::round (n / perBeat) * perBeat);
            check (aligned, "the host's beats fall on their samples (" + juce::String ((int) reported.size()) + " beats)");
            check (sign < 0, "and it says it renders offline");
        }
        s.setTempo (120);

        // Live through the synthetic device, at two block sizes.
        for (const int block : { 256, 128 })
        {
            auto setup = s.audioDeviceManager().getAudioDeviceSetup(); setup.bufferSize = block;
            check (s.audioDeviceManager().setAudioDeviceSetup (setup, true).isEmpty(), juce::String (block) + "-sample blocks");
            pumpSyntheticTestDevice (s, 40);
            s.seek (0); ok (s.play(), "play");
            const auto x = captureSyntheticTestDevice (s, 48000 * 3 / block);
            s.stop(); pumpSyntheticTestDevice (s, 20);
            const auto heard = notesIn (x);
            float sign = 0;
            const auto reported = beatsIn (x, sign);
            // At 120 BPM, beats 0.5, 1.25 and 3 are 18000 and 42000 samples apart, and 1.25 is 6000
            // samples after the beat at 1.
            bool spaced = heard.size() >= 3 && within (heard[1] - heard[0], 18000) && within (heard[2] - heard[1], 42000);
            bool beatsSpaced = reported.size() >= 4;
            for (size_t i = 1; beatsSpaced && i < reported.size(); ++i) beatsSpaced = within (reported[i] - reported[i - 1], 24000);
            const auto afterBeat = [&] { for (const auto b : reported) if (heard.size() > 1 && heard[1] > b && heard[1] - b < 24000) return heard[1] - b; return -1; }();
            juce::StringArray at; for (const auto n : heard) at.add (juce::String (n));
            check (spaced && beatsSpaced && within (afterBeat, 6000), "live at " + juce::String (block) + "-sample blocks, notes (" + at.joinIntoString (", ")
                   + ") and beats keep their exact spacing; the second note is " + juce::String (afterBeat) + " samples after its beat");
            check (sign > 0, "and the host says it plays live");
        }
        std::cout << "Plugin timing passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL plugin timing: " << e.what() << std::endl; return 1; }
}
}
