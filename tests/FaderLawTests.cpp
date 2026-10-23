#include "Session.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
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
    // Pan text, as the reference's pan formatter writes it: C within 0.01, else 1 to 50 L or R.
    check (Session::panText (0) == "C" && Session::panText (.01f) == "C" && Session::panText (-.01f) == "C" && Session::panText (.0101f) == "1R"
               && Session::panText (-1) == "50L" && Session::panText (1) == "50R" && Session::panText (-.5f) == "25L" && Session::panText (.1f) == "5R" && Session::panText (-.7f) == "35L",
           "pans read C at the centre and 1 to 50 L or R at the sides");
    check (Session::panFromText ("25L") == -.5f && Session::panFromText ("C") == 0 && Session::panFromText ("50R") == 1 && Session::panFromText ("10") == .2f
               && Session::panFromText ("80L") == -1, "pan text reads back, a bare number as R, and stops at the sides");
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

// The reference's panner: a balance law in Stereo mode, and in Split Stereo mode each input
// channel panned on its own, from a source with 0.5 on its left and 0.25 on its right.
juce::File stereoSignal (const juce::File& root)
{
    const auto file = root.getChildFile ("left-right.wav");
    juce::AudioBuffer<float> x (2, 4 * 48000);
    for (int n = 0; n < x.getNumSamples(); ++n) { x.setSample (0, n, .5f); x.setSample (1, n, .25f); }
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a 4 s signal, 0.5 left and 0.25 right");
    return file;
}
// A channel's mean over a tenth of a second around a time.
double meanAt (const juce::AudioBuffer<float>& x, int channel, double seconds)
{
    const int from = (int) ((seconds - .05) * 48000), to = (int) ((seconds + .05) * 48000);
    double sum = 0; for (int n = from; n < to; ++n) sum += x.getSample (channel, n);
    return sum / (to - from);
}
void panModes (const juce::File& root)
{
    constexpr double pi = 3.141592653589793;
    Session s (false);
    ok (s.importAudio (stereoSignal (root), 0), "put the signal on track 1");
    const auto trackId = s.snapshot().tracks[0].id, other = s.snapshot().tracks[1].id;
    const auto file = root.getChildFile ("pans.wav");
    const auto play = [&] (double at = 1.5) { const auto x = render (s, file); return std::pair { meanAt (x, 0, at), meanAt (x, 1, at) }; };
    const auto track = [&] { return s.snapshot().tracks[0]; };
    auto [l, r] = play();
    check (near (l, .5, 1e-6) && near (r, .25, 1e-6), "at the centre each side plays at unity");
    ok (s.setTrackPan (0, -1), "pan hard left");
    std::tie (l, r) = play();
    check (near (l, .5 * std::sqrt (2.0), 1e-5) && near (r, .25e-5, 1e-8), "hard left lifts the left side 3 dB and keeps the right at 1e-5 of its level, not silence");
    ok (s.setTrackPan (0, .5f), "pan halfway right");
    std::tie (l, r) = play();
    const double c = .5 * pi / 4 + pi / 4;
    check (near (l, .5 * std::sqrt (2.0) * std::cos (c), 1e-5) && near (r, .25 * std::sqrt (2.0) * std::sin (c), 1e-5), "halfway right is the balance law's");

    check (! track().splitPan && track().leftSplitPan == -1 && track().rightSplitPan == 1, "a track starts in Stereo mode, its split pans hard left and hard right");
    ok (s.setPanMode ({ trackId, other }, true), "switch two tracks to Split Stereo");
    check (track().splitPan && s.snapshot().tracks[1].splitPan, "both switch");
    s.undo(); check (! track().splitPan && ! s.snapshot().tracks[1].splitPan, "in one undo step");
    s.redo(); check (track().splitPan, "and redo");
    std::tie (l, r) = play();
    check (near (l, .5, 1e-6) && near (r, .25, 1e-6), "the split defaults keep the channels as they are, with the pan out of play");
    ok (s.setTrackSplitPan (0, false, 1), "the left channel hard right"); ok (s.setTrackSplitPan (0, true, -1), "the right channel hard left");
    std::tie (l, r) = play();
    check (near (l, .25, 1e-6) && near (r, .5, 1e-6), "panning each channel across swaps them");
    ok (s.setTrackSplitPan (0, false, 0), "the left channel at the centre"); ok (s.setTrackSplitPan (0, true, 0), "the right channel at the centre");
    std::tie (l, r) = play();
    check (near (l, .75 * std::cos (pi / 4), 1e-6) && near (r, .75 * std::cos (pi / 4), 1e-6), "both at the centre mix the channels 3 dB down each, without the balance law's lift");
    s.undo(); check (track().rightSplitPan == -1, "a split pan move is one undo step");
    s.redo();
    ok (s.setPanMode ({ trackId }, false), "back to Stereo");
    std::tie (l, r) = play();
    check (near (l, .5 * std::sqrt (2.0) * std::cos (c), 1e-5) && near (r, .25 * std::sqrt (2.0) * std::sin (c), 1e-5), "Stereo mode plays its pan again");
    check (s.setTrackSplitPan (0, false, std::numeric_limits<float>::quiet_NaN()).failed() && s.setPanMode ({ "missing" }, true).failed(), "a bad pan or track is refused");

    // Saved, reopened, and refused when malformed.
    ok (s.setPanMode ({ trackId }, true), "Split Stereo again");
    ok (s.setTrackSplitPan (0, false, -.5f), "left at 25L"); ok (s.setTrackSplitPan (0, true, .5f), "right at 25R");
    const auto saved = play();
    const auto project = root.getChildFile ("Split.halftrack");
    ok (s.saveProject (project), "save");
    Session reopened (false);
    ok (reopened.loadProject (project), "reopen");
    const auto again = reopened.snapshot().tracks[0];
    check (again.splitPan && again.leftSplitPan == -.5f && again.rightSplitPan == .5f, "the mode and split pans reopen");
    const auto replayed = render (reopened, file);
    check (near (meanAt (replayed, 0, 1.5), saved.first, 1e-6) && near (meanAt (replayed, 1, 1.5), saved.second, 1e-6), "and play as before");
    auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "read the project");
    std::function<juce::XmlElement* (juce::XmlElement&, const juce::String&)> find = [&] (juce::XmlElement& e, const juce::String& type) -> juce::XmlElement*
    {
        if (e.hasTagName ("PLUGIN") && e.getStringAttribute ("type") == type) return &e;
        for (auto* child : e.getChildIterator()) if (auto* found = find (*child, type)) return found;
        return nullptr;
    };
    for (int mutation = 0; mutation < 3; ++mutation)
    {
        auto bad = std::make_unique<juce::XmlElement> (*xml);
        auto* mixer = find (*bad, "halftrack-mixer"); auto* main = find (*bad, "volume");
        check (mixer != nullptr && main != nullptr, "find the track's mixer and the Main's");
        if (mutation == 0) mixer->setAttribute ("halftrackPanMode", "2");
        if (mutation == 1) mixer->setAttribute ("halftrackSplitLeft", "1.5");
        if (mutation == 2) main->setAttribute ("halftrackPanMode", "1");
        const auto broken = root.getChildFile ("Bad split.halftrack");
        check (bad->writeTo (broken), "write a malformed pan mode");
        const auto generation = reopened.snapshot().projectGeneration;
        check (reopened.loadProject (broken).failed() && reopened.snapshot().projectGeneration == generation, "a malformed pan mode or split pan is refused (" + juce::String (mutation) + ")");
    }

    // A split pan's automation and mapping, as any mixer control's.
    ok (s.setTrackSplitPan (0, true, 1), "right back hard right");
    check (s.midiTargetRange ("mixer:" + trackId + ":leftsplit") == std::pair { -1.0f, 1.0f }, "a split pan maps from -1 to 1");
    ok (s.setTrackAutomation (trackId, Session::AutomationTarget::leftSplitPan, { true, { { 0, -1 }, { 8, 1 } } }), "the left split pan sweeps from hard left to hard right over 8 beats");
    check (track().leftSplitAutomation.points.size() == 2, "its lane reads back");
    const auto swept = render (s, file);
    // Halfway (2 s) the left channel sits at the centre, half its power on each side, give or take
    // the block-rate automation and the mixer's 15 ms smoothing.
    check (near (meanAt (swept, 0, 2), .5 * std::cos (pi / 4), 3e-3) && near (meanAt (swept, 1, 2), .5 * std::sin (pi / 4) + .25, 3e-3), "the render follows the lane (" + juce::String (meanAt (swept, 0, 2), 5) + ", " + juce::String (meanAt (swept, 1, 2), 5) + ")");
    ok (s.setTrackSplitPan (0, false, -1), "move the left split pan by hand");
    check (! track().leftSplitAutomation.enabled && s.snapshot().automationOverridden, "moving it overrides its lane");
}

