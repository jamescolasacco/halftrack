#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Audio Engine Off and On, through the synthetic device. Turning the engine off ends a recording
// with its take kept, stops the transport and closes the device, after which meters fall silent and
// nothing plays or records. Turning it on reopens the device with no note left sounding.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS audio engine: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
}

int runAudioEngineTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("audio-engine-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Engine").getChildFile ("Engine.halftrack")), "save a recording destination");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const auto keys = s.snapshot().tracks.back().id;
        const int keysIndex = (int) s.snapshot().tracks.size() - 1;
        ok (s.addMidiClip (keys, 0, 64), "add a long clip");
        juce::StringArray added;
        ok (s.addMidiNotes (s.snapshot().tracks[(size_t) keysIndex].clips[0].id, { { "", 60, 0, 64, 1 } }, added), "hold one note through it");
        ok (s.setTrackInput (0, s.inputNames()[0]), "route the input"); ok (s.setTrackArmed (0, true), "arm track 1");
        pumpSyntheticTestDevice (s, 8);
        check (s.isAudioEngineEnabled(), "the engine starts on");

        s.seek (0); ok (s.record(), "record");
        const auto playing = captureSyntheticTestDevice (s, 94);
        check (playing.getMagnitude (0, playing.getNumSamples()) > .01f, "the held note sounds");
        check (s.snapshot().tracks[(size_t) keysIndex].peakL > 0, "and meters");

        ok (s.setAudioEngineEnabled (false), "turn the engine off while recording");
        auto now = s.snapshot();
        check (! s.isAudioEngineEnabled() && s.audioDeviceManager().getCurrentAudioDevice() == nullptr && ! now.playing && ! now.recording,
               "the device closes and the transport stops");
        check (now.tracks[0].clips.size() == 1 && now.tracks[0].clips[0].file.existsAsFile() && now.tracks[0].clips[0].length > .3, "the take is kept");
        juce::ignoreUnused (s.snapshot());
        now = s.snapshot();
        check (now.tracks[(size_t) keysIndex].peakL == 0 && now.masterPeakL == 0, "meters fall silent");
        check (s.play().getErrorMessage().contains ("Turn the audio engine on") && s.record().failed() && s.launchScene (0).failed() && ! s.snapshot().playing,
               "nothing plays, records or launches, and the reason is given");

        ok (s.setAudioEngineEnabled (true), "turn the engine on");
        check (s.isAudioEngineEnabled() && s.audioDeviceManager().getCurrentAudioDevice() != nullptr, "the device reopens");
        const auto after = captureSyntheticTestDevice (s, 40);
        check (after.getMagnitude (0, after.getNumSamples()) < 1e-6f, "no note is left sounding");
        s.seek (0); ok (s.play(), "play again");
        const auto again = captureSyntheticTestDevice (s, 40);
        s.stop();
        check (again.getMagnitude (0, again.getNumSamples()) > .01f, "and playback works as before");
        {
            // Delay Compensation: two tracks play the same click, one through a Limiter, whose 1 ms
            // lookahead the engine compensates. Off, that track's click is heard 48 samples late.
            const auto click = root.getChildFile ("click.wav");
            {
                juce::AudioBuffer<float> audio (1, 14400); audio.clear(); audio.setSample (0, 4800, .4f);
                juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (click.createOutputStream());
                auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (24));
                check (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write a click");
            }
            Session c (false, makeSyntheticTestDeviceType());
            ok (c.importAudio (click, 0, 0) , "the click on track 1");
            ok (c.importAudio (click, 1, 0), "and on track 2");
            ok (c.addNativeEffect (1, Session::EffectKind::limiter), "track 2 through a Limiter");
            check (c.delayCompensation(), "Delay Compensation is on at first");
            const auto clicks = [&] (const juce::String& when)
            {
                pumpSyntheticTestDevice (c, 8);
                c.seek (0); ok (c.play(), "play " + when);
                const auto heard = captureSyntheticTestDevice (c, 60);
                c.stop();
                std::vector<int> at;
                for (int n = 0; n < heard.getNumSamples(); ++n)
                    if (std::abs (heard.getSample (0, n)) > .05f && (at.empty() || n > at.back() + 4)) at.push_back (n);
                return at;
            };
            const auto together = clicks ("compensated");
            check (together.size() == 1, "compensated, the two clicks are heard as one (" + juce::String ((int) together.size()) + " found)");
            const auto steps = c.undoSteps().size();
            c.setDelayCompensation (false);
            check (! c.delayCompensation() && c.undoSteps().size() == steps, "switching it off is outside the undo history");
            const auto apart = clicks ("uncompensated");
            check (apart.size() == 2 && apart[1] - apart[0] == 48, "uncompensated, the Limiter's click is heard its 48 samples of lookahead late ("
                   + (apart.size() == 2 ? juce::String (apart[1] - apart[0]) : juce::String ((int) apart.size()) + " clicks") + ")");
            c.setDelayCompensation (true);
            check (clicks ("compensated again").size() == 1, "and on again they line up");
        }
        std::cout << "Audio engine passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL audio engine: " << e.what() << std::endl; return 1; }
}
}
