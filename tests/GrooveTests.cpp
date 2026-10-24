#include "Grooves.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// The groove pool. A clip of straight eighth notes plays through a sine synthesizer; its render's
// onsets and peaks must follow the clip's groove (swing, Timing, Global Amount, accents) while the
// clip's own notes stay put, until Commit writes exactly the grooved playback into the notes.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
constexpr double rate = 48000, beat = .5; // 120 BPM.
using Groove = Session::GrooveInfo;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS grooves: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
bool near (double a, double b, double tolerance = 1e-9) { return std::abs (a - b) <= tolerance; }
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
// Onsets (the first loud sample after 50 ms of quiet) and each onset's peak over the next 40 ms.
// A live capture can start inside a note, so it counts no onset before its first quiet.
struct Hit { double seconds; float peak; };
std::vector<Hit> hits (const juce::AudioBuffer<float>& x, bool live = false)
{
    std::vector<Hit> result; int quiet = live ? 0 : (int) rate;
    for (int n = 0; n < x.getNumSamples(); ++n)
    {
        if (std::abs (x.getSample (0, n)) > .01f)
        {
            if (quiet > (int) (.05 * rate))
            {
                float peak = 0;
                for (int k = n; k < std::min (x.getNumSamples(), n + (int) (.04 * rate)); ++k) peak = std::max (peak, std::abs (x.getSample (0, k)));
                result.push_back ({ n / rate, peak });
            }
            quiet = 0;
        }
        else ++quiet;
    }
    return result;
}
// Onsets of the sixteen eighth notes, with every offbeat moved by `late` beats.
bool follows (const std::vector<Hit>& found, double late)
{
    if (found.size() != 16) return false;
    for (int i = 0; i < 16; ++i)
        if (! near (found[(size_t) i].seconds, (i * .5 + (i % 2 == 1 ? late : 0)) * beat, .002)) return false;
    return true;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1;
    double worst = 0;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (a.getSample (c, n) - b.getSample (c, n)));
    return worst;
}
// Equal up to the project file's decimal precision.
bool same (const std::vector<Groove>& a, const std::vector<Groove>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        auto x = a[i], y = b[i];
        if (x.events.size() != y.events.size() || ! near (x.lengthBeats, y.lengthBeats)) return false;
        for (size_t k = 0; k < x.events.size(); ++k)
        {
            if (! near (x.events[k].beat, y.events[k].beat)) return false;
            y.events[k].beat = x.events[k].beat;
        }
        y.lengthBeats = x.lengthBeats;
        if (! (x == y)) return false;
    }
    return true;
}
// Intervals between a live capture's onsets: all a straight eighth apart, or alternating long and short swung eighths.
bool intervals (const std::vector<Hit>& found, bool swung)
{
    if (found.size() < 5) return false;
    bool previousLong = false;
    for (size_t i = 1; i < found.size(); ++i)
    {
        const auto gap = found[i].seconds - found[i - 1].seconds;
        if (! swung) { if (! near (gap, .25 * beat * 2, .003)) return false; continue; }
        const bool isLong = near (gap, 2.0 / 3 * beat, .003);
        if ((! isLong && ! near (gap, 1.0 / 3 * beat, .003)) || (i > 1 && isLong == previousLong)) return false;
        previousLong = isLong;
    }
    return true;
}
// Live playback rebuilds when a playing clip's groove changes, and again on undo.
void live (const Groove& swing)
{
    Session s (false, makeSyntheticTestDeviceType());
    ok (s.addMidiTrack ("Live"), "add a MIDI track to a live session");
    const auto trackId = s.snapshot().tracks.back().id;
    Session::SynthSettings synth; synth.attackSeconds = .001f; synth.releaseSeconds = .02f; synth.sustain = 1; synth.waveform = 2;
    ok (s.setSynthSettings (trackId, synth), "a fast sine synthesizer");
    ok (s.addMidiClip (trackId, 0, 32), "a long clip");
    const auto clipId = s.snapshot().tracks.back().clips[0].id;
    std::vector<Session::MidiNoteInfo> eighths;
    for (int i = 0; i < 64; ++i) eighths.push_back ({ {}, 69, i * .5, .1, 1.0f, true });
    juce::StringArray ids; ok (s.addMidiNotes (clipId, eighths, ids), "straight eighths");
    juce::String swingId; ok (s.addGroove (swing, swingId), "a swing groove in the pool");
    captureSyntheticTestDevice (s, 4);
    ok (s.play(), "play");
    check (intervals (hits (captureSyntheticTestDevice (s, 400), true), false), "the playing clip starts straight");
    ok (s.setClipGroove (clipId, swingId), "give the playing clip the swing groove");
    captureSyntheticTestDevice (s, 40);
    check (intervals (hits (captureSyntheticTestDevice (s, 400), true), true), "playback swings without stopping");
    s.undo();
    captureSyntheticTestDevice (s, 40);
    check (intervals (hits (captureSyntheticTestDevice (s, 400), true), false), "undo while playing straightens it again");
    s.stop();
}
Groove named (const juce::String& name)
{
    for (auto& g : Session::grooveLibrary()) if (g.name == name) return g;
    throw std::runtime_error (("missing library groove " + name).toStdString());
}
juce::String grooveFile (const juce::String& noteAttributes, int grid)
{
    return "<?xml version='1.0' encoding='UTF-8'?>\n<Ableton MajorVersion=\"5\"><Groove><Name Value=\"Late Push\" /><Clip><Value><MidiClip Id=\"0\" Time=\"0\">"
           "<Loop><LoopStart Value=\"0\" /><LoopEnd Value=\"4\" /></Loop><Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>" + noteAttributes
           + "</Notes><MidiKey Value=\"36\" /></KeyTrack></KeyTracks></Notes></MidiClip></Value></Clip><Grid Value=\"" + juce::String (grid)
           + "\" /><QuantizationAmount Value=\"10\" /><TimingAmount Value=\"80\" /><RandomAmount Value=\"5\" /><VelocityAmount Value=\"-40\" /></Groove></Ableton>\n";
}
}

int runGrooveTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("grooves-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto file = root.getChildFile ("render.wav");

        // The library and the placement model.
        const auto library = Session::grooveLibrary();
        bool allValid = library.size() == 21;
        for (const auto& g : library) allValid = allValid && g.id.isEmpty() && grooves::valid (g);
        check (allValid, "the library has 6 Quantize and 15 Swing grooves, all valid and without pool ids");
        const auto quantize16 = named ("Quantize 1/16");
        check (quantize16.events.empty() && quantize16.quantize == 100 && quantize16.timing == 0 && quantize16.base == Session::GrooveBase::sixteenth,
               "a Quantize groove only quantizes to its Base");
        const auto swing = named ("8th Swing 66");
        check (swing.base == Session::GrooveBase::eighth && swing.events.size() == 8 && near (swing.events[1].beat, 2.0 / 3), "8th Swing 66 puts each offbeat two thirds into its beat");
        {
            const grooves::Pattern full (swing, 100), half (swing, 50);
            check (near (full.place (.5, 100, 1).beat, 2.0 / 3) && near (full.place (1, 100, 1).beat, 1) && full.place (.5, 100, 1).velocity == 100
                   && near (full.place (1.5, 90, 1).beat, 1 + 2.0 / 3) && near (full.place (.55, 100, 1).beat, .55 + 1.0 / 6),
                   "offbeats move by the groove's offset and downbeats stay");
            check (near (half.place (.5, 100, 1).beat, .5 + 1.0 / 12), "the global amount scales Timing");
            auto q = quantize16; q.quantize = 50;
            check (near (grooves::Pattern (q, 100).place (.3, 100, 1).beat, .275) && near (grooves::Pattern (quantize16, 0).place (.3, 100, 1).beat, .25),
                   "Quantize pulls a note toward its grid line and ignores the global amount");
            Groove accent; accent.name = "Accent"; accent.base = Session::GrooveBase::eighth; accent.lengthBeats = 1; accent.timing = 0; accent.velocity = 100;
            accent.events = { { 0, 127 }, { .5, 64 } };
            auto inverted = accent; inverted.velocity = -100; auto partial = accent; partial.velocity = 50;
            check (grooves::Pattern (accent, 100).place (.5, 100, 1).velocity == 50 && grooves::Pattern (accent, 100).place (2, 100, 1).velocity == 100
                   && grooves::Pattern (inverted, 100).place (.5, 100, 1).velocity == 127 && grooves::Pattern (partial, 100).place (.5, 100, 1).velocity == 75,
                   "Velocity accents notes by the groove's velocities relative to its loudest, and negative Velocity inverts it");
            Groove jitter = quantize16; jitter.quantize = 0; jitter.random = 100;
            const grooves::Pattern random (jitter, 100);
            double low = 1, high = -1; bool repeatable = true;
            for (uint64_t seed = 0; seed < 400; ++seed)
            {
                const auto moved = random.place (1, 100, grooves::noteSeed (seed, 60, 1)).beat - 1;
                low = std::min (low, moved); high = std::max (high, moved);
                repeatable = repeatable && random.place (1, 100, grooves::noteSeed (seed, 60, 1)).beat == moved + 1;
            }
            check (repeatable && low >= -.125 && high <= .125 && high - low > .2, "Random jitters up to half a grid step, the same way for the same note");
            check (grooves::Pattern (swing, 0).neutral() && ! grooves::Pattern (swing, 100).neutral(), "a groove at zero amount is neutral");
        }

        live (swing);

        // A clip of straight eighth notes.
        Session s (false);
        ok (s.addMidiTrack ("Beats"), "add a MIDI track");
        const auto trackId = s.snapshot().tracks.back().id;
        Session::SynthSettings synth; synth.attackSeconds = .001f; synth.releaseSeconds = .02f; synth.sustain = 1; synth.waveform = 2; synth.cutoffHz = 18000;
        ok (s.setSynthSettings (trackId, synth), "a fast sine synthesizer");
        ok (s.addMidiClip (trackId, 0, 8), "an eight-beat clip");
        const auto clipId = s.snapshot().tracks.back().clips[0].id;
        std::vector<Session::MidiNoteInfo> eighths;
        for (int i = 0; i < 16; ++i) eighths.push_back ({ {}, 69, i * .5, .1, 1.0f, true });
        juce::StringArray ids; ok (s.addMidiNotes (clipId, eighths, ids), "sixteen straight eighth notes");
        const auto straight = render (s, file);
        check (follows (hits (straight), 0), "without a groove the eighths play straight");

        // The pool.
        juce::String swingId, duplicateId;
        check (s.snapshot().grooves.empty() && s.snapshot().grooveAmount == 100, "a new project has an empty pool at 100%");
        ok (s.addGroove (swing, swingId), "add 8th Swing 66 to the pool");
        ok (s.addGroove (swing, duplicateId), "add it again");
        check (s.snapshot().grooves.size() == 2 && s.snapshot().grooves[0].id == swingId && s.snapshot().grooves[0].name == "8th Swing 66"
               && s.snapshot().grooves[1].name == "8th Swing 66 2", "pool grooves get ids and distinguishable names");
        ok (s.removeGroove (duplicateId), "remove the copy");
        auto bad = swing; bad.timing = 150; auto backwards = swing; std::swap (backwards.events[0], backwards.events[1]);
        auto outside = swing; outside.events.push_back ({ 4, 127 }); auto unnamed = swing; unnamed.name = "  ";
        juce::String unused;
        check (s.addGroove (bad, unused).failed() && s.addGroove (backwards, unused).failed() && s.addGroove (outside, unused).failed()
               && s.addGroove (unnamed, unused).failed() && s.snapshot().grooves.size() == 1, "out-of-range grooves are rejected");
        check (s.setClipGroove (clipId, "missing").failed() && s.setClipGroove ("missing", swingId).failed() && s.commitClipGroove (clipId).failed(),
               "unknown grooves and clips are refused, and a clip without a groove has nothing to commit");

        // Grooved playback.
        ok (s.setClipGroove (clipId, swingId), "the clip uses the swing groove");
        check (s.snapshot().tracks.back().clips[0].grooveId == swingId, "the clip reports its groove");
        const auto swung = render (s, file);
        check (follows (hits (swung), 1.0 / 6), "offbeats play a sixth of a beat late");
        bool unchanged = true;
        const auto own = s.snapshot(); // Held, since the loop reads into it.
        for (const auto& n : own.tracks.back().clips[0].notes) unchanged = unchanged && near (std::fmod (n.startBeat, .5), 0) && n.velocity == 1.0f;
        check (unchanged, "the clip's own notes are unchanged");
        auto halfTiming = swing; halfTiming.timing = 50; halfTiming.name = "Half";
        ok (s.setGroove (swingId, halfTiming), "Timing 50%");
        check (follows (hits (render (s, file)), 1.0 / 12) && s.snapshot().grooves[0].name == "Half", "half the Timing moves offbeats half as far");
        ok (s.setGrooveAmount (0), "Global Amount 0%");
        check (follows (hits (render (s, file)), 0), "at 0% the clip plays straight");
        ok (s.setGrooveAmount (130), "Global Amount 130%");
        check (follows (hits (render (s, file)), 1.3 / 12), "130% exaggerates the Timing");
        check (s.setGrooveAmount (131).failed() && s.setGrooveAmount (-1).failed(), "amounts outside 0-130% are refused");
        s.undo(); check (s.snapshot().grooveAmount == 0, "undo restores the amount");
        s.undo(); s.undo(); check (s.snapshot().grooveAmount == 100 && s.snapshot().grooves[0] == [&] { auto g = swing; g.id = swingId; return g; } (),
                                   "undo restores the groove's settings");
        check (follows (hits (render (s, file)), 1.0 / 6), "and its swing");

        // Accents.
        Groove accent; accent.name = "Accent"; accent.base = Session::GrooveBase::eighth; accent.lengthBeats = 1; accent.timing = 0; accent.velocity = 100;
        accent.events = { { 0, 127 }, { .5, 64 } };
        juce::String accentId; ok (s.addGroove (accent, accentId), "add an accent groove");
        ok (s.setClipGroove (clipId, accentId), "the clip uses it");
        {
            const auto found = hits (render (s, file));
            bool accented = follows (found, 0);
            for (int i = 0; accented && i < 16; i += 2) accented = std::abs (found[(size_t) i + 1].peak / found[(size_t) i].peak - 64.0 / 127) < .02;
            check (accented, "offbeats play at the groove's half velocity, on time");
        }

        // Commit.
        ok (s.setClipGroove (clipId, swingId), "back to the swing groove");
        ok (s.setGroove (swingId, [&] { auto g = swing; g.velocity = 40; g.events[1].velocity = 80; g.random = 10; return g; } ()), "add accents and a little Random");
        const auto grooved = render (s, file);
        ok (s.commitClipGroove (clipId), "commit the groove");
        const auto committed = s.snapshot().tracks.back().clips[0];
        check (committed.grooveId.isEmpty() && committed.notes.size() == 16 && ! near (committed.notes[1].startBeat, .5, .01), "Commit moves the notes and sets the groove to None");
        check (difference (render (s, file), grooved) < 1e-6, "the committed clip renders exactly as the grooved playback did");
        s.undo();
        check (s.snapshot().tracks.back().clips[0].grooveId == swingId && near (s.snapshot().tracks.back().clips[0].notes[1].startBeat, .5), "one undo restores the notes and the groove");

        // Removing a groove sets its clips to None; Session clips use grooves too.
        ok (s.addMidiClipToSlot (trackId, 0, 4), "add a Session clip");
        const auto slotClip = s.snapshot().tracks.back().slots[0].clip.id;
        ok (s.setClipGroove (slotClip, swingId), "the Session clip uses the swing groove");
        ok (s.removeGroove (swingId), "remove the swing groove");
        check (s.snapshot().grooves.size() == 1 && s.snapshot().tracks.back().clips[0].grooveId.isEmpty() && s.snapshot().tracks.back().slots[0].clip.grooveId.isEmpty(),
               "the clips that used it are set to None");
        s.undo();
        check (s.snapshot().grooves.size() == 2 && s.snapshot().tracks.back().clips[0].grooveId == swingId && s.snapshot().tracks.back().slots[0].clip.grooveId == swingId,
               "one undo restores it and its clips");

        // Extraction.
        ok (s.addMidiClip (trackId, 16, 4), "a clip to extract from");
        const auto sourceId = s.snapshot().tracks.back().clips[1].id;
        ok (s.addMidiNotes (sourceId, { { {}, 60, 0, .1, .5f, true }, { {}, 60, .3, .1, 1.0f, true }, { {}, 64, .3, .1, .7f, true } }, ids), "notes with a late, loud offbeat chord");
        juce::String extractedId; ok (s.extractGroove (sourceId, extractedId), "extract its groove");
        {
            const auto pool = s.snapshot().grooves;
            const auto& g = pool.back();
            check (g.id == extractedId && g.name == s.snapshot().tracks.back().clips[1].name && near (g.lengthBeats, 4) && g.events.size() == 2
                   && near (g.events[0].beat, 0) && near (g.events[0].velocity, 63.5, .51) && near (g.events[1].beat, .3) && near (g.events[1].velocity, 127),
                   "the groove holds the clip's onsets, with a chord as one event at its loudest velocity");
        }
        ok (s.addMidiClip (trackId, 24, 2), "an empty clip");
        check (s.extractGroove (s.snapshot().tracks.back().clips[2].id, unused).failed(), "an empty clip has no groove to extract");
        {
            // From warped audio: an event at each click, as loud as the click against the loudest.
            const auto clicks = root.getChildFile ("clicks.wav");
            {
                juce::AudioBuffer<float> audio (1, 2 * 48000); audio.clear();
                for (int b = 0; b < 4; ++b)
                    for (int n = 0; n < 480; ++n) audio.setSample (0, b * 24000 + n, (b % 2 == 0 ? .8f : .2f) * std::exp (-n / 80.0f) * (n % 2 == 0 ? 1.0f : -1.0f));
                juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> stream (clicks.createOutputStream());
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (24));
                check (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write four beats of clicks");
            }
            ok (s.addAudioTrack ("Clicks"), "an audio track");
            const auto clickTrack = (int) s.snapshot().tracks.size() - 1;
            ok (s.importAudio (clicks, clickTrack, 0), "import the clicks");
            const auto clickClip = s.snapshot().tracks[(size_t) clickTrack].clips.at (0).id;
            check (s.extractGroove (clickClip, unused).failed(), "an unwarped audio clip has no groove to extract");
            ok (s.setAudioClipWarp (clickClip, { Session::WarpMode::beats, 120 }), "warp it at 120 BPM");
            juce::String clickGroove; ok (s.extractGroove (clickClip, clickGroove), "extract the clicks' groove");
            const auto pool = s.snapshot().grooves;
            const auto& g = pool.back();
            check (g.id == clickGroove && near (g.lengthBeats, 4, 1e-6) && g.events.size() == 4 && near (g.events[0].beat, 0, .01) && near (g.events[1].beat, 1, .01)
                       && near (g.events[2].beat, 2, .01) && near (g.events[0].velocity, 127, .51) && g.events[1].velocity < 40 && g.events[1].velocity > 20,
                   "the groove has an event at each click, the quiet ones softer");
            ok (s.removeGroove (clickGroove), "remove that groove"); ok (s.removeTrack (clickTrack), "and the clicks' track");
        }

        // Groove files.
        {
            const auto agr = root.getChildFile ("Late Push.agr");
            check (agr.replaceWithText (grooveFile ("<MidiNoteEvent Time=\"0\" Duration=\"0.1\" Velocity=\"127\" IsEnabled=\"true\" />"
                                                    "<MidiNoteEvent Velocity=\"90.5\" IsEnabled=\"true\" Duration=\"0.1\" Time=\"0.27\" />"
                                                    "<MidiNoteEvent Time=\"0.5\" Duration=\"0.1\" Velocity=\"10\" IsEnabled=\"false\" />", 3)), "write a groove file");
            Groove read;
            ok (Session::readGrooveFile (agr, read), "read the groove file");
            check (read.id.isEmpty() && read.name == "Late Push" && read.base == Session::GrooveBase::sixteenth && near (read.lengthBeats, 4) && read.quantize == 10
                   && read.timing == 80 && read.random == 5 && read.velocity == -40 && read.events.size() == 2 && near (read.events[1].beat, .27) && near (read.events[1].velocity, 90.5f),
                   "its name, Base, amounts and enabled notes are read");
            const auto zipped = root.getChildFile ("Zipped.agr");
            {
                juce::FileOutputStream out (zipped); check (out.openedOk(), "open the compressed file");
                juce::GZIPCompressorOutputStream gzip (out, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
                gzip.writeString (agr.loadFileAsString());
            }
            Groove unzipped; ok (Session::readGrooveFile (zipped, unzipped), "read a compressed groove file"); check (unzipped == read, "it reads identically");
            const auto broken = root.getChildFile ("Broken.agr"), badGrid = root.getChildFile ("Grid.agr"), badTime = root.getChildFile ("Time.agr");
            check (broken.replaceWithText ("<Ableton><Groove>") && badGrid.replaceWithText (grooveFile ({}, 9))
                   && badTime.replaceWithText (grooveFile ("<MidiNoteEvent Time=\"soon\" Velocity=\"127\" />", 3)), "write damaged groove files");
            check (Session::readGrooveFile (broken, unzipped).failed() && Session::readGrooveFile (badGrid, unzipped).failed()
                   && Session::readGrooveFile (badTime, unzipped).failed() && unzipped == read, "damaged groove files are rejected");
            juce::String importedId; ok (s.addGroove (read, importedId), "add the file's groove to the pool");
        }

        // Saving and reopening.
        ok (s.setGrooveAmount (80), "Global Amount 80%");
        const auto before = render (s, file);
        const auto pool = s.snapshot().grooves;
        const auto project = root.getChildFile ("Grooves").getChildFile ("Grooves.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create the project folder");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check (same (s.snapshot().grooves, pool) && s.snapshot().grooveAmount == 80 && s.snapshot().tracks.back().clips[0].grooveId == swingId
               && s.snapshot().tracks.back().slots[0].clip.grooveId == swingId, "the pool, the amount and the clips' grooves reopen");
        check (difference (render (s, file), before) < 1e-6, "the reopened project renders identically");
        const auto corrupt = [&] (const juce::String& name, const std::function<void (juce::XmlElement&)>& change)
        {
            auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "parse the project");
            change (*xml->getChildByName ("EDIT"));
            const auto damaged = project.getSiblingFile (name + ".halftrack"); check (xml->writeTo (damaged), "write " + name);
            check (s.loadProject (damaged).failed() && same (s.snapshot().grooves, pool), name + " is rejected and the open project stays");
        };
        const auto firstGroove = [] (juce::XmlElement& edit) { return edit.getChildByName ("HALFTRACKGROOVEPOOL")->getChildByName ("HALFTRACKGROOVE"); };
        corrupt ("Velocity", [&] (juce::XmlElement& e) { firstGroove (e)->getChildByName ("HALFTRACKGROOVEEVENT")->setAttribute ("velocity", 300); });
        corrupt ("Order", [&] (juce::XmlElement& e) { firstGroove (e)->getChildByName ("HALFTRACKGROOVEEVENT")->setAttribute ("beat", 3.9); });
        corrupt ("Timing", [&] (juce::XmlElement& e) { firstGroove (e)->setAttribute ("timing", "loud"); });
        corrupt ("Base", [&] (juce::XmlElement& e) { firstGroove (e)->setAttribute ("base", 9); });
        corrupt ("Amount", [] (juce::XmlElement& e) { e.getChildByName ("HALFTRACKGROOVEPOOL")->setAttribute ("amount", 500); });
        corrupt ("Duplicate", [&] (juce::XmlElement& e) { e.getChildByName ("HALFTRACKGROOVEPOOL")->addChildElement (new juce::XmlElement (*firstGroove (e))); });
        corrupt ("Missing", [] (juce::XmlElement& e)
        {
            for (auto* track : e.getChildWithTagNameIterator ("TRACK"))
                if (auto* clip = track->getChildByName ("MIDICLIP")) clip->setAttribute ("halftrackGroove", "nowhere");
        });
        std::cout << "Grooves passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL grooves: " << e.what() << std::endl; return 1; }
}
}
