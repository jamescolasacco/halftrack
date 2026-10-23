#include "Session.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

// The reference's fader laws (docs/research/mixer-laws-contract.md): track, group, return, Main
// and cue volume move along its volume scale and sends along its send scale, silent at the bottom.
// The expected levels are the research model's, from a static reading of the reference. Projects
// saved before Halftrack took these laws keep every level when they open.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS fader laws: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
using Target = Session::AutomationTarget;
bool near (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

// The engine's own law, which projects without a fader law were saved with.
double legacyPosition (double decibels) { return decibels <= -100 ? 0 : std::exp ((decibels - 6) / 20); }

juce::File constantSignal (const juce::File& root)
{
    const auto file = root.getChildFile ("half.wav");
    juce::AudioBuffer<float> x (2, 4 * 48000);
    for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .5f);
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a 4 s constant 0.5 signal");
    return file;
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr && reader->numChannels == 2, "read the render");
    juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples);
    reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
// The mean level of the left channel from one to two seconds.
double level (const juce::AudioBuffer<float>& x)
{
    double sum = 0; for (int n = 48000; n < 96000; ++n) sum += x.getSample (0, n);
    return sum / 48000;
}

void modelLevels()
{
    // The research model's levels, to its 0.1 dB.
    const std::pair<float, float> volume[] { { 1, 6 }, { .9f, 2 }, { .85f, 0 }, { .7f, -6 }, { .55f, -12 }, { .5f, -14 }, { .4f, -18 },
                                             { .3f, -24.2f }, { .2f, -34.4f }, { .1f, -48.6f }, { .01f, -66.4f } };
    for (const auto& [position, db] : volume)
        check (near (Session::automationProportionToValue (Target::volume, position), db, .05)
                   && near (Session::automationValueToProportion (Target::volume, db), position, 2e-3),
               "volume at " + juce::String (position) + " of its travel is " + juce::String (db) + " dB, and back");
    check (Session::automationProportionToValue (Target::volume, .0002f) == -100
               && near (Session::automationProportionToValue (Target::volume, .0003f), -69.867, .01),
           "volume is silent up to 0.000232 of its travel, and -69.9 dB just above");
    const auto send = Session::levelRange (true);
    const std::pair<double, double> sends[] { { 1, 0 }, { .85, -6 }, { .7, -12 }, { .5, -20 }, { .4, -24 }, { .3, -30.2 }, { .2, -40.4 }, { .1, -54.6 } };
    for (const auto& [position, db] : sends)
        check (near (send.convertFrom0to1 (position), db, .05) && near (send.convertTo0to1 (db), position, 2e-3),
               "a send at " + juce::String (position) + " of its travel is " + juce::String (db) + " dB, and back");
    check (send.convertFrom0to1 (.0016) == -100 && near (send.convertFrom0to1 (.002), -69.877, .01), "a send is silent up to 0.00167 of its travel");
    const auto fader = Session::levelRange (false), cue = Session::levelRange (false, -70);
    check (near (fader.convertTo0to1 (0), .85, 1e-6) && near (fader.convertTo0to1 (-14), .5, 1e-6) && fader.convertTo0to1 (-100) == 0
               && fader.convertFrom0to1 (0) == -100 && near (fader.convertFrom0to1 (1), 6, 1e-4),
           "a fader's travel follows the volume law, with silence at the bottom");
    check (cue.convertTo0to1 (-70) == 0 && cue.convertFrom0to1 (0) == -70 && near (cue.convertTo0to1 (-6), .7, 1e-6), "the cue volume's knob bottoms out at -70 dB");
}

