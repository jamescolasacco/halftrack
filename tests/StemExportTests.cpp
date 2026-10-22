#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Stem export. Generated sines on two tracks, one inside a -6 dB group, and a
// post-fader send at -6 dB to a return: each stem must hold only its own signal
// (returns separate) and Master the complete mix, without changing the project.
namespace halftrack
{
namespace
{
constexpr double rate = 48000, pi = 3.14159265358979323846;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS stems: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
float a (int n) { return (float) (.3 * std::sin (2 * pi * 220 * n / rate)); }
float b (int n) { return (float) (.2 * std::sin (2 * pi * 330 * n / rate)); }
void write (const juce::File& f, float (*source) (int))
{
    juce::AudioBuffer<float> x (2, 48000);
    for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, source (n));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
juce::AudioBuffer<float> read (const juce::File& f)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "read " + f.getFileName()); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
// Largest difference from an expected signal inside the sources, away from the render edges.
double error (const juce::AudioBuffer<float>& x, const std::function<double (int)>& expected)
{
    double worst = 0;
    for (int n = 2000; n < 46000; ++n) for (int c = 0; c < 2; ++c) worst = std::max (worst, std::abs (x.getSample (c, n) - expected (n)));
    return worst;
}
}
int runStemExportTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("stems-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto sourceA = root.getChildFile ("a.wav"), sourceB = root.getChildFile ("b.wav");
        write (sourceA, a); write (sourceB, b);
        Session s (false);
        ok (s.importAudio (sourceA, 0), "import the first source"); ok (s.importAudio (sourceB, 1), "import the second source");
        ok (s.renameTrack (0, "Lead"), "name the first track"); ok (s.renameTrack (1, "Pad"), "name the second track");
        juce::String group; ok (s.groupTracks ({ s.snapshot().tracks[1].id }, group), "group the second track");
        ok (s.renameGroup (group, "Keys"), "name the group"); ok (s.setGroupGain (group, -6.0206f), "lower the group by half");
        ok (s.addReturnTrack ("Room"), "add a return");
        const auto returnId = s.snapshot().tracks.back().id;
        ok (s.setTrackSendGain (0, returnId, -6.0206f), "send the first track to the return at half level");
        const auto project = root.getChildFile ("Song.halftrack"); ok (s.saveProject (project), "save the project");
        const auto before = s.snapshot();
        check (! before.dirty, "the project starts clean");
        const auto folder = root.getChildFile ("Stems");
        Session::AudioExportSettings settings; settings.bitDepth = 32;
        juce::StringArray files;
        ok (s.exportStems (folder, settings, files), "export stems");
        juce::StringArray names; for (const auto& f : files) names.add (juce::File (f).getFileName());
        check (names == juce::StringArray { "Song Lead.wav", "Song Keys.wav", "Song Pad.wav", "Song Room.wav", "Song Master.wav" },
               "one file per track, group, return and Master, in display order: " + names.joinIntoString (", "));
        const auto half = .5;
        check (error (read (folder.getChildFile ("Song Lead.wav")), [] (int n) { return (double) a (n); }) < 1e-5, "the Lead stem is the dry first track without its return");
        check (error (read (folder.getChildFile ("Song Pad.wav")), [&] (int n) { return half * b (n); }) < 1e-5, "the Pad stem is the second track through its group");
        check (error (read (folder.getChildFile ("Song Keys.wav")), [&] (int n) { return half * b (n); }) < 1e-5, "the Keys group stem holds its member at the group level");
        check (error (read (folder.getChildFile ("Song Room.wav")), [&] (int n) { return half * a (n); }) < 1e-5, "the Room return stem holds only the send it receives");
        check (error (read (folder.getChildFile ("Song Master.wav")), [&] (int n) { return a (n) + half * a (n) + half * b (n); }) < 1e-5, "the Master stem is the complete mix");
        const auto after = s.snapshot();
        bool same = ! after.dirty && ! s.canRedo() && after.tracks.size() == before.tracks.size();
        for (size_t i = 0; same && i < after.tracks.size(); ++i) same = after.tracks[i].solo == before.tracks[i].solo && after.tracks[i].mute == before.tracks[i].mute;
        check (same && after.groups[0].solo == before.groups[0].solo, "exporting stems leaves solo, mute, history and the saved state unchanged");
        check (s.exportStems (folder, settings, files).failed() && files.isEmpty(), "existing stem files are not replaced without consent");
        ok (s.setTrackMute (0, true), "mute the first track");
        ok (s.exportStems (folder, settings, files, true), "replace the stems");
        check (read (folder.getChildFile ("Song Lead.wav")).getMagnitude (0, 2000, 44000) < 1e-6 && s.snapshot().tracks[0].mute, "a muted track exports a silent stem and stays muted");
        ok (s.setTrackMute (0, false), "unmute it");
        // Selected Tracks Only, or one track: just those stems, without Master.
        const auto chosen = root.getChildFile ("Chosen");
        settings.stemTracks = { s.snapshot().tracks[0].id, group };
        ok (s.exportStems (chosen, settings, files), "export the first track and the group");
        names.clear(); for (const auto& f : files) names.add (juce::File (f).getFileName());
        check (names == juce::StringArray { "Song Lead.wav", "Song Keys.wav" } && ! chosen.getChildFile ("Song Master.wav").exists() && ! chosen.getChildFile ("Song Pad.wav").exists(),
               "only the chosen track and group are rendered, without Master: " + names.joinIntoString (", "));
        check (error (read (chosen.getChildFile ("Song Keys.wav")), [&] (int n) { return half * b (n); }) < 1e-5, "and they sound as the full export's do");
        settings.stemTracks = { "missing" };
        check (s.exportStems (root.getChildFile ("None"), settings, files).failed() && files.isEmpty(), "a choice with no track in the project is refused");
        std::cout << "Stems passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL stems: " << e.what() << std::endl; return 1; }
}
}
