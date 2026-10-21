#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Tempo automation. A MIDI clip plays a short note on every beat; the render's onsets must
// land where the tempo map puts each beat, through a drop from 120 to 60 BPM and a linear ramp
// from 60 to 180 BPM. Notes and automation keep their beats when the map changes.
namespace halftrack
{
namespace
{
constexpr double rate = 48000;
using Point = Session::TempoPoint;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS tempo map: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
// Onsets: the first loud sample after at least 50 ms of quiet.
std::vector<double> onsets (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
    std::vector<double> result; int quiet = (int) rate;
    for (int n = 0; n < x.getNumSamples(); ++n)
    {
        if (std::abs (x.getSample (0, n)) > .01f) { if (quiet > (int) (.05 * rate)) result.push_back (n / rate); quiet = 0; }
        else ++quiet;
    }
    return result;
}
}

int runTempoMapTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("tempo-map-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto file = root.getChildFile ("render.wav");
        Session s (false);
        check (s.snapshot().tempoMap == std::vector<Point> { { 0, 120 } }, "a new project has one tempo point");
        check (s.setTempoMap ({}).failed() && s.setTempoMap ({ { 1, 120 } }).failed() && s.setTempoMap ({ { 0, 120 }, { 4, 60 }, { 2, 90 } }).failed()
               && s.setTempoMap ({ { 0, 10 } }).failed() && s.setTempoMap ({ { 0, 120 }, { 4, 1000 } }).failed() && s.setTempoMap ({ { 0, 120 }, { 1e-4, 60 } }).failed()
               && ! s.canUndo(), "maps must start at beat 0 with increasing beats and 20-999 BPM");
        ok (s.addMidiTrack ("Beats"), "add a MIDI track");
        const auto trackId = s.snapshot().tracks.back().id;
        Session::SynthSettings synth; synth.attackSeconds = .001f; synth.releaseSeconds = .02f; synth.sustain = 1;
        ok (s.setSynthSettings (trackId, synth), "a fast synthesizer");
        ok (s.addMidiClip (trackId, 0, 12), "a twelve-beat clip");
        const auto clipId = s.snapshot().tracks.back().clips[0].id;
        std::vector<Session::MidiNoteInfo> beats;
        for (int b = 0; b < 12; ++b) beats.push_back ({ {}, 60, (double) b, .1, .8f, true });
        juce::StringArray ids; ok (s.addMidiNotes (clipId, beats, ids), "a short note on every beat");
        ok (s.setTrackAutomation (s.snapshot().tracks.back().id, Session::AutomationTarget::volume, { true, { { 6, -3 } } }), "a volume point at beat 6");

        // A drop from 120 to 60 BPM at beat 4.
        const std::vector<Point> drop { { 0, 120 }, { 4, 120 }, { 4 + 1.0 / 960, 60 } };
        ok (s.setTempoMap (drop), "drop to 60 BPM at beat 4");
        check (s.snapshot().tempoMap == drop, "the map is reported");
        check (std::abs (s.secondsAt (4) - 2) < 1e-6 && std::abs (s.secondsAt (8) - 6) < .01 && std::abs (s.beatsAt (2) - 4) < 1e-6,
               "beat 4 is at 2 s and each later beat lasts a second");
        auto found = onsets (s, file);
        bool aligned = found.size() == 12;
        for (int b = 0; aligned && b < 12; ++b) aligned = std::abs (found[(size_t) b] - s.secondsAt (b)) < .002;
        check (aligned, "the render's onsets follow the map (beat 8 at " + juce::String (found.size() > 8 ? found[8] : -1, 3) + " s)");
        auto snapshot = s.snapshot();
        check (snapshot.tracks.back().clips[0].notes.size() == 12 && std::abs (snapshot.tracks.back().clips[0].lengthBeats - 12) < 1e-6
               && std::abs (snapshot.tracks.back().volumeAutomation.points[0].beat - 6) < 1e-6, "the clip, its notes and the automation keep their beats");
        s.seek (5); check (std::abs (s.snapshot().tempo - 60) < 1e-6, "the reported tempo is the one at the playhead");
        s.seek (1); check (std::abs (s.snapshot().tempo - 120) < 1e-6, "earlier the playhead hears 120 BPM");

        // A ramp from 60 to 180 BPM over eight beats.
        ok (s.setTempoMap ({ { 0, 60 }, { 8, 180 } }), "ramp from 60 to 180 BPM over eight beats");
        const auto analytic = 4 * std::log (3.0);
        check (std::abs (s.secondsAt (8) - analytic) < .02 * analytic && s.secondsAt (8) < 8 && s.secondsAt (8) > 8.0 / 3, "the ramp's eight beats take about 4 ln 3 seconds");
        found = onsets (s, file);
        aligned = found.size() == 12;
        for (int b = 0; aligned && b < 12; ++b) aligned = std::abs (found[(size_t) b] - s.secondsAt (b)) < .002;
        check (aligned, "the render's onsets follow the ramp");
        s.seek (s.secondsAt (4)); check (std::abs (s.snapshot().tempo - 120) < 2, "halfway through the ramp the tempo is about 120 BPM");
        s.undo(); check (s.snapshot().tempoMap == drop, "undo restores the drop");
        s.undo(); check (s.snapshot().tempoMap == std::vector<Point> { { 0, 120 } } && std::abs (s.secondsAt (8) - 4) < 1e-6, "undo again restores the constant tempo");
        s.redo(); s.redo(); check (s.snapshot().tempoMap.size() == 2, "redo reapplies both");

        // Constant-tempo controls step aside while the tempo is automated.
        s.setTempo (90); check (s.snapshot().tempoMap.size() == 2, "the tempo field does not override an automated tempo");
        check (s.tapTempo (1).failed() && s.insertTime (1, 1).failed() && s.deleteTime (1, 1).failed(), "tap tempo and time edits are refused while it is automated");

        // Reopening, damaged maps and MIDI files.
        const auto project = root.getChildFile ("Tempo").getChildFile ("Tempo.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create the project folder");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check (s.snapshot().tempoMap == std::vector<Point> { { 0, 60 }, { 8, 180 } }, "the ramp reopens");
        const auto corrupt = [&] (const juce::String& name, const std::function<void (juce::XmlElement&)>& change)
        {
            auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "parse the project");
            change (*xml->getChildByName ("EDIT")->getChildByName ("TEMPOSEQUENCE"));
            const auto broken = project.getSiblingFile (name + ".halftrack"); check (xml->writeTo (broken), "write " + name);
            const auto result = s.loadProject (broken);
            check (result.failed() && s.snapshot().tempoMap.size() == 2, name + " is rejected and the open project stays");
        };
        corrupt ("LateStart", [] (juce::XmlElement& t) { t.getChildByName ("TEMPO")->setAttribute ("startBeat", 2.0); });
        corrupt ("Backwards", [] (juce::XmlElement& t)
        { int i = 0; for (auto* tempo : t.getChildWithTagNameIterator ("TEMPO")) if (i++ == 1) tempo->setAttribute ("startBeat", -3.0); });
        corrupt ("Curve", [] (juce::XmlElement& t) { t.getChildByName ("TEMPO")->setAttribute ("curve", 4.0); });
        const auto midi = root.getChildFile ("tempo.mid");
        ok (s.exportMidiClip (s.snapshot().tracks.back().clips[0].id, midi), "export the clip");
        {
            juce::FileInputStream in (midi); juce::MidiFile parsed; check (parsed.readFrom (in), "read the MIDI file");
            std::vector<double> tempos;
            for (int t = 0; t < parsed.getNumTracks(); ++t)
                for (auto* e : *parsed.getTrack (t)) if (e->message.isTempoMetaEvent()) tempos.push_back (60.0 / e->message.getTempoSecondsPerQuarterNote());
            check (tempos.size() == 2 && std::abs (tempos[0] - 60) < .01 && std::abs (tempos[1] - 180) < .01, "the MIDI file carries the clip's tempo points");
        }
        ok (s.setTempoMap ({ { 0, 100 } }), "return to one constant tempo");
        s.setTempo (110); check (std::abs (s.snapshot().tempo - 110) < 1e-6, "the tempo field works again");
        std::cout << "Tempo map passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL tempo map: " << e.what() << std::endl; return 1; }
}
}