void storedAndPlayed (const juce::File& root, const juce::File& signal)
{
    Session s (false);
    ok (s.importAudio (signal, 0), "put the signal on track 1");
    ok (s.addReturnTrack ("Room"), "add a return");
    const auto returnId = s.snapshot().tracks.back().id;
    ok (s.setTrackGain (0, -14), "track 1 at -14 dB");
    ok (s.setTrackSendGain (0, returnId, -20), "a send at -20 dB");
    const auto file = root.getChildFile ("Stored.halftrack");
    ok (s.saveProject (file), "save");
    auto xml = juce::XmlDocument::parse (file);
    check (xml != nullptr && xml->getChildByName ("EDIT")->getStringAttribute ("halftrackFaderLaw") == "2", "a new project is saved with its fader law");
    double fader = -1, sendPosition = -1;
    std::function<void (const juce::XmlElement&)> find = [&] (const juce::XmlElement& e)
    {
        if (e.hasTagName ("PLUGIN") && e.getStringAttribute ("type") == "auxsend") sendPosition = e.getDoubleAttribute ("auxSendSliderPos");
        if (e.hasTagName ("PLUGIN") && e.getStringAttribute ("type") == "halftrack-mixer" && fader < 0) fader = e.getDoubleAttribute ("volume");
        for (auto* child : e.getChildIterator()) find (*child);
    };
    find (*xml);
    check (near (fader, .5, 1e-6) && near (sendPosition, .5, 1e-6),
           "the track's fader sits at half its travel (" + juce::String (fader, 7) + ") and the send at half of its own (" + juce::String (sendPosition, 7) + ")");
    // Post-fader: the track at -14 dB, plus the return's copy of it at -20 dB more.
    const auto expected = .5 * std::pow (10.0, -14.0 / 20) * (1 + std::pow (10.0, -20.0 / 20));
    const auto played = level (render (s, root.getChildFile ("played.wav")));
    check (near (played, expected, 1e-5), "the mix plays the track and its send at their levels (" + juce::String (played, 6) + ", expected " + juce::String (expected, 6) + ")");

    // MIDI moves the fader along its travel, as the reference's CC does.
    const auto volume = "mixer:" + s.snapshot().tracks[0].id + ":volume";
    ok (s.setMidiLearnTarget (volume), "learn the track's volume"); ok (s.handleMidiControl (1, 7, 0), "with CC 7");
    ok (s.setMidiLearnTarget ({}), "end learning");
    const std::pair<int, double> controls[] { { 1, -67.05 }, { 64, -13.84 }, { 100, -2.50 }, { 108, .016 }, { 127, 6 } };
    for (const auto& [value, db] : controls)
    {
        ok (s.handleMidiControl (1, 7, value), "CC 7 at " + juce::String (value));
        check (near (s.snapshot().tracks[0].gainDb, db, .01), "CC 7 at " + juce::String (value) + " is " + juce::String (db) + " dB (" + juce::String (s.snapshot().tracks[0].gainDb, 3) + ")");
    }
    ok (s.handleMidiControl (1, 7, 0), "CC 7 at 0"); check (s.snapshot().tracks[0].gainDb == -100, "CC 0 is silence");
}

