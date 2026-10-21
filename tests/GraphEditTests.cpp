#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Graph edits during playback, through the synthetic device: 96 edits while a sine clip and a
// MIDI clip play. Devices come and go, the output moves to Sends Only and back, sends, mutes and
// gains change, the instrument is swapped and tracks are added and removed. Playback keeps going
// with finite, bounded output throughout, the play head keeps time, and once the edits are put
// back the clip plays at its level again.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS graph edits: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { if (r.failed()) throw std::runtime_error ((text + ": " + r.getErrorMessage()).toStdString()); }
double level (const juce::AudioBuffer<float>& x)
{
    double sum = 0; for (int n = 0; n < x.getNumSamples(); ++n) sum += x.getSample (0, n) * x.getSample (0, n);
    return std::sqrt (sum / std::max (1, x.getNumSamples()));
}
}

int runGraphEditTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("graph-edits-" + juce::Uuid().toString()); check (root.createDirectory().wasOk(), "create fixtures");
        const auto source = root.getChildFile ("sine.wav");
        {
            juce::AudioBuffer<float> x (2, 60 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.25 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * n / 48000)));
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a minute of 1 kHz sine");
        }
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.importAudio (source, 0, 0), "import the sine");
        const auto audioId = s.snapshot().tracks[0].id;
        ok (s.addMidiTrack ("Keys"), "a MIDI track");
        const int keys = (int) s.snapshot().tracks.size() - 1;
        const auto keysId = s.snapshot().tracks[(size_t) keys].id;
        ok (s.addMidiClip (keysId, 0, 128), "a long MIDI clip");
        juce::StringArray ids;
        ok (s.addMidiNotes (s.snapshot().tracks[(size_t) keys].clips[0].id, { { {}, 60, 0, 128, .5f }, { {}, 67, 0, 128, .5f } }, ids), "a held chord");
        ok (s.addReturnTrack ("Return"), "a return track");
        const auto returnId = s.snapshot().tracks.back().id;
        ok (s.setTrackMute (keys, true), "mute the chord for the baseline");
        pumpSyntheticTestDevice (s, 8);
        s.seek (0); check (s.play().wasOk(), "play");
        pumpSyntheticTestDevice (s, 40);
        const auto baseline = level (captureSyntheticTestDevice (s, 40));
        check (baseline > .1, "the sine plays (level " + juce::String (baseline, 4) + ")");
        ok (s.setTrackMute (keys, false), "unmute the chord");

        juce::Random random (0x47726170);
        const auto start = s.snapshot().position;
        int blocks = 0, added = 0;
        bool instrument = false, sendsOnly = false;
        for (int i = 0; i < 96; ++i)
        {
            switch (i % 8)
            {
                case 0: ok (s.addNativeEffect (0, Session::EffectKind::eqThree), "add an EQ Three"); ++added; break;
                case 1: if (added > 0) { ok (s.removePlugin (0, (int) s.snapshot().tracks[0].plugins.size() - 1), "remove it"); --added; } break;
                case 2: sendsOnly = ! sendsOnly; ok (s.setTrackOutput (audioId, { sendsOnly ? Session::OutputKind::sendsOnly : Session::OutputKind::master, {} }), "move the output"); break;
                case 3: ok (s.setTrackSendGain (0, returnId, -40 + 34 * random.nextFloat()), "change the send"); break;
                case 4: instrument = ! instrument; ok (s.setTrackInstrument (keysId, instrument ? std::optional (Session::EffectKind::polySynth) : std::nullopt), "swap the instrument"); break;
                case 5: ok (s.addAudioTrack ("Extra"), "add a track"); break;
                case 6:
                {
                    const auto tracks = s.snapshot().tracks;
                    for (int t = (int) tracks.size(); --t >= 0;) if (tracks[(size_t) t].name == "Extra") { ok (s.removeTrack (t), "remove the added track"); break; }
                    break;
                }
                case 7: ok (s.setTrackGain (keys, -24 + 24 * random.nextFloat()), "change a gain"); break;
            }
            const auto heard = captureSyntheticTestDevice (s, 3); blocks += 3;
            for (int c = 0; c < heard.getNumChannels(); ++c)
                for (int n = 0; n < heard.getNumSamples(); ++n)
                    if (! std::isfinite (heard.getSample (c, n)) || std::abs (heard.getSample (c, n)) > 4)
                        throw std::runtime_error (("edit " + juce::String (i) + " made a sample of " + juce::String (heard.getSample (c, n))).toStdString());
            if (! s.snapshot().playing) throw std::runtime_error (("edit " + juce::String (i) + " stopped playback").toStdString());
        }
        check (true, "96 edits kept playback going with finite output under four");
        // The reported position follows the play head by a timer, which can lag on slow machines, so
        // time is measured over a longer run: a stall or a jump would still be far outside.
        pumpSyntheticTestDevice (s, 400); blocks += 400;
        const auto travelled = s.snapshot().position - start, expected = blocks * 256 / 48000.0;
        check (std::abs (travelled - expected) < std::max (.3, expected * .12),
               "the play head kept time (" + juce::String (travelled, 3) + " s for " + juce::String (expected, 3) + " s of blocks)");

        while (added-- > 0) ok (s.removePlugin (0, (int) s.snapshot().tracks[0].plugins.size() - 1), "remove the remaining EQs");
        ok (s.setTrackOutput (audioId, { Session::OutputKind::master, {} }), "output to Master");
        ok (s.setTrackSendGain (0, returnId, -100), "send off");
        ok (s.setTrackMute (keys, true), "mute the chord");
        pumpSyntheticTestDevice (s, 40);
        const auto after = level (captureSyntheticTestDevice (s, 40));
        check (std::abs (after / baseline - 1) < .01, "with the edits put back, the sine plays at its level (" + juce::String (after / baseline, 4) + ")");
        s.stop();
        std::cout << "Graph edits passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL graph edits: " << e.what() << std::endl; return 1; }
}
}
