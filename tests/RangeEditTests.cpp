#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Arrangement time-selection edits at 120 BPM (a beat is half a second). Track 1 plays a
// four-second non-periodic source, track 2 holds a MIDI clip with notes and a controller
// lane, and track 3 is outside the selection. Renders of track 1 are compared with the
// source sample by sample; MIDI results are compared note by note.
namespace halftrack
{
namespace
{
constexpr double rate = 48000, pi = 3.14159265358979323846;
constexpr int frames = 4 * 48000;
using Point = Session::ControllerPoint;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS range edit: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
double source (int n) { return n < 0 || n >= frames ? 0 : .3 * std::sin (2 * pi * 233.7 * n / rate) + .1 * std::sin (2 * pi * .37 * n / rate); }
void write (const juce::File& f)
{
    juce::AudioBuffer<float> x (2, frames);
    for (int n = 0; n < frames; ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) source (n));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write the source");
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
// Largest error over [from, to) samples against an expected signal.
double error (const juce::AudioBuffer<float>& x, int from, int to, const std::function<double (int)>& expected)
{
    double worst = 0;
    for (int n = from; n < to; ++n) worst = std::max (worst, std::abs ((n < x.getNumSamples() ? x.getSample (0, n) : 0.0f) - expected (n)));
    return worst;
}
std::vector<Session::ClipInfo> clips (Session& s, int track)
{
    const auto snapshot = s.snapshot(); auto result = snapshot.tracks[(size_t) track].clips;
    std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    return result;
}
bool near (double a, double b) { return std::abs (a - b) < 1e-6; }
struct Note { int pitch; double start, length; };
std::vector<Note> notes (const Session::ClipInfo& clip)
{
    std::vector<Note> result;
    for (const auto& n : clip.notes) result.push_back ({ n.pitch, n.startBeat, n.lengthBeats });
    std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    return result;
}
bool same (const std::vector<Note>& a, const std::vector<Note>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].pitch != b[i].pitch || ! near (a[i].start, b[i].start) || ! near (a[i].length, b[i].length)) return false;
    return true;
}
}

int runRangeEditTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("range-edit-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto wav = root.getChildFile ("source.wav"), file = root.getChildFile ("render.wav");
        write (wav);
        Session s (false);
        ok (s.importAudio (wav, 0, 0), "track 1 plays the source from 0 to 4 s");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track"); ok (s.moveTrack (s.snapshot().tracks.back().id, 1), "make it track 2");
        ok (s.importAudio (wav, 2, 0), "track 3 plays the source too");
        const auto ids = [&] { const auto snapshot = s.snapshot(); return juce::StringArray { snapshot.tracks[0].id, snapshot.tracks[1].id }; };
        const auto keys = s.snapshot().tracks[1].id;
        ok (s.addMidiClip (keys, 0, 8), "an eight-beat MIDI clip");
        const auto clipId = clips (s, 1)[0].id;
        ok (s.addMidiNote (clipId, 60, 1, 2, .8f), "a note across beat 2"); ok (s.addMidiNote (clipId, 62, 0, 1, .8f), "a note before it");
        ok (s.addMidiNote (clipId, 64, 5, 1, .8f), "a note after it");
        ok (s.setMidiControllerLane (clipId, 1, { { 0, 10 }, { 1.5, 40 }, { 4, 90 } }), "a mod wheel lane");
        ok (s.setTrackMute (1, true), "silence the MIDI track"); ok (s.setTrackMute (2, true), "silence track 3");
        check (error (render (s, file), 0, frames, source) < 1e-6, "track 1 renders the source exactly");

        // Splitting a MIDI clip.
        juce::String right;
        check (s.splitMidiClip (clipId, 0, right).failed() && s.splitMidiClip (clipId, 4, right).failed() && clips (s, 1).size() == 1, "splits at the clip's ends are rejected");
        ok (s.splitMidiClip (clipId, 1, right), "split the MIDI clip at beat 2");
        auto midi = clips (s, 1);
        check (midi.size() == 2 && near (midi[0].lengthBeats, 2) && near (midi[1].start, 1) && near (midi[1].lengthBeats, 6) && midi[1].id == right,
               "the clip becomes two clips of 2 and 6 beats");
        check (same (notes (midi[0]), { { 62, 0, 1 }, { 60, 1, 1 } }) && same (notes (midi[1]), { { 60, 0, 1 }, { 64, 3, 1 } }),
               "the crossing note becomes two notes and later notes move into the right clip");
        check (midi[0].controllers.size() == 1 && midi[0].controllers[0].points == std::vector<Point> { { 0, 10 }, { 1.5, 40 } }
               && midi[1].controllers[0].points == std::vector<Point> { { 0, 40 }, { 2, 90 } }, "the mod wheel value at the split starts the right clip");
        s.undo(); check (clips (s, 1).size() == 1 && clips (s, 1)[0].notes.size() == 3, "one undo rejoins the clip");

        // Split a selection on tracks 1 and 2.
        ok (s.splitRange (ids(), 1, 2), "split tracks 1 and 2 at 1 s and 2 s");
        auto audio = clips (s, 0);
        check (audio.size() == 3 && near (audio[1].start, 1) && near (audio[1].length, 1) && near (audio[2].sourceOffset, 2) && clips (s, 1).size() == 3 && clips (s, 2).size() == 1,
               "the selected tracks split at both edges and track 3 is untouched");
        check (error (render (s, file), 0, frames, source) < 1e-6, "split audio renders exactly like the whole clip");
        s.undo(); check (clips (s, 0).size() == 1 && clips (s, 1).size() == 1, "one undo restores both tracks");

        // Delete a selection.
        ok (s.deleteRange (ids(), 1, 2), "delete from 1 s to 2 s on tracks 1 and 2");
        audio = clips (s, 0); midi = clips (s, 1);
        check (audio.size() == 2 && near (audio[0].length, 1) && near (audio[1].start, 2) && midi.size() == 2 && near (midi[1].start, 2), "the material inside the selection is gone");
        check (same (notes (midi[0]), { { 62, 0, 1 }, { 60, 1, 1 } }) && same (notes (midi[1]), { { 64, 1, 1 } }), "notes inside the selection go and crossing notes are cut");
        check (error (render (s, file), 0, frames, [] (int n) { return n >= 48000 && n < 96000 ? 0.0 : source (n); }) < 1e-6, "the deleted second is silent and the rest is unchanged");
        s.undo();

        // Duplicate a selection over the time after it.
        ok (s.duplicateRange (ids(), .5, 1.5), "duplicate 0.5 s to 1.5 s");
        check (error (render (s, file), 0, frames, [] (int n) { return n >= 72000 && n < 120000 ? source (n - 48000) : source (n); }) < 1e-6,
               "1.5 s to 2.5 s now plays the copy and the rest is unchanged");
        midi = clips (s, 1);
        std::vector<Note> all;
        for (const auto& c : midi) for (auto n : notes (c)) { n.start += c.startBeat; all.push_back (n); }
        std::sort (all.begin(), all.end(), [] (const auto& a, const auto& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
        check (same (all, { { 62, 0, 1 }, { 60, 1, 2 }, { 60, 3, 2 }, { 64, 5, 1 } }), "the MIDI copy of beats 1 to 3 lands on beats 3 to 5");
        s.undo(); check (clips (s, 0).size() == 1 && clips (s, 1).size() == 1 && error (render (s, file), 0, frames, source) < 1e-6, "one undo removes the copy and its splits");

        // Rejections leave the project unchanged.
        check (s.splitRange ({ "missing" }, 1, 2).failed() && s.deleteRange (ids(), 2, 1).failed() && s.duplicateRange (ids(), 1, std::nan ("")).failed(),
               "unknown tracks and invalid ranges are rejected");
        ok (s.setAudioClipWarp (clips (s, 0)[0].id, { Session::WarpMode::stretch, 120 }), "warp track 1's clip");
        check (s.deleteRange (ids(), 1, 2).failed() && clips (s, 0).size() == 1 && clips (s, 1).size() == 1, "a warped clip crossing an edge rejects the edit without change");
        s.undo();
        ok (s.setTrackFrozen (s.snapshot().tracks[0].id, true), "freeze track 1");
        check (s.duplicateRange (ids(), 1, 2).failed() && clips (s, 1).size() == 1, "a frozen track in the selection rejects the edit");
        // Consolidate, at the test engine's 44.1 kHz so renders compare without resampling.
        {
            constexpr double low = 44100;
            const auto wav44 = root.getChildFile ("source44.wav"), render44 = root.getChildFile ("render44.wav");
            {
                juce::AudioBuffer<float> x (2, 3 * 44100);
                for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) (.3 * std::sin (2 * pi * 233.7 * n / low) + .1 * std::sin (2 * pi * .37 * n / low)));
                juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (wav44.createOutputStream());
                auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (low).withNumChannels (2).withBitsPerSample (32));
                check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a 44.1 kHz source");
            }
            const auto read = [&] (Session& session)
            {
                ok (session.exportWav (render44, low, 32, 0), "render at 44.1 kHz");
                juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (render44));
                juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
            };
            Session c (false);
            ok (c.importAudio (wav44, 0, 0), "a three-second clip at 0 s"); ok (c.importAudio (wav44, 0, 3.5), "another at 3.5 s");
            auto first = clips (c, 0)[0]; auto settings = first.audio; settings.gainDb = -6; settings.fadeInSeconds = .2;
            ok (c.setAudioClipSettings (first.id, settings), "the first clip at -6 dB with a fade-in");
            ok (c.addMidiTrack ("Keys"), "add a MIDI track"); ok (c.moveTrack (c.snapshot().tracks.back().id, 1), "make it track 2");
            const auto keys = c.snapshot().tracks[1].id;
            ok (c.addMidiClip (keys, 0, 2), "a MIDI clip on beats 0 to 2"); ok (c.addMidiClip (keys, 4, 2), "another on beats 4 to 6");
            const auto a = clips (c, 1)[0].id, b = clips (c, 1)[1].id;
            ok (c.addMidiNote (a, 60, 1.25, .5, .8f), "a note at beat 1.25"); ok (c.addMidiNote (b, 62, .5, 1, .8f), "a note at beat 4.5");
            ok (c.setMidiControllerLane (b, 1, { { 0, 50 } }), "a mod wheel point at beat 4");
            ok (c.setTrackMute (1, true), "silence the MIDI track");
            ok (c.setTrackMute (0, true), "mute the audio track too; consolidation ignores mute");
            const auto tracks44 = [&] { const auto snapshot = c.snapshot(); return juce::StringArray { snapshot.tracks[0].id, snapshot.tracks[1].id }; };
            ok (c.setTrackMute (0, false), "unmute for the reference render");
            const auto reference = read (c);
            ok (c.setTrackMute (0, true), "mute the audio track again");
            juce::StringArray created;
            check (c.consolidateRange (tracks44(), 7, 7.5, created).failed() && created.isEmpty(), "a selection without clips has nothing to consolidate");
            ok (c.consolidateRange (tracks44(), .5, 4, created), "consolidate 0.5 s to 4 s on both tracks");
            auto audio44 = clips (c, 0);
            check (created.size() == 2 && audio44.size() == 3 && near (audio44[1].start, .5) && near (audio44[1].length, 3.5) && audio44[1].id == created[0]
                   && audio44[1].file.existsAsFile() && audio44[1].file.getParentDirectory().getFileName().endsWith ("Assets") && near (audio44[2].start, 4),
                   "the audio becomes one clip spanning the selection, rendered into the project's Assets");
            {
                juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (audio44[1].file));
                check (r != nullptr && r->sampleRate == low && r->lengthInSamples == (juce::int64) (3.5 * low), "the consolidated file holds 3.5 s at 44.1 kHz");
            }
            ok (c.setTrackMute (0, false), "unmute to compare");
            const auto after = read (c);
            double worst = 0; for (int n = 0; n < reference.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (after.getSample (0, n) - reference.getSample (0, n)));
            check (after.getNumSamples() == reference.getNumSamples() && worst < 1e-6, "the consolidated track renders like the original clips, with gain, fade and gap baked in");
            const auto midi44 = clips (c, 1);
            check (midi44.size() == 2 && midi44[1].id == created[1] && near (midi44[1].startBeat, 1) && near (midi44[1].lengthBeats, 7)
                   && same (notes (midi44[1]), { { 60, .25, .5 }, { 62, 3.5, 1 } }) && midi44[1].controllers.size() == 1
                   && midi44[1].controllers[0].points == std::vector<Point> { { 3, 50 } }, "the MIDI clips join into one clip on beats 1 to 8 with their notes and controller");
            c.undo(); c.undo();
            check (clips (c, 0).size() == 2 && clips (c, 1).size() == 2 && clips (c, 1)[1].notes.size() == 1, "undoing the unmute and then the consolidation restores the original clips");
        }
        std::cout << "Range edit passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL range edit: " << e.what() << std::endl; return 1; }
}
}