// A project saved with the engine's own law: a fader, a send, track automation, the Main level
// kept in the engine's parameter data, and a Session clip's volume automation.
void legacyProjects (const juce::File& root, const juce::File& signal)
{
    Session s (false);
    ok (s.importAudio (signal, 0), "put the signal on track 1");
    ok (s.importAudio (signal, 1), "and on track 2");
    ok (s.addReturnTrack ("Room"), "add a return");
    const auto returnId = s.snapshot().tracks.back().id;
    ok (s.setTrackGain (0, -6), "track 1 at -6 dB");
    ok (s.setTrackSendGain (0, returnId, -12), "a send at -12 dB");
    const Session::AutomationLane ramp { true, { { 0, -18 }, { 4, 0 } } };
    ok (s.setTrackAutomation (s.snapshot().tracks[1].id, Target::volume, ramp), "track 2's volume rises from -18 dB to 0 dB");
    ok (s.setMasterGain (-3), "Main at -3 dB");
    ok (s.importAudioToSlot (signal, 1, 0), "a Session clip on track 2");
    const auto clipId = s.snapshot().tracks[1].slots[0].clip.id;
    const Session::ClipEnvelope automation { { {}, 0 }, Session::EnvelopeMode::automation, { { 0, -12 }, { 2, -3 } } };
    ok (s.setClipEnvelope (clipId, automation), "the clip automates its track's volume");
    const auto before = render (s, root.getChildFile ("before.wav"));
    const auto current = root.getChildFile ("Current.halftrack");
    ok (s.saveProject (current), "save");

    // The same project as the engine's own law saved it.
    auto xml = juce::XmlDocument::parse (current); check (xml != nullptr, "read the project");
    auto* edit = xml->getChildByName ("EDIT");
    edit->removeAttribute ("halftrackFaderLaw");
    const auto fader = [] (double p) { return legacyPosition (Session::automationProportionToValue (Target::volume, (float) p)); };
    const auto sendLaw = Session::levelRange (true);
    int converted = 0;
    std::function<void (juce::XmlElement&, bool)> visit = [&] (juce::XmlElement& e, bool envelope)
    {
        const auto type = e.getStringAttribute ("type");
        if (e.hasTagName ("PLUGIN") && (type == "volume" || type == "halftrack-mixer"))
        {
            for (const auto* key : { "volume", "halftrackManualVolume" })
                if (e.hasAttribute (key)) { e.setAttribute (key, fader (e.getDoubleAttribute (key))); ++converted; }
            e.removeAttribute ("base64:parameters");
            for (auto* curve : e.getChildIterator())
                if (curve->hasTagName ("AUTOMATIONCURVE") && curve->getStringAttribute ("paramID").endsWith ("volume"))
                    for (auto* point : curve->getChildIterator()) { point->setAttribute ("v", fader (point->getDoubleAttribute ("v"))); ++converted; }
        }
        if (e.hasTagName ("PLUGIN") && type == "auxsend")
        {
            e.setAttribute ("auxSendSliderPos", legacyPosition (sendLaw.convertFrom0to1 (e.getDoubleAttribute ("auxSendSliderPos")))); ++converted;
            e.removeAttribute ("halftrackManualSend"); // Newer than the fader law.
        }
        if (envelope && e.hasTagName ("AUTOMATIONCURVE") && type == "absolute")
            for (auto* point : e.getChildIterator()) { point->setAttribute ("v", fader (point->getDoubleAttribute ("v"))); ++converted; }
        const bool volumeEnvelope = e.hasTagName ("AUTOMATIONCURVEMODIFIER") && e.getStringAttribute ("paramID") == "volume";
        for (auto* child : e.getChildIterator()) visit (*child, volumeEnvelope);
    };
    visit (*edit, false);
    check (converted >= 9, "write every level by the engine's own law (" + juce::String (converted) + ")");
    // The Main level only in the engine's parameter data, with its fader elsewhere.
    auto* main = edit->getChildByName ("MASTERVOLUME")->getChildByName ("PLUGIN");
    main->removeAttribute ("halftrackManualVolume");
    main->setAttribute ("volume", legacyPosition (-30));
    juce::MemoryOutputStream values; values.writeString ("master volume"); values.writeFloat ((float) legacyPosition (-3));
    main->setAttribute ("base64:parameters", values.getMemoryBlock().toBase64Encoding());
    const auto legacy = root.getChildFile ("Legacy.halftrack");
    check (xml->writeTo (legacy), "write the legacy project");

    Session reopened (false);
    ok (reopened.loadProject (legacy), "open the legacy project");
    const auto snapshot = reopened.snapshot();
    check (near (snapshot.tracks[0].gainDb, -6, 1e-3) && snapshot.tracks[0].sends.size() == 1 && near (snapshot.tracks[0].sends[0].gainDb, -12, 1e-3)
               && near (snapshot.masterGainDb, -3, 1e-3),
           "the fader, the send and the Main level keep their levels (" + juce::String (snapshot.tracks[0].gainDb, 4) + ", "
               + juce::String (snapshot.tracks[0].sends.empty() ? 0.0f : snapshot.tracks[0].sends[0].gainDb, 4) + ", " + juce::String (snapshot.masterGainDb, 4) + " dB)");
    const auto& lane = snapshot.tracks[1].volumeAutomation.points;
    check (lane.size() == 2 && near (lane[0].value, -18, 1e-3) && near (lane[1].value, 0, 1e-3), "the automation keeps its points' levels");
    const auto& envelopes = snapshot.tracks[1].slots[0].clip.envelopes;
    check (envelopes.size() == 1 && envelopes[0].points.size() == 2 && near (envelopes[0].points[0].value, -12, 1e-3) && near (envelopes[0].points[1].value, -3, 1e-3),
           "the clip's volume automation keeps its levels");
    const auto after = render (reopened, root.getChildFile ("after.wav"));
    double worst = 0;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < std::min (before.getNumSamples(), after.getNumSamples()); ++n)
        worst = std::max (worst, (double) std::abs (after.getSample (c, n) - before.getSample (c, n)));
    check (before.getNumSamples() == after.getNumSamples() && worst < 1e-4, "it plays as the project it was saved from (" + juce::String (worst, 7) + ")");

    // Saving writes the current law; opening that again changes nothing.
    const auto resaved = root.getChildFile ("Resaved.halftrack");
    ok (reopened.saveProject (resaved), "save the converted project");
    auto again = juce::XmlDocument::parse (resaved);
    check (again != nullptr && again->getChildByName ("EDIT")->getStringAttribute ("halftrackFaderLaw") == "2", "it is saved with the fader law");
    ok (reopened.loadProject (resaved), "open it again");
    check (near (reopened.snapshot().tracks[0].gainDb, -6, 1e-3) && near (reopened.snapshot().masterGainDb, -3, 1e-3)
               && near (reopened.snapshot().tracks[0].sends[0].gainDb, -12, 1e-3), "its levels are converted once");

    // A law this version does not know is refused, keeping the open project.
    edit->setAttribute ("halftrackFaderLaw", "3");
    const auto unknown = root.getChildFile ("Unknown.halftrack");
    check (xml->writeTo (unknown), "write a project with an unknown fader law");
    const auto generation = reopened.snapshot().projectGeneration;
    check (reopened.loadProject (unknown).failed() && reopened.snapshot().projectGeneration == generation, "a project with an unknown fader law is refused");
}
}

int runFaderLawTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("fader-laws-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto signal = constantSignal (root);
        modelLevels();
        storedAndPlayed (root, signal);
        legacyProjects (root, signal);
        std::cout << "Fader laws passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL fader laws: " << e.what() << std::endl; return 1; }
}
}
