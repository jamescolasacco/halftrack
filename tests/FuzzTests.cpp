#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Malformed input, from a fixed seed with bounded sizes. A project using most of the model is
// saved, then mutated 300 ways: attribute values swapped for empty, non-finite, huge, negative or
// junk text, elements removed or duplicated, the file cut short and bytes flipped. Each load
// either succeeds, and the result saves and reopens, or is refused leaving the open project as it
// was. Malformed audio, MIDI and preset files are refused without changing the project.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS fuzz: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
// What a load must keep when refused: the tracks, their clips and their notes.
juce::String signature (Session& s)
{
    juce::String text;
    for (const auto& t : s.snapshot().tracks)
    {
        text << t.id << ":" << (int) t.clips.size() << "/" << (int) t.plugins.size();
        for (const auto& c : t.clips) text << "," << c.id << "=" << (int) c.notes.size() << "+" << (int) c.envelopes.size();
        text << ";";
    }
    return text;
}
void collect (juce::XmlElement& e, std::vector<juce::XmlElement*>& all) { all.push_back (&e); for (auto* c : e.getChildIterator()) collect (*c, all); }
juce::String mutate (const juce::String& original, juce::Random& random)
{
    const int kind = random.nextInt (5);
    if (kind == 3) return original.substring (0, random.nextInt (original.length()));
    if (kind == 4)
    {
        auto text = original;
        for (int i = 0; i < 1 + random.nextInt (8); ++i)
        {
            const int at = random.nextInt (text.length());
            text = text.replaceSection (at, 1, juce::String::charToString ((juce::juce_wchar) (32 + random.nextInt (95))));
        }
        return text;
    }
    auto xml = juce::parseXML (original);
    if (xml == nullptr) return original;
    std::vector<juce::XmlElement*> all; collect (*xml, all);
    auto* target = all[(size_t) random.nextInt ((int) all.size())];
    if (kind == 0 && target->getNumAttributes() > 0)
    {
        static const char* values[] { "", "nan", "inf", "-inf", "1e309", "-1", "-99999999", "99999999", "0.5", "junk", "0x7fffffff", "-0", "1/0/0/0/64", "2/0/0/0/64/1e40/0/0/0" };
        const auto name = target->getAttributeName (random.nextInt (target->getNumAttributes()));
        const auto replacement = random.nextInt (8) == 0 ? juce::String::repeatedString ("9", 4000) : juce::String (values[random.nextInt ((int) std::size (values))]);
        target->setAttribute (name, replacement);
    }
    else if (kind == 1 && target != xml.get())
    {
        for (auto* parent : all) if (parent->containsChildElement (target)) { parent->removeChildElement (target, true); break; }
    }
    else if (target != xml.get())
    {
        for (auto* parent : all) if (parent->containsChildElement (target)) { parent->addChildElement (new juce::XmlElement (*target)); break; }
    }
    return xml->toString();
}
}

int runFuzzTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("fuzz-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto wav = root.getChildFile ("tone.wav");
        {
            juce::AudioBuffer<float> x (2, 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.3 * std::sin (n * .05)));
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a tone");
        }
        // A project using most of the model.
        const auto project = root.getChildFile ("Rich").getChildFile ("Rich.halftrack");
        juce::String original;
        {
            Session s (false);
            ok (s.importAudio (wav, 0, 0), "an audio clip");
            ok (s.importAudio (wav, 0, 2), "a second one");
            auto clips = s.snapshot().tracks[0].clips;
            ok (s.setAudioClipWarp (clips[1].id, [] { Session::WarpSettings w { Session::WarpMode::texture, 120 }; w.grainSize = 40; w.flux = 20; return w; }()), "warped in Texture");
            ok (s.setClipEnvelope (clips[0].id, { { {}, 0 }, Session::EnvelopeMode::modulation, { { 0, 1 }, { 1, .5f } } }), "a clip envelope");
            ok (s.addNativeEffect (0, Session::EffectKind::equalizer), "a device");
            ok (s.setTrackAutomation (s.snapshot().tracks[0].id, Session::AutomationTarget::volume, { true, { { 0, -6 }, { 4, 0 } } }), "volume automation");
            ok (s.addMidiTrack ("Keys"), "a MIDI track");
            const auto keys = s.snapshot().tracks.back().id;
            ok (s.addMidiClip (keys, 0, 4), "a MIDI clip");
            const auto midi = s.snapshot().tracks.back().clips[0].id;
            juce::StringArray ids;
            Session::MidiNoteInfo note { {}, 60, 0, 1, .8f, true, .5f, 20 };
            note.pitchBend = { { 0, 0 }, { .5, 2 } };
            ok (s.addMidiNotes (midi, { note, { {}, 64, 1, 1, .6f } }, ids), "notes with chance and expression");
            ok (s.addReturnTrack ("Return"), "a return track");
            ok (s.setTrackSendGain (0, s.snapshot().tracks.back().id, -6), "a send");
            ok (s.saveProject (project), "save it");
            original = project.loadFileAsString();
        }
        check (original.length() > 1000, "the saved project is " + juce::String (original.length()) + " characters");

        Session victim (false);
        ok (victim.loadProject (project), "open the project");
        const auto kept = signature (victim);
        // HALFTRACK_FUZZ_SEED and HALFTRACK_FUZZ_COUNT explore further locally; the suite's run is fixed.
        const auto* seedText = std::getenv ("HALFTRACK_FUZZ_SEED");
        const auto* countText = std::getenv ("HALFTRACK_FUZZ_COUNT");
        juce::Random random (seedText != nullptr ? juce::String (seedText).getLargeIntValue() : 0x48616c66);
        const int count = countText != nullptr ? juce::jlimit (1, 100000, juce::String (countText).getIntValue()) : 300;
        int accepted = 0, refused = 0;
        const auto mutated = root.getChildFile ("Mutated").getChildFile ("Mutated.halftrack");
        const auto resaved = root.getChildFile ("Resaved").getChildFile ("Resaved.halftrack");
        check (mutated.getParentDirectory().createDirectory().wasOk() && resaved.getParentDirectory().createDirectory().wasOk(), "make room for mutants");
        for (int i = 0; i < count; ++i)
        {
            const auto text = mutate (original, random);
            if (! mutated.replaceWithText (text)) throw std::runtime_error ("could not write a mutant");
            if (victim.loadProject (mutated).wasOk())
            {
                ++accepted;
                // An accepted project saves and reopens; then the original comes back for the next mutant.
                if (resaved.existsAsFile()) resaved.deleteFile();
                if (auto saved = victim.saveProject (resaved); saved.failed())
                    throw std::runtime_error (("mutant " + juce::String (i) + " opened but would not save: " + saved.getErrorMessage()).toStdString());
                if (auto reopened = victim.loadProject (resaved); reopened.failed())
                    throw std::runtime_error (("mutant " + juce::String (i) + " saved but would not reopen: " + reopened.getErrorMessage()).toStdString());
                ok (victim.loadProject (project), "reopen the original after mutant " + juce::String (i));
            }
            else
            {
                ++refused;
                if (signature (victim) != kept) throw std::runtime_error (("refusing mutant " + juce::String (i) + " changed the open project").toStdString());
            }
        }
        check (accepted + refused == count && refused > 0, juce::String (refused) + " mutants were refused keeping the open project, and "
                                                         + juce::String (accepted) + " opened, saved and reopened");

        // Malformed media, MIDI and presets.
        const auto before = signature (victim);
        const auto junk = root.getChildFile ("junk.wav");
        juce::MemoryBlock bytes; for (int i = 0; i < 4096; ++i) bytes.append (&i, 1);
        check (junk.replaceWithData (bytes.getData(), bytes.getSize()), "write junk audio");
        check (victim.importAudio (junk, 0, 0).failed(), "junk audio is refused");
        const auto cut = root.getChildFile ("cut.wav");
        juce::MemoryBlock tone; wav.loadFileAsData (tone);
        check (cut.replaceWithData (tone.getData(), 60), "write a WAV cut inside its header");
        check (victim.importAudio (cut, 0, 0).failed(), "a WAV cut short is refused");
        const auto badMidi = root.getChildFile ("bad.mid");
        check (badMidi.replaceWithText ("MThd\x00\x00\x00\x06 junk junk"), "write a broken MIDI file");
        std::vector<juce::String> imported;
        check (victim.importMidiFile (badMidi, 0, imported).failed(), "a broken MIDI file is refused");
        const auto badPreset = root.getChildFile (juce::String ("bad") + Session::presetExtension);
        check (badPreset.replaceWithText ("<HALFTRACKPRESET version=\"1\" name=\"x\" device=\"equalizer\"><SETTINGS v0=\"nan\"/></HALFTRACKPRESET>"), "write a broken preset");
        Session::DevicePreset preset; juce::String loaded;
        check (Session::readDevicePreset (badPreset, preset).failed() && victim.loadDevicePreset (0, badPreset, loaded).failed(), "a broken preset is refused");
        check (signature (victim) == before, "none of them changed the project");
        std::cout << "Fuzz passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL fuzz: " << e.what() << std::endl; return 1; }
}
}
