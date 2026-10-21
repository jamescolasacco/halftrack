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
        std::cout << "Audio engine passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL audio engine: " << e.what() << std::endl; return 1; }
}
}
