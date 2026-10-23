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
        corrupt ("TwoAtTheStart", [] (juce::XmlElement& t) { t.addChildElement (new juce::XmlElement (*t.getChildByName ("TIMESIG"))); });
        corrupt ("LateFirst", [] (juce::XmlElement& t) { t.getChildByName ("TIMESIG")->setAttribute ("startBeat", 4); });

        // Time signature markers, as the reference's: each starts a bar of its meter where it is.
        {
            Session m (false);
            ok (m.insertTimeSignatureChange (8, { 3, 4 }), "a 3/4 marker at beat 8, the start of the third bar");
            auto markers = m.snapshot().timeSignatures;
            check (markers.size() == 2 && markers[1] == Session::TimeSignatureChange { 8, { 3, 4 } }, "the project has two markers");
            auto bar = Session::barAt (markers, 9);
            check (bar.number == 2 && bar.start == 8 && bar.length == 3 && bar.meter == Session::TimeSignature { 3, 4 } && ! bar.fragmentary,
                   "beat 9 is in the third bar, of 3/4 from beat 8");
            check (Session::barAt (markers, 11.5).number == 3 && Session::barStart (markers, 3) == 11 && Session::barStart (markers, 1) == 4, "the fourth bar starts at beat 11");
            ok (m.moveTimeSignatureChange (1, 6), "move it to beat 6, inside the second bar");
            markers = m.snapshot().timeSignatures;
            bar = Session::barAt (markers, 5);
            check (bar.number == 1 && bar.start == 4 && bar.length == 2 && bar.fragmentary, "the second bar is cut to two beats, a fragmentary bar");
            check (Session::barAt (markers, 6).number == 2 && Session::barStart (markers, 2) == 6, "and the third starts at the marker");
            m.undo(); check (m.snapshot().timeSignatures[1].beat == 8, "one undo moves it back"); m.redo();
            ok (m.setTimeSignatureChange (1, { 7, 8 }), "change it to 7/8");
            check (Session::barStart (m.snapshot().timeSignatures, 4) == 13, "7/8 bars are three and a half beats: the fifth bar starts at beat 13");
            ok (m.insertTimeSignatureChange (6, { 5, 4 }), "insert a 5/4 marker where the marker is");
            check (m.snapshot().timeSignatures.size() == 2 && m.snapshot().timeSignatures[1].meter == Session::TimeSignature { 5, 4 }, "which changes its meter");
            ok (m.insertTimeSignatureChange (20, { 2, 4 }), "a third marker at beat 20");
            ok (m.moveTimeSignatureChange (2, 3), "move it before the second");
            markers = m.snapshot().timeSignatures;
            check (markers.size() == 3 && markers[1] == Session::TimeSignatureChange { 3, { 2, 4 } } && markers[2].beat == 6, "the markers keep their beat order");
            check (m.moveTimeSignatureChange (0, 2).failed() && m.removeTimeSignatureChange (0).failed() && m.moveTimeSignatureChange (1, 6).failed()
                       && m.insertTimeSignatureChange (0, { 3, 4 }).failed() && m.insertTimeSignatureChange (-1, { 3, 4 }).failed()
                       && m.insertTimeSignatureChange (4, { 0, 4 }).failed() && m.removeTimeSignatureChange (9).failed(),
                   "the first marker stays at the start, markers do not share a beat, and invalid meters and markers are refused");
            // The Control Bar's fields change the meter at the playhead.
            m.setInsertMarker (m.secondsAt (7));
            ok (m.setTimeSignature (6, 8), "type 6/8 with the playhead at beat 7");
            markers = m.snapshot().timeSignatures;
            check (markers[2].meter == Session::TimeSignature { 6, 8 } && markers[0].meter == Session::TimeSignature {} && m.snapshot().timeSignature == Session::TimeSignature { 6, 8 },
                   "it changes the marker at beat 6, the one in effect there");
            const auto saved = root.getChildFile ("Markers").getChildFile ("Markers.halftrack");
            ok (saved.getParentDirectory().createDirectory(), "create the markers' folder");
            ok (m.saveProject (saved), "save the markers");
            Session reopened (false); ok (reopened.loadProject (saved), "reopen");
            check (reopened.snapshot().timeSignatures == markers, "the markers reopen");
            ok (m.removeTimeSignatureChange (1), "delete the marker at beat 3");
            check (m.snapshot().timeSignatures.size() == 2 && m.snapshot().timeSignatures[1].beat == 6, "it is gone");
            // A MIDI clip's file carries the meter at its start and the markers inside it.
            ok (m.addMidiTrack ("Keys"), "a MIDI track for the markers");
            ok (m.addMidiClip (m.snapshot().tracks.back().id, 4, 8), "a clip from beat 4 to 12, across the marker at beat 6");
            const auto file = root.getChildFile ("markers.mid");
            ok (m.exportMidiClip (m.snapshot().tracks.back().clips[0].id, file), "export it as a MIDI file");
            juce::FileInputStream in (file); juce::MidiFile parsed; check (parsed.readFrom (in), "read the MIDI file");
            juce::StringArray meters;
            for (int t = 0; t < parsed.getNumTracks(); ++t)
                for (auto* e : *parsed.getTrack (t))
                    if (e->message.isTimeSignatureMetaEvent())
                    { int n = 0, d = 0; e->message.getTimeSignatureInfo (n, d); meters.add (juce::String (n) + "/" + juce::String (d) + "@" + juce::String (e->message.getTimeStamp() / parsed.getTimeFormat(), 2)); }
            check (meters == juce::StringArray { "4/4@0.00", "6/8@2.00" }, "4/4 at its start, and 6/8 two beats in: " + meters.joinIntoString (", "));
        }

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
            // As the reference's: a bar of 7/8 is three and a half quarter notes, and a 1/4 grid is a
            // quarter note in any meter (the engine counted seven beats and three quarters of a bar).
            const auto relaunch = [&] (int numerator, int denominator, Session::LaunchQuantisation grid)
            {
                live.stop(); live.returnToStart(); player.process (2400);
                ok (live.setTimeSignature (numerator, denominator), "choose " + juce::String (numerator) + "/" + juce::String (denominator));
                ok (live.setLaunchQuantisation (grid), "choose the launch grid");
                ok (live.play(), "start the transport again");
                player.process (4800);
                ok (live.launchSlot (0, 0), "launch the clip after a tenth of a second");
                return onset (player.process (200000), .01f) + 4800;
            };
            const auto sevenEight = relaunch (7, 8, Session::LaunchQuantisation::bar);
            check (std::abs (sevenEight - 3.5 * beat) <= 256, "a launch on the bar grid in 7/8 waits for beat 3.5 (onset " + juce::String (sevenEight) + ")");
            const auto quarter = relaunch (3, 4, Session::LaunchQuantisation::beat);
            check (std::abs (quarter - beat) <= 256, "a launch on the 1/4 grid in 3/4 waits one quarter note (onset " + juce::String (quarter) + ")");
            // A marker inside a bar starts the next bar there, as the reference's do.
            live.stop(); player.process (2400);
            ok (live.setTimeSignature (4, 4), "back to 4/4");
            ok (live.setLaunchQuantisation (Session::LaunchQuantisation::bar), "launch on bars");
            ok (live.insertTimeSignatureChange (6, { 3, 4 }), "a 3/4 marker at beat 6, cutting the second bar to two beats");
            live.setInsertMarker (live.secondsAt (4));
            ok (live.play(), "play from beat 4");
            player.process (4800);
            ok (live.launchSlot (0, 0), "launch the clip a tenth of a second in");
            const auto fragment = onset (player.process (200000), .01f) + 4800;
            check (std::abs (fragment - 2 * beat) <= 256, "the clip starts at the marker, where the fragmentary bar ends (onset " + juce::String (fragment) + ")");
            live.stop();
        }
        std::cout << "Time signature passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL time signature: " << e.what() << std::endl; return 1; }
}
}