// Selection linking, as the reference's: the selected tracks' mixer controls move together.
void selectionLinking()
{
    Session s (false);
    ok (s.addAudioTrack(), "a third track"); ok (s.addReturnTrack ("Echo"), "a return");
    const auto t = [&s] (int i) { return s.snapshot().tracks[(size_t) i]; };
    const auto a = t (0).id, b = t (1).id, echo = s.snapshot().tracks.back().id;
    ok (s.setTrackGain (1, -12), "track 2 at -12 dB, alone");
    s.setSelectedTracks ({ a, b });
    ok (s.setTrackGain (0, -6), "move track 1 to -6 dB with tracks 1 and 2 selected");
    check (near (t (1).gainDb, -18, .01) && t (2).gainDb == 0, "the other selected track moves by the same decibels, and an unselected one stays");
    s.undo(); check (near (t (0).gainDb, 0, 1e-4) && near (t (1).gainDb, -12, 1e-4), "one undo restores both");
    s.redo();
    ok (s.setTrackGain (0, -66), "track 1 far down");
    check (t (1).gainDb == -100, "track 2 stops at the bottom of its travel");
    ok (s.setTrackGain (0, -3), "track 1 back up");
    check (near (t (1).gainDb, -15, .01), "track 2 gets its offset back");
    s.setSelectedTracks ({ a });
    ok (s.setTrackGain (0, 0), "one selected track moves alone");
    check (near (t (1).gainDb, -15, .01), "and the other stays");

    s.setSelectedTracks ({}); ok (s.setTrackPan (1, -.5f), "track 2 at 25L, alone");
    s.setSelectedTracks ({ a, b });
    ok (s.setTrackPan (0, .25f), "pan track 1 to 13R");
    check (near (t (1).pan, -.25, 1e-6), "the other's pan moves by the same amount");
    ok (s.setPanMode ({ b }, true), "track 2 to Split Stereo");
    ok (s.setTrackPan (0, -.5f), "pan track 1 again");
    check (near (t (1).pan, -.25, 1e-6) && t (1).leftSplitPan == -1, "a track in the other pan mode keeps its pans");
    ok (s.setPanMode ({ a }, true), "track 1 to Split Stereo too");
    ok (s.setTrackSplitPan (0, false, 0), "track 1's left channel to the centre");
    check (t (1).leftSplitPan == 0 && t (1).rightSplitPan == 1, "the split pans move together, each with its own");

    ok (s.setTrackSendGain (0, echo, -6), "track 1's send to -6 dB");
    check (near (t (1).sends.at (0).gainDb, -6, .01), "the other's send, Off as it was, follows to -6 dB");
    s.setSelectedTracks ({}); ok (s.setTrackSendGain (1, echo, -20), "track 2's send to -20 dB, alone"); s.setSelectedTracks ({ a, b });
    ok (s.setTrackSendGain (0, echo, -12), "track 1's send to -12 dB");
    check (near (t (1).sends.at (0).gainDb, -26, .01), "sends keep their decibel offset along the send law");
    ok (s.setTrackMute (0, true), "turn track 1's activator off");
    check (t (1).mute && ! t (2).mute, "the other selected track's activator takes the same state");
    ok (s.setTrackMute (0, false), "and on");
    check (! t (1).mute, "and back");

    ok (s.resetTrackControl (0, "volume"), "reset track 1's volume");
    check (near (t (0).gainDb, 0, 1e-4) && near (t (1).gainDb, 0, 1e-4), "a reset sets every linked fader to the default");
    ok (s.resetTrackControl (1, "send:" + echo), "reset track 2's send");
    check (t (0).sends.at (0).gainDb == -100 && t (1).sends.at (0).gainDb == -100, "and every linked send Off");
    check (s.resetTrackControl (0, "width").failed(), "an unknown control is refused");

    // A mapped controller moves the linked tracks too.
    s.setSelectedTracks ({}); ok (s.setTrackGain (1, -10), "track 2 at -10 dB, alone"); s.setSelectedTracks ({ a, b });
    ok (s.setMidiLearnTarget ("mixer:" + a + ":volume"), "learn track 1's volume"); ok (s.handleMidiControl (1, 7, 0), "with CC 7");
    ok (s.setMidiLearnTarget ({}), "end learning");
    ok (s.handleMidiControl (1, 7, 108), "CC 7 to 108, about 0 dB");
    check (near (t (1).gainDb, -10 + t (0).gainDb, .05), "a controller's move links as a mouse move does");
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
        panModes (root);
        selectionLinking();
        std::cout << "Fader laws passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL fader laws: " << e.what() << std::endl; return 1; }
}
}
