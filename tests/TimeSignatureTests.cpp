#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

// The project time signature at 120 BPM and 48 kHz. Beats stay quarter notes in every
// meter (24000 samples), so a 6/8 note at beat 3 still starts at 1.5 s, while a launch on
// the bar grid in 3/4 waits for beat 3.
namespace halftrack
{
namespace
{
namespace te = tracktion;
constexpr int beat = 24000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS time signature: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
Session::TimeSignature meter (Session& s) { return s.snapshot().timeSignature; }
int onset (const juce::AudioBuffer<float>& x, float threshold = .001f)
{ for (int n = 0; n < x.getNumSamples(); ++n) if (std::abs (x.getSample (0, n)) > threshold) return n; return -1; }
te::Engine& engineFor (Session& session)
{
    for (auto* engine : te::Engine::getEngines())
        if (&engine->getDeviceManager().deviceManager == &session.audioDeviceManager()) return *engine;
    throw std::runtime_error ("session engine not found");
}
}

int runTimeSignatureTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("time-signature-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false);
        check (meter (s) == Session::TimeSignature {} && meter (s).beatsPerBar() == 4, "new projects are in 4/4");
        check (s.setTimeSignature (0, 4).failed() && s.setTimeSignature (100, 4).failed() && s.setTimeSignature (4, 3).failed()
               && s.setTimeSignature (4, 32).failed() && ! s.canUndo(), "numerators outside 1-99 and denominators other than 1, 2, 4, 8 or 16 are rejected");
        ok (s.setTimeSignature (6, 8), "choose 6/8");
        check (meter (s) == Session::TimeSignature { 6, 8 } && meter (s).beatsPerBar() == 3, "6/8 has three quarter-note beats per bar");
        s.undo(); check (meter (s) == Session::TimeSignature {}, "undo restores 4/4");
        s.redo(); check (meter (s) == Session::TimeSignature { 6, 8 }, "redo restores 6/8");

        // Beats are quarter notes in every meter.
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const auto keys = s.snapshot().tracks.back().id;
        ok (s.addMidiClip (keys, 0, 8), "an eight-beat clip");
        ok (s.addMidiNote (s.snapshot().tracks.back().clips[0].id, 60, 3, 1, .8f), "a note at beat 3");
        const auto file = root.getChildFile ("render.wav");
        const auto renderOnset = [&]
        {
            ok (s.exportWav (file, 48000, 32, 0), "render");
            juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
            juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return onset (x);
        };
        check (std::abs (renderOnset() - 3 * beat) < 64, "in 6/8 the note at beat 3 starts at 1.5 s");
        ok (s.setTimeSignature (4, 4), "back to 4/4");
        check (std::abs (renderOnset() - 3 * beat) < 64, "in 4/4 it starts at the same time");
        ok (s.setTimeSignature (7, 8), "choose 7/8");

        // Reopening and MIDI files.
        const auto project = root.getChildFile ("Meter").getChildFile ("Meter.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create the project folder");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check (meter (s) == Session::TimeSignature { 7, 8 }, "the meter reopens");
        const auto midi = root.getChildFile ("meter.mid");
        ok (s.exportMidiClip (s.snapshot().tracks.back().clips[0].id, midi), "export the clip as a MIDI file");
        {
            juce::FileInputStream in (midi); juce::MidiFile parsed; check (parsed.readFrom (in), "read the MIDI file");
            bool found = false;
            for (int t = 0; t < parsed.getNumTracks(); ++t)
                for (auto* e : *parsed.getTrack (t))
                    if (e->message.isTimeSignatureMetaEvent()) { int n = 0, d = 0; e->message.getTimeSignatureInfo (n, d); found = found || (n == 7 && d == 8); }
            check (found, "the MIDI file carries 7/8");
        }
        const auto corrupt = [&] (const juce::String& name, const std::function<void (juce::XmlElement&)>& change)
        {
            auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "parse the project");
            auto* tempo = xml->getChildByName ("EDIT")->getChildByName ("TEMPOSEQUENCE");
            check (tempo != nullptr && tempo->getChildByName ("TIMESIG") != nullptr, "the project stores its meter");
            change (*tempo);
            const auto broken = project.getSiblingFile (name + ".halftrack"); check (xml->writeTo (broken), "write " + name);
            const auto result = s.loadProject (broken);
            check (result.failed() && result.getErrorMessage().contains ("time signature") && meter (s) == Session::TimeSignature { 7, 8 }, name + " is rejected and the open project stays");
        };
        corrupt ("ZeroBeats", [] (juce::XmlElement& t) { t.getChildByName ("TIMESIG")->setAttribute ("numerator", 0); });
        corrupt ("OddDenominator", [] (juce::XmlElement& t) { t.getChildByName ("TIMESIG")->setAttribute ("denominator", 3); });
        corrupt ("TwoMeters", [] (juce::XmlElement& t) { auto* copy = new juce::XmlElement (*t.getChildByName ("TIMESIG")); copy->setAttribute ("startBeat", 8); t.addChildElement (copy); });

        // A launch on the bar grid in 3/4 waits for beat 3.
        {
            std::unique_ptr<choc::buffer::InterleavedBuffer<float>> buffer; // Outlives the session and its graph readers.
            Session live (false);
            auto& engine = engineFor (live);
            te::HostedAudioDeviceInterface::Parameters p; p.sampleRate = 48000; p.blockSize = 128; p.inputChannels = 0; p.outputChannels = 2; p.useMidiDevices = false;
            te::test_utilities::EnginePlayer player (engine, p);
            const auto source = root.getChildFile ("level.wav");
            juce::AudioBuffer<float> audio (2, 4 * beat);
            for (int n = 0; n < audio.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) audio.setSample (c, n, .2f);
            {
                juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
                auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
                check (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write a constant source");
            }
            ok (live.importAudioToSlot (source, 0, 0), "a Session clip");
            buffer = std::make_unique<choc::buffer::InterleavedBuffer<float>> (2, (unsigned int) audio.getNumSamples());
            for (unsigned int c = 0; c < 2; ++c) for (unsigned int n = 0; n < (unsigned int) audio.getNumSamples(); ++n) buffer->getSample (c, n) = .2f;
            engine.getAudioFileManager().registerMemoryBuffer (live.snapshot().tracks[0].slots[0].clip.file.getFullPathName().toStdString(), buffer->getView(), 48000);
            ok (live.setTimeSignature (3, 4), "choose 3/4");
            ok (live.setLaunchQuantisation (Session::LaunchQuantisation::bar), "launch on bars");
            ok (live.play(), "start the transport");
            player.process (4800);
            ok (live.launchSlot (0, 0), "launch the clip after a tenth of a second");
            const auto out = player.process (90000);
            check (std::abs (onset (out, .01f) - (3 * beat - 4800)) <= 256, "the clip starts on the 3/4 bar at beat 3 (onset " + juce::String (onset (out, .01f) + 4800) + ")");
            live.stop();
        }
        std::cout << "Time signature passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL time signature: " << e.what() << std::endl; return 1; }
}
}
