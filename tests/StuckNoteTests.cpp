#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Stuck notes, through the synthetic device with a sine voice that holds its note at full sustain.
// While a long note sounds, the transport stops, seeks, loops across the note's end, changes
// tempo; a Session clip stops; the clip or track is deleted; the instrument is swapped. Each time
// the note ends within the release (a quarter second), so nothing is left sounding.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS stuck notes: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
float peak (Session& s, int blocks)
{
    const auto x = captureSyntheticTestDevice (s, blocks);
    return x.getMagnitude (0, 0, x.getNumSamples());
}
}

int runStuckNoteTests (const juce::File&)
{
    try
    {
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.addMidiTrack ("Held"), "add a MIDI track");
        int track = (int) s.snapshot().tracks.size() - 1;
        auto trackId = s.snapshot().tracks[(size_t) track].id;
        for (int t = 0; t < track; ++t) ok (s.setTrackMute (t, true), "mute the other tracks");
        Session::SynthSettings sine; sine.waveform = 2; sine.attackSeconds = .002f; sine.sustain = 1; sine.releaseSeconds = .05f; sine.cutoffHz = 20000;
        ok (s.setSynthSettings (trackId, sine), "a sine voice holding its notes");
        const auto addNote = [&] (const juce::String& clipId, double start, double length)
        {
            juce::StringArray ids;
            ok (s.addMidiNotes (clipId, { { {}, 60, start, length, .8f } }, ids), "a long note");
        };
        ok (s.addMidiClip (trackId, 0, 64), "a 64-beat clip");
        auto clipId = s.snapshot().tracks[(size_t) track].clips[0].id;
        addNote (clipId, 0, 64);
        pumpSyntheticTestDevice (s, 8);
        const auto playFor = [&] (double seconds) { s.seek (0); ok (s.play(), "play"); pumpSyntheticTestDevice (s, (int) (seconds * blocksPerSecond)); };
        const auto silentSoon = [&] (const juce::String& what)
        {
            pumpSyntheticTestDevice (s, blocksPerSecond / 4);
            const auto left = peak (s, blocksPerSecond / 4);
            check (left < 1e-4f, what + " leaves nothing sounding (peak " + juce::String (left, 6) + ")");
        };

        playFor (.5);
        check (peak (s, 20) > .05f, "the note sounds");
        s.stop(); silentSoon ("stopping");

        playFor (.5);
        s.seek (20); // Beat 40, still inside the note: it restarts there.
        pumpSyntheticTestDevice (s, 20);
        check (peak (s, 20) > .05f, "seeking inside the note plays it from there");
        s.seek (40); // Past the clip's end.
        silentSoon ("seeking past the clip");
        s.stop();

        // A loop ending inside the note: the note ends at the loop end and starts again at its start.
        s.setLoop (true, 0, 1);
        playFor (2.2);
        const auto looped = peak (s, 10);
        s.setLoop (false, 0, 1);
        check (looped > .05f, "a loop inside the note keeps it sounding");
        s.stop(); silentSoon ("stopping a loop");

        playFor (.5);
        s.setTempo (90);
        pumpSyntheticTestDevice (s, 20);
        check (peak (s, 20) > .05f, "changing tempo keeps the note");
        s.stop(); silentSoon ("stopping after a tempo change");
        s.setTempo (120);

        // Deleting the clip while its note sounds.
        playFor (.5);
        ok (s.removeClip (track, 0), "delete the clip");
        silentSoon ("deleting the sounding clip");
        s.stop();
        s.undo();
        clipId = s.snapshot().tracks[(size_t) track].clips[0].id;

        // Swapping the instrument while its note sounds.
        playFor (.5);
        ok (s.setTrackInstrument (trackId, Session::EffectKind::polySynth), "swap to the Poly Synth");
        s.stop(); silentSoon ("swapping the instrument, then stopping");
        ok (s.setTrackInstrument (trackId, std::nullopt), "back to the built-in synth");
        ok (s.setSynthSettings (trackId, sine), "the sine voice again");

        // A Session clip stopped while its note sounds.
        ok (s.addMidiClipToSlot (trackId, 0, 16), "a Session clip");
        const auto slotClip = s.snapshot().tracks[(size_t) track].slots[0].clip.id;
        addNote (slotClip, 0, 16);
        ok (s.removeClip (track, 0), "clear the Arrangement");
        pumpSyntheticTestDevice (s, 8);
        ok (s.launchSlot (track, 0), "launch it");
        for (int i = 0; i < 2000 && ! s.snapshot().tracks[(size_t) track].slots[0].playing; ++i) pumpSyntheticTestDevice (s, 1);
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        check (peak (s, 20) > .05f, "the Session clip's note sounds");
        ok (s.stopLauncherTrack (track), "stop the track's clip");
        for (int i = 0; i < 2000 && s.snapshot().tracks[(size_t) track].slots[0].playing; ++i) pumpSyntheticTestDevice (s, 1);
        silentSoon ("stopping the Session clip");

        // Deleting the track while its note sounds.
        ok (s.launchSlot (track, 0), "launch it again");
        for (int i = 0; i < 2000 && ! s.snapshot().tracks[(size_t) track].slots[0].playing; ++i) pumpSyntheticTestDevice (s, 1);
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        ok (s.removeTrack (track), "delete the sounding track");
        silentSoon ("deleting the track");
        s.stop();
        std::cout << "Stuck notes passed" << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL stuck notes: " << e.what() << std::endl; return 1; }
}
}
