#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Audio From another track, through the synthetic device, with a constant 0.5 signal on track A
// played Sends Only so only the routed copy is heard. Track B takes A's output after its devices
// and mixer: heard through B's mixer as B monitors it, recorded as B's take, rendered in exports,
// and reopened. Routes that would loop are refused.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS audio from: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
bool near (float a, float b, float tolerance = 1e-3f) { return std::abs (a - b) < tolerance; }
// The output level once playback has settled, from the last of a short run of blocks.
float heard (Session& s)
{
    s.seek (.25); ok (s.play(), "play");
    const auto out = captureSyntheticTestDevice (s, 40);
    s.stop();
    return out.getSample (0, out.getNumSamples() - 1);
}
}

int runAudioFromTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("audio-from-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("half.wav");
        {
            juce::AudioBuffer<float> x (2, 4 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .5f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a constant 0.5 signal");
        }
        Session s (false, makeSyntheticTestDeviceType());
        const auto project = root.getChildFile ("Routed").getChildFile ("Routed.halftrack");
        ok (s.saveProject (project), "save a recording destination");
        ok (s.importAudio (source, 0, 0), "put it on track A");
        const auto a = s.snapshot().tracks[0].id, b = s.snapshot().tracks[1].id;
        const auto trackB = [&] { return s.snapshot().tracks[1]; };
        ok (s.setTrackOutput (a, { Session::OutputKind::sendsOnly, {} }), "play A Sends Only");
        pumpSyntheticTestDevice (s, 8);
        check (std::abs (heard (s)) < 1e-6f, "with nothing routed, A is not heard");

        check (s.inputSources (b) == juce::StringArray { a } && s.inputSources ("missing").isEmpty(), "B can take audio from A");
        ok (s.setTrackInputTrack (b, a), "B takes audio from A");
        pumpSyntheticTestDevice (s, 8);
        check (trackB().inputTrackId == a && trackB().inputName.isEmpty() && trackB().monitor == Session::MonitorMode::off, "B shows A as its input");
        check (std::abs (heard (s)) < 1e-6f, "with monitoring off, B is silent");
        ok (s.setTrackMonitor (1, Session::MonitorMode::on), "monitor B In");
        pumpSyntheticTestDevice (s, 8);
        check (near (heard (s), .5f), "B plays A's audio");
        ok (s.setTrackGain (1, -6), "B at -6 dB");
        pumpSyntheticTestDevice (s, 8);
        const auto g = std::pow (10.0f, -6 / 20.0f);
        check (near (heard (s), .5f * g), "through B's mixer");
        ok (s.setTrackGain (0, -6), "A at -6 dB");
        pumpSyntheticTestDevice (s, 8);
        check (near (heard (s), .5f * g * g), "after A's mixer");
        ok (s.setTrackGain (1, 0), "B back to 0 dB");

        // Taps, with a -6 dB Utility on A before its -6 dB mixer.
        ok (s.addUtility (0), "add a Utility to A");
        const auto utility = s.snapshot().tracks[0].plugins.back();
        auto settings = utility.utility; settings.gainDb = -6;
        ok (s.setUtilitySettings (utility.id, settings), "the Utility at -6 dB");
        pumpSyntheticTestDevice (s, 8);
        check (trackB().inputTap == Session::InputTap::postMixer && near (heard (s), .5f * g * g), "Post Mixer follows A's devices and mixer");
        ok (s.setTrackInputTrack (b, a, Session::InputTap::postFX), "tap A Post FX");
        pumpSyntheticTestDevice (s, 8);
        check (trackB().inputTap == Session::InputTap::postFX && near (heard (s), .5f * g), "Post FX follows A's devices but not its mixer");
        {
            const auto file = root.getChildFile ("post-fx.wav");
            ok (s.exportWav (file, 48000, 32, 0), "export the Post FX tap");
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
            check (x.getNumSamples() > 24000 && near (x.getSample (0, 24000), .5f * g), "the export takes the same tap");
        }
        ok (s.setTrackInputTrack (b, a, Session::InputTap::preFX), "tap A Pre FX");
        pumpSyntheticTestDevice (s, 8);
        check (trackB().inputTap == Session::InputTap::preFX && near (heard (s), .5f), "Pre FX comes before A's devices");
        s.undo();
        check (trackB().inputTap == Session::InputTap::postFX, "each tap change is one undo step");
        s.redo();
        ok (s.setTrackSolo (1, true), "solo B, which mutes A");
        pumpSyntheticTestDevice (s, 8);
        check (near (heard (s), .5f), "soloed, B still hears A Pre FX");
        ok (s.setTrackInputTrack (b, a, Session::InputTap::postMixer), "tap A Post Mixer");
        pumpSyntheticTestDevice (s, 8);
        check (std::abs (heard (s)) < 1e-6f, "but not Post Mixer, after A's muting");
        ok (s.setTrackSolo (1, false), "unsolo B");
        ok (s.removePlugin (0, (int) s.snapshot().tracks[0].plugins.size() - 1), "remove the Utility");
        ok (s.setTrackGain (0, 0), "A back to 0 dB");

        // Loops.
        check (s.inputSources (a).isEmpty() && s.setTrackInputTrack (a, b).failed(), "A cannot take audio from B, which it feeds");
        check (! s.outputDestinations (b).contains (a) && s.setTrackOutput (b, { Session::OutputKind::track, a }).failed(), "nor can B play into A");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const auto keys = s.snapshot().tracks.back().id;
        check (s.inputSources (b).contains (keys) && s.setTrackInputTrack (keys, a).failed(), "an instrument track is a source, but takes MIDI rather than audio");
        ok (s.addReturnTrack ("Return"), "add a return");
        const auto ret = s.snapshot().tracks.back().id;
        check (s.setTrackInputTrack (ret, a).failed(), "a return takes no recording input");
        // A return is a source, as the reference lists it: B hears A's send through it.
        ok (s.setTrackSendGain (0, ret, 0), "send A to the return at 0 dB");
        check (s.inputSources (b).contains (ret), "the return is offered as a source");
        ok (s.setTrackInputTrack (b, ret, Session::InputTap::postMixer), "B takes the return's audio");
        ok (s.setTrackMonitor (1, Session::MonitorMode::on), "monitor B In, as a new input resets it");
        ok (s.setTrackOutput (ret, { Session::OutputKind::sendsOnly, {} }), "the return Sends Only, so only B plays it");
        pumpSyntheticTestDevice (s, 8);
        check (near (heard (s), .5f), "B hears A's send through the return's Post Mixer tap");
        check (s.setTrackSendGain (1, ret, 0).failed(), "B cannot send into the return whose audio it takes");
        ok (s.setTrackInputTrack (b, a, Session::InputTap::postMixer), "B takes A again");
        ok (s.setTrackMonitor (1, Session::MonitorMode::on), "monitor B In again");
        ok (s.setTrackOutput (ret, {}), "the return to Main again");
        ok (s.setTrackSendGain (0, ret, -100), "A's send off");
        check (! s.inputSources (a).contains (ret), "A, which has a send to the return, cannot take its audio");

        // Resampling A into B.
        ok (s.setTrackArmed (1, true), "arm B");
        pumpSyntheticTestDevice (s, 8);
        s.seek (0); ok (s.record(), "record");
        pumpSyntheticTestDevice (s, 187);
        s.stop();
        const auto takes = trackB().clips;
        check (takes.size() == 1 && takes[0].file.existsAsFile(), "B records a take");
        {
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (takes[0].file));
            check (reader != nullptr && reader->lengthInSamples > 24000, "read the take");
            juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
            check (near (x.getSample (0, 24000), .5f), "the take holds A's audio");
        }
        s.undo();
        ok (s.setTrackArmed (1, false), "disarm B");

        // Exports and reopening.
        const auto out = root.getChildFile ("out.wav");
        ok (s.exportWav (out, 48000, 32, 0), "export");
        {
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out));
            juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
            check (x.getNumSamples() > 24000 && near (x.getSample (0, 24000), .5f), "the export holds the routed audio, as monitoring plays it");
        }
        ok (s.saveProject (project), "save");
        ok (s.loadProject (project), "reopen");
        check (s.snapshot().tracks[1].inputTrackId == a, "the route reopens");
        ok (s.setTrackInput (1, {}), "give B no input");
        check (trackB().inputTrackId.isEmpty(), "B no longer takes A's audio");
        s.undo();
        check (trackB().inputTrackId == a, "undo routes it again");
        std::cout << "Audio from passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL audio from: " << e.what() << std::endl; return 1; }
}
}
